#include "utils/binary_log.hpp"          // FileHeader, RecordHeader, RecordType
#include "workers/shared_state.hpp"      // StateSample, ActionSample, EventSample
#include "utils/logger.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

static void print_help(const char* argv0) {
  std::cout
    << "Usage:\n"
    << "  " << argv0 << " --in gateway.bin --out_dir ./out [--prefix run1]\n"
    << "\n"
    << "Naming:\n"
    << "  Output files are named as:\n"
    << "    <out_dir>/<prefix><stamp>_state.csv\n"
    << "    <out_dir>/<prefix><stamp>_cmd.csv\n"
    << "    <out_dir>/<prefix><stamp>_event.csv\n"
    << "\n"
    << "  <stamp> is derived from the input filename by default:\n"
    << "    - If basename contains YYYYMMDD_HHMMSS -> that is used.\n"
    << "    - Otherwise basename (without extension) is used.\n"
    << "\n"
    << "Examples:\n"
    << "  " << argv0 << " --in ./logs/gateway_20260214_185144_0.bin --out_dir ./out\n"
    << "    -> out/20260214_185144_state.csv, ...\n"
    << "  " << argv0 << " --in gateway.bin --out_dir ./out --prefix testA\n"
    << "    -> out/testA_gateway_state.csv, ...\n";
}

static bool read_exact(std::ifstream& in, void* dst, std::size_t n) {
  in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

static std::string path_join(std::string_view dir, std::string_view file) {
  if (dir.empty()) return std::string(file);
  std::string d(dir);
  if (!d.empty() && d.back() != '/' && d.back() != '\\') d.push_back('/');
  d += file;
  return d;
}

static std::string basename_no_dirs(std::string_view p) {
  std::size_t pos = p.find_last_of("/\\");
  if (pos == std::string_view::npos) return std::string(p);
  return std::string(p.substr(pos + 1));
}

static std::string strip_extension(std::string_view filename) {
  std::size_t dot = filename.find_last_of('.');
  if (dot == std::string_view::npos) return std::string(filename);
  return std::string(filename.substr(0, dot));
}

// Extract "YYYYMMDD_HHMMSS" from a string if present, else return empty.
static std::string extract_yyyymmdd_hhmmss(std::string_view s) {
  // Look for pattern: 8 digits, '_', 6 digits
  for (std::size_t i = 0; i + 15 <= s.size(); ++i) {
    auto isdig = [&](char c) { return c >= '0' && c <= '9'; };
    bool ok = true;
    for (int k = 0; k < 8; ++k) ok = ok && isdig(s[i + k]);
    ok = ok && (s[i + 8] == '_');
    for (int k = 0; k < 6; ++k) ok = ok && isdig(s[i + 9 + k]);
    if (ok) return std::string(s.substr(i, 15)); // "YYYYMMDD_HHMMSS"
  }
  return {};
}

static std::string normalize_prefix(std::string_view pfx) {
  if (pfx.empty()) return {};
  std::string p(pfx);
  // Make it nice to read: ensure it ends with '_' unless already ends with '_' or '-'
  char last = p.back();
  if (last != '_' && last != '-' ) p.push_back('_');
  return p;
}

static const char* record_type_name(utils::RecordType t) {
  switch (t) {
    case utils::RecordType::STATE:  return "STATE";
    case utils::RecordType::CMD: return "ACTION";
    case utils::RecordType::EVENT:  return "EVENT";
    default: return "UNKNOWN";
  }
}

static std::string event_type_name(gateway::EventType t) {
  switch (t) {
    case gateway::EventType::BEEP:           return "BEEP";
    case gateway::EventType::FLAG_RISE:      return "FLAG_RISE";
    case gateway::EventType::CONFIG_APPLIED: return "CONFIG_APPLIED";
    default: return "UNKNOWN";
  }
}

static bool parse_args(
    int argc, char** argv,
    std::string& in_path, std::string& out_dir, std::string& prefix) {

  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    auto need = [&](std::string_view name) -> std::string_view {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };

    if (a == "--in") in_path = std::string(need(a));
    else if (a == "--out_dir") out_dir = std::string(need(a));
    else if (a == "--prefix") prefix = std::string(need(a));
    else if (a == "--help") { print_help(argv[0]); return false; }
    else {
      std::cerr << "Unknown arg: " << a << "\n";
      print_help(argv[0]);
      return false;
    }
  }

  if (in_path.empty()) {
    std::cerr << "Missing --in\n";
    return false;
  }
  if (out_dir.empty()) out_dir = ".";
  return true;
}

int main(int argc, char** argv) {
  std::string in_path, out_dir, prefix_raw;
  if (!parse_args(argc, argv, in_path, out_dir, prefix_raw)) return 1;

  const std::string prefix = normalize_prefix(prefix_raw);

  // Derive stamp from input filename by default.
  const std::string base = basename_no_dirs(in_path);
  const std::string stamp_from_pattern = extract_yyyymmdd_hhmmss(base);
  const std::string stamp = !stamp_from_pattern.empty()
                              ? stamp_from_pattern
                              : strip_extension(base);

  auto out_name = [&](std::string_view suffix) {
    // e.g. prefix + "_" + stamp + "_state.csv"
    std::string name;
    name.reserve(prefix.size() + stamp.size() + suffix.size());
    name += prefix;
    name += stamp;
    name += suffix;
    return name;
  };

  std::ifstream in(in_path, std::ios::binary);
  if (!in.is_open()) {
    logger::error() << "Failed to open input: " << in_path << "\n";
    return 1;
  }

  // Read and verify file header
  utils::FileHeader fh{};
  if (!read_exact(in, &fh, sizeof(fh))) {
    logger::error() << "Failed to read FileHeader\n";
    return 1;
  }

  // Magic from writer: 0x47574C42 ('BLWG' little endian in our earlier code)
  if (fh.magic != 0x47574C42u) {
    logger::warn() << "Unexpected magic: 0x" << std::hex << fh.magic << std::dec
                   << " (expected 0x47574C42). Attempting to continue.\n";
  }
  if (fh.ver != 1) {
    logger::warn() << "Unexpected version: " << fh.ver << " (expected 1). Attempting to continue.\n";
  }

  // Open CSV outputs with prefix + stamp by default
  const std::string state_path  = path_join(out_dir, out_name("_state.csv"));
  const std::string cmd_path = path_join(out_dir, out_name("_cmd.csv"));
  const std::string event_path  = path_join(out_dir, out_name("_event.csv"));

  std::ofstream state_csv(state_path);
  std::ofstream cmd_csv(cmd_path);
  std::ofstream event_csv(event_path);

  if (!state_csv.is_open() || !cmd_csv.is_open() || !event_csv.is_open()) {
    logger::error() << "Failed to open output CSV(s) in dir: " << out_dir << "\n";
    logger::error() << "Tried:\n"
                   << "  " << state_path  << "\n"
                   << "  " << cmd_path << "\n"
                   << "  " << event_path  << "\n";
    return 1;
  }

  // CSV headers
  state_csv
    << "epoch_s,mono_s,seq,"
    << "roll,pitch,yaw,"
    << "gx,gy,gz,"
    << "ax,ay,az,"
    << "mx,my,mz,"
    << "e1,e2,e3,e4,"
    << "battery_voltage\n";

  cmd_csv
    << "epoch_s,mono_s,seq,"
    << "m1,m2,m3,m4,"
    << "beep_ms,flags\n";

  event_csv
    << "epoch_s,mono_s,"
    << "event_type,event_seq,"
    << "data0,data1,data2,data3,aux_u32\n";

  std::size_t n_records = 0;
  std::size_t n_skipped = 0;

  while (true) {
    utils::RecordHeader rh{};
    if (!read_exact(in, &rh, sizeof(rh))) {
      // normal EOF
      break;
    }

    const auto rtype = rh.type;
    const uint16_t payload_len = rh.payload_len;

    // Read payload bytes
    std::vector<uint8_t> payload(payload_len);
    if (payload_len > 0) {
      if (!read_exact(in, payload.data(), payload.size())) {
        logger::warn() << "Truncated payload while reading record " << n_records
                       << " type=" << record_type_name(rtype) << " len=" << payload_len << "\n";
        break;
      }
    }

    ++n_records;

    if (rtype == utils::RecordType::STATE) {
      if (payload_len != sizeof(workers::StateSample)) {
        ++n_skipped;
        continue;
      }
      workers::StateSample s{};
      std::memcpy(&s, payload.data(), sizeof(s));

      state_csv
        << rh.epoch_s << "," << rh.mono_s << "," << s.seq << ","
        << s.st.ang.roll << "," << s.st.ang.pitch << "," << s.st.ang.yaw << ","
        << s.st.imu.gyro.x << "," << s.st.imu.gyro.y << "," << s.st.imu.gyro.z << ","
        << s.st.imu.acc.x << "," << s.st.imu.acc.y << "," << s.st.imu.acc.z << ","
        << s.st.imu.mag.x << "," << s.st.imu.mag.y << "," << s.st.imu.mag.z << ","
        << s.st.enc.e1 << "," << s.st.enc.e2 << "," << s.st.enc.e3 << "," << s.st.enc.e4 << ","
        << s.st.battery_voltage
        << "\n";
    }
    else if (rtype == utils::RecordType::CMD) {
      if (payload_len != sizeof(workers::MotorCommandsSample)) {
        ++n_skipped;
        continue;
      }
      workers::MotorCommandsSample cmd{};
      std::memcpy(&cmd, payload.data(), sizeof(cmd));

      cmd_csv
        << rh.epoch_s << "," << rh.mono_s << "," << cmd.seq << ","
        << cmd.motors.m1 << "," << cmd.motors.m2 << "," << cmd.motors.m3 << "," << cmd.motors.m4
        << "\n";
    }
    else if (rtype == utils::RecordType::EVENT) {
      if (payload_len != sizeof(workers::EventSample)) {
        ++n_skipped;
        continue;
      }
      workers::EventSample e{};
      std::memcpy(&e, payload.data(), sizeof(e));

      event_csv
        << rh.epoch_s << "," << rh.mono_s << ","
        << event_type_name(e.ev.type) << ","
        << e.ev.seq << ","
        << static_cast<unsigned>(e.ev.data0) << ","
        << static_cast<unsigned>(e.ev.data1) << ","
        << static_cast<unsigned>(e.ev.data2) << ","
        << static_cast<unsigned>(e.ev.data3) << ","
        << e.ev.aux_u32
        << "\n";
    }
    else {
      ++n_skipped;
      continue;
    }
  }

  logger::info() << "Decoded " << n_records << " records, skipped " << n_skipped
                 << " (unknown/size-mismatch).\n"
                 << "Outputs:\n"
                 << "  " << state_path  << "\n"
                 << "  " << cmd_path << "\n"
                 << "  " << event_path  << "\n";
  return 0;
}

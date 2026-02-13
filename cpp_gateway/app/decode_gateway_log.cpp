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
    << "  " << argv0 << " --in gateway.bin --out_dir ./out\n"
    << "\n"
    << "Outputs:\n"
    << "  out_dir/state.csv\n"
    << "  out_dir/action.csv\n"
    << "  out_dir/event.csv\n";
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

static const char* record_type_name(utils::RecordType t) {
  switch (t) {
    case utils::RecordType::STATE:  return "STATE";
    case utils::RecordType::ACTION: return "ACTION";
    case utils::RecordType::EVENT:  return "EVENT";
    default: return "UNKNOWN";
  }
}

static std::string event_type_name(gateway::EventType t) {
  switch (t) {
    case gateway::EventType::BEEP:          return "BEEP";
    case gateway::EventType::FLAG_RISE:     return "FLAG_RISE";
    case gateway::EventType::CONFIG_APPLIED:return "CONFIG_APPLIED";
    default: return "UNKNOWN";
  }
}

static bool parse_args(int argc, char** argv, std::string& in_path, std::string& out_dir) {
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
  std::string in_path, out_dir;
  if (!parse_args(argc, argv, in_path, out_dir)) return 1;

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

  // Open CSV outputs
  std::ofstream state_csv(path_join(out_dir, "state.csv"));
  std::ofstream action_csv(path_join(out_dir, "action.csv"));
  std::ofstream event_csv(path_join(out_dir, "event.csv"));

  if (!state_csv.is_open() || !action_csv.is_open() || !event_csv.is_open()) {
    logger::error() << "Failed to open output CSV(s) in dir: " << out_dir << "\n";
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

  action_csv
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

      // NOTE: this assumes core::States layout matches what you log now.
      // Adjust fields below if your core::States differs.
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
    else if (rtype == utils::RecordType::ACTION) {
      if (payload_len != sizeof(workers::ActionSample)) {
        ++n_skipped;
        continue;
      }
      workers::ActionSample a{};
      std::memcpy(&a, payload.data(), sizeof(a));

      action_csv
        << rh.epoch_s << "," << rh.mono_s << "," << a.seq << ","
        << a.act.motors.m1 << "," << a.act.motors.m2 << "," << a.act.motors.m3 << "," << a.act.motors.m4 << ","
        << static_cast<unsigned>(a.act.beep_ms) << ","
        << static_cast<unsigned>(a.act.flags)
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
                 << " (unknown/size-mismatch). Output dir: " << out_dir << "\n";
  return 0;
}

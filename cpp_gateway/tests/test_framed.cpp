#include "connection/framed.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

static void test_basic_frame() {
  connection::FrameRx rx;
  // Build a CMD frame header+payload
  connection::MsgHdr h = connection::make_hdr(connection::MSG_CMD, 3);
  uint8_t frame[3+3]{};
  std::memcpy(frame, &h, 3);
  frame[3] = 0xAA;
  frame[4] = 0xBB;
  frame[5] = 0xCC;

  rx.push_bytes(frame, sizeof(frame));

  uint8_t type=0;
  std::vector<uint8_t> payload;
  const bool ok = rx.pop(type, payload);
  assert(ok);
  assert(type == connection::MSG_CMD);
  assert(payload.size() == 3);
  assert(payload[0]==0xAA && payload[1]==0xBB && payload[2]==0xCC);
}

static void test_resync_on_garbage() {
  connection::FrameRx rx;
  // garbage then valid header
  uint8_t garbage[5]{1,2,3,4,5};
  rx.push_bytes(garbage, 5);

  connection::MsgHdr h = connection::make_hdr(connection::MSG_STATS_REQ, 0);
  uint8_t frame[3]{};
  std::memcpy(frame, &h, 3);
  rx.push_bytes(frame, 3);

  uint8_t type=0;
  std::vector<uint8_t> payload;
  // It may take a couple calls because resync drops one byte per attempt
  for (int i=0;i<10;i++) {
    if (rx.pop(type, payload)) break;
  }
  assert(type == connection::MSG_STATS_REQ);
  assert(payload.empty());
}

int main() {
  test_basic_frame();
  test_resync_on_garbage();
  return 0;
}

#pragma once
#include "config.h"

namespace pkt {
enum Type : uint8_t {
  DISCOVERY = 1,
  JOIN = 2,
  LOBBY = 3,
  READY = 4,
  START = 5,
  INP = 6,
  SNAPSHOT = 7,
  EXPLOSION = 8,
  TERRAIN_REQ = 9,
  TERRAIN_CHUNK = 10,
  HEARTBEAT = 11,
  PAUSE = 12,
  QUIT = 13,
  REMATCH = 14,
  EVENT = 15
};

constexpr int HDR = 11; // magic2 + ver + type + session4 + sender + seq2

inline bool writeHdr(BufW& w, uint8_t type, uint32_t session, uint8_t sender, uint16_t seq) {
  return w.u8(PROTO_MAGIC0) && w.u8(PROTO_MAGIC1) && w.u8(PROTO_VERSION) && w.u8(type) &&
         w.u32(session) && w.u8(sender) && w.u16(seq);
}

inline bool checkHdr(BufR& r, uint8_t& type, uint32_t& session, uint8_t& sender, uint16_t& seq) {
  uint8_t m0 = r.u8(), m1 = r.u8(), ver = r.u8();
  type = r.u8();
  session = r.u32();
  sender = r.u8();
  seq = r.u16();
  if (r.err) return false;
  if (m0 != PROTO_MAGIC0 || m1 != PROTO_MAGIC1 || ver != PROTO_VERSION) return false;
  return true;
}

inline int finish(uint8_t* buf, BufW& w) {
  if (w.overflow) return 0;
  int n = w.used(buf);
  if (n + 1 > PKT_MAX) return 0;
  buf[n] = crc8(buf, n);
  return n + 1;
}

inline bool verify(const uint8_t* buf, int len) {
  if (len < HDR + 1 || len > PKT_MAX) return false;
  uint8_t got = buf[len - 1];
  return crc8(buf, len - 1) == got;
}
} // namespace pkt

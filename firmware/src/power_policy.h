#pragma once
#include <stdint.h>

// Board/RTC epochs already include local time (Beijing on this host).
static inline uint32_t nightSleepSeconds(uint32_t epoch, uint32_t awakeUntil = 0) {
  if (epoch < 1767225600u || epoch < awakeUntil) return 0;
  uint32_t day = epoch % 86400;
  if (day >= 20 * 3600) return 86400 - day + 9 * 3600;
  if (day < 9 * 3600) return 9 * 3600 - day;
  return 0;
}

static inline uint8_t powerBrightness(uint8_t full, bool usb, bool attention,
                                      uint32_t idleMs, bool faceDown) {
  if (faceDown) return 0;
  if (!usb) {
    if (!attention && idleMs >= 30000) return 40;
    return full < 110 ? full : 110;
  }
  if (!attention && idleMs > 20 * 60000) return 40;
  if (!attention && idleMs > 8 * 60000) return full < 100 ? full : 100;
  return full;
}

static inline uint32_t powerFrameInterval(bool animated, bool interactive, bool dirty) {
  return interactive || dirty ? 33 : animated ? 67 : 1000;
}

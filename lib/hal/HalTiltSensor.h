#pragma once

#include <cstdint>

#include "TiltRate.h"

namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}  // namespace CrossPointOrientation

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}  // namespace CrossPointTiltPageTurn

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;

class HalTiltSensor {
 public:
  void begin() {}
  bool wake() { return false; }
  bool deepSleep() { return true; }

  bool isAvailable() const { return false; }

  void update(CrossPointTiltPageTurn::Value, TiltRate::Axis, CrossPointOrientation::Value, bool, bool) {}
  bool takeOrientation(uint8_t&) { return false; }

  bool wasTiltedForward() { return false; }
  bool wasTiltedBack() { return false; }
  bool hadActivity() { return false; }
  void clearPendingEvents() {}
};

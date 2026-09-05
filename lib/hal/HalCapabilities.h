#pragma once

#include <BoardConfig.h>

// Ask what the board CAN DO, not what it IS.
//
// Why this file exists (workstream B0, see
// docs/multiboard-bringup-handover-2026-08-15.md):
//
// `HalGPIO::deviceIsX3()` has ~38 call sites and is a stand-in for at least six
// unrelated questions — is there a hardware RTC, is there an IMU, is the battery
// read over I2C or ADC, does the panel need a half-refresh settle, how is USB
// presence detected, and where are the physical buttons. That works only while
// exactly two boards exist and every question happens to correlate.
//
// It breaks silently on a third board. On X4 Pro `deviceIsX3()` returns false,
// so every one of those questions takes the **X4** branch by default. The board
// does not fail loudly; it is subtly wrong in six places that each look like an
// unrelated bug. Widening `DeviceType` to {X3, X4, X4Pro, LilyGo} multiplies the
// conflation by four rather than removing it.
//
// So each predicate below answers ONE question, from the active board profile.
// Adding a board then means filling in a profile, not editing call sites.
//
// These are pure additions: nothing is converted here. Call sites move over in a
// separate step so that step's diff is reviewable on its own, and so this one can
// be gated on the C3 coming out byte-identical.
//
// Naming note: `deviceIsX3()` itself is NOT deprecated wholesale. Two of its uses
// are legitimately about board identity — the dual X3+X4 binary's runtime
// detection and `BoardConfig::selectDevice()` — and those stay.
namespace HalCapabilities {

// --- Sensors -----------------------------------------------------------------

// A battery-backed real-time clock is present. X3 has a DS3231, X4 Pro a BM8563;
// X4 has none and falls back to the ESP32 RTC across deep sleep.
inline bool hasHardwareRtc() { return BoardConfig::ACTIVE.sensors.rtcType != BoardConfig::RtcType::None; }

// Which RTC, for the register/address differences. DS3231 is 0x68, the
// PCF8563-compatible BM8563 is 0x51 — same bus, different protocol, so a caller
// that only checks "is there an RTC" would talk nonsense to the wrong one.
inline BoardConfig::RtcType rtcType() { return BoardConfig::ACTIVE.sensors.rtcType; }

// An IMU is present, so tilt gestures are physically possible. X3 carries a
// QMI8658; X4 and X4 Pro carry nothing.
inline bool hasTiltSensor() { return BoardConfig::ACTIVE.sensors.imuType != BoardConfig::ImuType::None; }

// --- Battery -----------------------------------------------------------------

// Charge comes from an I2C fuel gauge rather than an ADC divider. X3 has a
// BQ27220, X4 Pro a CW2017; X4 reads an ADC pin. These are mutually exclusive
// and each needs a completely different read path.
inline bool hasI2cFuelGauge() { return BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0; }

// Charge comes from an ADC pin. Note this is not simply !hasI2cFuelGauge(): a
// profile may legitimately have neither, in which case there is no battery
// reading at all and the UI should not pretend otherwise.
inline bool hasAdcBattery() { return !hasI2cFuelGauge() && BoardConfig::ACTIVE.batteryAdc >= 0; }

// Any battery reading is available, by either route.
inline bool hasBatteryReading() { return hasI2cFuelGauge() || hasAdcBattery(); }

// --- Display -----------------------------------------------------------------

// A genuine silicon quirk, and the one case B0 allows to key on the controller:
// the UC8253 needs extra settle passes after a half refresh or it ghosts. This
// is a property of the panel controller, not of "being an X3" — a future UC8253
// board would need it too, and an SSD1677 X3 variant would not.
inline bool panelNeedsHalfRefreshSettle() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8253;
}

// The controller develops grayscale from a swappable LUT, so the fast/OEM
// waveform trade-off is a real choice. SSD1677 uses a factory waveform and has
// nothing to swap. Mirrors SettingRequires::SelectableGrayscaleLut.
inline bool hasSelectableGrayscaleLut() {
  return BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::SSD1677;
}

// --- Input / chrome ----------------------------------------------------------

// A touch panel is present (disabled for C3 X3/X4).
inline bool hasTouch() { return false; }

// The touch controller reports a capacitive Home key (disabled for C3 X3/X4).
inline bool hasHomeKey() { return false; }

// Dedicated Back and Confirm buttons exist in hardware (always true on X3/X4).
inline bool hasBackAndConfirmButtons() { return true; }

// The physical button topology, for the places that genuinely need to know the
// arrangement rather than a yes/no capability.
inline BoardConfig::InputStyle inputStyle() { return BoardConfig::ACTIVE.inputStyle; }

// Chrome scale factor for finger-sized targets. 1.0 on button boards.
inline float uiScale() { return 1.0f; }

// A frontlight is present (none on X3/X4).
inline bool hasFrontlight() { return false; }

// The frontlight has a second (warm) channel.
inline bool hasColorTemperature() { return false; }

// Human-readable board name for the UI.
inline const char* boardDisplayName(BoardConfig::Board board) {
  switch (board) {
    case BoardConfig::Board::XteinkX4:
      return "X4";
    case BoardConfig::Board::XteinkX3:
    case BoardConfig::Board::XteinkX3Uc8279:
      return "X3";
    default:
      return "X4";
  }
}

}  // namespace HalCapabilities

#define BOARD_SUPPORT_OWNS_BUSES 0

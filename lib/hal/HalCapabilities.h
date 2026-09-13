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

// A battery-backed real-time clock is present (X4 has none; falls back to ESP32 RTC).
inline bool hasHardwareRtc() { return false; }

inline BoardConfig::RtcType rtcType() { return BoardConfig::RtcType::None; }

// An IMU is present (X4 carries none).
inline bool hasTiltSensor() { return false; }

// --- Battery -----------------------------------------------------------------

// Charge comes from an I2C fuel gauge (X4 has none).
inline bool hasI2cFuelGauge() { return false; }

// Charge comes from an ADC pin (X4 reads ADC).
inline bool hasAdcBattery() { return true; }

// Any battery reading is available, by either route.
inline bool hasBatteryReading() { return true; }

// --- Display -----------------------------------------------------------------

// Settle passes after a half refresh (UC8253 quirk, X4 does not need this).
inline bool panelNeedsHalfRefreshSettle() { return false; }

// Grayscale from a swappable LUT (X4 does not use this).
inline bool hasSelectableGrayscaleLut() { return false; }

// --- Input / chrome ----------------------------------------------------------

// A touch panel is present (disabled for X4).
inline bool hasTouch() { return false; }

// The touch controller reports a capacitive Home key.
inline bool hasHomeKey() { return false; }

// Dedicated Back and Confirm buttons exist in hardware (always true on X4).
inline bool hasBackAndConfirmButtons() { return true; }

// The physical button topology.
inline BoardConfig::InputStyle inputStyle() { return BoardConfig::ACTIVE.inputStyle; }

// True when the SD card is reached over SPI (always true on X4).
inline bool sdUsesSpi() { return true; }

// The MCU pin the ROM samples at reset to choose boot vs. download mode. Holding
// it LOW while the chip comes out of reset enters ROM download mode, so firmware
// never runs and no boot-time key combo on that pin can ever be observed.
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
inline constexpr int8_t BOOT_MODE_STRAP_PIN = 9;
#else  // ESP32 / ESP32-S2 / ESP32-S3
inline constexpr int8_t BOOT_MODE_STRAP_PIN = 0;
#endif

// True when this board wires its Up key to that strapping pin, so a boot-time
// combo must use a different key. Always false on X4 (uses ADC ladder).
inline bool upKeyIsBootStrap() { return false; }

// Chrome scale factor for finger-sized targets. 1.0 on button boards.
inline float uiScale() { return 1.0f; }

// A frontlight is present (none on X4).
inline bool hasFrontlight() { return false; }

// The frontlight has a second (warm) channel.
inline bool hasColorTemperature() { return false; }

// Human-readable board name for the UI.
inline const char* boardDisplayName(BoardConfig::Board board) { return "X4"; }

}  // namespace HalCapabilities

#define BOARD_SUPPORT_OWNS_BUSES 0

#include "DarkModeNow.h"

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DarkMode.h"

namespace DarkModeNow {

namespace {

// In Witchling Reader, HalClock is pruned (X4 has no RTC / hardware clock).
// readClock() always reports the clock as unsynced.
DarkMode::ClockReading readClock() { return {false, 0, 0}; }

DarkMode::NightFlip savedFlip() { return {APP_STATE.darkModeFlipUntil, APP_STATE.darkModeFlipDark}; }

}  // namespace

bool isDark() {
  return DarkMode::isDark(SETTINGS.darkMode, SETTINGS.darkModeFrom, SETTINGS.darkModeUntil, readClock(), savedFlip());
}

// The settings row's own field and save when that is all there is to it (Off, On); under Scheduled the flip the night
// hours carry until they next change (DarkMode::flipped). The drawer is a second way to the same setting.
void flip() {
  const auto result =
      DarkMode::flipped(SETTINGS.darkMode, SETTINGS.darkModeFrom, SETTINGS.darkModeUntil, readClock(), savedFlip());
  if (result.setting != SETTINGS.darkMode) {
    SETTINGS.darkMode = result.setting;
    SETTINGS.saveToFile();
  }
  if (result.flip.until != APP_STATE.darkModeFlipUntil || result.flip.dark != APP_STATE.darkModeFlipDark) {
    APP_STATE.darkModeFlipUntil = result.flip.until;
    APP_STATE.darkModeFlipDark = result.flip.dark;
    APP_STATE.saveToFile();
  }
}

}  // namespace DarkModeNow

#include "SliderSettingPicker.h"

#include <I18n.h>

#include "CrossPointSettings.h"

namespace SliderSetting {

bool configFor(const SettingAction action, SliderPickerActivity::Config& cfg) {
  switch (action) {
    case SettingAction::SleepTimeoutPicker:
      cfg = {.titleId = StrId::STR_TIME_TO_SLEEP,
             .hintId = StrId::STR_SLIDER_STEP_HINT,
             .minValue = 0,
             .maxValue = 60,
             .initialValue = SETTINGS.sleepTimeoutMinutes,
             .suffix = tr(STR_MIN_SUFFIX),
             .zeroLabel = tr(STR_NEVER)};
      return true;
    case SettingAction::RefreshFrequencyPicker:
      cfg = {.titleId = StrId::STR_REFRESH_FREQ,
             .hintId = StrId::STR_SLIDER_STEP_HINT,
             .minValue = 0,
             .maxValue = 60,
             .initialValue = SETTINGS.refreshFrequencyPages,
             .suffix = tr(STR_PAGES_SUFFIX),
             .zeroLabel = tr(STR_NEVER)};
      return true;
    default:
      return false;
  }
}

void apply(const SettingAction action, const uint8_t value) {
  switch (action) {
    case SettingAction::SleepTimeoutPicker:
      SETTINGS.sleepTimeoutMinutes = value;
      break;
    case SettingAction::RefreshFrequencyPicker:
      SETTINGS.refreshFrequencyPages = value;
      break;
    default:
      break;
  }
}

}  // namespace SliderSetting

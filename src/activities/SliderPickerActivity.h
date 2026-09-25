#pragma once

#include <I18nKeys.h>

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Generic slider picker (0–N range, configurable label, suffix, zero label).
// Returns a PercentResult containing the selected value.
class SliderPickerActivity : public Activity, private UiAppHost {
 public:
  struct Config {
    StrId titleId;  // Heading shown at top
    StrId hintId;   // Step hint shown below slider
    int minValue = 0;
    int maxValue = 100;
    int initialValue = 0;
    // Suffix appended after the numeric display (e.g. " min", " pages").
    std::string suffix;
    // Label used instead of the numeric value when value == minValue.
    // Empty string = show numeric value even at min.
    std::string zeroLabel;
    // Same idea as zeroLabel, just for the max value
    std::string maxLabel;
    // Applied as the value moves, so a setting the reader can SEE is judged by looking at it
    // rather than by reading a number and guessing. Empty for settings with nothing to show
    // (a sleep timeout has no preview).
    //
    // The picker never persists what it previews: the caller still applies-and-saves on
    // confirm, and undoes the preview on cancel. Keeping it that way is what makes Back a
    // real cancel rather than a save of whatever the value happened to be.
    std::function<void(int)> onPreview;
  };

  explicit SliderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Config config);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  void adjustValue(int delta);

 private:
  int value;
  Config cfg;
  ButtonNavigator buttonNavigator;
  std::string valueText;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onDecrementEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onIncrementEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onSliderEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildScreen(UiScreen& screen);
  void updateValueText();
};

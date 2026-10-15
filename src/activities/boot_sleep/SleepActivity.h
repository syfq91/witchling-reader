#pragma once

#include <string>

#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderSleepScreen();
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;

  // No default member initializer: the constructor always sets it, and a `= false` here would
  // make every read of it look statically false to cppcheck (knownConditionTrueFalse).
  const bool fromTimeout;
};

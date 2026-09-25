#pragma once

#include <array>
#include <functional>
#include <string>

#include "activities/UiListActivity.h"

class ButtonRemapActivity final : public UiListActivity {
 public:
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ButtonRemap", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  int listCount() const override { return 4; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override {}
  bool handleCustomInput() override;
  void afterUiRender() override;
  void drawFooter() override;

 private:
  uint8_t currentStep = 0;
  uint8_t tempMapping[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  unsigned long errorUntil = 0;
  std::string errorMessage;

  std::array<freeink::ui::ListItem, 4> items;
  std::array<std::string, 4> itemValues;

  void applyTempMapping();
  bool validateUnassigned(uint8_t pressedButton);
  const char* getRoleName(uint8_t roleIndex) const;
  const char* getHardwareName(uint8_t buttonIndex) const;
};

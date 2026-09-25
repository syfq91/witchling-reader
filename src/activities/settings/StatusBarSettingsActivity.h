#pragma once

#include <array>
#include <string>

#include "activities/UiListActivity.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("StatusBarSettings", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void afterUiRender() override;
  void drawFooter() override;

 private:
  void handleSelection(int index);

  std::array<freeink::ui::ListItem, 5> items;
  std::array<std::string, 5> itemValues;
};

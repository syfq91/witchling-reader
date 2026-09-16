#pragma once

#include <GfxRenderer.h>

#include <optional>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/settings/SettingInfo.h"

class MappedInputManager;

/// Full-screen list of all reader fonts (built-in + SD card families).
/// Replaces in-place enum cycling for the Reader Font Family setting.
class FontSelectionActivity final : public UiListActivity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FontSelect", renderer, mappedInput) {}
  FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SettingInfo& overrideSetting)
      : UiListActivity("FontSelect", renderer, mappedInput), overrideSetting(overrideSetting) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onSelectionChanged(int index) override;
  void updatePreviewFont(int index);
  void updatePreviewFontLocked(int index);
  int previewOptionIndex(int index) const;
  uint8_t selectedFontSize() const;

  std::vector<std::string> rowLabels;
  std::vector<freeink::ui::ListItem> rowItems;
  std::optional<SettingInfo> overrideSetting;
};

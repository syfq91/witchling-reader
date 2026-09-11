#pragma once
#include <I18n.h>

#include <vector>

#include "SettingInfo.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  static constexpr int categoryCount = 4;
  // constexpr, not `static const` with an out-of-line definition: a compile-time table has no
  // reason to occupy .data and be initialized at startup when it can sit in flash.
  static constexpr StrId categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                         StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

  std::vector<SettingInfo::SubmenuData> submenuData;
  bool needsHalfRefresh = false;

  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  [[nodiscard]] bool isListItemSelectable(int settingIdx) const;

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

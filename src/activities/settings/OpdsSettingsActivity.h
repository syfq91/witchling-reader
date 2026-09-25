#pragma once

#include <array>
#include <string>

#include "OpdsServerStore.h"
#include "activities/UiListActivity.h"

/**
 * Edit screen for a single OPDS server.
 * Shows Name, URL, Username, Password fields and a Delete option.
 * Used for both adding new servers and editing existing ones.
 */
class OpdsSettingsActivity final : public UiListActivity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1)
      : UiListActivity("OpdsSettings", renderer, mappedInput), serverIndex(serverIndex) {}

  void onEnter() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void afterUiRender() override;

 private:
  int serverIndex;
  OpdsServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;
  std::string popupMessage;

  std::array<freeink::ui::ListItem, 5> items;
  std::array<std::string, 5> itemValues;

  int getMenuItemCount() const;
  void handleSelection(int index);
  bool saveServer();
};

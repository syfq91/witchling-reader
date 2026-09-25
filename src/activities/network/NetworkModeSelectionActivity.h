#pragma once

#include <array>
#include <functional>

#include "activities/UiListActivity.h"

enum class NetworkMode { JOIN_NETWORK, CREATE_HOTSPOT, OPDS_BROWSER };

/**
 * NetworkModeSelectionActivity presents the user with a choice:
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 * - "OPDS Browser" - Browse and download books from configured OPDS catalogs
 *
 * The onModeSelected callback is called with the user's choice.
 * The onCancel callback is called if the user presses back.
 */
class NetworkModeSelectionActivity final : public UiListActivity {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("NetworkModeSelection", renderer, mappedInput) {}

  bool usesWifi() const override { return true; }

  void onModeSelected(NetworkMode mode);
  void onCancel();

 protected:
  int listCount() const override { return 3; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onCancel(); }

 private:
  std::array<freeink::ui::ListItem, 3> items;
};

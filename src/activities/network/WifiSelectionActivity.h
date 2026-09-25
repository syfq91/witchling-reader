#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Structure to hold WiFi network information
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted;
  bool hasSavedPassword;  // Whether we have saved credentials for this network
  std::string ipAddress;  // Populated after connection for display
};

// WiFi selection states
enum class WifiSelectionState {
  AUTO_CONNECTING,    // Trying to connect to the last known network
  AUTO_CYCLING,       // Cycling through remaining saved credentials after AUTO_CONNECTING failed
  SCANNING,           // Scanning for networks
  NETWORK_LIST,       // Displaying available networks
  PASSWORD_ENTRY,     // Entering password for selected network
  CONNECTING,         // Attempting to connect
  CONNECTED,          // Successfully connected
  SAVE_PROMPT,        // Asking user if they want to save the password
  CONNECTION_FAILED,  // Connection failed
  FORGET_PROMPT,      // Asking user if they want to forget the network
  CAPTIVE_PORTAL      // Connected but network requires web-based login
};

/**
 * WifiSelectionActivity is responsible for scanning WiFi APs and connecting to them.
 * It will:
 * - Enter scanning mode on entry
 * - List available WiFi networks
 * - Allow selection and launch KeyboardEntryActivity for password if needed
 * - Save the password if requested
 * - Call onComplete callback when connected or cancelled
 *
 * The onComplete callback receives true if connected successfully, false if cancelled.
 */
class WifiSelectionActivity final : public Activity, private UiAppHost {
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  static constexpr freeink::ui::ActionId ACTION_BACK = 2;
  static constexpr freeink::ui::ActionId ACTION_RESCAN = 3;
  static constexpr freeink::ui::ActionId ACTION_OPTIONS = 4;
  static constexpr freeink::ui::ActionId ACTION_PROMPT_YES = 5;
  static constexpr freeink::ui::ActionId ACTION_PROMPT_NO = 6;
  static constexpr freeink::ui::ActionId ACTION_FORGET_CANCEL = 7;
  static constexpr freeink::ui::ActionId ACTION_FORGET_RESET = 8;
  static constexpr freeink::ui::ActionId ACTION_FORGET_CONFIRM = 9;
  static constexpr freeink::ui::ActionId ACTION_FAILED_DONE = 10;
  static constexpr freeink::ui::ActionId ACTION_CAPTIVE_DONE = 11;

  ButtonNavigator buttonNavigator;

  WifiSelectionState state = WifiSelectionState::SCANNING;
  int selectedNetworkIndex = 0;
  std::vector<WifiNetworkInfo> networks;

  // Selected network for connection
  std::string selectedSSID;
  bool selectedRequiresPassword = false;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for display
  std::string cachedMacAddress;

  // Whether network was connected using a saved password (skip save prompt)
  bool usedSavedPassword = false;

  // Whether to attempt auto-connect on entry
  const bool allowAutoConnect;

  // Whether we are attempting to auto-connect
  bool autoConnecting = false;

  // Saved-credential candidates for auto-cycling (SSIDs visible in scan, sorted by RSSI desc)
  std::vector<std::string> autoCycleCandidates;
  size_t autoCycleCandidateIndex = 0;
  bool autoCycleAfterScan = false;  // Scan was triggered to build cycle candidates

  // Save/forget prompt selection (0 = Yes, 1 = No)
  int savePromptSelection = 0;
  int forgetPromptSelection = 0;

  // Windowed list for FreeInkUI
  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems{};
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels{};
  std::array<std::string, LIST_WINDOW_CAPACITY> windowValues{};
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;
  freeink::ui::ListNav nav{};
  freeink::ui::Rect captiveQrRect_{};

  // Connection timeouts
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000;
  static constexpr unsigned long AUTO_CYCLE_TIMEOUT_MS = 5000;
  static constexpr uint32_t SCAN_ACTIVE_DWELL_MAX_MS = 120;
  static constexpr uint32_t SCAN_PASSIVE_DWELL_MS = 200;
  static constexpr uint8_t SCAN_HOME_CHAN_DWELL_MS = 30;

  unsigned long connectionStartTime = 0;

  // WiFi event handler IDs so we can deregister on exit.
  uint16_t evtIdConnected = 0;
  uint16_t evtIdGotIp = 0;
  uint16_t evtIdStaStart = 0;
  uint16_t evtIdDisconnected = 0;

  void buildScreen(UiScreen& screen);
  void materializeListWindow();
  void syncListViewport(UiScreen& screen, freeink::ui::ListProps& props);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  void startWifiScan();
  void processWifiScanResults();
  void buildAutoCycleCandidates();
  void tryNextAutoCycleCandidate();
  void selectNetwork(int index);
  void attemptConnection();
  void checkConnectionStatus();
  void issueWifiBegin();
  void applyScanBudget();
  void prepareForConnect();
  bool checkCaptivePortal();
  std::string getSignalStrengthIndicator(int32_t rssi) const;

  std::string captivePortalUrl;

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true)
      : Activity("WifiSelection", renderer, mappedInput),
        UiAppHost(renderer),
        allowAutoConnect(autoConnect) {}
  void onEnter() override;
  bool usesWifi() const override { return true; }
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void afterUiRender();
  bool preventAutoSleep() override { return true; }
};

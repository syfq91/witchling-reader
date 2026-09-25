#pragma once
#include <OpdsParser.h>

#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "OpdsServerStore.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

/**
 * Activity for browsing and downloading books from an OPDS server.
 * Supports navigation through catalog hierarchy and downloading EPUBs.
 */
class OpdsBookBrowserActivity final : public Activity, private UiAppHost {
 public:
  enum class BrowserState {
    CHECK_WIFI,
    WIFI_SELECTION,
    LOADING,
    BROWSING,
    BOOK_DETAIL,
    FORMAT_SELECTION,
    DOWNLOADING,
    ERROR,
    SEARCH_INPUT
  };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server,
                                   std::string initialQuery = {})
      : Activity("OpdsBookBrowser", renderer, mappedInput),
        UiAppHost(renderer),
        buttonNavigator(),
        server(std::move(server)),
        initialQuery_(std::move(initialQuery)) {}

  void onEnter() override;
  bool usesWifi() const override { return true; }
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr freeink::ui::ActionId ACTION_BACK = 1;
  static constexpr freeink::ui::ActionId ACTION_RETRY = 2;
  static constexpr freeink::ui::ActionId ACTION_CONFIRM = 3;
  static constexpr freeink::ui::ActionId ACTION_SEARCH = 4;
  static constexpr freeink::ui::ActionId ACTION_INFO = 5;
  static constexpr freeink::ui::ActionId ACTION_SELECT_ENTRY = 6;
  static constexpr freeink::ui::ActionId ACTION_SELECT_FORMAT = 7;
  static constexpr freeink::ui::ActionId ACTION_DOWNLOAD = 8;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  void buildScreen(UiScreen& screen);
  void handleAction(const freeink::ui::ActionEvent& event);
  void afterUiRender();
  void materializeListWindow();

  static constexpr size_t LIST_WINDOW_CAPACITY = 16;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;
  freeink::ui::ListItem windowItems[LIST_WINDOW_CAPACITY];
  std::string windowLabels[LIST_WINDOW_CAPACITY];
  std::string windowSubtitles[LIST_WINDOW_CAPACITY];

  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LOADING;
  std::vector<uint32_t> entryOffsets;
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  std::string searchTemplate;
  bool consumeConfirm = false;
  bool consumeBack = false;
  bool memoryTrimmed = false;
  bool coverAvailable = false;
  int selectorIndex = 0;
  int selectedBookIndex = -1;
  int formatSelectorIndex = 0;
  std::vector<std::string> formatSelectionLabels;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  OpdsServer server;  // Copied at construction — safe even if the store changes during browsing
  std::string initialQuery_;

  OpdsEntry getEntry(size_t index) const;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book, const OpdsAcquisitionLink& acquisition);
  void chooseBookFormat(const OpdsEntry& book);
  void fetchOsdTemplate(const std::string& osdUrl);
  void launchSearch();
  void performSearch(const std::string& query);
  void fetchCoverForEntry(const OpdsEntry& entry);
  bool preventAutoSleep() override;
};

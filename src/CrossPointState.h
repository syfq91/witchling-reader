#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

struct PendingBookmarkJumpState {
  bool active = false;
  std::string bookPath;     // source file path for disambiguation
  uint16_t spineIndex = 0;  // EPUB spine; ignored for TXT
  uint16_t pageNumber = 0;  // page within spine (EPUB) or global page (TXT)

  void clear() {
    active = false;
    bookPath.clear();
    spineIndex = 0;
    pageNumber = 0;
  }
};

#if SERIAL_TEST_API
// Where the next EPUB open should land, as a share of the book (the serial
// CMD:ACT:READER:<path>@<n>%). RAM only: unlike a bookmark jump it is acted on by the open that
// follows it at once, so there is nothing to survive a reboot for.
struct PendingPercentJumpState {
  bool active = false;
  std::string bookPath;
  uint8_t percent = 0;
};
#endif

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  size_t lastSleepImage = SIZE_MAX;  // SIZE_MAX = unset sentinel
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  // How Recent Books shows its list: a CrossPointSettings::FILE_BROWSER_VIEW, chosen there from
  // Options > View. Its own, not Browse Files' (SETTINGS.fileBrowserView): changing one leaves the
  // other as it was. Details (list view) by default.
  uint8_t recentBooksView = 1;
  // When false, setup() skips the boot screen on wake (used by reader resume).
  bool showBootScreen = true;
  PendingBookmarkJumpState pendingBookmarkJump;
#if SERIAL_TEST_API
  PendingPercentJumpState pendingPercentJump;  // not saved; see the type
#endif
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()

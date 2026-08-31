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

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  size_t lastSleepImage = SIZE_MAX;  // SIZE_MAX = unset sentinel
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool recentBooksGridView = false;  // true = grid/thumbnail view, false = list view
  // When false, setup() skips the boot screen on wake and instead restores the
  // saved framebuffer overlaid with a loading icon (Quick Resume).
  bool showBootScreen = true;
  PendingBookmarkJumpState pendingBookmarkJump;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()

#pragma once

#include <Epub.h>

#include <array>
#include <memory>
#include <string>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex, const int currentTocIndex)
      : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        currentTocIndex(currentTocIndex) {}

  void onEnter() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void navigateButtons() override;
  void drawFooter() override;

 private:
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex = 0;
  int currentTocIndex = 0;

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void materializeListWindow();
};

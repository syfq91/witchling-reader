#pragma once

#include <Epub/FootnoteEntry.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class EpubReaderFootnotesActivity final : public UiListActivity {
 public:
  explicit EpubReaderFootnotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::vector<FootnoteEntry>& footnotes,
                                       std::vector<std::string> previews = {}, std::vector<uint8_t> isNote = {})
      : UiListActivity("EpubReaderFootnotes", renderer, mappedInput),
        footnotes(footnotes),
        previews(std::move(previews)),
        isNote(std::move(isNote)) {}

  void onEnter() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;

 private:
  bool entryIsNote(size_t i) const { return i >= isNote.size() || isNote[i] != 0; }

  const std::vector<FootnoteEntry>& footnotes;
  const std::vector<std::string> previews;
  const std::vector<uint8_t> isNote;
  std::vector<uint16_t> order;
  int firstLinkRow = -1;

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowSubtitles;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void materializeListWindow();
};

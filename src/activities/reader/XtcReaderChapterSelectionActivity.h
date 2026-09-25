#pragma once

#include <Xtc.h>

#include <array>
#include <memory>
#include <string>

#include "activities/UiListActivity.h"

class XtcReaderChapterSelectionActivity final : public UiListActivity {
 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::shared_ptr<Xtc>& xtc, const uint32_t currentPage)
      : UiListActivity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}

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
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowValues;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void materializeListWindow();
  int findChapterIndexForPage(uint32_t page) const;
};

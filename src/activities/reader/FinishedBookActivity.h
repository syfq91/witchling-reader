#pragma once

#include <array>
#include <string>
#include <vector>

#include "CrossPointState.h"
#include "activities/UiListActivity.h"

namespace BookFinished {
std::string findNextBookInDirectory(const std::string& currentBookPath, const std::string& currentBookSeries,
                                    const std::string& currentBookSeriesIndex);

enum class FinishedBookAction {
  Stay = 0,
  GoHome = 1,
  OpenNextBook = 2,
  SearchOpdsForAuthor = 3,
};

// Launches the finished-book menu on top of `host` and handles its result:
// credits a finish to the reading-stats session, applies the remove-from-recents
// setting, then navigates home or to the next book.
// On cancel/stay the host gets a requestUpdate() to re-render its last page.
// The caller is responsible for persisting reading progress beforehand (the
// progress formats differ per reader).
// `onMenuClosed` (optional, with `onMenuClosedCtx`) runs first in the result
// handler regardless of outcome — readers use it to clear a "menu is open"
// flag. Plain function pointer + context instead of std::function per the
// project callback convention.
void launchFinishedBookFlow(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::string& bookPath, const std::string& series, const std::string& seriesIndex,
                            const std::string& author = {}, void (*onMenuClosed)(void*) = nullptr,
                            void* onMenuClosedCtx = nullptr);
}  // namespace BookFinished

class FinishedBookActivity final : public UiListActivity {
 public:
  FinishedBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string currentBookPath,
                       std::string nextBookPath, std::string currentBookAuthor = {});

  void onEnter() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void afterUiRender() override;

 private:
  struct RowModel {
    enum class Action { GoHome, OpenNext, SearchOpds, ToggleForget };
    std::vector<Action> actions;
    std::vector<std::string> titles;
    std::vector<std::string> values;

    int count() const { return static_cast<int>(actions.size()); }
  };

  RowModel buildRowModel() const;

  std::string currentBookPath_;
  std::string nextBookPath_;
  std::string currentBookAuthor_;
  std::string nextBookName_;
  std::string nextBookTitle_;
  std::string nextBookAuthor_;
  std::string nextBookSeries_;
  std::string nextBookCoverPath_;
  bool nextBookAvailable_ = false;
  bool nextBookMetadataLoaded_ = false;
  bool removeFinishedBooksFromRecents_ = false;

  static constexpr size_t MAX_ROWS = 6;
  std::array<std::string, MAX_ROWS> rowTitles_;
  std::array<std::string, MAX_ROWS> rowValues_;
  std::array<freeink::ui::ListItem, MAX_ROWS> items_;

  int previewX_ = 0;
  int previewY_ = 0;
  int previewWidth_ = 0;
  int previewHeight_ = 0;
  int previewTextWidth_ = 0;
};

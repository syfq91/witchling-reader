#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "CrossPointState.h"
#include "activities/UiListActivity.h"

// Home-screen activity that aggregates bookmarks from every indexed book and
// jumps directly into the chosen book/position on Confirm.
//
// Data source: GlobalBookmarkIndex (persisted at /.crosspoint/global_bookmarks.bin).
// Reconciles against the filesystem on entry (drops entries whose source file
// has disappeared).
//
// The display list is a flat vector of rows, where each row is either a book
// header separator or a bookmark entry belonging to the preceding header.
class GlobalBookmarksActivity final : public UiListActivity {
 public:
  explicit GlobalBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ReturnHint restoreHint = {})
      : UiListActivity("GlobalBookmarks", renderer, mappedInput), restoreHint(std::move(restoreHint)) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;
  ListRowTap::Result selectListRow(int index) override;

 private:
  struct Row {
    bool isSeparator = false;
    size_t bookIndex = 0;      // index into GlobalBookmarkIndex entries
    size_t bookmarkIndex = 0;  // index within that entry's bookmarks (separator: ignored)
  };

  std::vector<Row> rows;
  ReturnHint restoreHint;

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void rebuildRows();
  void materializeListWindow();
  std::string getRowTitle(int index) const;
  bool isSeparatorRow(int index) const;
  int firstSelectableIndex() const;

  void openSelected();
  void deleteSelected();
  void renameSelected();

  // Apply a mutation to the underlying per-book BookmarkStore + global index.
  // `op` is invoked with the loaded store; it should mutate and return true
  // when something changed worth persisting. Title/cacheDir/isTxt are taken
  // from the current index entry.
  template <typename Op>
  void mutateBook(size_t bookIndex, Op&& op);
};

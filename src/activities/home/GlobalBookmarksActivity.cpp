#include "GlobalBookmarksActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "../ActivityManager.h"
#include "BookmarkStore.h"
#include "CrossPointState.h"
#include "GlobalBookmarkIndex.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void GlobalBookmarksActivity::onEnter() {
  GLOBAL_BOOKMARKS.reconcile();
  rebuildRows();

  const int first = firstSelectableIndex();
  int initialIndex = first >= 0 ? first : 0;

  if (restoreHint.target == ReturnTo::GlobalBookmarks) {
    const auto& entries = GLOBAL_BOOKMARKS.getEntries();
    if (!restoreHint.selectionContext.empty() && restoreHint.selectBookmarkIndex >= 0) {
      for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row.isSeparator) continue;
        if (row.bookmarkIndex == static_cast<size_t>(restoreHint.selectBookmarkIndex) &&
            row.bookIndex < entries.size() && entries[row.bookIndex].sourcePath == restoreHint.selectionContext) {
          initialIndex = static_cast<int>(i);
          break;
        }
      }
    }
    if (initialIndex < 0 || initialIndex >= static_cast<int>(rows.size()) || isSeparatorRow(initialIndex)) {
      const int fallback = firstSelectableIndex();
      initialIndex = fallback >= 0 ? fallback : 0;
    }
    restoreHint = {};
  }

  UiListActivity::onEnter();
  nav.selected = initialIndex;

  const auto total = static_cast<int>(rows.size());
  buttonNavigator.setSelectablePredicate([this](int index) { return !isSeparatorRow(index); }, total);
}

void GlobalBookmarksActivity::onExit() {
  buttonNavigator.clearSelectablePredicate();
  rows.clear();
  UiListActivity::onExit();
}

int GlobalBookmarksActivity::listCount() const { return static_cast<int>(rows.size()); }

const char* GlobalBookmarksActivity::headerTitle() const { return tr(STR_GLOBAL_BOOKMARKS); }

void GlobalBookmarksActivity::onBackButton() { onGoHome(); }

void GlobalBookmarksActivity::activateIndex(const int index) {
  if (isSeparatorRow(index)) return;
  nav.selected = index;
  openSelected();
}

ListRowTap::Result GlobalBookmarksActivity::selectListRow(const int index) {
  if (index >= 0 && index < listCount() && isSeparatorRow(index)) {
    return ListRowTap::Result::Rejected;
  }
  return ListRowTap::apply(index, listCount(), activeNav().selected);
}

void GlobalBookmarksActivity::rebuildRows() {
  rows.clear();
  const auto& entries = GLOBAL_BOOKMARKS.getEntries();
  for (size_t bi = 0; bi < entries.size(); bi++) {
    const auto& entry = entries[bi];
    if (entry.bookmarks.empty()) continue;
    Row sep;
    sep.isSeparator = true;
    sep.bookIndex = bi;
    rows.push_back(sep);
    for (size_t mi = 0; mi < entry.bookmarks.size(); mi++) {
      Row row;
      row.isSeparator = false;
      row.bookIndex = bi;
      row.bookmarkIndex = mi;
      rows.push_back(row);
    }
  }
}

bool GlobalBookmarksActivity::isSeparatorRow(const int index) const {
  return index >= 0 && index < static_cast<int>(rows.size()) && rows[index].isSeparator;
}

int GlobalBookmarksActivity::firstSelectableIndex() const {
  for (size_t i = 0; i < rows.size(); i++) {
    if (!rows[i].isSeparator) return static_cast<int>(i);
  }
  return -1;
}

std::string GlobalBookmarksActivity::getRowTitle(const int index) const {
  if (index < 0 || index >= static_cast<int>(rows.size())) return {};
  const auto& row = rows[index];
  const auto& entries = GLOBAL_BOOKMARKS.getEntries();
  if (row.bookIndex >= entries.size()) return {};
  const auto& entry = entries[row.bookIndex];

  if (row.isSeparator) {
    return entry.title.empty() ? entry.sourcePath : entry.title;
  }

  if (row.bookmarkIndex >= entry.bookmarks.size()) return {};
  const auto& bm = entry.bookmarks[row.bookmarkIndex];
  if (!bm.name.empty()) return bm.name;

  char buf[64];
  if (entry.isTxt) {
    snprintf(buf, sizeof(buf), "%s%d", tr(STR_PAGE_PREFIX), bm.pageNumber + 1);
  } else {
    snprintf(buf, sizeof(buf), "%s%d, %s%d", tr(STR_SECTION_PREFIX), bm.spineIndex + 1, tr(STR_PAGE_PREFIX),
             bm.pageNumber + 1);
  }
  return std::string(buf);
}

void GlobalBookmarksActivity::openSelected() {
  const int selected = nav.selected;
  if (isSeparatorRow(selected)) return;
  const auto& row = rows[selected];
  const auto& entries = GLOBAL_BOOKMARKS.getEntries();
  if (row.bookIndex >= entries.size()) return;
  const auto& entry = entries[row.bookIndex];
  if (row.bookmarkIndex >= entry.bookmarks.size()) return;
  const auto& bm = entry.bookmarks[row.bookmarkIndex];

  if (!Storage.exists(entry.sourcePath.c_str())) {
    LOG_ERR("GBA", "Source file missing, reconciling: %s", entry.sourcePath.c_str());
    GLOBAL_BOOKMARKS.removeBySourcePath(entry.sourcePath);
    GLOBAL_BOOKMARKS.save();
    rebuildRows();
    const int first = firstSelectableIndex();
    nav.selected = first >= 0 ? first : 0;
    buttonNavigator.setSelectablePredicate([this](int index) { return !isSeparatorRow(index); },
                                           static_cast<int>(rows.size()));
    requestUpdate();
    return;
  }

  auto& jump = APP_STATE.pendingBookmarkJump;
  jump.active = true;
  jump.bookPath = entry.sourcePath;
  jump.spineIndex = bm.spineIndex;
  jump.pageNumber = bm.pageNumber;
  APP_STATE.saveToFile();

  LOG_DBG("GBA", "Jumping to bookmark in %s at %u/%u", entry.sourcePath.c_str(), bm.spineIndex, bm.pageNumber);
  ReturnHint hint;
  hint.target = ReturnTo::GlobalBookmarks;
  hint.selectionContext = entry.sourcePath;
  hint.selectBookmarkIndex = static_cast<int>(row.bookmarkIndex);
  activityManager.replaceWithReader(entry.sourcePath, std::move(hint));
}

template <typename Op>
void GlobalBookmarksActivity::mutateBook(size_t bookIndex, Op&& op) {
  const auto& entries = GLOBAL_BOOKMARKS.getEntries();
  if (bookIndex >= entries.size()) return;
  const auto entry = entries[bookIndex];  // copy — index may invalidate after sync

  BookmarkStore store;
  store.load(entry.cacheDir);
  if (!op(store)) return;
  store.save();

  GLOBAL_BOOKMARKS.syncFromStore(store, entry.sourcePath, entry.cacheDir, entry.title, entry.isTxt);
  GLOBAL_BOOKMARKS.save();
}

void GlobalBookmarksActivity::deleteSelected() {
  const int selected = nav.selected;
  if (isSeparatorRow(selected)) return;
  const auto& row = rows[selected];
  const size_t bookmarkIndex = row.bookmarkIndex;

  mutateBook(row.bookIndex, [bookmarkIndex](BookmarkStore& store) {
    if (bookmarkIndex >= store.getAll().size()) return false;
    store.removeAt(bookmarkIndex);
    return true;
  });

  rebuildRows();
  const int total = static_cast<int>(rows.size());
  buttonNavigator.setSelectablePredicate([this](int index) { return !isSeparatorRow(index); }, total);

  if (rows.empty()) {
    onGoHome();
    return;
  }
  if (nav.selected >= total) nav.selected = total - 1;
  if (isSeparatorRow(nav.selected)) {
    const int next = ButtonNavigator::nextIndex(nav.selected, total, [this](int i) { return !isSeparatorRow(i); });
    if (next >= 0) nav.selected = next;
  }
  requestUpdate();
}

void GlobalBookmarksActivity::renameSelected() {
  const int selected = nav.selected;
  if (isSeparatorRow(selected)) return;
  const auto& row = rows[selected];
  const auto& entries = GLOBAL_BOOKMARKS.getEntries();
  if (row.bookIndex >= entries.size()) return;
  const auto& entry = entries[row.bookIndex];
  if (row.bookmarkIndex >= entry.bookmarks.size()) return;

  const size_t bookIndex = row.bookIndex;
  const size_t bookmarkIndex = row.bookmarkIndex;
  const std::string initial =
      entry.bookmarks[bookmarkIndex].name.empty() ? getRowTitle(selected) : entry.bookmarks[bookmarkIndex].name;

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initial,
                                                                 BookmarkStore::MAX_NAME_LENGTH, InputType::Text),
                         [this, bookIndex, bookmarkIndex](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& kr = std::get<KeyboardResult>(result.data);
                             mutateBook(bookIndex, [bookmarkIndex, &kr](BookmarkStore& store) {
                               if (bookmarkIndex >= store.getAll().size()) return false;
                               store.rename(bookmarkIndex, kr.text);
                               return true;
                             });
                             rebuildRows();
                             buttonNavigator.setSelectablePredicate([this](int i) { return !isSeparatorRow(i); },
                                                                    static_cast<int>(rows.size()));
                           }
                           requestUpdate();
                         });
}

bool GlobalBookmarksActivity::handleCustomInput() {
  const int total = listCount();
  if (total == 0) return false;

  const int selected = nav.selected;
  if (!isSeparatorRow(selected)) {
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Left)) {
      renameSelected();
      return true;
    }
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Right)) {
      deleteSelected();
      return true;
    }
  }
  return false;
}

void GlobalBookmarksActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  const auto& entries = GLOBAL_BOOKMARKS.getEntries();

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    auto& rowItem = windowItems[offset];
    rowItem = {};
    rowItem.actionValue = static_cast<int16_t>(index);

    const auto& row = rows[index];
    if (row.isSeparator) {
      if (row.bookIndex < entries.size()) {
        const auto& entry = entries[row.bookIndex];
        windowLabels[offset] = entry.title.empty() ? entry.sourcePath : entry.title;
      } else {
        windowLabels[offset] = "";
      }
      rowItem.label = windowLabels[offset].c_str();
      rowItem.isHeader = true;
      rowItem.enabled = false;
    } else {
      windowLabels[offset] = getRowTitle(static_cast<int>(index));
      rowItem.label = windowLabels[offset].c_str();
      rowItem.isHeader = false;
      rowItem.enabled = true;
    }
  }
}

void GlobalBookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (listCount() == 0) {
    fui::TextAreaProps empty;
    empty.text = tr(STR_NO_GLOBAL_BOOKMARKS);
    empty.style = screen.theme().bodyText;
    empty.showCaret = false;
    screen.textArea(empty);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void GlobalBookmarksActivity::drawFooter() {
  const bool hasBookmarks = !rows.empty() && !isSeparatorRow(nav.selected);
  const auto hints =
      mappedInput.mapHints(tr(STR_HOME), hasBookmarks ? tr(STR_OPEN) : "", hasBookmarks ? tr(STR_RENAME) : "",
                           hasBookmarks ? tr(STR_DELETE) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
}

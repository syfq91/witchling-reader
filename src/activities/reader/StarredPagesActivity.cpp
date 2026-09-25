#include "StarredPagesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

std::string StarredPagesActivity::getDefaultLabel(const int index) const {
  const auto& bm = bookmarkStore.getAll()[index];
  char buf[64];
  if (epub) {
    const int tocIndex = epub->getTocIndexForSpineIndex(bm.spineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      return tocItem.title + " - " + tr(STR_PAGE_PREFIX) + std::to_string(bm.pageNumber + 1);
    }
    snprintf(buf, sizeof(buf), "%s%d, %s%d", tr(STR_SECTION_PREFIX), bm.spineIndex + 1, tr(STR_PAGE_PREFIX),
             bm.pageNumber + 1);
  } else {
    snprintf(buf, sizeof(buf), "%s%d", tr(STR_PAGE_PREFIX), bm.pageNumber + 1);
  }
  return std::string(buf);
}

std::string StarredPagesActivity::getItemLabel(const int index) const {
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%d. ", index + 1);
  const auto& bm = bookmarkStore.getAll()[index];
  return std::string(prefix) + (bm.name.empty() ? getDefaultLabel(index) : bm.name);
}

void StarredPagesActivity::onExit() {
  bookmarkStore.save();
  UiListActivity::onExit();
}

int StarredPagesActivity::listCount() const { return static_cast<int>(bookmarkStore.getAll().size()); }

const char* StarredPagesActivity::headerTitle() const { return tr(STR_STARRED_PAGES); }

void StarredPagesActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void StarredPagesActivity::activateIndex(const int index) {
  const auto& all = bookmarkStore.getAll();
  if (index >= 0 && index < static_cast<int>(all.size())) {
    const auto& bm = all[index];
    setResult(StarredPageResult{bm.spineIndex, bm.pageNumber});
    finish();
  }
}

bool StarredPagesActivity::handleCustomInput() {
  const int totalItems = listCount();
  if (totalItems > 0 && mappedInput.wasLogicalReleased(MappedInputManager::Direction::Left)) {
    startRename();
    return true;
  }
  if (totalItems > 0 && mappedInput.wasLogicalReleased(MappedInputManager::Direction::Right)) {
    deleteSelected();
    return true;
  }
  return false;
}

void StarredPagesActivity::startRename() {
  const auto& all = bookmarkStore.getAll();
  const int selected = nav.selected;
  if (all.empty() || selected < 0 || selected >= static_cast<int>(all.size())) return;
  const int renamingIndex = selected;
  const std::string initial =
      all[renamingIndex].name.empty() ? getDefaultLabel(renamingIndex) : all[renamingIndex].name;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initial,
                                                                 BookmarkStore::MAX_NAME_LENGTH, InputType::Text),
                         [this, renamingIndex](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& kr = std::get<KeyboardResult>(result.data);
                             bookmarkStore.rename(renamingIndex, kr.text);
                           }
                           requestUpdate();
                         });
}

void StarredPagesActivity::deleteSelected() {
  const auto& all = bookmarkStore.getAll();
  const int selected = nav.selected;
  if (all.empty() || selected < 0 || selected >= static_cast<int>(all.size())) return;
  bookmarkStore.removeAt(selected);
  const int remaining = static_cast<int>(bookmarkStore.getAll().size());
  if (remaining == 0) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (nav.selected >= remaining) nav.selected = remaining - 1;
  requestUpdate();
}

void StarredPagesActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    auto& row = windowItems[offset];
    row = {};
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = true;
    windowLabels[offset] = getItemLabel(static_cast<int>(index));
    row.label = windowLabels[offset].c_str();
  }
}

void StarredPagesActivity::buildScreen(UiScreen& screen) {
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
    empty.text = tr(STR_NO_STARRED_PAGES);
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

void StarredPagesActivity::drawFooter() {
  const bool hasItems = listCount() > 0;
  const auto hints = mappedInput.mapHints(tr(STR_BACK), hasItems ? tr(STR_SELECT) : "", hasItems ? tr(STR_RENAME) : "",
                                          hasItems ? tr(STR_DELETE) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
}

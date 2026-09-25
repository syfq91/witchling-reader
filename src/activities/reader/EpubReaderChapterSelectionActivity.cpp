#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

int EpubReaderChapterSelectionActivity::listCount() const { return epub ? epub->getTocItemsCount() : 0; }

const char* EpubReaderChapterSelectionActivity::headerTitle() const { return tr(STR_SELECT_CHAPTER); }

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) {
    return;
  }

  int initialIndex = (currentTocIndex >= 0 && currentTocIndex < epub->getTocItemsCount())
                         ? currentTocIndex
                         : epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (initialIndex < 0) {
    initialIndex = 0;
  }
  moveSelectionTo(initialIndex);
}

void EpubReaderChapterSelectionActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    auto item = epub->getTocItem(static_cast<int>(index));
    const int level = std::max(1, static_cast<int>(item.level));
    std::string indent;
    if (level > 1) {
      indent.assign(static_cast<size_t>(std::min(level - 1, 6) * 2), ' ');
    }
    windowLabels[offset] = indent + (item.title.empty() ? tr(STR_UNNAMED) : item.title);

    auto& row = windowItems[offset];
    row = {};
    row.label = windowLabels[offset].c_str();
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = true;
  }
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
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
    empty.text = tr(STR_NO_CHAPTERS);
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

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (!epub) return;
  const auto newSpineIndex = epub->getSpineIndexForTocIndex(index);
  if (newSpineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{newSpineIndex, index});
    finish();
  }
}

void EpubReaderChapterSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderChapterSelectionActivity::navigateButtons() {
  bool changed = false;
  const int count = listCount();
  const int pageRows = activeNav().inputPageRows();
  int selected = activeNav().selected;

  buttonNavigator.onNextList(selected, count, [&changed] { changed = true; }, pageRows);
  buttonNavigator.onPreviousList(selected, count, [&changed] { changed = true; }, pageRows);

  if (changed) {
    moveSelectionTo(selected);
  }
}

void EpubReaderChapterSelectionActivity::drawFooter() {
  const bool pages = listCount() > activeNav().inputPageRows();
  const auto hints = mappedInput.mapHints(tr(STR_BACK), tr(STR_SELECT), pages ? tr(STR_LIST_PAGE_PREV) : "",
                                          pages ? tr(STR_LIST_PAGE_NEXT) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
}

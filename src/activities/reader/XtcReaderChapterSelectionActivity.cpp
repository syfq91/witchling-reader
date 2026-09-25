#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

int XtcReaderChapterSelectionActivity::listCount() const {
  return xtc ? static_cast<int>(xtc->getChapters().size()) : 0;
}

const char* XtcReaderChapterSelectionActivity::headerTitle() const { return tr(STR_SELECT_CHAPTER); }

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) return 0;
  const auto& chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!xtc) return;
  const int initialIndex = findChapterIndexForPage(currentPage);
  moveSelectionTo(initialIndex);
}

void XtcReaderChapterSelectionActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  if (!xtc) return;
  const auto& chapters = xtc->getChapters();

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    const auto& chapter = chapters[index];
    windowLabels[offset] = chapter.name.empty() ? tr(STR_UNNAMED) : chapter.name;
    windowValues[offset] = "p. " + std::to_string(chapter.startPage + 1);

    auto& row = windowItems[offset];
    row = {};
    row.label = windowLabels[offset].c_str();
    row.value = windowValues[offset].c_str();
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = true;
  }
}

void XtcReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
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
  props.valueText = screen.theme().smallText;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::activateIndex(const int index) {
  if (!xtc) return;
  const auto& chapters = xtc->getChapters();
  if (index >= 0 && index < static_cast<int>(chapters.size())) {
    setResult(PageResult{chapters[index].startPage});
    finish();
  }
}

void XtcReaderChapterSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void XtcReaderChapterSelectionActivity::navigateButtons() {
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

void XtcReaderChapterSelectionActivity::drawFooter() {
  const bool pages = listCount() > activeNav().inputPageRows();
  const auto hints = mappedInput.mapHints(tr(STR_BACK), tr(STR_SELECT), pages ? tr(STR_LIST_PAGE_PREV) : "",
                                          pages ? tr(STR_LIST_PAGE_NEXT) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);
}

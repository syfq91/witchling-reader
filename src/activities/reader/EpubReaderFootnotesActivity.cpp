#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

int EpubReaderFootnotesActivity::listCount() const { return static_cast<int>(order.size()); }

const char* EpubReaderFootnotesActivity::headerTitle() const { return tr(STR_FOOTNOTES); }

void EpubReaderFootnotesActivity::onEnter() {
  UiListActivity::onEnter();

  // Notes first, then navigation, each in page order.
  order.clear();
  order.reserve(footnotes.size());
  for (size_t i = 0; i < footnotes.size(); ++i) {
    if (entryIsNote(i)) order.push_back(static_cast<uint16_t>(i));
  }
  firstLinkRow = -1;
  for (size_t i = 0; i < footnotes.size(); ++i) {
    if (!entryIsNote(i)) {
      if (firstLinkRow < 0) firstLinkRow = static_cast<int>(order.size());
      order.push_back(static_cast<uint16_t>(i));
    }
  }
  // A page of only links needs no divider — there is nothing above it to divide from.
  if (firstLinkRow == 0) firstLinkRow = -1;
}

void EpubReaderFootnotesActivity::materializeListWindow() {
  const int count = listCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t rowIdx = windowFirst + offset;
    const size_t entry = order[rowIdx];
    auto& item = windowItems[offset];
    item = {};
    item.actionValue = static_cast<int16_t>(rowIdx);
    item.enabled = true;

    if (static_cast<int>(rowIdx) == firstLinkRow) {
      item.sectionHeading = tr(STR_LINK);
    }

    const bool note = entryIsNote(entry);
    std::string label = footnotes[entry].number;
    if (label.empty()) {
      label = tr(STR_LINK);
    } else if (!note) {
      label += "  ";
      label += tr(STR_LINK);
    }

    windowLabels[offset] = label;
    item.label = windowLabels[offset].c_str();

    if (note && entry < previews.size() && !previews[entry].empty()) {
      windowSubtitles[offset] = previews[entry];
      item.subtitle = windowSubtitles[offset].c_str();
    }
  }
}

void EpubReaderFootnotesActivity::buildScreen(UiScreen& screen) {
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
    empty.text = tr(STR_NO_FOOTNOTES);
    empty.style = screen.theme().bodyText;
    empty.showCaret = false;
    screen.textArea(empty);
    return;
  }

  const bool hasPreviews =
      std::any_of(previews.begin(), previews.end(), [](const std::string& s) { return !s.empty(); });

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.subtitleText = screen.theme().smallText;
  props.headerText = screen.theme().smallText;

  syncListViewport(screen, props, hasPreviews);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void EpubReaderFootnotesActivity::activateIndex(const int index) {
  if (index >= 0 && index < static_cast<int>(order.size())) {
    setResult(FootnoteResult{footnotes[order[index]].href});
    finish();
  }
}

void EpubReaderFootnotesActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool EpubReaderFootnotesActivity::handleCustomInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) {
      activateIndex(selected);
      return true;
    }
  }
  return false;
}

void EpubReaderFootnotesActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

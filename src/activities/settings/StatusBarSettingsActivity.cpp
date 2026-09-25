#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
const StrId statusPositionNames[] = {StrId::STR_TOP, StrId::STR_BOTTOM};
const StrId slotContentNames[] = {
    StrId::STR_HIDE,
    StrId::STR_BATTERY,
    StrId::STR_STATUS_BAR_PAGE_COUNT,
    StrId::STR_STATUS_BAR_PERCENTAGE,
    StrId::STR_STATUS_BAR_PAGE_AND_PERCENTAGE,
    StrId::STR_STATUS_BAR_CHAPTER_TITLE,
    StrId::STR_STATUS_BAR_BOOK_TITLE,
};
const StrId progressBarNames[] = {StrId::STR_BOOK, StrId::STR_HIDE};

// One menu row. Editing a status-bar option means: cycle `field` through `valueCount` values and
// display its current value. Rows with an enum-style set of choices provide `valueNames` (indexed by
// the field value); rows with no `valueNames` are on/off toggles rendered as Show/Hide.
struct StatusBarItem {
  StrId label;
  uint8_t CrossPointSettings::* field;
  uint8_t valueCount;
  uint8_t defaultValue;     // value to reset to if the stored one is out of range
  const StrId* valueNames;  // nullptr → boolean Show/Hide toggle
};

template <size_t N>
constexpr StatusBarItem enumItem(StrId label, uint8_t CrossPointSettings::* field, const StrId (&names)[N],
                                 uint8_t defaultValue) {
  return {label, field, static_cast<uint8_t>(N), defaultValue, names};
}

const StatusBarItem statusBarItems[] = {
    enumItem(StrId::STR_STATUS_BAR_LOCATION, &CrossPointSettings::statusBarPosition, statusPositionNames,
             CrossPointSettings::STATUS_BAR_POSITION::STATUS_BAR_BOTTOM),
    enumItem(StrId::STR_STATUS_BAR_LEFT, &CrossPointSettings::statusBarLeft, slotContentNames,
             CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BATTERY),
    enumItem(StrId::STR_STATUS_BAR_MIDDLE, &CrossPointSettings::statusBarMiddle, slotContentNames,
             CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_CHAPTER_TITLE),
    enumItem(StrId::STR_STATUS_BAR_RIGHT, &CrossPointSettings::statusBarRight, slotContentNames,
             CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_PAGE_AND_PERCENTAGE),
    enumItem(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar, progressBarNames,
             CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS),
};

const StatusBarItem& visibleItem(int visibleIndex) {
  if (visibleIndex >= 0 && static_cast<size_t>(visibleIndex) < sizeof(statusBarItems) / sizeof(statusBarItems[0])) {
    return statusBarItems[visibleIndex];
  }
  return statusBarItems[0];  // out-of-range guard; callers clamp the index first
}

int visibleItemCount() { return static_cast<int>(sizeof(statusBarItems) / sizeof(statusBarItems[0])); }

constexpr int previewHorizontalInset = 10;
constexpr int previewHeight = 78;
constexpr int previewInnerMargin = 4;
constexpr int previewBatteryInset = 2;  // matches the battery's inset from the margin in the real bar

void drawPreviewProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t progressBar,
                            const bool topEdge) {
  if (progressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS) {
    return;
  }

  constexpr int percent = 75;
  const int barHeight = UITheme::getProgressBarHeight(progressBar, CrossPointSettings::PROGRESS_BAR_THIN);
  const int y = topEdge ? rect.y + previewInnerMargin : rect.y + rect.height - previewInnerMargin - barHeight;
  const int maxWidth = rect.width - previewInnerMargin * 2;
  const int barWidth = maxWidth * percent / 100;
  const int startX = rect.x + previewInnerMargin;

  renderer.fillRect(startX, y, maxWidth, barHeight, false);
  renderer.fillRect(startX, y, barWidth, barHeight, true);

  // Show sample chapter markers at 20%, 40%, 60%, 80%
  constexpr float sampleMarkers[] = {0.20f, 0.40f, 0.60f, 0.80f};
  for (const float markerProg : sampleMarkers) {
    const int markerX = startX + static_cast<int>(std::round(maxWidth * markerProg));
    const bool passed = (markerX < startX + barWidth);
    renderer.fillRect(markerX, y, 1, barHeight, !passed);
  }
}

void drawPreviewStatusItems(const GfxRenderer& renderer, const Rect& rect, const ThemeMetrics& metrics) {
  if (!UITheme::hasStatusBarItems()) {
    return;
  }

  const bool statusAtTop = SETTINGS.statusBarPosition == CrossPointSettings::STATUS_BAR_POSITION::STATUS_BAR_TOP;
  const int adjacentProgressHeight =
      UITheme::getProgressBarHeight(SETTINGS.statusBarProgressBar, CrossPointSettings::PROGRESS_BAR_THIN);
  const int statusItemsHeight = UITheme::getStatusBarItemsHeight();
  const int textY = statusAtTop
                        ? rect.y + previewInnerMargin + adjacentProgressHeight + 4
                        : rect.y + rect.height - previewInnerMargin - adjacentProgressHeight - statusItemsHeight + 4;

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  auto getPreviewSlotText = [&](const uint8_t slot) -> std::string {
    switch (slot) {
      case CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_PAGE_COUNT:
        return SETTINGS.statusBarPrintedPage ? "(vii) 8/32" : "8/32";
      case CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BOOK_PERCENTAGE:
        return "75%";
      case CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_PAGE_AND_PERCENTAGE:
        return SETTINGS.statusBarPrintedPage ? "(vii) 8/32  75%" : "8/32  75%";
      case CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_CHAPTER_TITLE:
        return tr(STR_EXAMPLE_CHAPTER);
      case CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BOOK_TITLE:
        return tr(STR_EXAMPLE_BOOK);
      default:
        return "";
    }
  };

  const int leftEdge = rect.x + previewInnerMargin;
  const int rightEdge = rect.x + rect.width - previewInnerMargin;

  // 1. Left slot
  int leftClusterWidth = 0;
  if (SETTINGS.statusBarLeft == CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BATTERY) {
    GUI.drawBatteryLeft(renderer,
                        Rect{leftEdge + previewBatteryInset, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
    leftClusterWidth = BaseTheme::statusBarBatteryWidth(renderer, metrics, showBatteryPercentage);
  } else if (SETTINGS.statusBarLeft != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE) {
    const std::string text = getPreviewSlotText(SETTINGS.statusBarLeft);
    if (!text.empty()) {
      leftClusterWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
      renderer.drawText(SMALL_FONT_ID, leftEdge, textY, text.c_str());
    }
  }

  // 2. Right slot
  int rightClusterWidth = 0;
  if (SETTINGS.statusBarRight == CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BATTERY) {
    const int battWidth = BaseTheme::statusBarBatteryWidth(renderer, metrics, showBatteryPercentage);
    GUI.drawBatteryRight(renderer,
                         Rect{rightEdge - metrics.batteryWidth, textY, metrics.batteryWidth, metrics.batteryHeight},
                         showBatteryPercentage);
    rightClusterWidth = battWidth;
  } else if (SETTINGS.statusBarRight != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE) {
    const std::string text = getPreviewSlotText(SETTINGS.statusBarRight);
    if (!text.empty()) {
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
      renderer.drawText(SMALL_FONT_ID, rightEdge - textWidth, textY, text.c_str());
      rightClusterWidth = textWidth;
    }
  }

  // 3. Middle slot
  if (SETTINGS.statusBarMiddle == CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_BATTERY) {
    const int battWidth = BaseTheme::statusBarBatteryWidth(renderer, metrics, showBatteryPercentage);
    const int midX = leftEdge + (rightEdge - leftEdge - battWidth) / 2;
    GUI.drawBatteryLeft(renderer, Rect{midX, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
  } else if (SETTINGS.statusBarMiddle != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE) {
    std::string text = getPreviewSlotText(SETTINGS.statusBarMiddle);
    if (!text.empty()) {
      const int renderableWidth = rightEdge - leftEdge;
      const int titleMarginLeft = leftClusterWidth > 0 ? leftClusterWidth + 12 : 0;
      const int titleMarginRight = rightClusterWidth > 0 ? rightClusterWidth + 12 : 0;

      int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
      int availableSpace = renderableWidth - 2 * titleMarginLeftAdjusted;

      int textWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
      if (textWidth > availableSpace) {
        availableSpace = renderableWidth - titleMarginLeft - titleMarginRight;
        titleMarginLeftAdjusted = titleMarginLeft;
      }
      if (textWidth > availableSpace && availableSpace > 0) {
        text = renderer.truncatedText(SMALL_FONT_ID, text.c_str(), availableSpace);
        textWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
      }
      if (availableSpace > 0) {
        renderer.drawText(SMALL_FONT_ID, leftEdge + titleMarginLeftAdjusted + (availableSpace - textWidth) / 2, textY,
                          text.c_str());
      }
    }
  }
}
}  // namespace

int StatusBarSettingsActivity::listCount() const { return visibleItemCount(); }

const char* StatusBarSettingsActivity::headerTitle() const { return tr(STR_CUSTOMISE_STATUS_BAR); }

void StatusBarSettingsActivity::onEnter() {
  // Clamp status bar settings in case of corrupt/migrated data: every field must hold a valid value
  // index (0..valueCount-1). A stray value would index past its valueNames array when rendered.
  for (const auto& item : statusBarItems) {
    if (SETTINGS.*item.field >= item.valueCount) {
      SETTINGS.*item.field = item.defaultValue;
    }
  }

  UiListActivity::onEnter();
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  const int previewLabelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int previewAreaHeight = previewLabelHeight + previewHeight + metrics.verticalSpacing * 2;

  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height) + previewAreaHeight),
      static_cast<int16_t>(contentRect.x)});

  const int count = visibleItemCount();
  for (int i = 0; i < count; ++i) {
    const StatusBarItem& item = visibleItem(i);
    const uint8_t value = SETTINGS.*item.field;
    if (item.valueNames) {
      itemValues[i] = I18N.get(item.valueNames[value]);
    } else {
      itemValues[i] = value ? tr(STR_SHOW) : tr(STR_HIDE);
    }
    items[i] = {};
    items[i].label = I18N.get(item.label);
    items[i].value = itemValues[i].c_str();
    items[i].actionValue = static_cast<int16_t>(i);
    items[i].enabled = true;
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  handleSelection(index);
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection(const int index) {
  const StatusBarItem& item = visibleItem(index);
  SETTINGS.*item.field = (SETTINGS.*item.field + 1) % item.valueCount;
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::afterUiRender() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int pageWidth = static_cast<int>(renderer.getScreenWidth());

  const int previewLabelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int previewAreaHeight = previewLabelHeight + previewHeight + metrics.verticalSpacing * 2;
  const int previewLabelY = contentRect.y + contentRect.height - previewAreaHeight;

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, previewLabelY, tr(STR_PREVIEW));
  const Rect previewRect{previewHorizontalInset, previewLabelY + previewLabelHeight + metrics.verticalSpacing / 2,
                         pageWidth - previewHorizontalInset * 2, previewHeight};
  renderer.drawRect(previewRect.x, previewRect.y, previewRect.width, previewRect.height);

  const bool statusAtTop = (SETTINGS.statusBarPosition == CrossPointSettings::STATUS_BAR_POSITION::STATUS_BAR_TOP);
  drawPreviewProgressBar(renderer, previewRect, SETTINGS.statusBarProgressBar, statusAtTop);
  drawPreviewStatusItems(renderer, previewRect, metrics);
}

void StatusBarSettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

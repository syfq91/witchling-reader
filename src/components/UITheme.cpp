#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>


#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "UiFontScale.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

namespace {
constexpr int SKIP_PAGE_MS = 700;
constexpr int STATUS_BAR_ITEM_PADDING = 4;
constexpr int STATUS_BAR_DESCENDER_CLEARANCE = 4;

uint8_t normalizeProgressBar(const uint8_t progressBar) {
  return progressBar < CrossPointSettings::STATUS_BAR_PROGRESS_BAR_COUNT ? progressBar
                                                                         : CrossPointSettings::HIDE_PROGRESS;
}

uint8_t normalizeProgressBarThickness(const uint8_t thickness) {
  return thickness < CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
             ? thickness
             : CrossPointSettings::PROGRESS_BAR_NORMAL;
}

uint8_t normalizeStatusBarPosition(const uint8_t position) {
  return position < CrossPointSettings::STATUS_BAR_POSITION_COUNT ? position
                                                                  : CrossPointSettings::STATUS_BAR_BOTTOM;
}

uint8_t normalizeStatusBarItemsPosition(const uint8_t position) {
  return normalizeStatusBarPosition(position);
}

}  // namespace

#include <BootHeapProbe.h>

// The singleton's ctor is the only global constructor that executes real code
// (LOG_DBG + make_unique of a theme) — bracket it with static-init heap probes (slots 2/3).
static BootHeapProbe s_probePreTheme(2);
UITheme UITheme::instance;
static BootHeapProbe s_probePostTheme(3);

// The ladder is indexed by UI_FONT_SIZE but does not depend on it (see UiFontScale.h); this is
// the one place the two meet, so it is the one place that can check they agree.
static_assert(UiFontLadder::STEP_COUNT == CrossPointSettings::UI_FONT_SIZE_COUNT,
              "every UI_FONT_SIZE needs a row in UiFontLadder::STEPS");

UiFontLadder::Step UITheme::fontGrowth() { return UiFontLadder::growthAt(SETTINGS.uiFontSize); }

UITheme::UITheme() {
  LOG_DBG("UI", "Using Lyra theme");
  currentTheme = std::make_unique<LyraTheme>();
  currentMetrics = LyraMetrics::values;
  UiFontLadder::applyTo(currentMetrics, fontGrowth());
}

void UITheme::reload() {
  if (!currentTheme) {
    currentTheme = std::make_unique<LyraTheme>();
  }
  currentMetrics = LyraMetrics::values;
  UiFontLadder::applyTo(currentMetrics, fontGrowth());
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = getContentRect(renderer, hasButtonHints, /*hasSideHints=*/false);
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints) {
    reservedHeight += metrics.verticalSpacing;
  }
  const int availableHeight = contentRect.height - reservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

Rect UITheme::getContentRect(const GfxRenderer& renderer, bool hasBottomHints, bool hasSideHints) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const int bh = hasBottomHints ? metrics.buttonHintsHeight : 0;
  const int sw = hasSideHints ? metrics.sideButtonHintsWidth : 0;

  int top = 0, right = 0, bottom = 0, left = 0;
  switch (renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      bottom = bh;
      right = sw;
      break;
    case GfxRenderer::PortraitInverted:
      top = bh;
      left = sw;
      break;
    case GfxRenderer::LandscapeClockwise:
      left = bh;
      bottom = sw;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      right = bh;
      top = sw;
      break;
  }

  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  return Rect{left, top, w - left - right, h - top - bottom};
}

std::string UITheme::makeSeparatorTitle(const std::string& title) { return std::string("__") + title; }

std::string UITheme::makeSeparatorTitle(StrId labelId) { return std::string("__") + I18N.get(labelId); }

bool UITheme::isSeparatorTitle(const std::string& title) { return title.rfind("__", 0) == 0; }

std::string UITheme::stripSeparatorTitle(const std::string& title) {
  return isSeparatorTitle(title) ? title.substr(2) : title;
}

std::function<bool(int)> UITheme::makeSelectablePredicate(int total, std::function<std::string(int)> titleGetter) {
  return
      [total, titleGetter](int index) { return index >= 0 && index < total && !isSeparatorTitle(titleGetter(index)); };
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int width, int height) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(width) + "x" + std::to_string(height));
  }
  return coverBmpPath;
}


UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasJpgExtension(filename) ||
      FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getProgressBarHeight(const uint8_t progressBar, const uint8_t thickness) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const uint8_t normalizedProgressBar = normalizeProgressBar(progressBar);
  if (normalizedProgressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS) {
    return 0;
  }
  const uint8_t normalizedThickness = normalizeProgressBarThickness(thickness);
  return ((normalizedThickness + 1) * 2) + metrics.progressBarMarginTop;
}

int UITheme::getStatusBarItemsHeight() {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  return std::max(metrics.statusBarVerticalMargin + (STATUS_BAR_ITEM_PADDING * 2) + STATUS_BAR_DESCENDER_CLEARANCE,
                  metrics.batteryHeight + (STATUS_BAR_ITEM_PADDING * 2) + STATUS_BAR_DESCENDER_CLEARANCE);
}

bool UITheme::hasStatusBarItems() {
  return SETTINGS.statusBarLeft != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE ||
         SETTINGS.statusBarMiddle != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE ||
         SETTINGS.statusBarRight != CrossPointSettings::STATUS_BAR_SLOT_CONTENT::SLOT_HIDE;
}

int UITheme::getStatusBarTopHeight(const bool forceStatusItems) {
  const uint8_t pos = normalizeStatusBarPosition(SETTINGS.statusBarPosition);
  if (pos != CrossPointSettings::STATUS_BAR_POSITION::STATUS_BAR_TOP) {
    return 0;
  }
  const bool showStatusItems = forceStatusItems || hasStatusBarItems();
  const int statusItemsHeight = showStatusItems ? getStatusBarItemsHeight() : 0;
  return getProgressBarHeight(SETTINGS.statusBarProgressBar, CrossPointSettings::PROGRESS_BAR_THIN) +
         statusItemsHeight;
}

int UITheme::getStatusBarBottomHeight(const bool forceStatusItems) {
  const uint8_t pos = normalizeStatusBarPosition(SETTINGS.statusBarPosition);
  if (pos != CrossPointSettings::STATUS_BAR_POSITION::STATUS_BAR_BOTTOM) {
    return 0;
  }
  const bool showStatusItems = forceStatusItems || hasStatusBarItems();
  const int statusItemsHeight = showStatusItems ? getStatusBarItemsHeight() : 0;
  return getProgressBarHeight(SETTINGS.statusBarProgressBar, CrossPointSettings::PROGRESS_BAR_THIN) +
         statusItemsHeight;
}

int UITheme::getStatusBarHeight(const bool forceStatusItems) {
  return getStatusBarTopHeight(forceStatusItems) + getStatusBarBottomHeight(forceStatusItems);
}

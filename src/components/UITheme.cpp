#include "UITheme.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "fontIds.h"

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

uint8_t normalizeStatusBarItemsPosition(const uint8_t position) {
  return position < CrossPointSettings::STATUS_BAR_ITEMS_POSITION_COUNT ? position
                                                                        : CrossPointSettings::STATUS_BAR_ITEMS_BOTTOM;
}
}  // namespace

#include <BootHeapProbe.h>

// The singleton's ctor is the only global constructor that executes real code
// (LOG_DBG + make_unique of a theme) — bracket it with static-init heap probes (slots 2/3).
static BootHeapProbe s_probePreTheme(2);
UITheme UITheme::instance;
static BootHeapProbe s_probePostTheme(3);

UITheme::UITheme() {
  LOG_DBG("UI", "Using Lyra theme");
  currentTheme = std::make_unique<LyraTheme>();
  currentMetrics = &LyraMetrics::values;
}

void UITheme::reload() {
  if (!currentTheme) {
    currentTheme = std::make_unique<LyraTheme>();
    currentMetrics = &LyraMetrics::values;
  }
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

int UITheme::getBookProgressPercent(const RecentBook& book) {
  if (book.path.empty()) {
    return -1;
  }

  std::string cachePath;
  int percentByteOffset = 0;  // byte index of the percent field in progress.bin

  if (FsHelpers::hasEpubExtension(book.path)) {
    cachePath = Epub(book.path, "/.crosspoint").getCachePath();
    percentByteOffset = 6;  // epub: [spineIdx(2), page(2), chapterPageCount(2), percent(1)]
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    cachePath = Xtc(book.path, "/.crosspoint").getCachePath();
    percentByteOffset = 4;  // xtc: [page(4), percent(1)]
  } else {
    return -1;
  }

  FsFile progressFile;
  if (!Storage.openFileForRead("UIT", cachePath + "/progress.bin", progressFile)) {
    return -1;
  }

  uint8_t data[7];
  const int dataSize = progressFile.read(data, 7);
  progressFile.close();

  if (dataSize < percentByteOffset + 1) {
    return -1;  // old format (or pre-render placeholder) without the percent byte
  }

  int percent = static_cast<int>(data[percentByteOffset]);
  if (percent > 100) percent = 100;
  return percent;
}

void UITheme::drawCoverProgressIndicator(const GfxRenderer& renderer, Rect coverRect, int progressPercent) {
  if (progressPercent <= 0) {
    return;  // unread or no data — no indicator
  }

  if (progressPercent >= 100) {
    // Finished: a folded top-right corner. White backing wedge first so the
    // fold reads over dark art, then the solid black fold on top.
    constexpr int size = 18;
    const int right = coverRect.x + coverRect.width - 2;  // stay inside the 1px cover border
    const int top = coverRect.y + 1;
    for (int i = 0; i <= size; ++i) {
      const int haloW = size - i + 2;
      renderer.fillRect(right - haloW + 1, top + i, haloW, 1, false);
    }
    for (int i = 0; i <= size; ++i) {
      const int w = size - i;
      if (w > 0) {
        renderer.fillRect(right - w + 1, top + i, w, 1, true);
      }
    }
    return;
  }

  // In progress: a bar along the bottom edge, black outline + black fill on a
  // white halo so the empty portion stays visible over dark art.
  constexpr int barH = 5;
  const int barX = coverRect.x + 3;
  const int barW = coverRect.width - 6;
  const int barY = coverRect.y + coverRect.height - barH - 3;
  if (barW < 6) {
    return;
  }
  renderer.fillRect(barX - 1, barY - 1, barW + 2, barH + 2, false);
  renderer.drawRect(barX, barY, barW, barH, true);
  const int fillW = (barW - 2) * progressPercent / 100;
  if (fillW > 0) {
    renderer.fillRect(barX + 1, barY + 1, fillW, barH - 2, true);
  }
}

std::string UITheme::formatBookProgressStatus(const RecentBook& /*book*/, int progressPercent) {
  if (progressPercent < 0) {
    return {};
  }
  return std::to_string(progressPercent) + "%";
}

void UITheme::drawCoverProgressBadge(const GfxRenderer& renderer, Rect coverRect, const RecentBook& /*book*/,
                                     int progressPercent) {
  if (progressPercent < 0) {
    return;
  }
  const std::string line1 = std::to_string(progressPercent) + "%";

  constexpr int inset = 6;  // clear the cover's rounded corner + selection ring
  constexpr int padX = 6;
  constexpr int padY = 3;
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  const int w1 = renderer.getTextWidth(SMALL_FONT_ID, line1.c_str());
  const int badgeW = w1 + 2 * padX;
  const int badgeH = textH + 2 * padY;
  const int badgeX = coverRect.x + coverRect.width - badgeW - inset;
  const int badgeY = coverRect.y + inset;

  // White fill + black frame + black text: self-contained contrast on any cover
  // corner (the white background guarantees the black text stays legible, the
  // frame separates the pill from light artwork).
  renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, 4, Color::White);
  renderer.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 1, 4, true);
  renderer.drawText(SMALL_FONT_ID, badgeX + (badgeW - w1) / 2, badgeY + padY, line1.c_str(), true);
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

int UITheme::getStatusBarTopHeight(const bool forceStatusItems) {
  const bool showStatusItems = forceStatusItems || SETTINGS.statusBarChapterPageCount ||
                               SETTINGS.statusBarBookProgressPercentage ||
                               SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE ||
                               SETTINGS.statusBarBattery;
  const uint8_t statusBarItemsPosition = normalizeStatusBarItemsPosition(SETTINGS.statusBarItemsPosition);
  const bool statusItemsAtTop =
      statusBarItemsPosition == CrossPointSettings::STATUS_BAR_ITEMS_POSITION::STATUS_BAR_ITEMS_TOP;
  const int statusItemsHeight = showStatusItems && statusItemsAtTop ? getStatusBarItemsHeight() : 0;
  return getProgressBarHeight(SETTINGS.statusBarUpperProgressBar, SETTINGS.statusBarUpperProgressBarThickness) +
         statusItemsHeight;
}

int UITheme::getStatusBarBottomHeight(const bool forceStatusItems) {
  const bool showStatusItems = forceStatusItems || SETTINGS.statusBarChapterPageCount ||
                               SETTINGS.statusBarBookProgressPercentage ||
                               SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE ||
                               SETTINGS.statusBarBattery;
  const uint8_t statusBarItemsPosition = normalizeStatusBarItemsPosition(SETTINGS.statusBarItemsPosition);
  const bool statusItemsAtBottom =
      statusBarItemsPosition == CrossPointSettings::STATUS_BAR_ITEMS_POSITION::STATUS_BAR_ITEMS_BOTTOM;
  const int statusItemsHeight = showStatusItems && statusItemsAtBottom ? getStatusBarItemsHeight() : 0;
  return getProgressBarHeight(SETTINGS.statusBarLowerProgressBar, SETTINGS.statusBarLowerProgressBarThickness) +
         statusItemsHeight;
}

int UITheme::getStatusBarHeight(const bool forceStatusItems) {
  return getStatusBarTopHeight(forceStatusItems) + getStatusBarBottomHeight(forceStatusItems);
}

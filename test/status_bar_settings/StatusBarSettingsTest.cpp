#include <gtest/gtest.h>

#include "CrossPointSettings.h"

CrossPointSettings CrossPointSettings::instance;

namespace {

using S = CrossPointSettings;

TEST(StatusBarSettingsTest, DefaultStatusBarSettings) {
  // Status bar position default: bottom
  EXPECT_EQ(SETTINGS.statusBarPosition, S::STATUS_BAR_POSITION::STATUS_BAR_BOTTOM);

  // Left slot default: percentage
  EXPECT_EQ(SETTINGS.statusBarLeft, S::STATUS_BAR_SLOT_CONTENT::SLOT_BOOK_PERCENTAGE);

  // Middle slot default: chapter title
  EXPECT_EQ(SETTINGS.statusBarMiddle, S::STATUS_BAR_SLOT_CONTENT::SLOT_CHAPTER_TITLE);

  // Right slot default: page count
  EXPECT_EQ(SETTINGS.statusBarRight, S::STATUS_BAR_SLOT_CONTENT::SLOT_PAGE_COUNT);

  // Progress bar default: shown (BOOK_PROGRESS)
  EXPECT_EQ(SETTINGS.statusBarProgressBar, S::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS);
}

}  // namespace

// keyboard_layouts: which layouts the language key cycles through, and which one
// a keyboard opens on, for each UI language.

#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardLayoutSet.h"

CrossPointSettings CrossPointSettings::instance;

// The real lib/I18n/I18n.cpp cannot link on the host -- it reaches into the flash
// language partition -- and KeyboardLayoutSet only ever asks for the language.
I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}
void I18n::setLanguage(const Language lang) { _language = lang; }

namespace {

using freeink::ui::KeyboardLayoutId;

// Restores English and an unconfigured set after each test, so the order they run in cannot matter.
class KeyboardLayoutSetTest : public ::testing::Test {
 protected:
  void TearDown() override {
    I18N.setLanguage(Language::EN);
    SETTINGS.keyboardLayouts = 0;
  }

  static uint16_t bitOf(const KeyboardLayoutId id) {
    for (uint8_t i = 0; i < keyboard_layouts::COUNT; ++i) {
      if (keyboard_layouts::ALL[i].id == id) return keyboard_layouts::bitAt(i);
    }
    return 0;
  }

  static int enabledCount() { return __builtin_popcount(keyboard_layouts::enabled()); }
};

TEST_F(KeyboardLayoutSetTest, EnglishGetsQwertyAloneAndNoLanguageKey) {
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn);
  EXPECT_EQ(enabledCount(), 1);
  // One layout: the key has nowhere to go, so next() stays put.
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::QwertyEn), KeyboardLayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutSetTest, NoScriptTheUiFontsCannotDrawIsOffered) {
  // The SDK has Hebrew and Arabic; Inter UI has neither script.
  for (const auto& info : keyboard_layouts::ALL) {
    EXPECT_NE(info.id, KeyboardLayoutId::HebrewIl);
    EXPECT_NE(info.id, KeyboardLayoutId::ArabicAr);
  }
}

// ---------------------------------------------------------------- configured on the settings screen

TEST_F(KeyboardLayoutSetTest, EnglishUiWithCyrillicSwitchedOnGetsTheLanguageKey) {
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::QwertyEn) | bitOf(KeyboardLayoutId::CyrillicRu);
  EXPECT_EQ(enabledCount(), 2);
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn);
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::QwertyEn), KeyboardLayoutId::CyrillicRu);
  EXPECT_EQ(keyboard_layouts::next(KeyboardLayoutId::CyrillicRu), KeyboardLayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutSetTest, AConfiguredSetAlwaysKeepsALatinLayout) {
  // A hand-edited file can switch every Latin layout off; URL and password fields still need one.
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::CyrillicUk);
  EXPECT_TRUE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::QwertyEn));
  EXPECT_TRUE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::CyrillicUk));
  // A Latin layout other than English satisfies it without adding English.
  SETTINGS.keyboardLayouts = bitOf(KeyboardLayoutId::QwertzDe) | bitOf(KeyboardLayoutId::CyrillicRu);
  EXPECT_FALSE(keyboard_layouts::enabled() & bitOf(KeyboardLayoutId::QwertyEn));
}

TEST_F(KeyboardLayoutSetTest, BitsNamingNoLayoutAreIgnored) {
  // From a file written by a build with more layouts: only unknown bits reads as unconfigured.
  SETTINGS.keyboardLayouts = 0x8000;
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::QwertyEn);
  EXPECT_EQ(enabledCount(), 1);
}

}  // namespace

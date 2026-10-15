#pragma once

#include <FreeInkApp.h>

#include "components/UiAppHost.h"

// One dialog, themed once, for every screen that asks a yes/no question -- or a question with a
// third answer between those two.
//
// ConfirmationActivity draws it, and every question goes through that activity -- including the
// ones that are one step of a screen's own work (Check for Updates, Clear Cache), which push it and
// act on the result. So the keys, the hints and the touch targets are the same on every question.
//
// Kept apart from the activity because it is the part with a trap in it: Screen::dialog() passes
// OptionDialogProps straight through WITHOUT substituting text styles, so a caller would otherwise
// have to remember that an unset TextStyle measures against font slot 0 and theme all five slots
// by hand.
namespace ConfirmDialog {

// optionDialog's three text slots, top to bottom. Any may be null.
//
// `title` is a small caption, `headline` a prominent bold line (a book title, "New update"),
// `message` a body line. The wrap caps are deliberate rather than unbounded: a book title or a
// cache warning is not guaranteed short, and optionDialogHeight() honours the same maxLines, so
// what the panel reserves is what gets drawn.
struct Spec {
  const char* title = nullptr;
  const char* headline = nullptr;
  const char* message = nullptr;
  const char* cancelLabel = nullptr;
  const char* acceptLabel = nullptr;
  // Optional: null draws the plain two-answer dialog.
  const char* alternateLabel = nullptr;
  freeink::ui::ActionId cancelAction = freeink::ui::NO_ACTION;
  freeink::ui::ActionId acceptAction = freeink::ui::NO_ACTION;
  freeink::ui::ActionId alternateAction = freeink::ui::NO_ACTION;
  // Default 1 keeps `title` a caption, which is what optionDialog assumes. A caller whose title is
  // really a sentence (the clear-cache warning) raises it.
  uint8_t titleMaxLines = 1;
  uint8_t headlineMaxLines = 3;
  uint8_t messageMaxLines = 4;
};

// Answers are displayed exclusively on the physical button hint strip at the bottom of the screen.
// ConfirmDialog renders the modal card containing the title, headline, and message text.
inline void draw(UiAppHost::UiScreen& screen, const Spec& spec) {
  namespace fui = freeink::ui;
  const auto& theme = screen.theme();

  fui::OptionDialogProps props;
  props.title = spec.title;
  props.headline = spec.headline;
  props.message = spec.message;
  props.options = nullptr;
  props.optionCount = 0;

  props.titleText = theme.smallText;
  props.titleText.align = fui::TextAlign::Center;
  props.titleText.maxLines = spec.titleMaxLines;
  props.headlineText = theme.titleText;
  props.headlineText.bold = true;
  props.headlineText.align = fui::TextAlign::Center;
  props.headlineText.maxLines = spec.headlineMaxLines;
  props.messageText = theme.bodyText;
  props.messageText.align = fui::TextAlign::Center;
  props.messageText.maxLines = spec.messageMaxLines;
  props.gap = theme.spaceMd;

  // The panel is sized to its text and is not clipped, so a long message (release notes, in
  // landscape at a large UI font) could push it off the screen. The message gives up lines first,
  // its last one ending in an ellipsis, until the panel fits. Width as Screen::dialog()'s default.
  const auto safe = screen.frame().safeRect();
  const auto width = static_cast<int16_t>(safe.width * 4 / 5);
  while (props.message && props.messageText.maxLines > 1 &&
         fui::optionDialogHeight(screen.target(), props, width) > safe.height) {
    --props.messageText.maxLines;
  }
  screen.dialog(props, width);
}

}  // namespace ConfirmDialog

#include "SliderPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;

constexpr int kBarWidth = 360;
constexpr int kBarHeight = 16;
constexpr int kBarY = 140;
constexpr int kTrackInset = 2;

int barLeft(const GfxRenderer& renderer) {
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  return contentRect.x + (contentRect.width - kBarWidth) / 2;
}

int fillWidthFor(int val, int width, int minVal, int maxVal) {
  if (maxVal <= minVal) return 0;
  const int usable = width - kTrackInset * 2;
  const int clamped = std::max(minVal, std::min(maxVal, val));
  return (clamped - minVal) * usable / (maxVal - minVal);
}
}  // namespace

void SliderPickerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SliderPickerActivity::onExit() { Activity::onExit(); }

void SliderPickerActivity::adjustValue(const int delta) {
  value += delta;
  if (value < cfg.minValue) value = cfg.minValue;
  if (value > cfg.maxValue) value = cfg.maxValue;
  requestUpdate();
}

void SliderPickerActivity::loop() {
  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    if (ev.button == MappedInputManager::Button::Back && ev.type == ButtonEventManager::PressType::Short) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }

    if (ev.button == MappedInputManager::Button::Confirm && ev.type == ButtonEventManager::PressType::Short) {
      setResult(PercentResult{value});
      finish();
      return;
    }

    if (MappedInputManager::isDirection(ev.button, MappedInputManager::Direction::Left) &&
        ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(-kSmallStep);
      return;
    }

    if (MappedInputManager::isDirection(ev.button, MappedInputManager::Direction::Right) &&
        ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(kSmallStep);
      return;
    }
  }

  buttonNavigator.onPressAndContinuous(ButtonNavigator::getStepPreviousButtons(), [this] { adjustValue(kLargeStep); });
  buttonNavigator.onPressAndContinuous(ButtonNavigator::getStepNextButtons(), [this] { adjustValue(-kLargeStep); });
}

void SliderPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, I18N.get(cfg.titleId), true, EpdFontFamily::BOLD);

  std::string valueText;
  if (!cfg.zeroLabel.empty() && value == cfg.minValue) {
    valueText = cfg.zeroLabel;
  } else {
    valueText = std::to_string(value) + cfg.suffix;
  }
  renderer.drawCenteredText(UI_12_FONT_ID, 90, valueText.c_str(), true, EpdFontFamily::BOLD);

  const int barX = barLeft(renderer);

  renderer.drawRect(barX, kBarY, kBarWidth, kBarHeight);

  const int fillWidth = fillWidthFor(value, kBarWidth, cfg.minValue, cfg.maxValue);
  if (fillWidth > 0) {
    renderer.fillRect(barX + kTrackInset, kBarY + 2, fillWidth, kBarHeight - 4);
  }

  const int knobX = barX + kTrackInset + fillWidth - 2;
  renderer.fillRect(knobX, kBarY - 4, 4, kBarHeight + 8, true);

  renderer.drawCenteredText(SMALL_FONT_ID, kBarY + 30, I18N.get(cfg.hintId), true);

  // The slider runs across the screen, so - / + ride logical Left/Right and move to whichever
  // button pair lies on that axis; the coarse step rides logical Up/Down and is unlabelled.
  const auto hints = mappedInput.mapHints(tr(STR_BACK), tr(STR_SELECT), "-", "+", "", "");
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);

  renderer.displayBuffer();
}

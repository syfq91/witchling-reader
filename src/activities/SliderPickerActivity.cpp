#include "SliderPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;

constexpr fui::ActionId ACTION_DEC = 1;
constexpr fui::ActionId ACTION_INC = 2;
constexpr fui::ActionId ACTION_SLIDER = 3;
}  // namespace

SliderPickerActivity::SliderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Config config)
    : Activity("SliderPicker", renderer, mappedInput),
      UiAppHost(renderer),
      value(std::max(config.minValue, std::min(config.maxValue, config.initialValue))),
      cfg(std::move(config)) {}

void SliderPickerActivity::updateValueText() {
  if (!cfg.zeroLabel.empty() && value == cfg.minValue) {
    valueText = cfg.zeroLabel;
  } else if (!cfg.maxLabel.empty() && value == cfg.maxValue) {
    valueText = cfg.maxLabel;
  } else {
    valueText = std::to_string(value) + cfg.suffix;
  }
}

void SliderPickerActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_DEC, &SliderPickerActivity::onDecrementEvent, this);
  app.on(ACTION_INC, &SliderPickerActivity::onIncrementEvent, this);
  app.on(ACTION_SLIDER, &SliderPickerActivity::onSliderEvent, this);
  app.setScreen(&SliderPickerActivity::screenTrampoline, this);
  updateValueText();
  requestUpdate();
}

void SliderPickerActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void SliderPickerActivity::adjustValue(const int delta) {
  const int before = value;
  value += delta;
  if (value < cfg.minValue) value = cfg.minValue;
  if (value > cfg.maxValue) value = cfg.maxValue;
  updateValueText();
  if (value != before && cfg.onPreview) cfg.onPreview(value);
  requestUpdate();
}

void SliderPickerActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<SliderPickerActivity*>(user)->buildScreen(screen);
}

void SliderPickerActivity::onDecrementEvent(const fui::ActionEvent&, void* user) {
  static_cast<SliderPickerActivity*>(user)->adjustValue(-kSmallStep);
}

void SliderPickerActivity::onIncrementEvent(const fui::ActionEvent&, void* user) {
  static_cast<SliderPickerActivity*>(user)->adjustValue(kSmallStep);
}

void SliderPickerActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SliderPickerActivity*>(user);
  if (event.dragPermille >= 0) {
    const int range = self->cfg.maxValue - self->cfg.minValue;
    if (range > 0) {
      const int newVal = self->cfg.minValue + (event.dragPermille * range + 500) / 1000;
      self->adjustValue(newVal - self->value);
    }
  }
}

void SliderPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing * 2));

  // Prominent value readout
  fui::TextAreaProps readout;
  readout.text = valueText.c_str();
  readout.style = screen.theme().titleText;
  readout.style.bold = true;
  readout.style.align = fui::TextAlign::Center;
  readout.style.maxLines = 1;
  readout.showCaret = false;
  screen.textArea(readout, 40);

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing * 2));

  // Slider row: [-] [===O===] [+]
  fui::SliderRowProps row;
  row.sliderValue = value - cfg.minValue;
  row.max = cfg.maxValue - cfg.minValue;
  row.decrement = ACTION_DEC;
  row.increment = ACTION_INC;
  row.sliderAction = ACTION_SLIDER;
  row.decrementLabel = "-";
  row.incrementLabel = "+";
  row.buttonText = screen.theme().titleText;
  row.buttonText.bold = true;
  row.buttonText.align = fui::TextAlign::Center;
  screen.sliderRow(row, 48);

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing * 2));

  // Step hint text
  fui::TextAreaProps hint;
  hint.text = I18N.get(cfg.hintId);
  hint.style = screen.theme().smallText;
  hint.style.align = fui::TextAlign::Center;
  hint.style.maxLines = 2;
  hint.showCaret = false;
  screen.textArea(hint);
}

void SliderPickerActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

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

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 I18N.get(cfg.titleId));

  renderUi();

  const auto hints = mappedInput.mapHints(tr(STR_BACK), tr(STR_SELECT), "-", "+", "", "");
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);

  renderer.displayBuffer();
}

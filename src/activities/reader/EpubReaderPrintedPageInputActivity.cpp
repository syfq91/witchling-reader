#include "EpubReaderPrintedPageInputActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "ButtonEventManager.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DEC = 1;
constexpr fui::ActionId ACTION_INC = 2;
constexpr fui::ActionId ACTION_LEFT = 3;
constexpr fui::ActionId ACTION_RIGHT = 4;
}  // namespace

EpubReaderPrintedPageInputActivity::EpubReaderPrintedPageInputActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const int initialValue, const int minValue,
                                                                       const int maxValue)
    : Activity("EpubReaderPrintedPageInput", renderer, mappedInput),
      UiAppHost(renderer),
      value(initialValue),
      minValue(minValue),
      maxValue(maxValue) {
  clampValue();
  cursorDigit = 0;  // ones place
}

int EpubReaderPrintedPageInputActivity::powTen(const int exponent) {
  int result = 1;
  for (int i = 0; i < exponent; i++) result *= 10;
  return result;
}

int EpubReaderPrintedPageInputActivity::digitCount() const {
  int n = (value > 0) ? value : 1;
  int count = 0;
  while (n > 0) {
    count++;
    n /= 10;
  }
  return count;
}

int EpubReaderPrintedPageInputActivity::maxCursorDigit() const {
  int n = (maxValue > 0) ? maxValue : 1;
  int count = 0;
  while (n > 0) {
    count++;
    n /= 10;
  }
  return count - 1;  // 0-based: ones=0, tens=1, hundreds=2, ...
}

void EpubReaderPrintedPageInputActivity::clampValue() {
  if (value < minValue) value = minValue;
  if (value > maxValue) value = maxValue;
}

void EpubReaderPrintedPageInputActivity::adjustDigit(const int delta) {
  value += delta * powTen(cursorDigit);
  clampValue();
  if (cursorDigit > maxCursorDigit()) cursorDigit = maxCursorDigit();
  requestUpdate();
}

void EpubReaderPrintedPageInputActivity::adjustDigitTimes(const int multiplier, const int sign) {
  value += sign * multiplier * powTen(cursorDigit);
  clampValue();
  if (cursorDigit > maxCursorDigit()) cursorDigit = maxCursorDigit();
  requestUpdate();
}

void EpubReaderPrintedPageInputActivity::moveCursor(const int delta) {
  cursorDigit += delta;
  if (cursorDigit < 0) cursorDigit = 0;
  if (cursorDigit > maxCursorDigit()) cursorDigit = maxCursorDigit();
  requestUpdate();
}

void EpubReaderPrintedPageInputActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<EpubReaderPrintedPageInputActivity*>(user)->buildScreen(screen);
}

void EpubReaderPrintedPageInputActivity::onDecrementEvent(const fui::ActionEvent&, void* user) {
  static_cast<EpubReaderPrintedPageInputActivity*>(user)->adjustDigit(-1);
}

void EpubReaderPrintedPageInputActivity::onIncrementEvent(const fui::ActionEvent&, void* user) {
  static_cast<EpubReaderPrintedPageInputActivity*>(user)->adjustDigit(1);
}

void EpubReaderPrintedPageInputActivity::onCursorLeftEvent(const fui::ActionEvent&, void* user) {
  static_cast<EpubReaderPrintedPageInputActivity*>(user)->moveCursor(1);
}

void EpubReaderPrintedPageInputActivity::onCursorRightEvent(const fui::ActionEvent&, void* user) {
  static_cast<EpubReaderPrintedPageInputActivity*>(user)->moveCursor(-1);
}

void EpubReaderPrintedPageInputActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_DEC, &EpubReaderPrintedPageInputActivity::onDecrementEvent, this);
  app.on(ACTION_INC, &EpubReaderPrintedPageInputActivity::onIncrementEvent, this);
  app.on(ACTION_LEFT, &EpubReaderPrintedPageInputActivity::onCursorLeftEvent, this);
  app.on(ACTION_RIGHT, &EpubReaderPrintedPageInputActivity::onCursorRightEvent, this);
  app.setScreen(&EpubReaderPrintedPageInputActivity::screenTrampoline, this);

  buttonEvents.forceDoubleAction(MappedInputManager::buttonFor(MappedInputManager::Direction::Up), true);
  buttonEvents.forceDoubleAction(MappedInputManager::buttonFor(MappedInputManager::Direction::Down), true);
  requestUpdate();
}

void EpubReaderPrintedPageInputActivity::onExit() {
  buttonEvents.forceDoubleAction(MappedInputManager::buttonFor(MappedInputManager::Direction::Up), false);
  buttonEvents.forceDoubleAction(MappedInputManager::buttonFor(MappedInputManager::Direction::Down), false);
  closeRouting();
  Activity::onExit();
}

void EpubReaderPrintedPageInputActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  // Reserve space for numeric readout and active digit underline rendered in afterUiRender
  screen.spacer(90);

  const int16_t lineH = screen.target().lineHeight(screen.theme().smallText.font);

  // Range hint: "Range: 1 - 305"
  char rangeBuf[48];
  snprintf(rangeBuf, sizeof(rangeBuf), tr(STR_GO_TO_PRINTED_PAGE_RANGE), static_cast<unsigned>(minValue),
           static_cast<unsigned>(maxValue));
  fui::TextAreaProps rangeHint;
  rangeHint.text = rangeBuf;
  rangeHint.style = screen.theme().smallText;
  rangeHint.style.align = fui::TextAlign::Center;
  rangeHint.showCaret = false;
  screen.textArea(rangeHint, lineH);

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Step hint: "Up/Down: change digit (double-click: ±10)"
  fui::TextAreaProps stepHint;
  stepHint.text = tr(STR_GO_TO_PRINTED_PAGE_HINT);
  stepHint.style = screen.theme().smallText;
  stepHint.style.align = fui::TextAlign::Center;
  stepHint.showCaret = false;
  screen.textArea(stepHint, static_cast<int16_t>(lineH * 2));
}

void EpubReaderPrintedPageInputActivity::afterUiRender() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  const std::string rawValueText = std::to_string(value);
  const int visibleDigits = std::max(digitCount(), cursorDigit + 1);
  std::string valueText;
  for (int i = 0; i < visibleDigits - digitCount(); i++) valueText += "0";
  valueText += rawValueText;

  const int valueY = contentRect.y + metrics.topPadding + metrics.headerHeight + 20;
  renderer.drawCenteredText(UI_12_FONT_ID, valueY, valueText.c_str(), true, EpdFontFamily::BOLD);

  const int totalWidth = renderer.getTextWidth(UI_12_FONT_ID, valueText.c_str());
  const int screenWidth = renderer.getScreenWidth();
  const int startX = (screenWidth - totalWidth) / 2;
  const int digitIndexFromLeft = visibleDigits - 1 - cursorDigit;
  const int avgDigitWidth = (visibleDigits > 0) ? totalWidth / visibleDigits : 0;
  const int underlineX = startX + digitIndexFromLeft * avgDigitWidth;
  const int underlineWidth = avgDigitWidth;
  const int underlineY = valueY + renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.fillRect(underlineX, underlineY, underlineWidth, 3, true);
}

void EpubReaderPrintedPageInputActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    setResult(PrintedPageResult{std::to_string(value)});
    finish();
    return;
  }
  if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Left)) {
    moveCursor(1);
    return;
  }
  if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
    moveCursor(-1);
    return;
  }

  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    if (MappedInputManager::isDirection(ev.button, MappedInputManager::Direction::Up)) {
      if (ev.type == ButtonEventManager::PressType::Short) {
        adjustDigit(1);
      } else if (ev.type == ButtonEventManager::PressType::Double) {
        adjustDigitTimes(10, 1);
      }
    } else if (MappedInputManager::isDirection(ev.button, MappedInputManager::Direction::Down)) {
      if (ev.type == ButtonEventManager::PressType::Short) {
        adjustDigit(-1);
      } else if (ev.type == ButtonEventManager::PressType::Double) {
        adjustDigitTimes(10, -1);
      }
    }
  }
}

void EpubReaderPrintedPageInputActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_GO_TO_PRINTED_PAGE));

  renderUi();
  afterUiRender();

  const auto hints = mappedInput.mapHints(tr(STR_BACK), tr(STR_SELECT), "-", "+", "", "");
  GUI.drawButtonHints(renderer, hints.front.btn1, hints.front.btn2, hints.front.btn3, hints.front.btn4);
  GUI.drawSideButtonHints(renderer, hints.side.up, hints.side.down);

  renderer.displayBuffer();
}

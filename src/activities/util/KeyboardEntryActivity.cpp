#include "KeyboardEntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>
#include <components/keyboard/keyboard.h>
#include <components/keyboard/qwerty-keyboard.h>
#include <components/text/text-field.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  cursorPos = text.length();
  shifted = false;
  symbols = false;
  urlMode = false;
  urlNavIndex = 0;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  hintVisible = false;
  hintShowTime = 0;
  confirmHeld = false;
  confirmLongHandled = false;
  upHeld = false;
  upLongHandled = false;
  downHeld = false;
  downLongHandled = false;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  delPressCount = 0;

  keyboardNav.reset(1, 0);  // start on 'q' or middle row

  resetUi();
  app.setScreen(screenTrampoline, this);
  app.on(ACTION_BACK, actionTrampoline, this);
  app.on(ACTION_KEY, actionTrampoline, this);
  app.on(ACTION_SHIFT, actionTrampoline, this);
  app.on(ACTION_MODE, actionTrampoline, this);
  app.on(ACTION_DEL, actionTrampoline, this);
  app.on(ACTION_OK, actionTrampoline, this);
  app.on(ACTION_TOGGLE_PASSWORD, actionTrampoline, this);
  app.on(ACTION_URL_SNIPPET, actionTrampoline, this);
  app.on(ACTION_TOGGLE_URL_MODE, actionTrampoline, this);

  requestUpdate();
}

void KeyboardEntryActivity::onExit() {
  resetUi();
  Activity::onExit();
}

void KeyboardEntryActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<KeyboardEntryActivity*>(user)->buildScreen(screen);
}

void KeyboardEntryActivity::actionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  static_cast<KeyboardEntryActivity*>(user)->handleAction(event);
}

bool KeyboardEntryActivity::insertChar(const char c) {
  if (c == '\0') return true;
  if (maxLength != 0 && text.length() >= maxLength) return true;
  if (cursorPos > text.length()) cursorPos = text.length();

  text.insert(cursorPos, 1, c);
  cursorPos++;
  return true;
}

void KeyboardEntryActivity::insertString(const std::string& str) {
  if (str.empty()) return;
  if (maxLength != 0 && text.length() + str.length() > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();

  text.insert(cursorPos, str);
  cursorPos += str.length();
}

void KeyboardEntryActivity::handleAction(const freeink::ui::ActionEvent& event) {
  const auto& layout = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, shifted, symbols, numberRow);

  switch (event.action) {
    case ACTION_KEY: {
      delPressCount = 0;
      hintVisible = false;
      const char* out = event.longPress ? freeink::ui::keyboardAltOutputFor(layout, event.value)
                                        : freeink::ui::keyboardOutputFor(layout, event.value);
      if (!out) out = freeink::ui::keyboardOutputFor(layout, event.value);
      if (out) {
        insertString(out);
        if (shifted && !symbols) shifted = false;
        keyboardNav.syncToValue(layout, event.value);
        requestUpdate();
      }
      break;
    }
    case ACTION_SHIFT:
      delPressCount = 0;
      hintVisible = false;
      shifted = !shifted;
      requestUpdate();
      break;
    case ACTION_MODE:
      delPressCount = 0;
      hintVisible = false;
      symbols = !symbols;
      shifted = false;
      requestUpdate();
      break;
    case ACTION_DEL:
      if (event.longPress) {
        text.clear();
        cursorPos = 0;
        delPressCount = 0;
        hintVisible = false;
      } else {
        delPressCount++;
        if (delPressCount >= 2) {
          hintVisible = true;
          hintShowTime = millis();
        }
        if (cursorPos > 0 && !text.empty()) {
          text.erase(cursorPos - 1, 1);
          cursorPos--;
        }
      }
      requestUpdate();
      break;
    case ACTION_OK:
      delPressCount = 0;
      hintVisible = false;
      onComplete(text);
      break;
    case ACTION_BACK:
      onCancel();
      break;
    case ACTION_TOGGLE_PASSWORD:
      passwordVisible = !passwordVisible;
      requestUpdate();
      break;
    case ACTION_URL_SNIPPET:
      delPressCount = 0;
      hintVisible = false;
      if (event.value >= 0 && event.value < URL_SNIPPET_COUNT) {
        insertString(urlSnippets[event.value]);
        requestUpdate();
      }
      break;
    case ACTION_TOGGLE_URL_MODE:
      delPressCount = 0;
      hintVisible = false;
      urlMode = !urlMode;
      requestUpdate();
      break;
    default:
      break;
  }
}

void KeyboardEntryActivity::loop() {
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route) {
    return;
  }

  const auto& layout = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, shifted, symbols, numberRow);

  if (urlMode) {
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Up)) {
      urlNavIndex = (urlNavIndex - 3 + URL_SNIPPET_COUNT) % URL_SNIPPET_COUNT;
      requestUpdate();
    }
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Down)) {
      urlNavIndex = (urlNavIndex + 3) % URL_SNIPPET_COUNT;
      requestUpdate();
    }
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Left)) {
      urlNavIndex = (urlNavIndex - 1 + URL_SNIPPET_COUNT) % URL_SNIPPET_COUNT;
      requestUpdate();
    }
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
      urlNavIndex = (urlNavIndex + 1) % URL_SNIPPET_COUNT;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (urlNavIndex >= 0 && urlNavIndex < URL_SNIPPET_COUNT) {
        insertString(urlSnippets[urlNavIndex]);
        requestUpdate();
      }
    }
  } else {
    // Normal keyboard navigation with D-pad
    if (!cursorMode && mappedInput.wasLogicalPressed(MappedInputManager::Direction::Up)) {
      upHeld = true;
      upLongHandled = false;
    }
    if (upHeld && !upLongHandled && mappedInput.isLogicalPressed(MappedInputManager::Direction::Up) &&
        mappedInput.getHeldTime() > LONG_PRESS_MS) {
      cursorMode = true;
      upLongHandled = true;
      hintVisible = true;
      hintShowTime = millis();
      requestUpdate();
    }
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Up)) {
      if (upHeld && !upLongHandled && !cursorMode) {
        keyboardNav.moveRow(layout, -1);
        requestUpdate();
      }
      upHeld = false;
      upLongHandled = false;
    }

    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Down)) {
      downHeld = true;
      if (cursorMode) {
        togglePos = false;
        passwordVisible = false;
        cursorMode = false;
        hintVisible = false;
        downLongHandled = true;
        requestUpdate();
      } else {
        downLongHandled = false;
      }
    }
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Down)) {
      if (downHeld && !downLongHandled && !cursorMode) {
        keyboardNav.moveRow(layout, 1);
        requestUpdate();
      }
      downHeld = false;
      downLongHandled = false;
    }

    buttonNavigator.onPressAndContinuous({MappedInputManager::buttonFor(MappedInputManager::Direction::Left)}, [this, &layout] {
      if (cursorMode) return;
      keyboardNav.moveCol(layout, -1);
      requestUpdate();
    });
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Left)) {
      if (cursorMode) {
        if (togglePos) {
          cursorPos = savedCursorPos;
          togglePos = false;
          requestUpdate();
        } else if (cursorPos > 0) {
          cursorPos--;
          requestUpdate();
        }
      }
    }

    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
      if (cursorMode && inputType == InputType::Password && !togglePos) {
        rightHeld = true;
        rightLongHandled = false;
        rightStartCursorPos = cursorPos;
      }
    }
    buttonNavigator.onPressAndContinuous({MappedInputManager::buttonFor(MappedInputManager::Direction::Right)}, [this, &layout] {
      if (cursorMode) return;
      keyboardNav.moveCol(layout, 1);
      requestUpdate();
    });
    if (rightHeld && !rightLongHandled && mappedInput.isLogicalPressed(MappedInputManager::Direction::Right) &&
        mappedInput.getHeldTime() > LONG_PRESS_MS) {
      if (cursorMode && inputType == InputType::Password && !togglePos) {
        savedCursorPos = rightStartCursorPos;
        togglePos = true;
        rightLongHandled = true;
        requestUpdate();
      }
    }
    if (mappedInput.wasLogicalReleased(MappedInputManager::Direction::Right)) {
      if (cursorMode && inputType == InputType::Password) {
        rightHeld = false;
        rightLongHandled = false;
      }
      if (cursorMode && !togglePos && cursorPos < text.length()) {
        cursorPos++;
        requestUpdate();
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      confirmHeld = true;
      confirmLongHandled = false;
    }
    if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      const auto* key = keyboardNav.selected(layout);
      if (key && key->kind == freeink::ui::KeyKind::Delete && mappedInput.getHeldTime() > DEL_LONG_PRESS_MS) {
        text.clear();
        cursorPos = 0;
        confirmLongHandled = true;
        requestUpdate();
      } else if (key && key->kind == freeink::ui::KeyKind::Normal && mappedInput.getHeldTime() > LONG_PRESS_MS) {
        const char* alt = freeink::ui::keyboardAltOutputFor(layout, key->value);
        if (alt) {
          insertString(alt);
          confirmLongHandled = true;
          requestUpdate();
        }
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmHeld && !confirmLongHandled) {
        if (cursorMode) {
          if (inputType == InputType::Password && togglePos) {
            passwordVisible = !passwordVisible;
            requestUpdate();
          }
        } else {
          const auto* key = keyboardNav.selected(layout);
          if (key) {
            switch (key->kind) {
              case freeink::ui::KeyKind::Shift:
                delPressCount = 0;
                hintVisible = false;
                shifted = !shifted;
                requestUpdate();
                break;
              case freeink::ui::KeyKind::Mode:
                delPressCount = 0;
                hintVisible = false;
                symbols = !symbols;
                shifted = false;
                requestUpdate();
                break;
              case freeink::ui::KeyKind::Space:
                delPressCount = 0;
                hintVisible = false;
                if (inputType == InputType::Url) {
                  urlMode = !urlMode;
                } else {
                  insertChar(' ');
                }
                requestUpdate();
                break;
              case freeink::ui::KeyKind::Delete:
                delPressCount++;
                if (delPressCount >= 2) {
                  hintVisible = true;
                  hintShowTime = millis();
                }
                if (cursorPos > 0 && !text.empty()) {
                  text.erase(cursorPos - 1, 1);
                  cursorPos--;
                  requestUpdate();
                }
                break;
              case freeink::ui::KeyKind::Ok:
                delPressCount = 0;
                hintVisible = false;
                onComplete(text);
                return;
              default: {
                delPressCount = 0;
                hintVisible = false;
                const char* out = freeink::ui::keyboardOutputFor(layout, key->value);
                if (out) {
                  insertString(out);
                  if (shifted && !symbols) shifted = false;
                  requestUpdate();
                }
                break;
              }
            }
          }
        }
      }
      confirmHeld = false;
      confirmLongHandled = false;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  if (hintVisible && !cursorMode && millis() - hintShowTime > 4000) {
    hintVisible = false;
    requestUpdate();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  resetUi();
  renderUi();
  renderer.displayBuffer();
}

void KeyboardEntryActivity::buildScreen(UiScreen& screen) {
  using freeink::ui::Rect;

  screen.header(title.c_str());

  // Input field row
  const Rect inputRow = screen.take(freeink::ui::LayoutAnchor::Top, 44, 6);

  std::string displayText;
  if (inputType == InputType::Password && !passwordVisible) {
    if (cursorMode) {
      displayText = std::string(text.length(), '*');
    } else {
      displayText = text;
      const size_t revealPos = (text.length() > 0 && cursorPos > 0) ? cursorPos - 1 : std::string::npos;
      for (size_t i = 0; i < displayText.length(); i++) {
        if (i != revealPos) {
          displayText[i] = '*';
        }
      }
    }
  } else {
    displayText = text;
  }

  freeink::ui::TextFieldProps fieldProps;
  fieldProps.text = displayText.c_str();
  fieldProps.textStyle = screen.theme().bodyText;
  fieldProps.cursor = static_cast<uint16_t>(cursorPos);
  fieldProps.cursorVisible = true;
  fieldProps.cursorBlock = cursorMode && !togglePos;
  fieldProps.selected = cursorMode && !togglePos;

  if (inputType == InputType::Password) {
    const int16_t toggleW = 56;
    const Rect fieldRect{inputRow.x, inputRow.y, static_cast<int16_t>(inputRow.width - toggleW - 6), inputRow.height};
    const Rect toggleRect{static_cast<int16_t>(inputRow.x + inputRow.width - toggleW), inputRow.y, toggleW, inputRow.height};
    freeink::ui::textField(screen.frame(), fieldRect, fieldProps);

    freeink::ui::ButtonProps passBtn;
    passBtn.label = passwordVisible ? "[***]" : "[abc]";
    passBtn.action = ACTION_TOGGLE_PASSWORD;
    passBtn.state = (cursorMode && togglePos) ? freeink::ui::StateSelected : freeink::ui::StateNormal;
    passBtn.text = screen.theme().smallText;
    screen.button(passBtn, toggleRect);
  } else {
    freeink::ui::textField(screen.frame(), inputRow, fieldProps);
  }

  // URL snippets row or hint text
  if (inputType == InputType::Url && !urlMode) {
    const Rect snippetRow = screen.take(freeink::ui::LayoutAnchor::Top, 32, 4);
    const char* quickSnippets[] = {"https://", "www.", ".com", "/opds"};
    const int16_t btnGap = 4;
    const int16_t totalBtns = 5;
    const int16_t btnW = static_cast<int16_t>((snippetRow.width - (totalBtns - 1) * btnGap) / totalBtns);
    for (int i = 0; i < 4; ++i) {
      const Rect bRect{static_cast<int16_t>(snippetRow.x + i * (btnW + btnGap)), snippetRow.y, btnW, snippetRow.height};
      freeink::ui::ButtonProps bp;
      bp.label = quickSnippets[i];
      bp.action = ACTION_URL_SNIPPET;
      bp.value = (i == 3) ? 6 : i;  // index 6 in urlSnippets is "/opds"
      bp.text = screen.theme().smallText;
      screen.button(bp, bRect);
    }
    const Rect moreRect{static_cast<int16_t>(snippetRow.x + 4 * (btnW + btnGap)), snippetRow.y,
                        static_cast<int16_t>(snippetRow.right() - (snippetRow.x + 4 * (btnW + btnGap))), snippetRow.height};
    freeink::ui::ButtonProps moreBp;
    moreBp.label = "...";
    moreBp.action = ACTION_TOGGLE_URL_MODE;
    moreBp.text = screen.theme().smallText;
    screen.button(moreBp, moreRect);
  } else if (hintVisible) {
    screen.take(freeink::ui::LayoutAnchor::Top, 4);
    const Rect hintRect = screen.take(freeink::ui::LayoutAnchor::Top, 24, 4);
    const char* hint = cursorMode
                           ? (togglePos ? (passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD))
                                        : tr(STR_KB_HINT_MOVE_CURSOR))
                           : tr(STR_KB_HINT_EDIT_ENTRY);
    freeink::ui::TextStyle hintStyle = screen.theme().smallText;
    hintStyle.align = freeink::ui::TextAlign::Center;
    screen.target().text(hintRect, hint, hintStyle);
  }

  // Keyboard or full URL snippets
  if (urlMode) {
    const Rect urlGridRect = screen.take(freeink::ui::LayoutAnchor::Bottom, 200, 8);
    const int16_t rows = 3;
    const int16_t cols = 3;
    const int16_t gap = 4;
    const int16_t cellW = static_cast<int16_t>((urlGridRect.width - (cols - 1) * gap) / cols);
    const int16_t cellH = static_cast<int16_t>((urlGridRect.height - (rows - 1) * gap) / rows);
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        const int idx = r * cols + c;
        if (idx < URL_SNIPPET_COUNT) {
          const Rect cRect{static_cast<int16_t>(urlGridRect.x + c * (cellW + gap)),
                           static_cast<int16_t>(urlGridRect.y + r * (cellH + gap)), cellW, cellH};
          freeink::ui::ButtonProps bp;
          bp.label = urlSnippets[idx];
          bp.action = ACTION_URL_SNIPPET;
          bp.value = static_cast<int16_t>(idx);
          bp.state = (urlNavIndex == idx) ? freeink::ui::StateSelected : freeink::ui::StateNormal;
          bp.text = screen.theme().bodyText;
          screen.button(bp, cRect);
        }
      }
    }
    const freeink::ui::FooterAction urlActions[3] = {
        {"Keyboard", ACTION_TOGGLE_URL_MODE},
        {tr(STR_BACK), ACTION_DEL},
        {tr(STR_OK_BUTTON), ACTION_OK},
    };
    screen.footer(urlActions, 3, freeink::ui::LayoutAnchor::Bottom);
  } else {
    const auto& layout = freeink::ui::builtinKeyboardLayout(freeink::ui::KeyboardLayoutId::QwertyEn, shifted, symbols, numberRow);

    freeink::ui::QwertyKeyboardProps kprops;
    kprops.keyAction = ACTION_KEY;
    kprops.shiftAction = ACTION_SHIFT;
    kprops.modeAction = ACTION_MODE;
    kprops.deleteAction = ACTION_DEL;
    kprops.okAction = ACTION_OK;
    kprops.okLabel = tr(STR_OK_BUTTON);
    kprops.shiftLabel = tr(STR_SHIFT);
    kprops.shifted = shifted;
    kprops.symbols = symbols;
    kprops.numberRow = numberRow;
    kprops.selectedIndex = cursorMode ? -1 : keyboardNav.logicalIndex(layout);
    kprops.inactiveSelection = cursorMode;
    screen.qwertyKeyboard(kprops, 0, freeink::ui::LayoutAnchor::Bottom);
  }
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

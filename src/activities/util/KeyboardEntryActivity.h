#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <string>
#include <utility>

#include "../Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

enum class InputType { Text, Password, Url };

class KeyboardEntryActivity : public Activity, private UiAppHost {
 public:
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "",
                                 const size_t maxLength = 0, InputType inputType = InputType::Text)
      : Activity("KeyboardEntry", renderer, mappedInput),
        UiAppHost(renderer),
        title(std::move(title)),
        text(std::move(initialText)),
        maxLength(maxLength),
        inputType(inputType) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  static constexpr freeink::ui::ActionId ACTION_BACK = 1;
  static constexpr freeink::ui::ActionId ACTION_KEY = 2;
  static constexpr freeink::ui::ActionId ACTION_SHIFT = 3;
  static constexpr freeink::ui::ActionId ACTION_MODE = 4;
  static constexpr freeink::ui::ActionId ACTION_DEL = 5;
  static constexpr freeink::ui::ActionId ACTION_OK = 6;
  static constexpr freeink::ui::ActionId ACTION_TOGGLE_PASSWORD = 7;
  static constexpr freeink::ui::ActionId ACTION_URL_SNIPPET = 8;
  static constexpr freeink::ui::ActionId ACTION_TOGGLE_URL_MODE = 9;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  void buildScreen(UiScreen& screen);
  void handleAction(const freeink::ui::ActionEvent& event);

  std::string title;
  std::string text;
  size_t maxLength;
  InputType inputType;
  bool passwordVisible = false;

  ButtonNavigator buttonNavigator;
  freeink::ui::KeyboardNavigator keyboardNav;

  bool shifted = false;
  bool symbols = false;
  bool numberRow = true;

  bool confirmHeld = false;
  bool confirmLongHandled = false;

  bool cursorMode = false;
  bool togglePos = false;
  size_t cursorPos = 0;
  bool upHeld = false;
  bool upLongHandled = false;
  bool downHeld = false;
  bool downLongHandled = false;
  bool rightHeld = false;
  bool rightLongHandled = false;
  size_t savedCursorPos = 0;
  size_t rightStartCursorPos = 0;

  bool urlMode = false;
  int urlNavIndex = 0;
  static constexpr int URL_SNIPPET_COUNT = 9;
  static constexpr const char* const urlSnippets[URL_SNIPPET_COUNT] = {
      "https://", "www.", ".com", "http://", "192.168.", ".org", "/opds", ":8080", ".net"};

  int delPressCount = 0;
  bool hintVisible = false;
  unsigned long hintShowTime = 0;

  void onComplete(std::string text);
  void onCancel();
  bool insertChar(char c);
  void insertString(const std::string& str);

  static constexpr uint16_t LONG_PRESS_MS = 500;
  static constexpr uint16_t DEL_LONG_PRESS_MS = 1500;
};

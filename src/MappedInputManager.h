#pragma once

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  // Screen orientation as the input layer sees it. Mirrors GfxRenderer::Orientation value for
  // value; it is duplicated rather than included because the input layer sits below the renderer
  // and must not depend on it. main.cpp installs the provider that bridges the two.
  enum class ScreenOrientation : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  // A movement direction *on screen*, as the reader perceives it while holding the device however
  // they hold it. Which physical button produces it depends on the orientation — see buttonFor().
  enum class Direction : uint8_t { Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  // Hint text for the two side buttons, in physical order: `up` belongs on BTN_UP, `down` on
  // BTN_DOWN. Which movement each of them performs depends on the orientation — see mapHints().
  struct SideLabels {
    const char* up;
    const char* down;
  };

  // Every hint a screen draws, already routed to the physical buttons that perform them.
  struct Hints {
    Labels front;
    SideLabels side;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  static void setOrientationProvider(ScreenOrientation (*provider)());
  static ScreenOrientation screenOrientation();
  static bool isVerticalStripReversed();

  [[nodiscard]] static Button buttonFor(Direction direction) { return buttonFor(screenOrientation(), direction); }
  [[nodiscard]] static constexpr Button buttonFor(const ScreenOrientation orientation, const Direction direction) {
    return applyStripOrder(geometricButtonFor(orientation, direction),
                           orientation == ScreenOrientation::LandscapeCounterClockwise);
  }
  [[nodiscard]] bool wasLogicalPressed(Direction direction) const { return wasPressed(buttonFor(direction)); }
  [[nodiscard]] bool wasLogicalReleased(Direction direction) const { return wasReleased(buttonFor(direction)); }
  [[nodiscard]] bool isLogicalPressed(Direction direction) const { return isPressed(buttonFor(direction)); }
  [[nodiscard]] static bool isDirection(const Button button, const Direction direction) {
    return button == buttonFor(direction);
  }

  [[nodiscard]] static Button frontStripPrevious() { return buttonFor(frontStripDirections().previous); }
  [[nodiscard]] static Button frontStripNext() { return buttonFor(frontStripDirections().next); }

  void update() const { gpio.update(); }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool hasPendingInput() const { return gpio.hasPendingInput(); }
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }

  // Front-strip hints for a screen that labels only the front buttons.
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Both hint strips for a screen that labels all six buttons.
  Hints mapHints(const char* back, const char* confirm, const char* left, const char* right, const char* up,
                 const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // Raw HalGPIO button index a logical button currently maps to.
  uint8_t rawIndex(Button button) const;

  // Drain one queued raw button edge from the background sampler (FIFO). Returns
  // false when empty. Used by ButtonEventManager to drive its press-type FSM.
  bool popRawEdge(HalGPIO::ButtonEdge& out) const { return gpio.popButtonEdge(out); }
  // Drop all queued/pending raw edges (activity transitions).
  void flushRawEdges() const { gpio.flushButtonEdges(); }

 private:
  HalGPIO& gpio;
  const GfxRenderer& renderer;
  static ScreenOrientation (*orientationProvider)();

  // Left/Right swap when the front-button strip runs bottom-to-top on screen, so
  // "previous" always sits above "next".
  static Button applyStripOrder(Button button);
  static constexpr Button applyStripOrder(const Button button, const bool stripReversed) {
    if (!stripReversed) {
      return button;
    }
    switch (button) {
      case Button::Left:
        return Button::Right;
      case Button::Right:
        return Button::Left;
      default:
        return button;
    }
  }

  static constexpr Button geometricButtonFor(const ScreenOrientation orientation, const Direction direction) {
    switch (orientation) {
      case ScreenOrientation::Portrait:
        switch (direction) {
          case Direction::Left:
            return Button::Left;
          case Direction::Right:
            return Button::Right;
          case Direction::Up:
            return Button::Up;
          case Direction::Down:
            return Button::Down;
        }
        break;
      case ScreenOrientation::LandscapeClockwise:
        switch (direction) {
          case Direction::Left:
            return Button::Down;
          case Direction::Right:
            return Button::Up;
          case Direction::Up:
            return Button::Left;
          case Direction::Down:
            return Button::Right;
        }
        break;
      case ScreenOrientation::PortraitInverted:
        switch (direction) {
          case Direction::Left:
            return Button::Right;
          case Direction::Right:
            return Button::Left;
          case Direction::Up:
            return Button::Down;
          case Direction::Down:
            return Button::Up;
        }
        break;
      case ScreenOrientation::LandscapeCounterClockwise:
        switch (direction) {
          case Direction::Left:
            return Button::Up;
          case Direction::Right:
            return Button::Down;
          case Direction::Up:
            return Button::Right;
          case Direction::Down:
            return Button::Left;
        }
        break;
    }
    return Button::Down;
  }

  struct DirectionPair {
    Direction previous;
    Direction next;
  };
  static DirectionPair frontStripDirections();
  static DirectionPair sideButtonDirections();

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};

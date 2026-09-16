import re

with open("src/main.cpp", "r") as f:
    content = f.read()

content = content.replace('#include "util/ScreenshotUtil.h"\n', "")
content = content.replace("""  // Power-hold timer for sleep. Hoisted above the screenshot block so the
  // screenshot path can clear it and avoid a stale POWER press triggering sleep
  // after the screenshot completes.""", "  // Power-hold timer for sleep.")

screenshot_logic = """  static bool screenshotButtonsReleased = true;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        // The reader may have left a pre-rendered next page in the frame buffer; ask the
        // current activity to redraw the visible page first so the screenshot matches the screen.
        activityManager.prepareFramebufferForCapture();
        ScreenshotUtil::takeScreenshot(renderer);
      }
      // Discard the POWER+DOWN presses so they don't fire Short/Long events
      // (e.g. page turn, sleep) once the user releases the combo.
      buttonEventManager.drain();
      powerHoldStart = 0;
    }
    return;
  } else {
    screenshotButtonsReleased = true;
  }
"""

content = content.replace(screenshot_logic, "")

screenshot_hold_logic = """      // If the screenshot combination is potentially being pressed, don't sleep
      if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
        return;
      }
"""

content = content.replace(screenshot_hold_logic, "")

with open("src/main.cpp", "w") as f:
    f.write(content)


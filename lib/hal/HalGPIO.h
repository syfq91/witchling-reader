#pragma once

#include <Arduino.h>
#include <InputManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;
  unsigned long usbLastPollMs = 0;
  bool usbElectricalConnected = false;  // last result of the per-device electrical/charge check

  // Live USB host link, straight from the IDF's SOF monitor
  // (usb_serial_jtag_is_connected(), maintained by a FreeRTOS tick hook that
  // watches the SOF interrupt bit with a 3 ms no-SOF tolerance). Catches what
  // the electrical check misses: a data-only cable, and any cable once the
  // battery is full. Both matter for HalPowerManager::lightSleep(), which must
  // not halt the chip out from under an enumerated CDC link — and for main.cpp,
  // which only opens the serial log when a host is present.
  bool usbHostLinkActive = false;

  // Electrical USB check (VBUS-driven level on GPIO20).
  bool isUsbElectricalConnected() const;

  // SOF sampling + electrical check + combined-verdict edge tracking.
  void updateUsbState(unsigned long now);

 public:
  enum class DeviceType : uint8_t { X4 };

  // A single debounced button transition captured by the background sampler.
  // `button` is a raw BTN_* index; `pressed` is true for a press edge, false for
  // a release edge; `timeMs` is the millis() value at the time the edge was
  // detected, so consumers can classify Short/Long/Double independent of how
  // often the loop task gets around to draining the queue.
  struct ButtonEdge {
    uint8_t button = 0;
    bool pressed = false;
    uint32_t timeMs = 0;
  };

 private:
  DeviceType _deviceType = DeviceType::X4;

  // ---- Background button sampler (see HalGPIO.cpp) ----------------------------
  // The buttons are read by polling the ADC; the loop task can be blocked for
  // hundreds of ms at a time (e.g. a sliced background section build whose slice
  // overshoots its budget on a heavy page), so polling once per loop iteration
  // drops presses that begin and end inside one slow iteration. A dedicated task
  // samples + debounces on a fixed ~10ms cadence regardless of loop progress and
  // latches every edge for the loop task to drain.
  TaskHandle_t samplerTaskHandle_ = nullptr;
  volatile bool samplerRunning_ = false;
  portMUX_TYPE inputMux_ = portMUX_INITIALIZER_UNLOCKED;

  // Shared sampler→loop state, guarded by inputMux_.
  uint8_t accumPressed_ = 0;   // press edges seen since the last update() drain
  uint8_t accumReleased_ = 0;  // release edges seen since the last update() drain
  uint8_t liveState_ = 0;      // latest debounced button state
  unsigned long heldTimeSnapshot_ = 0;
  static constexpr int EDGE_BUF = 32;
  ButtonEdge edgeBuf_[EDGE_BUF] = {};
  int edgeHead_ = 0;
  int edgeTail_ = 0;

  // Loop-side snapshot refreshed by update(); only the loop task reads/writes these.
  uint8_t snapState_ = 0;
  uint8_t snapPressed_ = 0;
  uint8_t snapReleased_ = 0;

  void sampleOnce();
  void pushEdgeLocked(uint8_t button, bool pressed, uint32_t timeMs);
  static void samplerTask(void* arg);

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return false; }
  inline bool deviceIsX4() const { return true; }

  // True on the Xteink C3 boards (X3, X3/UC8279, X4).
  bool isXteinkDevice() const { return true; }

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  // True while ANY button is currently held down.
  bool isAnyPressed() const;
  // True while a raw button-state change is still inside the debounce window.
  bool isDebouncePending() const;
  unsigned long getHeldTime() const;

  // Touch query for callers expecting capability check
  inline bool hasTouch() const { return false; }

  // Start/stop the background sampler. startInputSampler() must be called once
  // input handling is wanted (end of setup, after the boot-time power-button
  // handling that drives inputMgr.update() directly). Until then update() falls
  // back to sampling synchronously. stopInputSampler() is called before deep
  // sleep so no ADC read races with the display/power-rail teardown.
  void startInputSampler();
  void stopInputSampler();

  // True if any button press has been sampled since the last update() drain.
  // Safe to call from any context (e.g. mid-stall inside loop()); does not
  // consume the edge — update() will still see it on the next main-loop tick.
  bool hasPendingInput() const;

  // Pop the oldest queued button edge (FIFO). Returns false when the queue is
  // empty. Drained by ButtonEventManager to drive its press-type FSM.
  bool popButtonEdge(ButtonEdge& out);
  // Drop all queued edges and pending accumulated press/release bits. Called on
  // activity transitions so stale input does not bleed across screens.
  void flushButtonEdges();

  // Minimum free stack (bytes) the sampler task has ever had, for right-sizing its
  // stack allocation. 0 when the sampler is not running.
  UBaseType_t samplerStackHighWater() const {
    return samplerTaskHandle_ ? uxTaskGetStackHighWaterMark(samplerTaskHandle_) : 0;
  }

  // Wait until the raw power-button GPIO reads HIGH (released) for a sustained period.
  // Uses the raw pin directly instead of the InputManager debounced state to avoid
  // the 5 ms debounce being fooled by mechanical switch bounce during release.
  // Block until the raw power pin has read HIGH for 200 ms straight, or `timeoutMs`
  // elapses — whichever comes first. Returns how long it waited; compare against
  // `timeoutMs` to tell a clean release from a give-up. Bypasses the InputManager
  // debounce deliberately (5 ms is too short for 10-50 ms mechanical release bounce),
  // and reads the pin directly so it works with the input sampler stopped.
  unsigned long waitForStablePowerRelease(unsigned long timeoutMs = POWER_RELEASE_TIMEOUT_MS);

  // Ceiling for the wait above. Long enough that no deliberate hold reaches it (the
  // longest configurable power-hold-to-sleep gesture is well under this), short enough
  // that a stuck pin costs the user seconds rather than a reset.
  static constexpr unsigned long POWER_RELEASE_TIMEOUT_MS = 5000;

  // "Is this button held right now?", answered from fresh hardware samples rather than
  // the cache isPressed() reads. The six front buttons are resistor dividers on two ADC
  // pins, so a sample is a plain analogRead plus a band lookup — no debounce state to
  // warm up, which is what makes this usable during boot before update() has run enough
  // times to populate the cache. Power is a digital pin and is read directly.
  //
  // Every sample must agree, so a transient cannot produce a false positive; a genuinely
  // held button is stable and passes. Blocks for confirmSamples * 10 ms.
  bool isHeldNow(uint8_t buttonIndex, uint8_t confirmSamples = 4);

  // Which power-button press pattern(s) are accepted as an intentional wake. Mirrors
  // whichever press type(s) the user configured to put the device to sleep — several
  // can be set at once (e.g. both short AND long press power off), in which case any
  // one of the enabled gestures wakes it.
  struct WakeGestures {
    bool shortAllowed = false;  // any press/release, however brief, wakes the device
    bool doubleClick = false;   // two presses with releases within the double-click window
    bool longHold = true;       // sustained hold of requiredDurationMs (the safe default)
  };

  // What the boot-time wake gate actually observed on the power pin. Reported so a
  // "the button did nothing" report can be told apart from a slow boot: the rejecting
  // verdicts each name a different user error (let go too soon, second click missed).
  enum class WakeVerdict : uint8_t {
    NotPressed,     // pin already HIGH when the gate first sampled it
    ShortPress,     // accepted: short-press-to-sleep is configured, any press wakes
    LongHold,       // accepted: held past requiredDurationMs
    DoubleClick,    // accepted: released early, second press inside the window
    ReleasedEarly,  // rejected: released before the threshold, double-click not configured
    NoSecondPress,  // rejected: released early, double-click armed, second press never came
  };

  struct WakeCheck {
    WakeVerdict verdict = WakeVerdict::NotPressed;
    uint16_t decidedAtMs = 0;  // millis() when the verdict was reached
    // How long the gate itself saw the first press, NOT the user's real hold: the press
    // began before the app did, so the bootloader and everything up to the gate is not
    // counted. Use decidedAtMs for "when the device committed to waking".
    uint16_t heldMs = 0;

    bool accepted() const {
      return verdict == WakeVerdict::ShortPress || verdict == WakeVerdict::LongHold ||
             verdict == WakeVerdict::DoubleClick;
    }
  };

  // Human-readable name of a verdict, for logging. Points at a string literal.
  static const char* wakeVerdictName(WakeVerdict verdict);

  // Verify the raw power button was pressed in one of the patterns enabled by `gestures`,
  // mirroring the press type(s) configured to put the device to sleep. requiredDurationMs
  // is only used by the longHold gesture.
  // Call as early as possible so cold-boot initialization cannot hide a short press.
  WakeCheck verifyPowerButtonWakeup(WakeGestures gestures, uint16_t requiredDurationMs);

  // Check if USB is connected
  bool isUsbConnected() const;

  // Enumerated USB host link only (SOF activity), with no charge-state
  // inference mixed in. Separated out so a caller that needs to know *why* the
  // verdict came out the way it did — the serial-log gate's diagnostic — can
  // report the two terms apart.
  bool isUsbHostLinkActive() const;

  // USB state as sampled by the last update() call. Prefer this in per-loop
  // polling: isUsbConnected() performs a fresh I2C read on X3.
  bool isUsbConnectedCached() const { return lastUsbConnected; }

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;

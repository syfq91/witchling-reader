#include <BoardConfig.h>
#include <HalCapabilities.h>
#include <HalGPIO.h>
#include <HalI2cBus.h>
#include <Logging.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

void HalGPIO::begin() {
  _deviceType = DeviceType::X4;
  BoardConfig::selectDevice(BoardConfig::Board::XteinkX4);
  freeink::applyXteinkDisplayController();

  inputMgr.begin();

  // Pre-claim the shared SPI bus with the C3's display + SD pins.
  //
  // C3 ONLY, and the reason is subtle: SPIClass::begin() early-returns once the
  // bus is started, so whoever calls first wins the pin assignment. On the C3
  // the panel and the card share one bus and nothing else claims it, so this
  // pre-claim is correct and load-bearing. On every other board someone better
  // informed gets there first -- the SDK's display driver, SDCardManager, or a
  // board-support layer (the T5S3's BoardT5S3::begin(), which also deselects the
  // LoRa radio sharing that bus before starting it). Pre-claiming here would
  // stick on the wrong pins and undo those deselects.
  //
  // Pins come from the active profile rather than the EPD_* macros; verified
  // equal on the C3 (display.sclk 8 / mosi 10 / cs 21, sd.miso 7).
#if FREEINK_MCU_C3
  const BoardConfig::DisplayPins& display = BoardConfig::ACTIVE.display;
  SPI.begin(display.sclk, BoardConfig::ACTIVE.sd.miso, display.mosi, BoardConfig::ACTIVE.sd.cs);
#endif

  // ADC battery sense. Gated on the board actually reading its battery that way:
  // on a fuel-gauge board the same GPIO is the gauge's I2C bus (on X3, BAT_GPIO0
  // is X3_I2C_SCL), so configuring it as an ADC input would break the bus. This
  // replaces a deviceIsX4() test that would have been TRUE on X4 Pro — whose
  // batteryAdc is PIN_UNASSIGNED — and driven pinMode(-1).
  if (HalCapabilities::hasAdcBattery()) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }

  // USB-presence detect. The pin must exist AND not be shared with the fuel-gauge
  // I2C bus: X3 lists usbDetect = GPIO20, which is also its gauge SDA, so it must
  // stay an I2C line. X4 has no gauge and owns GPIO20 outright; X4 Pro leaves
  // usbDetect unassigned because the pin has not been identified yet.
  const int8_t usbDetect = BoardConfig::ACTIVE.usbDetect;
  const BoardConfig::BatteryGaugeConfig& gauge = BoardConfig::ACTIVE.batteryGauge;
  if (usbDetect != BoardConfig::PIN_UNASSIGNED && usbDetect != gauge.i2cSda && usbDetect != gauge.i2cScl) {
    pinMode(usbDetect, INPUT);
  }
}

// Push one debounced edge into the loop-drained FIFO. Caller holds inputMux_.
// Drops the newest edge if the ring is full — a full ring means the loop task
// has been blocked through 32 distinct transitions, far more than any real burst.
void HalGPIO::pushEdgeLocked(uint8_t button, bool pressed, uint32_t timeMs) {
  const int next = (edgeTail_ + 1) % EDGE_BUF;
  if (next == edgeHead_) {
    return;
  }
  edgeBuf_[edgeTail_] = {button, pressed, timeMs};
  edgeTail_ = next;
}

// One sampling pass: read + debounce the buttons, then latch any edges. Runs on
// the sampler task once started; also called synchronously from update() before
// the sampler is up. inputMgr.update() does the ADC read and must run OUTSIDE the
// critical section (analogRead may take the ADC driver mutex). Only the latching
// of the results into the shared accumulators/queue is done under inputMux_.
void HalGPIO::sampleOnce() {
  inputMgr.update();

  uint8_t live = 0;
  uint8_t pressed = 0;
  uint8_t released = 0;
  for (uint8_t i = 0; i <= BTN_POWER; i++) {
    if (inputMgr.isPressed(i)) live |= (1u << i);
    if (inputMgr.wasPressed(i)) pressed |= (1u << i);
    if (inputMgr.wasReleased(i)) released |= (1u << i);
  }
  const uint32_t now = millis();
  const unsigned long held = inputMgr.getHeldTime();

  portENTER_CRITICAL(&inputMux_);
  liveState_ = live;
  accumPressed_ |= pressed;
  accumReleased_ |= released;
  heldTimeSnapshot_ = held;
  for (uint8_t i = 0; i <= BTN_POWER; i++) {
    if (pressed & (1u << i)) pushEdgeLocked(i, true, now);
    if (released & (1u << i)) pushEdgeLocked(i, false, now);
  }
  portEXIT_CRITICAL(&inputMux_);
}

void HalGPIO::samplerTask(void* arg) {
  HalGPIO* self = static_cast<HalGPIO*>(arg);
  TickType_t last = xTaskGetTickCount();
  while (self->samplerRunning_.load(std::memory_order_acquire)) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    self->sampleOnce();
  }
  xSemaphoreGive(self->samplerStoppedSemaphore_);
  vTaskDelete(nullptr);  // self-terminate once stopInputSampler() clears the flag
}

void HalGPIO::startInputSampler() {
  if (samplerRunning_.load(std::memory_order_acquire)) {
    return;
  }
  samplerStoppedSemaphore_ = xSemaphoreCreateBinary();
  if (samplerStoppedSemaphore_ == nullptr) {
    LOG_ERR("BTN", "Failed to create input sampler stop semaphore");
    return;
  }
  samplerRunning_.store(true, std::memory_order_release);
  sampleOnce();  // prime so the first loop iteration sees current state
  constexpr uint32_t SAMPLER_STACK_BYTES = 2048;
  const BaseType_t created =
      xTaskCreate(&HalGPIO::samplerTask, "btnsample", SAMPLER_STACK_BYTES, this, 2, &samplerTaskHandle_);
  if (created != pdPASS || samplerTaskHandle_ == nullptr) {
    LOG_ERR("BTN", "Failed to create input sampler task");
    samplerRunning_.store(false, std::memory_order_release);
    vSemaphoreDelete(samplerStoppedSemaphore_);
    samplerStoppedSemaphore_ = nullptr;
  }
}

void HalGPIO::stopInputSampler() {
  if (!samplerRunning_.load(std::memory_order_acquire)) {
    return;
  }
  // Signal the task to exit on its next wake and self-delete. Avoids vTaskDelete()
  // tearing it down mid-analogRead (which would leak the ADC driver mutex).
  samplerRunning_.store(false, std::memory_order_release);
  xSemaphoreTake(samplerStoppedSemaphore_, portMAX_DELAY);
  samplerTaskHandle_ = nullptr;
  vSemaphoreDelete(samplerStoppedSemaphore_);
  samplerStoppedSemaphore_ = nullptr;
}

bool HalGPIO::hasPendingInput() const {
  bool pending = false;
  portENTER_CRITICAL_SAFE(const_cast<portMUX_TYPE*>(&inputMux_));
  pending = accumPressed_ != 0;
  portEXIT_CRITICAL_SAFE(const_cast<portMUX_TYPE*>(&inputMux_));
  return pending;
}

bool HalGPIO::popButtonEdge(ButtonEdge& out) {
  bool got = false;
  portENTER_CRITICAL(&inputMux_);
  if (edgeHead_ != edgeTail_) {
    out = edgeBuf_[edgeHead_];
    edgeHead_ = (edgeHead_ + 1) % EDGE_BUF;
    got = true;
  }
  portEXIT_CRITICAL(&inputMux_);
  return got;
}

void HalGPIO::flushButtonEdges() {
  portENTER_CRITICAL(&inputMux_);
  edgeHead_ = 0;
  edgeTail_ = 0;
  accumPressed_ = 0;
  accumReleased_ = 0;
  portEXIT_CRITICAL(&inputMux_);
  snapPressed_ = 0;
  snapReleased_ = 0;
}

void HalGPIO::update() {
  if (!samplerRunning_.load(std::memory_order_acquire)) {
    // Pre-sampler (early boot): sample synchronously on the calling task.
    sampleOnce();
  }
  // Drain the sampler's accumulated edges + latest state into the loop-side snapshot.
  portENTER_CRITICAL(&inputMux_);
  snapState_ = liveState_;
  snapPressed_ = accumPressed_;
  snapReleased_ = accumReleased_;
  accumPressed_ = 0;
  accumReleased_ = 0;
  portEXIT_CRITICAL(&inputMux_);
  updateUsbState(millis());
}

void HalGPIO::updateUsbState(const unsigned long now) {
  usbLastPollMs = now;
  usbElectricalConnected = isUsbElectricalConnected();
  const bool connected = usbElectricalConnected;
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

void HalGPIO::injectPress(const uint8_t buttonIndex, const bool longPress) {
  if (buttonIndex > BTN_POWER) return;
  const uint32_t now = millis();
  const uint32_t holdMs = longPress ? INJECTED_LONG_PRESS_MS : 0;
  const uint32_t pressAt = now > holdMs ? now - holdMs : 0;
  portENTER_CRITICAL(&inputMux_);
  accumPressed_ |= (1u << buttonIndex);
  accumReleased_ |= (1u << buttonIndex);
  pushEdgeLocked(buttonIndex, true, pressAt);
  pushEdgeLocked(buttonIndex, false, now);
  portEXIT_CRITICAL(&inputMux_);
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return (snapState_ & (1u << buttonIndex)) != 0; }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return (snapPressed_ & (1u << buttonIndex)) != 0; }

bool HalGPIO::wasAnyPressed() const { return snapPressed_ != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return (snapReleased_ & (1u << buttonIndex)) != 0; }

bool HalGPIO::wasAnyReleased() const { return snapReleased_ != 0; }

bool HalGPIO::isAnyPressed() const { return snapState_ != 0; }

bool HalGPIO::isDebouncePending() const { return inputMgr.isDebouncePending(); }

unsigned long HalGPIO::getHeldTime() const {
  return samplerRunning_.load(std::memory_order_acquire) ? heldTimeSnapshot_ : inputMgr.getHeldTime();
}

unsigned long HalGPIO::waitForStablePowerRelease(unsigned long timeoutMs) {
  // Wait until the raw power-button pin reads HIGH (released) for RELEASE_STABLE_MS
  // consecutive milliseconds.  The InputManager debounce (5 ms) is too short for
  // mechanical switch bounce which can last 10-50 ms, so we bypass it entirely here.
  constexpr unsigned long RELEASE_STABLE_MS = 200;
  const unsigned long waitStart = millis();
  unsigned long stableStart = 0;
  while (true) {
    if (digitalRead(InputManager::POWER_BUTTON_PIN) == HIGH) {
      if (stableStart == 0) stableStart = millis();
      if (millis() - stableStart >= RELEASE_STABLE_MS) break;
    } else {
      stableStart = 0;
    }
    // Bounded, and deliberately so. This used to be `while (true)` with no way out, run
    // at the point of no return on the sleep path: the input sampler is already stopped,
    // the panel is already in deep sleep holding the sleep screen, and the loop task is
    // not subscribed to the task WDT (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is unset
    // and nothing calls esp_task_wdt_add on it), so nothing was going to rescue it. A pin
    // that reads LOW indefinitely — a sticky or dirty switch, a finger that never comes
    // off — therefore froze the device with the sleep screen displayed and every button
    // dead, recoverable only by a reset (issue #155).
    //
    // Sleeping with the button still down is the lesser evil: the worst case is one
    // immediate re-wake (the deep-sleep GPIO trigger is level-LOW), which the boot-side
    // wake gate then classifies like any other press. A permanent freeze has no recovery
    // at all. The caller is told how long we waited so the breadcrumb can record it.
    if (millis() - waitStart >= timeoutMs) {
      LOG_ERR("GPIO", "Power button still held after %lums — sleeping anyway (raw pin LOW)", timeoutMs);
      return millis() - waitStart;
    }
    delay(10);
  }
  const unsigned long waited = millis() - waitStart;
  LOG_DBG("GPIO", "Power button stable-released after %lu ms", waited);
  return waited;
}

bool HalGPIO::isHeldNow(uint8_t buttonIndex, uint8_t confirmSamples) {
  constexpr unsigned long SAMPLE_GAP_MS = 10;
  // Raw values from the deciding sample, logged below. A negative verdict on the ADC
  // ladder has two very different causes — the button genuinely is not held, or its
  // divider reads outside the band the firmware expects — and only the raw value tells
  // them apart. Worth a line: the boot-time callers run before any UI exists to report a
  // misdetected combo, so the log is the sole evidence.
  int raw1 = -1;
  int raw2 = -1;
  int classified1 = -1;
  int classified2 = -1;
  bool held = true;

  for (uint8_t i = 0; i < confirmSamples; i++) {
    if (i > 0) delay(SAMPLE_GAP_MS);
    if (buttonIndex == BTN_POWER) {
      if (digitalRead(InputManager::POWER_BUTTON_PIN) != LOW) {
        held = false;
        break;
      }
      continue;
    }
    InputManager::ButtonAdcSample group1{};
    InputManager::ButtonAdcSample group2{};
    inputMgr.readButtonAdc(group1, group2);
    raw1 = group1.raw;
    raw2 = group2.raw;
    classified1 = group1.button;
    classified2 = group2.button;
    if (group1.button != buttonIndex && group2.button != buttonIndex) {
      held = false;
      break;
    }
  }

  if (buttonIndex != BTN_POWER) {
    LOG_DBG("GPIO", "isHeldNow(btn=%u) -> %d (adc1 raw=%d btn=%d, adc2 raw=%d btn=%d)", buttonIndex, held ? 1 : 0, raw1,
            classified1, raw2, classified2);
  }
  return held;
}

const char* HalGPIO::wakeVerdictName(WakeVerdict verdict) {
  switch (verdict) {
    case WakeVerdict::NotPressed:
      return "not-pressed";
    case WakeVerdict::ShortPress:
      return "short";
    case WakeVerdict::LongHold:
      return "long-hold";
    case WakeVerdict::DoubleClick:
      return "double-click";
    case WakeVerdict::ReleasedEarly:
      return "released-early";
    case WakeVerdict::NoSecondPress:
      return "no-second-press";
  }
  return "?";
}

HalGPIO::WakeCheck HalGPIO::verifyPowerButtonWakeup(WakeGestures gestures, uint16_t requiredDurationMs) {
  // How long the pin must stay HIGH before we conclude the press really ENDED. Long,
  // because concluding it wrongly refuses the wake: a contact bounce mid-hold would return
  // ReleasedEarly and drop the device back into sleep with the user's finger still on the
  // button.
  constexpr unsigned long BOUNCE_TOLERANCE_MS = 100;
  // How long the pin must stay HIGH before a following press counts as the SECOND CLICK of
  // a double rather than the same press bouncing. Much shorter than the tolerance above,
  // and deliberately so: the two thresholds guard opposite errors. Concluding "second
  // click" wrongly only accepts a wake the user was asking for anyway, so this side can
  // afford to be generous, while the reject side cannot.
  //
  // It has to be short. At 100 ms — when this decision shared BOUNCE_TOLERANCE_MS — a
  // second press landing inside the tolerance window never reached the release branch at
  // all: it just refreshed lastSeenPressed, so the gate saw ONE continuous press and then
  // rejected it as ReleasedEarly. A brisk double-click could not wake the device, while
  // the awake FSM (5 ms InputManager debounce) accepted the very same gesture as the
  // double-click that put it to sleep. 30 ms clears typical contact bounce (10-50 ms is
  // the range waitForStablePowerRelease() is written against) and sits well under the
  // ~60 ms floor of a deliberate double-click.
  constexpr unsigned long RELEASE_DEBOUNCE_MS = 30;
  constexpr unsigned long POLL_INTERVAL_MS = 10;
  // Mirrors ButtonEventManager::DOUBLE_WINDOW_MS so a double-click-to-sleep gesture
  // wakes on the same cadence it was configured with. Measured from the first HIGH sample,
  // like the awake FSM measures from the release edge — not from the sample that confirms
  // the release, which would shift the whole window BOUNCE_TOLERANCE_MS late.
  constexpr unsigned long DOUBLE_WINDOW_MS = 300;

  // Must be called before any long-running init (it is the first statement of setup()) so a short
  // press cannot be hidden by boot work: a released button is detected below and returns
  // immediately, which is also why running this on every boot costs nothing on non-button resets.
  const unsigned long gateStart = millis();
  const auto stamp = [](unsigned long ms) { return static_cast<uint16_t>(ms > UINT16_MAX ? UINT16_MAX : ms); };

  pinMode(InputManager::POWER_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(InputManager::POWER_BUTTON_PIN) != LOW) {
    return {WakeVerdict::NotPressed, stamp(millis()), 0};
  }

  if (gestures.shortAllowed) {
    return {WakeVerdict::ShortPress, stamp(millis()), stamp(millis() - gateStart)};
  }

  // requiredDurationMs (longHold) is compared against millis() directly — time since app
  // start, NOT time since this call. That is deliberate: the press that caused the wake
  // began before setup() ran, so boot time counts toward the hold rather than being
  // charged to the user twice. The caveat is that millis() starts at app init and so
  // excludes the ~200-300 ms bootloader, making the real-world hold needed that much
  // longer than the configured value. Erring long is the safe direction — the failure
  // mode being guarded against is a stray tap waking the device.
  //
  // Watch the first press through to release, classifying it exactly like the awake FSM
  // (ButtonEventManager): held past requiredDurationMs is a long press; released earlier
  // followed by a second press within DOUBLE_WINDOW_MS is a double click; otherwise it's
  // a short tap. Whichever it turns out to be, accept it only if that gesture is enabled.
  unsigned long lastSeenPressed = millis();
  unsigned long releaseSeenAt = 0;  // first HIGH sample of the current release; 0 while held
  while (true) {
    const unsigned long now = millis();
    const uint16_t heldMs = stamp(lastSeenPressed - gateStart);
    if (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW) {
      if (releaseSeenAt != 0) {
        // The pin went HIGH and is LOW again: either the second click of a double, or the
        // first press bouncing. The gap decides.
        if (gestures.doubleClick && now - releaseSeenAt >= RELEASE_DEBOUNCE_MS) {
          return {WakeVerdict::DoubleClick, stamp(now), heldMs};
        }
        releaseSeenAt = 0;  // bounce — still the same press
      }
      lastSeenPressed = now;
      if (gestures.longHold && now >= requiredDurationMs) {
        return {WakeVerdict::LongHold, stamp(now), stamp(now - gateStart)};
      }
    } else {
      if (releaseSeenAt == 0) {
        releaseSeenAt = now;
      }
      const unsigned long releasedFor = now - releaseSeenAt;
      // Released before the long-hold threshold. A double-click gesture gets one more
      // chance: a second press within the window after this release, handled above.
      if (!gestures.doubleClick) {
        if (releasedFor >= BOUNCE_TOLERANCE_MS) {
          return {WakeVerdict::ReleasedEarly, stamp(now), heldMs};
        }
      } else if (releasedFor >= DOUBLE_WINDOW_MS) {
        return {WakeVerdict::NoSecondPress, stamp(now), heldMs};
      }
    }
    delay(POLL_INTERVAL_MS);
  }
}

bool HalGPIO::isUsbConnected() const { return isUsbElectricalConnected(); }

bool HalGPIO::isUsbElectricalConnected() const {
  const int8_t usbDetect = BoardConfig::ACTIVE.usbDetect;
  if (usbDetect == BoardConfig::PIN_UNASSIGNED) return false;
  return digitalRead(usbDetect) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();
  LOG_DBG("GPIO", "getWakeupReason: wakeupCause=%d, resetReason=%d, usbConnected=%d", static_cast<int>(wakeupCause),
          static_cast<int>(resetReason), usbConnected);

  // A GPIO deep-sleep wake means POWER_BUTTON_PIN was pulled LOW, which is a button press by
  // definition — whatever the power source. This clause used to also require usbConnected, on the
  // premise that on battery the MCU is fully powered down so every wake arrives as POWERON.
  // HalPowerManager::startDeepSleep(keepClockAlive=true) broke that premise: with the clock
  // enabled on X4 (enterDeepSleep's keepLpAlive) GPIO13 is held HIGH, the MCU stays powered
  // through sleep, and a battery wake arrives as GPIO+DEEPSLEEP with no USB. That combination
  // matched nothing and fell through to Other, so setup() skipped the hold verification entirely
  // and any tap woke the device. Plugging USB does not pull this pin low, so the AfterUSBPower
  // case below (POWERON + USB) is unaffected.
  // The POWERON arm below infers "the user pressed power" from "we booted with
  // no USB". That inference needs BOTH of the things it assumes:
  //
  //  - a power path the button actually gates, so that being powered at all
  //    implies a press (the C3's GPIO13 battery latch), and
  //  - a trustworthy USB-detect signal, since the whole test is !usbConnected.
  //
  // The T5S3 has neither. Its profile leaves usbDetect PIN_UNASSIGNED, so
  // isUsbConnected() reports false even on USB power, and its power path is the
  // PMIC/PWR button rather than the BOOT button the profile maps as `power`. The
  // result was that pressing the hardware RST button -- a POWERON-class reset
  // with usbConnected forced false -- was classified as a power-button wake,
  // failed the wake gate (the button is not held during a reset), and sent the
  // device straight back to deep sleep in setup(). It looked like RST bricking
  // the board; it was RST being mistaken for a spurious power press.
  //
  // So require a usable USB-detect signal before trusting the inference. A GPIO
  // deep-sleep wake needs no such caveat: the pin was physically pulled low, so
  // it is a press by definition on any board.
  const bool canTrustUsbDetect = BoardConfig::ACTIVE.usbDetect != BoardConfig::PIN_UNASSIGNED;
  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected &&
       canTrustUsbDetect) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}

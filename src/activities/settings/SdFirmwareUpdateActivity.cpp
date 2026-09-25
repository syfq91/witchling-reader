#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/ConfirmDialog.h"
#include "components/controls/progress-bar.h"
#include "network/FirmwareFlasher.h"

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(screenTrampoline, this);
  app.on(ACTION_BACK, actionTrampoline, this);
  LOG_INF("FW", "SdFirmwareUpdateActivity build=%s %s recovery=%d", __DATE__, __TIME__, recoveryMode ? 1 : 0);
  if (!firmwarePath.empty()) {
    // Pre-selected path: skip picker and go straight to validation.
    {
      RenderLock lock(*this);
      state = State::VALIDATING;
    }
    requestUpdateAndWait();
    if (!validateFirmware()) {
      state = State::FAILED;
      requestUpdate();
      return;
    }
    promptConfirmation();
  } else {
    state = State::PICKING;
    launchPicker();
  }
}

void SdFirmwareUpdateActivity::onExit() {
  resetUi();
  Activity::onExit();
}

void SdFirmwareUpdateActivity::launchPicker() {
  startActivityForResult(std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", std::string{},
                                                               FileBrowserActivity::Mode::PickFirmware),
                         [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      launchPicker();
      return;
    }
    finish();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    LOG_ERR("FW", "Picker returned no path");
    finish();
    return;
  }
  firmwarePath = path->path;
  LOG_DBG("FW", "Selected: %s", firmwarePath.c_str());

  {
    RenderLock lock(*this);
    state = State::VALIDATING;
  }
  requestUpdateAndWait();

  if (!validateFirmware()) {
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FW", "no next-update partition available");
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else if (vr == firmware_flash::Result::BAD_CHIP || vr == firmware_flash::Result::WRONG_BOARD) {
      errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
  }
  std::string heading = tr(STR_FIRMWARE_UPDATE_PROMPT);
  std::string body = firmwarePath;
  const auto pos = body.find_last_of('/');
  if (pos != std::string::npos) body = body.substr(pos + 1);

  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body, false),
                         [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      launchPicker();
      return;
    }
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    self->requestUpdate(true);
  };

  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    // BAD_CHIP / WRONG_BOARD here is the re-validation inside flashFromSdPath
    // catching a wrong-device image the pre-confirmation pass missed, e.g. the
    // SD card was swapped between the prompt and the confirmation.
    errorMessage = result == firmware_flash::Result::BAD_CHIP || result == firmware_flash::Result::WRONG_BOARD
                       ? tr(STR_FIRMWARE_WRONG_DEVICE)
                       : tr(STR_FIRMWARE_WRITE_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  if (state == State::FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (recoveryMode) {
        state = State::PICKING;
        launchPicker();
        return;
      }
      finish();
    }
  }
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) {
      return;
    }
    lastRenderedPercent = pct;
  }
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer();
}

void SdFirmwareUpdateActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<SdFirmwareUpdateActivity*>(user)->buildScreen(screen);
}

void SdFirmwareUpdateActivity::actionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  auto* self = static_cast<SdFirmwareUpdateActivity*>(user);
  if (event.action == ACTION_BACK) {
    if (self->recoveryMode) {
      self->state = State::PICKING;
      self->launchPicker();
    } else {
      self->finish();
    }
  }
}

void SdFirmwareUpdateActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const char* headerText = recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);
  screen.header(headerText);

  if (state == State::VALIDATING) {
    screen.centeredText(tr(STR_VALIDATING_FIRMWARE), screen.theme().bodyText);
  } else if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;

    fui::TextStyle titleStyle = screen.theme().titleText;
    titleStyle.bold = true;
    titleStyle.align = fui::TextAlign::Center;
    const int16_t titleH = screen.target().lineHeight(titleStyle.font);

    const int16_t gap1 = screen.theme().spaceLg;

    const int16_t barH = 16;
    const int16_t barW = static_cast<int16_t>(screen.body().width * 4 / 5);

    fui::ProgressBarProps barProps;
    barProps.value = pct;
    barProps.max = 100;
    barProps.radius = 4;
    barProps.borderWidth = 1;
    barProps.border = fui::Paint::solid(fui::Color::Black);
    barProps.fill = fui::Paint::solid(fui::Color::Black);

    const int16_t gap2 = screen.theme().spaceMd;

    fui::TextStyle pctStyle = screen.theme().bodyText;
    pctStyle.bold = true;
    pctStyle.align = fui::TextAlign::Center;
    const int16_t pctH = screen.target().lineHeight(pctStyle.font);

    const int16_t gap3 = screen.theme().spaceLg;

    fui::TextStyle warnStyle = screen.theme().smallText;
    warnStyle.align = fui::TextAlign::Center;
    warnStyle.maxLines = 2;
    const int16_t warnH = static_cast<int16_t>(screen.target().lineHeight(warnStyle.font) * 2);

    const int16_t totalH = static_cast<int16_t>(titleH + gap1 + barH + gap2 + pctH + gap3 + warnH);
    const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
    if (topMargin > 0) {
      screen.spacer(topMargin);
    }

    const fui::Rect titleRect = screen.takeTop(titleH);
    screen.target().text(titleRect, tr(STR_UPDATING), titleStyle);

    screen.spacer(gap1);

    const fui::Rect barSlot = screen.takeTop(barH);
    const fui::Rect barRect = fui::centeredRect(barSlot, fui::Size{barW, barH});
    fui::progressBar(screen.frame(), barRect, barProps);

    screen.spacer(gap2);

    std::string pctStr = std::to_string(pct) + "%";
    const fui::Rect pctRect = screen.takeTop(pctH);
    screen.target().text(pctRect, pctStr.c_str(), pctStyle);

    screen.spacer(gap3);

    const int16_t sidePadding = screen.theme().spaceLg;
    const fui::Rect warnSlot = screen.takeTop(warnH);
    const fui::Rect warnRect{
        static_cast<int16_t>(warnSlot.x + sidePadding), warnSlot.y,
        static_cast<int16_t>(warnSlot.width > sidePadding * 2 ? warnSlot.width - sidePadding * 2 : warnSlot.width),
        warnSlot.height};
    screen.target().text(warnRect, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF), warnStyle);
  } else if (state == State::SUCCESS) {
    fui::TextStyle titleStyle = screen.theme().titleText;
    titleStyle.bold = true;
    titleStyle.align = fui::TextAlign::Center;
    const int16_t titleH = screen.target().lineHeight(titleStyle.font);

    const int16_t gap = screen.theme().spaceLg;

    fui::TextStyle hintStyle = screen.theme().bodyText;
    hintStyle.align = fui::TextAlign::Center;
    hintStyle.maxLines = 3;
    const int16_t hintH = static_cast<int16_t>(screen.target().lineHeight(hintStyle.font) * 3);

    const int16_t totalH = static_cast<int16_t>(titleH + gap + hintH);
    const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
    if (topMargin > 0) {
      screen.spacer(topMargin);
    }

    const fui::Rect titleRect = screen.takeTop(titleH);
    screen.target().text(titleRect, tr(STR_UPDATE_COMPLETE), titleStyle);

    screen.spacer(gap);

    const int16_t sidePadding = screen.theme().spaceLg;
    const fui::Rect hintSlot = screen.takeTop(hintH);
    const fui::Rect hintRect{
        static_cast<int16_t>(hintSlot.x + sidePadding), hintSlot.y,
        static_cast<int16_t>(hintSlot.width > sidePadding * 2 ? hintSlot.width - sidePadding * 2 : hintSlot.width),
        hintSlot.height};
    screen.target().text(hintRect, tr(STR_RESTARTING_HINT), hintStyle);
  } else if (state == State::FAILED) {
    ConfirmDialog::Spec spec;
    spec.headline = tr(STR_UPDATE_FAILED);
    spec.message = errorMessage.empty() ? nullptr : errorMessage.c_str();
    spec.acceptLabel = tr(STR_BACK);
    spec.acceptAction = ACTION_BACK;
    ConfirmDialog::draw(screen, spec);
  } else {
    // PICKING / CONFIRMING: a sub-activity is on top, nothing to draw.
    if (recoveryMode) {
      screen.centeredText(tr(STR_RECOVERY_MODE_HINT), screen.theme().bodyText);
    }
  }
}

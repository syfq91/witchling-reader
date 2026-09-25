#include "OpdsProgressionSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cmath>
#include <memory>
#include <utility>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/ConfirmDialog.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* TAG = "OPDS_SYNC_ACT";

constexpr fui::ActionId ACTION_CANCEL = 1;
constexpr fui::ActionId ACTION_CONFIRM = 2;
}  // namespace

OpdsProgressionSyncActivity::OpdsProgressionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         std::string cachePath, const float localProgression,
                                                         std::string localTitle, std::string localReference)
    : Activity("OpdsProgressionSync", renderer, mappedInput),
      UiAppHost(renderer),
      cachePath(std::move(cachePath)),
      localProgression(localProgression),
      localTitle(std::move(localTitle)),
      localReference(std::move(localReference)) {}

void OpdsProgressionSyncActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CANCEL, &OpdsProgressionSyncActivity::onCancelEvent, this);
  app.on(ACTION_CONFIRM, &OpdsProgressionSyncActivity::onConfirmEvent, this);
  app.setScreen(&OpdsProgressionSyncActivity::screenTrampoline, this);

  if (!OpdsProgressionSync::hasSyncConfig(cachePath)) {
    state = NO_CONFIG;
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    state = SYNCING;
    requestUpdateAndWait();
    performSync();
  } else {
    state = CONNECTING_WIFI;
    requestUpdate();
    startWifi();
  }
}

void OpdsProgressionSyncActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void OpdsProgressionSyncActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<OpdsProgressionSyncActivity*>(user)->buildScreen(screen);
}

void OpdsProgressionSyncActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  static_cast<OpdsProgressionSyncActivity*>(user)->finish();
}

void OpdsProgressionSyncActivity::onConfirmEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OpdsProgressionSyncActivity*>(user);
  if (self->state == FAILED) {
    self->state = SYNCING;
    self->requestUpdateAndWait();
    self->performSync();
  } else {
    self->finish();
  }
}

void OpdsProgressionSyncActivity::startWifi() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             finish();
                             return;
                           }
                           state = SYNCING;
                           requestUpdateAndWait();
                           performSync();
                         });
}

void OpdsProgressionSyncActivity::performSync() {
  LOG_INF(TAG, "Starting sync for cache %s", cachePath.c_str());
  const auto result = OpdsProgressionSync::performSync(cachePath, localProgression, localTitle, localReference);

  switch (result.status) {
    case OpdsProgressionSync::SyncStatus::SUCCESS_REMOTE_NEWER:
      state = SUCCESS_REMOTE;
      remoteData = result.remote;
      setResult(OpdsProgressionResult{remoteData.progression, remoteData.reference});
      break;

    case OpdsProgressionSync::SyncStatus::SUCCESS_LOCAL_PUSHED:
      state = SUCCESS_PUSHED;
      break;

    case OpdsProgressionSync::SyncStatus::SUCCESS_IN_SYNC:
      state = SUCCESS_SAME;
      break;

    case OpdsProgressionSync::SyncStatus::NO_CONFIG:
      state = NO_CONFIG;
      break;

    case OpdsProgressionSync::SyncStatus::NO_WIFI:
    case OpdsProgressionSync::SyncStatus::NETWORK_ERROR:
    case OpdsProgressionSync::SyncStatus::AUTH_ERROR:
    case OpdsProgressionSync::SyncStatus::PARSE_ERROR:
    default:
      state = FAILED;
      statusMessage = result.errorMessage.empty() ? tr(STR_SYNC_PROGRESS_FAILED) : result.errorMessage;
      break;
  }

  requestUpdate();
}

void OpdsProgressionSyncActivity::buildScreen(UiScreen& screen) {
  ConfirmDialog::Spec spec;
  spec.title = tr(STR_SYNC_PROGRESS);

  char detailBuf[128];
  detailBuf[0] = '\0';

  switch (state) {
    case CONNECTING_WIFI:
      spec.headline = tr(STR_CONNECTING);
      break;
    case SYNCING:
      spec.headline = tr(STR_SYNCING_PROGRESS);
      break;
    case SUCCESS_REMOTE:
      spec.headline = tr(STR_SYNC_PROGRESS_REMOTE_UPDATED);
      if (!remoteData.title.empty()) {
        snprintf(detailBuf, sizeof(detailBuf), "Position: %d%%\n%s",
                 static_cast<int>(remoteData.progression * 100.0f + 0.5f), remoteData.title.c_str());
      } else {
        snprintf(detailBuf, sizeof(detailBuf), "Position: %d%%",
                 static_cast<int>(remoteData.progression * 100.0f + 0.5f));
      }
      spec.message = detailBuf;
      spec.acceptLabel = tr(STR_CONFIRM);
      spec.acceptAction = ACTION_CONFIRM;
      break;
    case SUCCESS_PUSHED:
      spec.headline = tr(STR_SYNC_PROGRESS_SUCCESS);
      snprintf(detailBuf, sizeof(detailBuf), "Saved to server: %d%%",
               static_cast<int>(localProgression * 100.0f + 0.5f));
      spec.message = detailBuf;
      spec.acceptLabel = tr(STR_CONFIRM);
      spec.acceptAction = ACTION_CONFIRM;
      break;
    case SUCCESS_SAME:
      spec.headline = tr(STR_SYNC_PROGRESS_IN_SYNC);
      snprintf(detailBuf, sizeof(detailBuf), "Current position: %d%%",
               static_cast<int>(localProgression * 100.0f + 0.5f));
      spec.message = detailBuf;
      spec.acceptLabel = tr(STR_CONFIRM);
      spec.acceptAction = ACTION_CONFIRM;
      break;
    case NO_CONFIG:
      spec.headline = tr(STR_SYNC_PROGRESS_NO_SERVER);
      spec.acceptLabel = tr(STR_CONFIRM);
      spec.acceptAction = ACTION_CONFIRM;
      break;
    case FAILED:
      spec.headline = tr(STR_SYNC_PROGRESS_FAILED);
      spec.message = statusMessage.empty() ? nullptr : statusMessage.c_str();
      spec.cancelLabel = tr(STR_BACK);
      spec.cancelAction = ACTION_CANCEL;
      spec.acceptLabel = tr(STR_RETRY);
      spec.acceptAction = ACTION_CONFIRM;
      break;
    default:
      break;
  }

  ConfirmDialog::draw(screen, spec);
}

void OpdsProgressionSyncActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    if (ev.type != ButtonEventManager::PressType::Short) continue;

    if (state == FAILED) {
      if (ev.button == MappedInputManager::Button::Back) {
        finish();
        return;
      }
      if (ev.button == MappedInputManager::Button::Confirm) {
        state = SYNCING;
        requestUpdateAndWait();
        performSync();
        return;
      }
    } else if (state == SUCCESS_REMOTE || state == SUCCESS_PUSHED || state == SUCCESS_SAME || state == NO_CONFIG) {
      if (ev.button == MappedInputManager::Button::Back || ev.button == MappedInputManager::Button::Confirm) {
        finish();
        return;
      }
    }
  }
}

void OpdsProgressionSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();

  if (state == FAILED) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state != SYNCING && state != CONNECTING_WIFI) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

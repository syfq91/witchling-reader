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
// The CONFLICT prompt's two answers. They are their own ids rather than a reuse of
// CANCEL/CONFIRM because there the same ids also mean "close the dialog" and "retry", and a
// stale action must never be read as an answer to a question the dialog is not asking.
constexpr fui::ActionId ACTION_USE_DEVICE = 3;
constexpr fui::ActionId ACTION_USE_SERVER = 4;
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
  app.on(ACTION_USE_DEVICE, &OpdsProgressionSyncActivity::onUseDeviceEvent, this);
  app.on(ACTION_USE_SERVER, &OpdsProgressionSyncActivity::onUseServerEvent, this);
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

void OpdsProgressionSyncActivity::onUseDeviceEvent(const fui::ActionEvent&, void* user) {
  static_cast<OpdsProgressionSyncActivity*>(user)->resolveConflict(OpdsProgressionSync::ConflictResolution::TAKE_LOCAL);
}

void OpdsProgressionSyncActivity::onUseServerEvent(const fui::ActionEvent&, void* user) {
  static_cast<OpdsProgressionSyncActivity*>(user)->resolveConflict(
      OpdsProgressionSync::ConflictResolution::TAKE_REMOTE);
}

void OpdsProgressionSyncActivity::resolveConflict(const OpdsProgressionSync::ConflictResolution resolution) {
  // A double tap or a press that arrived after the answer already landed must not run a second
  // exchange under a resolution the screen is no longer showing.
  if (state != CONFLICT) return;
  state = SYNCING;
  requestUpdateAndWait();
  performSync(resolution);
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

void OpdsProgressionSyncActivity::performSync(const OpdsProgressionSync::ConflictResolution resolution) {
  LOG_INF(TAG, "Starting sync for cache %s", cachePath.c_str());
  const auto result =
      OpdsProgressionSync::performSync(cachePath, localProgression, localTitle, localReference, resolution);

  switch (result.status) {
    case OpdsProgressionSync::SyncStatus::CONFLICT:
      // Hold here rather than set any result: which position the caller ends up jumping to is
      // decided by the answer, not by the exchange.
      state = CONFLICT;
      remoteData = result.remote;
      break;

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
    case CONFLICT: {
      spec.headline = tr(STR_SYNC_PROGRESS_CONFLICT);
      int written = snprintf(detailBuf, sizeof(detailBuf), "%s %d%%\n%s %d%%", tr(STR_SYNC_POSITION_DEVICE),
                             static_cast<int>(localProgression * 100.0f + 0.5f), tr(STR_SYNC_POSITION_SERVER),
                             static_cast<int>(remoteData.progression * 100.0f + 0.5f));
      if (!remoteData.title.empty() && written > 0 && static_cast<size_t>(written) < sizeof(detailBuf)) {
        snprintf(detailBuf + written, sizeof(detailBuf) - written, "\n%s", remoteData.title.c_str());
      }
      spec.message = detailBuf;
      spec.cancelLabel = tr(STR_SYNC_USE_DEVICE);
      spec.acceptLabel = tr(STR_SYNC_USE_SERVER);
      spec.cancelAction = ACTION_USE_DEVICE;
      spec.acceptAction = ACTION_USE_SERVER;
      break;
    }
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
      break;
    case SUCCESS_PUSHED:
      spec.headline = tr(STR_SYNC_PROGRESS_SUCCESS);
      snprintf(detailBuf, sizeof(detailBuf), "Saved to server: %d%%",
               static_cast<int>(localProgression * 100.0f + 0.5f));
      spec.message = detailBuf;
      break;
    case SUCCESS_SAME:
      spec.headline = tr(STR_SYNC_PROGRESS_IN_SYNC);
      snprintf(detailBuf, sizeof(detailBuf), "Current position: %d%%",
               static_cast<int>(localProgression * 100.0f + 0.5f));
      spec.message = detailBuf;
      break;
    case NO_CONFIG:
      spec.headline = tr(STR_SYNC_PROGRESS_NO_SERVER);
      break;
    case FAILED:
      spec.headline = tr(STR_SYNC_PROGRESS_FAILED);
      spec.message = statusMessage.empty() ? nullptr : statusMessage.c_str();
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
    } else if (state == CONFLICT) {
      // Back first: on a board whose Back and "previous" share a physical key, leaving is the
      // answer that must win, so the user can always decline to choose.
      if (ev.button == MappedInputManager::Button::Back) {
        finish();
        return;
      }
      if (ev.button == MappedInputManager::frontStripPrevious()) {
        resolveConflict(OpdsProgressionSync::ConflictResolution::TAKE_LOCAL);
        return;
      }
      if (ev.button == MappedInputManager::frontStripNext()) {
        resolveConflict(OpdsProgressionSync::ConflictResolution::TAKE_REMOTE);
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
  } else if (state == CONFLICT) {
    // Back leaves without choosing; the other two sit on the two front buttons under the
    // dialog's own two answers, which is where mapLabels() puts previous/next.
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_SYNC_USE_DEVICE), tr(STR_SYNC_USE_SERVER));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state != SYNCING && state != CONNECTING_WIFI) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

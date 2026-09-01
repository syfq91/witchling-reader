#include "OpdsProgressionSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cmath>
#include <memory>
#include <utility>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* TAG = "OPDS_SYNC_ACT";
}

OpdsProgressionSyncActivity::OpdsProgressionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         std::string cachePath, float localProgression,
                                                         std::string localTitle, std::string localReference)
    : Activity("OpdsProgressionSync", renderer, mappedInput),
      cachePath(std::move(cachePath)),
      localProgression(localProgression),
      localTitle(std::move(localTitle)),
      localReference(std::move(localReference)) {}

void OpdsProgressionSyncActivity::onEnter() {
  Activity::onEnter();

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

void OpdsProgressionSyncActivity::onExit() { Activity::onExit(); }

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

void OpdsProgressionSyncActivity::loop() {
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

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int headerBottom = contentRect.y + metrics.topPadding + metrics.headerHeight;
  const Rect bodyRect(contentRect.x, headerBottom, contentRect.width,
                      contentRect.height - (metrics.topPadding + metrics.headerHeight));

  GUI.drawHeader(renderer,
                 Rect(contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight),
                 tr(STR_SYNC_PROGRESS));

  int y = bodyRect.y + bodyRect.height / 3;

  if (state == CONNECTING_WIFI) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CONNECTING), true, EpdFontFamily::BOLD);
  } else if (state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNCING_PROGRESS), true, EpdFontFamily::BOLD);
  } else if (state == SUCCESS_REMOTE) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_PROGRESS_REMOTE_UPDATED), true, EpdFontFamily::BOLD);
    y += 30;
    char buf[64];
    snprintf(buf, sizeof(buf), "Position: %d%%", static_cast<int>(remoteData.progression * 100.0f + 0.5f));
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    if (!remoteData.title.empty()) {
      y += 25;
      renderer.drawCenteredText(UI_10_FONT_ID, y, remoteData.title.c_str());
    }
  } else if (state == SUCCESS_PUSHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_PROGRESS_SUCCESS), true, EpdFontFamily::BOLD);
    y += 30;
    char buf[64];
    snprintf(buf, sizeof(buf), "Saved to server: %d%%", static_cast<int>(localProgression * 100.0f + 0.5f));
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
  } else if (state == SUCCESS_SAME) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_PROGRESS_IN_SYNC), true, EpdFontFamily::BOLD);
    y += 30;
    char buf[64];
    snprintf(buf, sizeof(buf), "Current position: %d%%", static_cast<int>(localProgression * 100.0f + 0.5f));
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
  } else if (state == NO_CONFIG) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_PROGRESS_NO_SERVER), true, EpdFontFamily::BOLD);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_PROGRESS_FAILED), true, EpdFontFamily::BOLD);
    if (!statusMessage.empty()) {
      y += 30;
      renderer.drawCenteredText(UI_10_FONT_ID, y, statusMessage.c_str());
    }
  }

  if (state == FAILED) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state != SYNCING && state != CONNECTING_WIFI) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

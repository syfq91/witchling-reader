#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Name, URL, Username, Password.
// Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 4;
constexpr char INVALID_OPDS_URL_MESSAGE[] = "Enter a valid OPDS URL";
}  // namespace

int OpdsSettingsActivity::getMenuItemCount() const {
  return isNewServer ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

int OpdsSettingsActivity::listCount() const { return getMenuItemCount(); }

const char* OpdsSettingsActivity::headerTitle() const {
  return isNewServer ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
}

void OpdsSettingsActivity::onEnter() {
  isNewServer = (serverIndex < 0);
  showSaveError = false;
  popupMessage.clear();

  if (!isNewServer) {
    // Edit flow: copy the selected server into local editable state.
    // Changes are persisted field-by-field through saveServer().
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(serverIndex));
    if (server) {
      editServer = *server;
    } else {
      // Server was deleted between navigation and entering this screen — treat as new
      isNewServer = true;
      serverIndex = -1;
    }
  }

  UiListActivity::onEnter();
}

bool OpdsSettingsActivity::saveServer() {
  bool success = false;

  if (isNewServer) {
    // Create flow: first save inserts a new server record into the multi-server store.
    const auto insertedIndex = OPDS_STORE.addServer(editServer);
    success = insertedIndex.has_value();
    if (success) {
      // After the first successful save, promote to an existing server so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewServer = false;
      serverIndex = static_cast<int>(*insertedIndex);
    } else {
      LOG_ERR("OPS", "Failed to add OPDS server");
    }
  } else {
    // Edit flow: update the same server entry in-place.
    success = OPDS_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
    if (!success) {
      LOG_ERR("OPS", "Failed to update OPDS server at index %d", serverIndex);
    }
  }

  showSaveError = !success;
  if (success) {
    popupMessage.clear();
  }
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::activateIndex(const int index) { handleSelection(index); }

void OpdsSettingsActivity::handleSelection(const int index) {
  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (index == 0) {
    // Server Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.name = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERVER_NAME), editServer.name,
                                                OpdsServerStore::MAX_NAME_LENGTH, InputType::Text),
        handler);
  } else if (index == 1) {
    // Server URL
    const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const auto normalizedUrl = OpdsServerValidation::normalizeUrl(kb.text);
        if (!normalizedUrl) {
          popupMessage = INVALID_OPDS_URL_MESSAGE;
          requestUpdate();
          return;
        }
        popupMessage.clear();
        editServer.url = *normalizedUrl;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_SERVER_URL), prefillUrl,
                                                OpdsServerStore::MAX_URL_LENGTH, InputType::Url),
        handler);
  } else if (index == 2) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.username = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME), editServer.username,
                                                OpdsServerStore::MAX_USERNAME_LENGTH, InputType::Text),
        handler);
  } else if (index == 3) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.password = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD), editServer.password,
                                                OpdsServerStore::MAX_PASSWORD_LENGTH, InputType::Password),
        handler);
  } else if (index == 4 && !isNewServer) {
    // Delete flow is only available for existing servers.
    if (!OPDS_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", serverIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  }
}

void OpdsSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  // SubHeader hint: "For Calibre-Web, append /opds to the URL"
  fui::TextAreaProps hint;
  hint.text = tr(STR_CALIBRE_URL_HINT);
  hint.style = screen.theme().smallText;
  screen.textArea(hint, static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const StrId fieldNames[] = {StrId::STR_SERVER_NAME, StrId::STR_OPDS_SERVER_URL, StrId::STR_USERNAME,
                              StrId::STR_PASSWORD};
  const int count = getMenuItemCount();

  for (int i = 0; i < count; ++i) {
    items[i] = {};
    if (i < BASE_ITEMS) {
      items[i].label = I18N.get(fieldNames[i]);
      if (i == 0) {
        itemValues[i] = editServer.name.empty() ? std::string(tr(STR_NOT_SET)) : editServer.name;
      } else if (i == 1) {
        itemValues[i] = editServer.url.empty() ? std::string(tr(STR_NOT_SET)) : editServer.url;
      } else if (i == 2) {
        itemValues[i] = editServer.username.empty() ? std::string(tr(STR_NOT_SET)) : editServer.username;
      } else if (i == 3) {
        itemValues[i] = editServer.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
      }
      items[i].value = itemValues[i].c_str();
    } else {
      items[i].label = tr(STR_DELETE_SERVER);
      items[i].value = nullptr;
    }
    items[i].actionValue = static_cast<int16_t>(i);
    items[i].enabled = true;
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;

  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);
}

void OpdsSettingsActivity::afterUiRender() {
  if (!popupMessage.empty()) {
    GUI.drawPopup(renderer, popupMessage.c_str(), /*overlayDisplayedFrame=*/false);
  } else if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE), /*overlayDisplayedFrame=*/false);
  }
}

#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/OpdsFilename.h"

namespace fui = freeink::ui;

namespace {
// Normalizes a user-typed folder: trims spaces, "" => SD root, otherwise a
// single leading '/' and no trailing '/'. Cold path (runs once per edit).
std::string normalizeFolder(std::string v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
  if (v.empty()) return "";
  if (v.front() != '/') v.insert(v.begin(), '/');
  while (v.size() > 1 && v.back() == '/') v.pop_back();
  if (v == "/") return "";  // a bare slash is SD root, same as empty
  return v;
}

// Label shown for the current OPDS filename format in the list subtitle.
StrId opdsFormatLabel(uint8_t format) {
  switch (format) {
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleAuthor):
      return StrId::STR_FMT_TITLE_AUTHOR;
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleOnly):
      return StrId::STR_FMT_TITLE;
    default:
      return StrId::STR_FMT_AUTHOR_TITLE;
  }
}
}  // namespace

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  // Settings mode appends three virtual items: "Add Server", "Download folder"
  // and "Filename format".
  if (!pickerMode) {
    count += 3;
  }
  return count;
}

int OpdsServerListActivity::listCount() const { return getItemCount(); }

const char* OpdsServerListActivity::headerTitle() const { return tr(STR_OPDS_SERVERS); }

void OpdsServerListActivity::onEnter() {
  // Reload from disk in case servers were added/removed by a subactivity or the web UI
  OPDS_STORE.loadFromFile();
  UiListActivity::onEnter();
}

void OpdsServerListActivity::onBackButton() {
  if (pickerMode) {
    activityManager.goHome();
  } else {
    finish();
  }
}

void OpdsServerListActivity::materializeListWindow() {
  const int count = getItemCount();
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  const auto& servers = OPDS_STORE.getServers();
  const auto serverCount = static_cast<int>(servers.size());

  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    auto& row = windowItems[offset];
    row = {};
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = true;

    if (static_cast<int>(index) < serverCount) {
      const auto& server = servers[index];
      windowLabels[offset] = server.name.empty() ? server.url : server.name;
      windowSubtitles[offset] = server.name.empty() ? "" : server.url;
      row.label = windowLabels[offset].c_str();
      row.subtitle = windowSubtitles[offset].empty() ? nullptr : windowSubtitles[offset].c_str();
    } else if (static_cast<int>(index) == serverCount) {
      windowLabels[offset] = tr(STR_ADD_SERVER);
      windowSubtitles[offset] = "";
      row.label = windowLabels[offset].c_str();
      row.subtitle = nullptr;
    } else if (static_cast<int>(index) == serverCount + 1) {
      windowLabels[offset] = tr(STR_OPDS_DOWNLOAD_FOLDER);
      const char* f = SETTINGS.opdsDownloadFolder;
      windowSubtitles[offset] = f[0] ? std::string(f) : std::string(tr(STR_OPDS_SD_ROOT));
      row.label = windowLabels[offset].c_str();
      row.subtitle = windowSubtitles[offset].c_str();
    } else if (static_cast<int>(index) == serverCount + 2) {
      windowLabels[offset] = tr(STR_OPDS_FILENAME_FORMAT);
      windowSubtitles[offset] = I18n::getInstance().get(opdsFormatLabel(SETTINGS.opdsFilenameFormat));
      row.label = windowLabels[offset].c_str();
      row.subtitle = windowSubtitles[offset].c_str();
    }
  }
}

void OpdsServerListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (listCount() == 0) {
    fui::TextAreaProps empty;
    empty.text = tr(STR_NO_SERVERS);
    empty.style = screen.theme().bodyText;
    empty.showCaret = false;
    screen.textArea(empty);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;

  syncListViewport(screen, props, /*hasSubtitle=*/true);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void OpdsServerListActivity::activateIndex(int index) { handleSelection(index); }

void OpdsServerListActivity::handleSelection(int selectedIndex) {
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());

  if (pickerMode) {
    // Picker mode: selecting a server navigates to the OPDS browser
    if (selectedIndex < serverCount) {
      const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex));
      if (server) {
        activityManager.replaceActivity(
            std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server, initialQuery_));
      }
    }
    return;
  }

  // Index layout: [servers 0..serverCount-1], [Add Server], [Download folder], [Filename format].
  if (selectedIndex == serverCount + 1) {
    auto folderHandler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const std::string norm = normalizeFolder(kb.text);
        strncpy(SETTINGS.opdsDownloadFolder, norm.c_str(), sizeof(SETTINGS.opdsDownloadFolder) - 1);
        SETTINGS.opdsDownloadFolder[sizeof(SETTINGS.opdsDownloadFolder) - 1] = '\0';
        SETTINGS.saveToFile();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_DOWNLOAD_FOLDER),
                                                std::string(SETTINGS.opdsDownloadFolder), 63, InputType::Text),
        folderHandler);
    return;
  }

  // "Filename format": tap cycles through the available formats.
  if (selectedIndex == serverCount + 2) {
    SETTINGS.opdsFilenameFormat =
        static_cast<uint8_t>((SETTINGS.opdsFilenameFormat + 1) % static_cast<uint8_t>(OpdsFilenameFormat::Count));
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  // Settings mode: open editor for selected server, or create a new one
  auto resultHandler = [this](const ActivityResult&) {
    // Reload server list when returning from editor
    OPDS_STORE.loadFromFile();
    const int itemCount = getItemCount();
    nav.selected = itemCount > 0 ? std::min(static_cast<int>(nav.selected), itemCount - 1) : 0;
    requestUpdate();
  };

  if (selectedIndex < serverCount) {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, selectedIndex), resultHandler);
  } else {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

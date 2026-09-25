#include "HomeMenu.h"

#include "CrossPointSettings.h"

namespace {

// What an entry needs before it appears anywhere.
enum class Requires : uint8_t { Nothing, Bookmarks, OpdsServers };

struct HomeMenuRow {
  HomeMenuEntry entry;
  Requires requirement;
  // The "show on home screen" setting; nullptr pins the entry to the home screen.
  uint8_t CrossPointSettings::* shownOnHome;
};

// Display order, top to bottom.
constexpr HomeMenuRow kRows[] = {
    {{HomeMenuAction::FileBrowser, StrId::STR_BROWSE_FILES, Folder},
     Requires::Nothing,
     &CrossPointSettings::showBrowseFilesOnHome},
    {{HomeMenuAction::Recents, StrId::STR_MENU_RECENT_BOOKS, Recent},
     Requires::Nothing,
     &CrossPointSettings::showRecentBooksOnHome},
    {{HomeMenuAction::GlobalBookmarks, StrId::STR_GLOBAL_BOOKMARKS, Book},
     Requires::Bookmarks,
     &CrossPointSettings::showBookmarksOnHome},
    {{HomeMenuAction::OpdsBrowser, StrId::STR_OPDS_BROWSER, Library},
     Requires::OpdsServers,
     &CrossPointSettings::showOpdsBrowserOnHome},
    {{HomeMenuAction::FileTransfer, StrId::STR_FILE_TRANSFER, Transfer},
     Requires::Nothing,
     &CrossPointSettings::showFileTransferOnHome},
    {{HomeMenuAction::Settings, StrId::STR_SETTINGS_TITLE, Settings}, Requires::Nothing, nullptr},
};

constexpr HomeMenuEntry kMoreEntry{HomeMenuAction::More, StrId::STR_MORE, Ellipsis};

bool isAvailable(const Requires requirement, const HomeMenuAvailability& availability) {
  switch (requirement) {
    case Requires::Nothing:
      return true;
    case Requires::Bookmarks:
      return availability.hasBookmarks;
    case Requires::OpdsServers:
      return availability.hasOpdsServers;
  }
  return false;
}

HomeMenuPlacement placementOf(const HomeMenuRow& row) {
  if (row.shownOnHome == nullptr || SETTINGS.*row.shownOnHome) return HomeMenuPlacement::Home;
  return HomeMenuPlacement::More;
}

}  // namespace

void collectHomeMenuEntries(const HomeMenuPlacement placement, const HomeMenuAvailability& availability,
                            std::vector<HomeMenuEntry>& out) {
  bool anyMovedToMore = false;
  for (const HomeMenuRow& row : kRows) {
    if (isAvailable(row.requirement, availability) && placementOf(row) == HomeMenuPlacement::More) {
      anyMovedToMore = true;
      break;
    }
  }

  for (const HomeMenuRow& row : kRows) {
    if (!isAvailable(row.requirement, availability)) continue;
    // More goes directly above the pinned entry (Settings), the one that can never move to it.
    if (placement == HomeMenuPlacement::Home && anyMovedToMore && row.shownOnHome == nullptr) {
      out.push_back(kMoreEntry);
    }
    if (placementOf(row) == placement) out.push_back(row.entry);
  }
}

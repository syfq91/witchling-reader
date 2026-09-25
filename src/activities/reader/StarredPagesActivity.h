#pragma once

#include <Epub.h>

#include <array>
#include <memory>
#include <string>

#include "BookmarkStore.h"
#include "activities/UiListActivity.h"

class StarredPagesActivity final : public UiListActivity {
 public:
  explicit StarredPagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BookmarkStore& bookmarkStore,
                                std::shared_ptr<Epub> epub = nullptr)
      : UiListActivity("StarredPages", renderer, mappedInput), epub(std::move(epub)), bookmarkStore(bookmarkStore) {}

  void onExit() override;

 protected:
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void drawFooter() override;

 private:
  std::shared_ptr<Epub> epub;  // nullptr for TXT files
  BookmarkStore& bookmarkStore;

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void materializeListWindow();
  std::string getItemLabel(int index) const;
  std::string getDefaultLabel(int index) const;
  void startRename();
  void deleteSelected();
};

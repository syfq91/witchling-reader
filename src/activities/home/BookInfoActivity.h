#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "../Activity.h"
#include "components/UiAppHost.h"

class BookInfoActivity final : public Activity, private UiAppHost {
  const std::string filePath;

  // Metadata populated in onEnter
  std::string title;
  std::string author;
  std::string series;
  std::string seriesIndex;
  std::string description;
  std::string coverBmpPath;
  std::string loadError;
  bool loadSucceeded = false;
  size_t fileSizeBytes = 0;

  // Description paging (populated lazily on first render)
  std::vector<std::string> descLines;
  int descWrappedWidth = 0;
  int descPage = 0;
  int descLinesPerPage = 0;
  int descTotalPages = 0;

  freeink::ui::Rect bodyRect_{};

  static constexpr freeink::ui::ActionId ACTION_BACK = 1;
  static constexpr freeink::ui::ActionId ACTION_PREV = 2;
  static constexpr freeink::ui::ActionId ACTION_NEXT = 3;

  static std::string formatFileSize(size_t bytes);
  void loadData();
  void buildScreen(UiScreen& screen);
  void afterUiRender();
  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);

 public:
  explicit BookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
      : Activity("BookInfo", renderer, mappedInput), UiAppHost(renderer), filePath(std::move(filePath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

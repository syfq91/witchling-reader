#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"

class ButtonActionsOverviewActivity final : public Activity, private UiAppHost {
 public:
  explicit ButtonActionsOverviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonActionsOverview", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr freeink::ui::ActionId ACTION_BACK = 1;

  void buildScreen(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);
};

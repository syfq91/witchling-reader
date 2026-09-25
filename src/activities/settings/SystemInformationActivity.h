#pragma once

#include <optional>

#include "SystemStatus.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

class SystemInformationActivity final : public Activity, private UiAppHost {
 public:
  explicit SystemInformationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SystemInformation", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  void afterUiRender();

 private:
  static constexpr freeink::ui::ActionId ACTION_BACK = 1;
  static constexpr freeink::ui::ActionId ACTION_UPDATE_SD = 2;

  std::optional<SystemStatus> status_;
  bool sdStatusReady_ = false;
  bool sdLoadRequested_ = false;
  freeink::ui::Rect bodyRect_{};

  void buildScreen(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void actionTrampoline(const freeink::ui::ActionEvent& event, void* user);
};

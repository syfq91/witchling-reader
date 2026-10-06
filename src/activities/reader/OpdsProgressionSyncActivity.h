#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "network/OpdsProgressionSync.h"

class OpdsProgressionSyncActivity final : public Activity, private UiAppHost {
 public:
  enum State {
    INITIAL,
    CONNECTING_WIFI,
    SYNCING,
    CONFLICT,  // device and server disagree; the user has not chosen yet
    SUCCESS_REMOTE,
    SUCCESS_PUSHED,
    SUCCESS_SAME,
    NO_CONFIG,
    FAILED
  };

  OpdsProgressionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string cachePath,
                              float localProgression, std::string localTitle = "", std::string localReference = "");

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void startWifi();
  // Runs the exchange. ASK is what every entry point uses; the two resolved values are only
  // reached from resolveConflict(), i.e. after the user has answered the CONFLICT prompt.
  void performSync(OpdsProgressionSync::ConflictResolution resolution = OpdsProgressionSync::ConflictResolution::ASK);
  // Back from the prompt: settle it, then let performSync() report the outcome as usual.
  void resolveConflict(OpdsProgressionSync::ConflictResolution resolution);

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onConfirmEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onUseDeviceEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onUseServerEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildScreen(UiScreen& screen);

  std::string cachePath;
  float localProgression = 0.0f;
  std::string localTitle;
  std::string localReference;

  State state = INITIAL;
  std::string statusMessage;
  OpdsProgressionSync::RemoteProgression remoteData;
};

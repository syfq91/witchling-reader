#pragma once

#include <string>

#include "activities/Activity.h"
#include "network/OpdsProgressionSync.h"

class OpdsProgressionSyncActivity final : public Activity {
 public:
  enum State { INITIAL, CONNECTING_WIFI, SYNCING, SUCCESS_REMOTE, SUCCESS_PUSHED, SUCCESS_SAME, NO_CONFIG, FAILED };

  OpdsProgressionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string cachePath,
                              float localProgression, std::string localTitle = "", std::string localReference = "");

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void startWifi();
  void performSync();

  std::string cachePath;
  float localProgression = 0.0f;
  std::string localTitle;
  std::string localReference;

  State state = INITIAL;
  std::string statusMessage;
  OpdsProgressionSync::RemoteProgression remoteData;
};

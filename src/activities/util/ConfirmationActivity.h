#pragma once
#include <string>

#include "../Activity.h"
#include "components/UiAppHost.h"

// Yes/No prompt. Built on ConfirmDialog (fui::optionDialog). Prompts do not draw on-screen
// touch buttons, relying on the physical button hint strip.
class ConfirmationActivity : public Activity, private UiAppHost {
 private:
  std::string heading;
  std::string body;
  bool inputArmed = false;
  bool showButtons = false;

  static void dialogScreen(UiScreen& screen, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onConfirmEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildDialogScreen(UiScreen& screen);
  void finishWith(bool cancelled);

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, bool showButtons = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};

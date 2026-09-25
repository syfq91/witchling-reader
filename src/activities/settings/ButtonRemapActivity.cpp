#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
}  // namespace

const char* ButtonRemapActivity::headerTitle() const { return tr(STR_REMAP_FRONT_BUTTONS); }

void ButtonRemapActivity::onEnter() {
  currentStep = 0;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;

  UiListActivity::onEnter();
  nav.selected = 0;
}

bool ButtonRemapActivity::handleCustomInput() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return true;
  }

  // Side buttons:
  // - Up: reset mapping to defaults and exit.
  // - Down: cancel without saving.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.saveToFile();
    finish();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    finish();
    return true;
  }

  {
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return true;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return true;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return true;
    }

    nav.selected = currentStep;
    requestUpdate();
  }
  return true;
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int hintsAreaHeight = lineH * 2 + metrics.verticalSpacing * 2;

  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height) + hintsAreaHeight),
      static_cast<int16_t>(contentRect.x)});

  // Subheader prompt: "Press the button for:"
  fui::TextAreaProps prompt;
  prompt.text = tr(STR_REMAP_PROMPT);
  prompt.style = screen.theme().bodyText;
  prompt.style.font = fui::GfxRendererTarget::FONT_BODY;
  prompt.style.bold = true;
  screen.textArea(prompt, static_cast<int16_t>(renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const uint8_t assignedButton = tempMapping[i];
    itemValues[i] = (assignedButton == kUnassigned) ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
    items[i] = {};
    items[i].label = getRoleName(i);
    items[i].value = itemValues[i].c_str();
    items[i].actionValue = static_cast<int16_t>(i);
    items[i].enabled = true;
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = kRoleCount;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;

  nav.selected = currentStep;
  syncListViewport(screen, props, /*hasSubtitle=*/false);
  screen.list(props);

  if (!errorMessage.empty()) {
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
    fui::TextAreaProps err;
    err.text = errorMessage.c_str();
    err.style = screen.theme().smallText;
    err.style.bold = true;
    screen.textArea(err, static_cast<int16_t>(lineH + 4));
  }
}

void ButtonRemapActivity::afterUiRender() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int hintsY = contentRect.y + contentRect.height - lineH * 2 - metrics.verticalSpacing;

  renderer.drawCenteredText(SMALL_FONT_ID, hintsY, tr(STR_REMAP_RESET_HINT));
  renderer.drawCenteredText(SMALL_FONT_ID, hintsY + lineH + 2, tr(STR_REMAP_CANCEL_HINT));
}

void ButtonRemapActivity::drawFooter() {
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    return "-";
  };

  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) const {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_CONFIRM);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
    default:
      return tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}

#include "ButtonActionsOverviewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <array>
#include <string>

#include "MappedInputManager.h"
#include "SettingInfo.h"
#include "SettingsList.h"

namespace {

struct ButtonRow {
  StrId submenu;     // e.g. STR_BTN_BACK — identifies which logical button
  StrId labelStrId;  // e.g. STR_BTN_BACK — display label for the row
};

// Order matches the on-screen rendering order — power → confirm.
constexpr std::array<ButtonRow, 7> kButtonRows = {{
    {StrId::STR_BTN_POWER, StrId::STR_BTN_POWER},
    {StrId::STR_BTN_BACK, StrId::STR_BTN_BACK},
    {StrId::STR_BTN_LEFT, StrId::STR_BTN_LEFT},
    {StrId::STR_BTN_RIGHT, StrId::STR_BTN_RIGHT},
    {StrId::STR_BTN_UP, StrId::STR_BTN_UP},
    {StrId::STR_BTN_DOWN, StrId::STR_BTN_DOWN},
    {StrId::STR_BTN_CONFIRM, StrId::STR_BTN_CONFIRM},
}};

// Find the SettingInfo for a given (button submenu, press kind) pair in the shared settings list.
std::string cellValue(const std::vector<SettingInfo>& settings, StrId submenu, StrId pressKind) {
  auto it = std::find_if(settings.begin(), settings.end(), [submenu, pressKind](const SettingInfo& s) {
    return s.submenu == submenu && s.nameId == pressKind && s.category == StrId::STR_CAT_CONTROLS;
  });
  if (it == settings.end()) return {};
  return it->getDisplayValue();
}

}  // namespace

void ButtonActionsOverviewActivity::onEnter() {
  Activity::onEnter();

  // Force landscape so the four-column table has room for full action labels.
  {
    RenderLock lock(*this);
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
  }

  resetUi();
  app.setScreen(screenTrampoline, this);
  app.on(ACTION_BACK, actionTrampoline, this);

  requestUpdate();
}

void ButtonActionsOverviewActivity::onExit() {
  resetUi();

  // Restore portrait — matches the rest of the settings UI.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  Activity::onExit();
}

void ButtonActionsOverviewActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
}

void ButtonActionsOverviewActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer();
}

void ButtonActionsOverviewActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonActionsOverviewActivity*>(user)->buildScreen(screen);
}

void ButtonActionsOverviewActivity::actionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  auto* self = static_cast<ButtonActionsOverviewActivity*>(user);
  if (event.action == ACTION_BACK) {
    self->finish();
  }
}

void ButtonActionsOverviewActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  screen.header(tr(STR_BTN_ACTIONS_OVERVIEW), CROSSPOINT_VERSION);

  fui::FooterAction footerActions[1];
  footerActions[0].label = tr(STR_BACK);
  footerActions[0].action = ACTION_BACK;
  screen.footer(footerActions, 1);

  const auto settings = getSettingsList();

  std::string strings[32];
  const char* cells[32];

  strings[0] = tr(STR_BTN_OVERVIEW_HEADER_BUTTON);
  strings[1] = tr(STR_BTN_OVERVIEW_HEADER_SHORT);
  strings[2] = tr(STR_BTN_OVERVIEW_HEADER_DOUBLE);
  strings[3] = tr(STR_BTN_OVERVIEW_HEADER_LONG);

  size_t idx = 4;
  for (const auto& row : kButtonRows) {
    strings[idx] = I18N.get(row.labelStrId);
    strings[idx + 1] = cellValue(settings, row.submenu, StrId::STR_BTN_SHORT_PRESS);
    strings[idx + 2] = cellValue(settings, row.submenu, StrId::STR_BTN_DOUBLE_PRESS);
    strings[idx + 3] = cellValue(settings, row.submenu, StrId::STR_BTN_LONG_PRESS);
    idx += 4;
  }

  for (size_t i = 0; i < 32; ++i) {
    cells[i] = strings[i].c_str();
  }

  fui::TableProps props;
  props.cells = cells;
  props.rows = 8;
  props.cols = 4;
  props.headerRow = true;
  props.text = screen.theme().smallText;
  props.padding = 4;
  props.rowHeight = 0;  // Distribute rows across remaining content area
  screen.table(props);
}

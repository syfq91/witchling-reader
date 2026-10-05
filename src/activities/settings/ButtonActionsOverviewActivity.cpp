#include "ButtonActionsOverviewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <array>
#include <string>
#include <vector>

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

// The table's columns, left to right.
constexpr std::array<StrId, 3> kPressKinds = {StrId::STR_BTN_SHORT_PRESS, StrId::STR_BTN_DOUBLE_PRESS,
                                              StrId::STR_BTN_LONG_PRESS};

// On the heap: 21 strings are ~500 bytes, too much for a render's stack frame.
using CellValues = std::vector<std::array<std::string, kPressKinds.size()>>;

// Every (button, press kind) cell's display value, read in ONE walk of the settings: a cell is the
// first Controls row with that button's submenu and that press kind. Looked up cell by cell, the
// table needs the whole list at hand, which is one 11.5 KB block (see forEachSetting()).
CellValues cellValues() {
  CellValues values(kButtonRows.size());
  std::array<std::array<bool, kPressKinds.size()>, kButtonRows.size()> found{};
  forEachSetting([&](const SettingInfo& s) {
    if (s.category != StrId::STR_CAT_CONTROLS) return;
    for (size_t r = 0; r < kButtonRows.size(); r++) {
      if (kButtonRows[r].submenu != s.submenu) continue;
      for (size_t k = 0; k < kPressKinds.size(); k++) {
        if (s.nameId != kPressKinds[k] || found[r][k]) continue;
        values[r][k] = s.getDisplayValue();
        found[r][k] = true;
      }
    }
  });
  return values;
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
  screen.header(tr(STR_BTN_ACTIONS_OVERVIEW), nullptr, CROSSPOINT_VERSION);

  fui::FooterAction footerActions[1];
  footerActions[0].label = tr(STR_BACK);
  footerActions[0].action = ACTION_BACK;
  screen.footer(footerActions, 1);

  const CellValues cells = cellValues();

  // The fui table wants a flat const char* grid, so the header row and the seven
  // per-button rows are staged in one string block first; the cell values come out
  // of the single settings walk cellValues() already did.
  std::string strings[32];
  const char* tableCells[32];

  strings[0] = tr(STR_BTN_OVERVIEW_HEADER_BUTTON);
  strings[1] = tr(STR_BTN_OVERVIEW_HEADER_SHORT);
  strings[2] = tr(STR_BTN_OVERVIEW_HEADER_DOUBLE);
  strings[3] = tr(STR_BTN_OVERVIEW_HEADER_LONG);

  size_t idx = 4;
  for (size_t r = 0; r < kButtonRows.size(); r++) {
    strings[idx] = I18N.get(kButtonRows[r].labelStrId);
    for (size_t k = 0; k < kPressKinds.size(); k++) strings[idx + 1 + k] = cells[r][k];
    idx += 4;
  }

  for (size_t i = 0; i < 32; ++i) {
    tableCells[i] = strings[i].c_str();
  }

  fui::TableProps props;
  props.cells = tableCells;
  props.rows = 8;
  props.cols = 4;
  props.headerRow = true;
  props.text = screen.theme().smallText;
  props.padding = 4;
  props.rowHeight = 0;  // Distribute rows across remaining content area
  screen.table(props);
}

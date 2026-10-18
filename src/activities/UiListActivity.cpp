#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "SerialTestApi.h"
#include "TouchUi.h"
#include "activities/ActivityManager.h"
#include "activities/SliderPickerActivity.h"
#include "components/UITheme.h"
#include "settings/SliderSettingPicker.h"
#include "util/ListGrammar.h"

namespace fui = freeink::ui;

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const ListDeclaration& declaration)
    : Activity(name, renderer, mappedInput),
      UiAppHost(renderer),
      listController(mappedInput, buttonEvents, controllerHost, declaration) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  activeNav().reset();
  listController.reset();
  resetPublishedWindow();
  resetUi();
  app.on(ACTION_ROW, &UiListActivity::rowActionTrampoline, this);
  app.setScreen(&UiListActivity::screenTrampoline, this);
  requestUpdate();
}

void UiListActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void UiListActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiListActivity*>(user)->buildScreen(screen);
}

void UiListActivity::rowActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  const int index = self->indexForActionValue(event.value);
  if (index < 0 || index >= self->listCount()) return;
  self->onRowAction(index);
}

ListRowTap::Result UiListActivity::selectListRow(const int index) {
  return ListRowTap::apply(index, listCount(), activeNav().selected);
}

void UiListActivity::onRowAction(const int index) {
  const auto result = listTapActivation.applyPreference(index, selectListRow(index), /*activateImmediately=*/false);
  if (result == ListRowTap::Result::Rejected) return;
  if (result == ListRowTap::Result::Selected) {
    moveSelectionTo(index);
    return;
  }
  activateIndex(index);
}

bool UiListActivity::routeListTouch() {
#if CP_TOUCH_UI
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  return static_cast<bool>(route);
#else
  return false;
#endif
}

// No RenderLock for a plain selection: `selected` is atomic, and requestSelection() defers the
// viewport pull to the next build, where ListNav::syncToProps consumes followOnBuild -- so nothing
// here touches the render task's `top`, and a step never parks the loop task behind a screen build
// in flight. A page turn does set `top`; showRowPage() below takes the lock for that.
void UiListActivity::moveSelectionTo(const int index) {
  activeNav().requestSelection(index);
  onSelectionChanged(index);
  requestUpdate();
}

void UiListActivity::loop() {
  if (handleCustomInput()) return;
#if CP_TOUCH_UI
  if (routeListTouch()) return;

  // A swipe is a page, the same page as a long Left/Right (spec R3).
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    listController.page(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
#endif

  navigateButtons();
}

void UiListActivity::navigateButtons() { listController.update(); }

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
#if CP_TOUCH_UI
  if (!mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    props.rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
  }
#else
  const auto& metrics = UITheme::getInstance().getMetrics();
  props.rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
#endif
  props = screen.resolveListProps(props);
  auto& currentNav = activeNav();
  const int count = listCount();
  const int prevTop = currentNav.top;
  const int drawn = currentNav.trusts(count) ? currentNav.drawnRows : 0;
  currentNav.syncToProps(screen.body(), props.rowHeight, props.rowGap, count, props);

  // Page-by-page screen turning: keep the viewport on the current page while the selection is
  // within it, and flip to a brand new screen when stepping or jumping past page boundaries.
  if (currentNav.followPending) {
    const int sel = props.selectedIndex;
    if (sel < 0) {
      currentNav.top = 0;
      props.topIndex = 0;
    } else {
      const int newTop = ListGrammar::pageTopFor(sel, prevTop, drawn, currentNav.visibleRows, count);
      currentNav.top = newTop;
      props.topIndex = static_cast<uint16_t>(newTop);
    }
  }
}

// Out of line on purpose: every list screen's buildScreen() calls these, and inlined they cost
// about 800 B of FreeInkUI code per screen.
void UiListActivity::layoutListArea(UiScreen& screen, const int16_t extraTop, const int16_t extraBottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect fullRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  const Rect listRect = listContentRect();
  constexpr int16_t kSideHintInset = 21;
  const int16_t sideInsetRight = (fullRect.width > listRect.width) ? kSideHintInset : 0;
  const int16_t sideInsetLeft = (listRect.x > fullRect.x) ? kSideHintInset : 0;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(fullRect.y + metrics.topPadding + metrics.headerHeight + extraTop),
                  sideInsetRight,
                  static_cast<int16_t>(renderer.getScreenHeight() - (fullRect.y + fullRect.height) + extraBottom),
                  sideInsetLeft});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

fui::ListProps UiListActivity::listProps(UiScreen& screen) const {
  fui::ListProps props;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  return props;
}

void UiListActivity::addList(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  syncListViewport(screen, props, hasSubtitle);
  noteListSelection(props);
  screen.list(props);
}

#if SERIAL_TEST_API
void UiListActivity::noteListSelection(const fui::ListProps& props) {
  const int index = props.selectedIndex;
  const char* label = nullptr;
  fui::ListItem row;  // the provider formats into buffers it owns; the probe copies the label at once
  if (index >= 0 && index < props.count) {
    if (props.rowProvider) {
      props.rowProvider(props.rowProviderCtx, static_cast<uint16_t>(index), row);
      label = row.label;
    } else if (props.items) {
      const int inWindow = index - props.itemsWindowFirst;
      const int windowRows = props.itemsWindowCount ? props.itemsWindowCount : props.count - props.itemsWindowFirst;
      if (inWindow >= 0 && inWindow < windowRows) label = props.items[inWindow].label;
    }
  }
  SerialTestApi::noteSelection(index, label);
}
#endif  // SERIAL_TEST_API

void UiListActivity::drawChrome() {
  const char* title = headerTitle();
  if (!title) return;
  GUI.drawHeader(renderer, listHeaderRect(), title);
}

void UiListActivity::drawFooter() { drawListHints(); }

const char* UiListActivity::footerBackLabel() const { return tr(STR_BACK); }

const char* UiListActivity::footerConfirmLabel() const { return tr(STR_SELECT); }

void UiListActivity::drawListHints() { listController.drawHints(renderer, footerBackLabel(), footerConfirmLabel()); }

Rect UiListActivity::listContentRect() const { return UITheme::getContentRect(renderer, true, true); }

Rect UiListActivity::listHeaderRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect full = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  return Rect{full.x, full.y + metrics.topPadding, full.width, metrics.headerHeight};
}

int UiListActivity::selectedPosition() const {
  // activeNav() hands out a mutable reference; reading through it changes nothing.
  return const_cast<UiListActivity*>(this)->activeNav().selected;
}

void UiListActivity::activatePosition(const int position, bool /*longPress*/) {
  if (position >= 0 && position < listCount()) activateIndex(position);
}

void UiListActivity::homeFromList() {
  if (activityManager.isReaderActivity()) {
    onBackButton();
    return;
  }
  onGoHome();
}

void UiListActivity::showRowPage(const int row, const int top) {
  {
    // `top` belongs to the render task: take the lock rather than write it under a build in flight.
    // The follow request stays on: the build scrolls the least it must from this top to keep the
    // selection on screen, which matters when rows differ in height.
    RenderLock lock(*this);
    auto& current = activeNav();
    current.requestSelection(row);
    current.followPending = false;
    current.top = top;
  }
  onSelectionChanged(row);
  requestUpdate();
}

void UiListActivity::publishListWindow() {
  auto& current = activeNav();
  const int count = listCount();
  const int top = current.top;
  const int drawn = current.drawnRows > 0 ? current.drawnRows : 1;
  windowTop.store(top);
  // A short last screen keeps the last full screen's row count, so paging back from it moves a
  // whole screen rather than only as many rows as the tail had.
  windowDrawn.store(top + drawn >= count ? std::max(drawn, windowDrawn.load()) : drawn);
}

void UiListActivity::resetPublishedWindow() {
  windowTop.store(0);
  windowDrawn.store(1);
}

ListWindow UiListActivity::publishedWindow() const {
  ListWindow window;
  window.top = windowTop.load();
  window.drawn = windowDrawn.load();
  return window;
}

void UiListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  publishListWindow();
  afterUiRender();
  drawFooter();
  renderer.displayBuffer();
}

bool UiListActivity::tryOpenSliderFor(const SettingAction action, std::function<void()> onDone) {
  SliderPickerActivity::Config cfg;
  if (!SliderSetting::configFor(action, cfg)) return false;

  startActivityForResult(std::make_unique<SliderPickerActivity>(renderer, mappedInput, std::move(cfg)),
                         [this, action, onDone = std::move(onDone)](const ActivityResult& result) {
                           const auto* pr = std::get_if<PercentResult>(&result.data);
                           if (!result.isCancelled && pr != nullptr) {
                             if (SliderSetting::apply(action, static_cast<uint8_t>(pr->percent))) {
                               SETTINGS.saveToFile();
                             }
                           } else {
                             SliderSetting::cancel(action);
                           }
                           if (onDone) onDone();
                         });
  return true;
}

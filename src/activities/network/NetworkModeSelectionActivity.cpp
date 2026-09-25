#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId kMenuItems[3] = {StrId::STR_JOIN_NETWORK, StrId::STR_CREATE_HOTSPOT, StrId::STR_OPDS_BROWSER};
constexpr StrId kMenuDescs[3] = {StrId::STR_JOIN_DESC, StrId::STR_HOTSPOT_DESC, StrId::STR_OPDS_DESC};
}  // namespace

const char* NetworkModeSelectionActivity::headerTitle() const { return tr(STR_FILE_TRANSFER); }

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(contentRect.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (contentRect.x + contentRect.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (contentRect.y + contentRect.height)),
                  static_cast<int16_t>(contentRect.x)});

  for (size_t i = 0; i < 3; ++i) {
    items[i] = {};
    items[i].label = I18N.get(kMenuItems[i]);
    items[i].subtitle = I18N.get(kMenuDescs[i]);
    items[i].actionValue = static_cast<int16_t>(i);
    items[i].enabled = true;
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = 3;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;

  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void NetworkModeSelectionActivity::activateIndex(const int index) {
  NetworkMode mode = NetworkMode::JOIN_NETWORK;
  if (index == 1) {
    mode = NetworkMode::CREATE_HOTSPOT;
  } else if (index == 2) {
    mode = NetworkMode::OPDS_BROWSER;
  }

  if (mode == NetworkMode::OPDS_BROWSER) {
    activityManager.goToBrowser();
    return;
  }

  onModeSelected(mode);
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

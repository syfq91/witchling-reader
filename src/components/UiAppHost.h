#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

class GfxRenderer;
class MappedInputManager;

class UiAppHost {
 public:
  using UiApp = freeink::ui::FreeInkApp<48, 12>;
  using UiScreen = UiApp::ScreenType;

  explicit UiAppHost(const GfxRenderer& renderer);

  void resetUi();
  void renderUi();

  // What loop-task routing saw this pass. `routed` is true when the gate was open and the
  // snapshot carried touch input the screen cares about; `snap` is that snapshot, so a caller
  // can also ask where the contact was and whether it ended -- which is how a drawer tells a
  // tap on itself from a tap on the page it is covering.
  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    freeink::ui::InputSnapshot snap{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  // `routeHeld` forwards held frames to InputDrag elements (sliders). Without it a contact is
  // only routed on its press and release edges, so a slider knob jumps to where the finger
  // landed and then to where it lifted, with nothing in between.
  //
  // Upstream (crosspoint-reader bbca4886) also carries a `withLongPress` flag here. It is left
  // out rather than accepted-and-ignored: touchSnapshotFrom() in this fork has no long-press
  // path for it to forward, so the parameter would be a promise the code does not keep.
  TouchRoute routeTouch(const MappedInputManager& input, bool routeHeld = false);
  void closeRouting() { uiReady = false; }

  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;

 private:
  std::atomic<bool> uiReady{false};
  // The layout values this screen's app draws with (row and header heights, padding, the scroll
  // bar), derived from the theme metrics and UI fonts in resetUi(). Each screen holds its own,
  // so they live on the heap only while a list or dialog does: they used to be a static
  // double-buffered pool shared by every screen -- 3.4 KB of RAM for the whole session,
  // reading included, where no FreeInkUI screen is on display. Only this screen's own render
  // reads them, after its onEnter() has filled them in, so one copy needs no second buffer.
  freeink::ui::ThemeTokens themeTokens{};
  std::atomic<const freeink::ui::ThemeTokens*> themeCell{nullptr};
};

namespace freeink {
namespace ui {

// Explicit template specialization for Frame<48> used across the reader firmware.
// Corrects vertical layout when both title and subtitle are present: stacks them as a centered
// two-line block instead of assigning content.height to titleRect (which vertically centered the
// title inside the entire band directly over the subtitle).
template <>
inline void header<48>(Frame<48>& frame, Rect rect, const HeaderProps& props) {
  StyleSet styles = props.styles.unset() ? defaultPopupStyles() : props.styles;
  const BoxStyle& style = styles.resolve(StateNormal);
  frame.target().fill(rect, style.background, style.radius, style.corners);
  if (style.border.kind != PaintKind::None && style.borderWidth > 0) {
    drawBorderEdges(frame.target(), rect, style.border, style.borderWidth, style.radius, style.corners,
                    props.borderEdges);
  }

  const int16_t sidePad = props.sidePadding < 0 ? 6 : props.sidePadding;
  Rect content = rect.inset(Insets{0, sidePad, 0, sidePad});
  if (props.leftReserve > 0) {
    content.x = static_cast<int16_t>(content.x + props.leftReserve);
    content.width = static_cast<int16_t>(content.width - props.leftReserve);
  }
  if (props.rightReserve > 0) content.width = static_cast<int16_t>(content.width - props.rightReserve);

  const bool centeredTitle = props.centered || props.titleText.align == TextAlign::Center;

  const HeaderStatusProps& status = props.status;
  const int16_t statusStripH = status.stripHeight;
  const int16_t statusInset = status.edgeInset < 0 ? sidePad : status.edgeInset;
  int16_t batteryReserve = 0;
  if (status.showBattery) {
    batteryReserve = static_cast<int16_t>(status.battery.glyphWidth + 2);
    if (status.battery.label) {
      batteryReserve = static_cast<int16_t>(
          batteryReserve + status.battery.gap +
          frame.target().measureText(status.battery.text.font, status.battery.label, status.battery.text).width);
    }
  }
  int16_t clockWidth = 0;
  if (status.clockText) {
    clockWidth = frame.target().measureText(status.battery.text.font, status.clockText, status.battery.text).width;
  }
  const int16_t titleLineH = frame.target().lineHeight(props.titleText.font);
  const bool titleSharesStrip =
      static_cast<int16_t>((rect.height - titleLineH) / 2 + props.titleOffsetY) < statusStripH;
  if (titleSharesStrip && status.showBattery) {
    const int16_t reserve = static_cast<int16_t>(batteryReserve + 8);
    if (status.batteryLeft) {
      content.x = static_cast<int16_t>(content.x + reserve);
      content.width = static_cast<int16_t>(content.width - reserve);
    } else {
      content.width = static_cast<int16_t>(content.width - reserve);
    }
  }
  if (clockWidth > 0 && status.clockCentered && !centeredTitle && titleSharesStrip) {
    const int16_t clockStart = static_cast<int16_t>(rect.x + (rect.width - clockWidth) / 2);
    const int16_t maxWidth = static_cast<int16_t>(clockStart - 8 - content.x);
    if (maxWidth < content.width) content.width = maxWidth;
  }

  const BitmapRef leading =
      props.leadingIcon ? props.leadingIcon : resolveBitmap(frame.assets(), props.leadingIconAsset);
  if (leading && props.leadingAction != NO_ACTION) {
    const int16_t btn = props.leadingSize > 0 ? props.leadingSize : static_cast<int16_t>(rect.height - 8);
    ButtonProps back;
    back.icon = leading;
    back.action = props.leadingAction;
    back.value = props.leadingValue;
    back.styles = props.leadingStyles;
    back.radius = props.leadingRadius;
    back.minTouchSize = props.minTouchSize;
    button(frame,
           Rect{static_cast<int16_t>(rect.x + 4), static_cast<int16_t>(rect.y + 4 + props.actionOffsetY), btn, btn},
           back);
    if (!centeredTitle) {
      const int16_t iconEnd = static_cast<int16_t>(4 + (btn + leading.width) / 2);
      const int16_t inset = static_cast<int16_t>(iconEnd + 6 - sidePad);
      if (inset > 0) {
        content.x = static_cast<int16_t>(content.x + inset);
        content.width = static_cast<int16_t>(content.width - inset);
      }
    }
  }
  const BitmapRef trailing =
      props.trailingIcon ? props.trailingIcon : resolveBitmap(frame.assets(), props.trailingIconAsset);
  if ((props.trailingLabel || trailing) && props.trailingAction != NO_ACTION) {
    const int16_t btnH = props.trailingSize > 0 ? props.trailingSize : static_cast<int16_t>(rect.height - 8);
    int16_t btnW = btnH;
    if (props.trailingLabel) {
      const Size labelSize =
          frame.target().measureText(props.trailingText.font, props.trailingLabel, props.trailingText);
      btnW = static_cast<int16_t>(labelSize.width + 20 + (trailing ? trailing.width + 4 : 0));
    }
    const int16_t trailingAnchor = static_cast<int16_t>(rect.right() - 12);
    const int16_t trailingY = static_cast<int16_t>(rect.y + 4 + props.actionOffsetY);
    ButtonProps action;
    action.label = props.trailingLabel;
    action.icon = trailing;
    action.action = props.trailingAction;
    action.value = props.trailingValue;
    action.styles = props.trailingStyles;
    action.radius = props.trailingRadius;
    action.text = props.trailingText;
    action.enabled = props.trailingEnabled;
    action.minTouchSize = props.minTouchSize;
    button(frame, Rect{static_cast<int16_t>(trailingAnchor - btnW), trailingY, btnW, btnH}, action);
    const bool hasAdjacentTrailing = props.trailingAdjacentIcon && props.trailingAdjacentAction != NO_ACTION;
    if (hasAdjacentTrailing) {
      ButtonProps adjacent;
      adjacent.icon = props.trailingAdjacentIcon;
      adjacent.action = props.trailingAdjacentAction;
      adjacent.value = props.trailingAdjacentValue;
      adjacent.styles = props.trailingStyles;
      adjacent.radius = props.trailingRadius;
      adjacent.minTouchSize = props.minTouchSize;
      button(frame, Rect{static_cast<int16_t>(trailingAnchor - 4 - btnW - btnH), trailingY, btnH, btnH}, adjacent);
    }
    if (!centeredTitle) {
      content.width = static_cast<int16_t>(content.width - btnW - (hasAdjacentTrailing ? btnH + 4 : 0) - 8);
    }
  }

  TextStyle titleStyle = props.titleText;
  if (props.centered) titleStyle.align = TextAlign::Center;

  const int16_t subLh = props.subtitle ? frame.target().lineHeight(props.subtitleText.font) : 0;
  constexpr int16_t subtitleGap = 4;

  if (props.subtitle) {
    const int16_t blockH = static_cast<int16_t>(titleLineH + subtitleGap + subLh);
    const int16_t topMargin = static_cast<int16_t>(content.height > blockH ? (content.height - blockH) / 2 : 0);
    const int16_t titleY = static_cast<int16_t>(content.y + topMargin + props.titleOffsetY);
    const int16_t subY = static_cast<int16_t>(titleY + titleLineH + subtitleGap);

    if (props.title) {
      Rect titleRect{content.x, titleY, content.width, titleLineH};
      if (props.rightLabel) {
        const Size rightSize =
            frame.target().measureText(props.subtitleText.font, props.rightLabel, props.subtitleText);
        Rect rightRect{static_cast<int16_t>(content.right() - rightSize.width),
                       static_cast<int16_t>(titleY + titleLineH - rightSize.height), rightSize.width,
                       rightSize.height};
        frame.target().text(rightRect, props.rightLabel, props.subtitleText);
        const int16_t used = static_cast<int16_t>(rightSize.width + 6);
        titleRect.width = static_cast<int16_t>(titleRect.width - used);
        if (centeredTitle) {
          titleRect.x = static_cast<int16_t>(titleRect.x + used);
          titleRect.width = static_cast<int16_t>(titleRect.width - used);
        }
      }
      frame.target().text(titleRect, props.title, titleStyle);
    }

    Rect subRect{content.x, subY, content.width, subLh};
    frame.target().text(subRect, props.subtitle, props.subtitleText);
  } else {
    if (props.title) {
      int16_t titleY = static_cast<int16_t>(content.y + props.titleOffsetY);
      Rect titleRect{content.x, titleY, content.width, content.height};
      if (props.rightLabel) {
        const Size rightSize =
            frame.target().measureText(props.subtitleText.font, props.rightLabel, props.subtitleText);
        const int16_t titleTop =
            static_cast<int16_t>(content.y + props.titleOffsetY + (content.height - titleLineH) / 2);
        Rect rightRect{static_cast<int16_t>(content.right() - rightSize.width),
                       static_cast<int16_t>(titleTop + titleLineH - rightSize.height), rightSize.width,
                       rightSize.height};
        frame.target().text(rightRect, props.rightLabel, props.subtitleText);
        const int16_t used = static_cast<int16_t>(rightSize.width + 6);
        titleRect.width = static_cast<int16_t>(titleRect.width - used);
        if (centeredTitle) {
          titleRect.x = static_cast<int16_t>(titleRect.x + used);
          titleRect.width = static_cast<int16_t>(titleRect.width - used);
        }
      }
      frame.target().text(titleRect, props.title, titleStyle);
    }
  }

  if (status.showBattery) {
    const int16_t batteryX = status.batteryLeft
                                 ? static_cast<int16_t>(rect.x + statusInset)
                                 : static_cast<int16_t>(rect.right() - statusInset - batteryReserve);
    batteryIndicator(frame, Rect{batteryX, rect.y, batteryReserve, statusStripH}, status.battery);
  }
  if (clockWidth > 0) {
    const int16_t clockLineH = frame.target().lineHeight(status.battery.text.font);
    const Rect ink = frame.target().inkBounds(status.battery.text.font, status.clockText, status.battery.text);
    const int16_t clockY = static_cast<int16_t>(rect.y + (statusStripH - ink.height) / 2 - ink.y);
    const int16_t clockX = status.clockCentered
                               ? static_cast<int16_t>(rect.x + (rect.width - clockWidth) / 2)
                               : status.batteryLeft ? static_cast<int16_t>(rect.right() - statusInset - clockWidth)
                                                    : static_cast<int16_t>(rect.x + statusInset);
    frame.target().text(Rect{clockX, clockY, clockWidth, clockLineH}, status.clockText, status.battery.text);
  }
}

}  // namespace ui
}  // namespace freeink

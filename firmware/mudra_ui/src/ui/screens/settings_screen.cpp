#include "settings_screen.h"

#include <stdio.h>

#include "../../config/board_config.h"
#include "../../config/ui_config.h"
#include "../components/button.h"
#include "../components/common.h"
#include "../components/navigation_bar.h"
#include "../components/status_indicator.h"
#include "../theme.h"

namespace ui {
namespace {

constexpr Rect kConnectionCard{14, 64, 212, 60};
constexpr Rect kDisplayCard{14, 142, 212, 76};
constexpr Rect kTouchButton{14, 236, 212, 40};
constexpr Rect kBack{0, 0, 56, 44};

}  // namespace

void SettingsScreen::invalidate() { painted_ = false; }

void SettingsScreen::drawConnectionCard(display::Driver& d, const AppState& state) {
    card(d, kConnectionCard.x, kConnectionCard.y, kConnectionCard.w, kConnectionCard.h);
    drawStatusRow(d, 28, 76, 184, "SERIAL LINK",
                  state.link == LinkStatus::Connected ? "CONNECTED" : "WAITING", linkColor(state.link));

    char count[16];
    snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(state.rxMessages));
    drawStatusRow(d, 28, 98, 184, "MESSAGES RECEIVED", count, theme::kText);
}

void SettingsScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    header(d, "SETTINGS", state.link == LinkStatus::Connected, true);

    sectionLabel(d, 14, 52, "CONNECTION");
    drawConnectionCard(d, state);

    sectionLabel(d, 14, 130, "DISPLAY");
    card(d, kDisplayCard.x, kDisplayCard.y, kDisplayCard.w, kDisplayCard.h);
    d.text(28, 154, "CONTROLLER", 1, theme::kTextDim);
    d.textRight(212, 154, d.controllerName(), 1, theme::kText);

    char resolution[16];
    snprintf(resolution, sizeof(resolution), "%dx%d", d.width(), d.height());
    d.text(28, 176, "RESOLUTION", 1, theme::kTextDim);
    d.textRight(212, 176, resolution, 1, theme::kText);

    d.text(28, 198, "ORIENTATION", 1, theme::kTextDim);
    d.textRight(212, 198, d.width() <= d.height() ? "PORTRAIT" : "LANDSCAPE", 1, theme::kText);

    sectionLabel(d, 14, 224, "INPUT");
    if (touchAvailable_) {
        drawButton(d, kTouchButton, "CALIBRATE TOUCH", secondaryButtonStyle());
    } else {
        d.fillRoundRect(kTouchButton.x, kTouchButton.y, kTouchButton.w, kTouchButton.h, ui_cfg::kButtonRadius,
                        theme::kSurface);
        d.roundRect(kTouchButton.x, kTouchButton.y, kTouchButton.w, kTouchButton.h, ui_cfg::kButtonRadius,
                    theme::kStroke);
        d.text(28, 246, "TOUCH", 1, theme::kTextDim);
        d.textRight(212, 246, "NOT ASSIGNED", 1, theme::kAmber);
    }

    drawNavigationBar(d, Screen::Settings);

    painted_ = true;
    lastLink_ = state.link;
    lastRxMessages_ = state.rxMessages;
}

void SettingsScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) {
        render(d, state);
        return;
    }
    if (state.link == lastLink_ && state.rxMessages == lastRxMessages_) return;
    lastLink_ = state.link;
    lastRxMessages_ = state.rxMessages;
    drawConnectionCard(d, state);
}

UiAction SettingsScreen::tap(int16_t x, int16_t y, const AppState&) {
    if (kBack.contains(x, y)) return UiAction::Back;
    if (touchAvailable_ && kTouchButton.contains(x, y)) return UiAction::Calibrate;
    return navigationHit(x, y, 240, 320);
}

}  // namespace ui

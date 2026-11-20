#include "home_screen.h"

#include "../../config/ui_config.h"
#include "../components/button.h"
#include "../components/common.h"
#include "../components/navigation_bar.h"
#include "../components/status_indicator.h"
#include "../theme.h"

namespace ui {
namespace {

constexpr Rect kStatusCard{14, 92, 212, 78};
constexpr Rect kPrimary{14, 190, 212, 52};
constexpr Rect kStudio{14, 248, 212, 30};

}  // namespace

void HomeScreen::invalidate() { painted_ = false; }

void HomeScreen::drawPrimary(display::Driver& d, const AppState& state) {
    if (state.mode == AppMode::Detecting) {
        drawButton(d, kPrimary, "STOP DETECTION", dangerButtonStyle());
    } else {
        drawButtonWithIcon(d, kPrimary, "START DETECTION", true, primaryButtonStyle());
    }
}

void HomeScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    header(d, "MUDRA AI", state.link == LinkStatus::Connected, false);

    d.text(16, 52, "GESTURE STUDIO", 2, theme::kText);
    d.text(16, 74, state.mode == AppMode::Detecting ? "Detection running" : "Ready to practice", 1,
           state.mode == AppMode::Detecting ? theme::kGreen : theme::kTextDim);

    card(d, kStatusCard.x, kStatusCard.y, kStatusCard.w, kStatusCard.h);
    drawStatusRow(d, 28, 103, 184, "LAPTOP", linkText(state.link), linkColor(state.link));
    drawStatusRow(d, 28, 125, 184, "CAMERA", cameraText(state.camera), cameraColor(state.camera));
    drawStatusRow(d, 28, 147, 184, "MODEL", modelText(state.model), modelColor(state.model));

    drawPrimary(d, state);

    d.fillRoundRect(kStudio.x, kStudio.y, kStudio.w, kStudio.h, 8, theme::kSurface2);
    d.roundRect(kStudio.x, kStudio.y, kStudio.w, kStudio.h, 8, theme::kStroke);
    d.textCentered(kStudio.cx(), static_cast<int16_t>(kStudio.y + 11), "OPEN LAPTOP STUDIO", 1,
                   theme::kViolet);

    drawNavigationBar(d, Screen::Home);

    painted_ = true;
    lastMode_ = state.mode;
    lastLink_ = state.link;
    lastCamera_ = state.camera;
    lastModel_ = state.model;
}

void HomeScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) {
        render(d, state);
        return;
    }
    if (state.mode == lastMode_ && state.link == lastLink_ && state.camera == lastCamera_ &&
        state.model == lastModel_) {
        return;
    }
    render(d, state);
}

UiAction HomeScreen::tap(int16_t x, int16_t y, const AppState& state) {
    if (kPrimary.contains(x, y)) {
        return state.mode == AppMode::Detecting ? UiAction::Stop : UiAction::Start;
    }
    if (kStudio.contains(x, y)) return UiAction::OpenStudio;
    return navigationHit(x, y, 240, 320);
}

}  // namespace ui

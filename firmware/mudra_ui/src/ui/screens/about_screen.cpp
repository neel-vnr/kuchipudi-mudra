#include "about_screen.h"

#include <stdio.h>

#include "../components/common.h"
#include "../components/navigation_bar.h"
#include "../theme.h"

namespace ui {
namespace {
constexpr Rect kBack{0, 0, 56, 44};
constexpr Rect kInfoCard{14, 214, 212, 54};
}  // namespace

void AboutScreen::invalidate() { painted_ = false; }

void AboutScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    header(d, "ABOUT", state.link == LinkStatus::Connected, true);

    mandala(d, d.width() / 2, 90, 34);
    d.textCentered(d.width() / 2, 126, "MUDRA AI", 3, theme::kCyan);
    d.textCentered(d.width() / 2, 152, "BHARATANATYAM GESTURE RECOGNITION", 1, theme::kTextDim);

    d.textCentered(d.width() / 2, 176, "Embedded display controller", 1, theme::kText);
    d.textCentered(d.width() / 2, 188, "for the MUDRA AI vision", 1, theme::kText);
    d.textCentered(d.width() / 2, 200, "pipeline on the laptop.", 1, theme::kText);

    card(d, kInfoCard.x, kInfoCard.y, kInfoCard.w, kInfoCard.h);
    d.text(28, 226, "FIRMWARE", 1, theme::kTextDim);
    d.textRight(212, 226, "v" MUDRA_FW_VERSION, 1, theme::kText);
    d.text(28, 248, "BOARD", 1, theme::kTextDim);
    d.textRight(212, 248, "ESP32-S3 N16R8", 1, theme::kText);

    drawNavigationBar(d, Screen::About);
    painted_ = true;
}

void AboutScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) render(d, state);
}

UiAction AboutScreen::tap(int16_t x, int16_t y, const AppState&) {
    if (kBack.contains(x, y)) return UiAction::Back;
    return navigationHit(x, y, 240, 320);
}

}  // namespace ui

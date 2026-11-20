#include "splash_screen.h"

#include <stdio.h>
#include <string.h>

#include "../../config/board_config.h"
#include "../../config/display_config.h"
#include "../../config/ui_config.h"
#include "../components/common.h"
#include "../theme.h"

namespace ui {
namespace {
constexpr int16_t kProgressX = 50;
constexpr int16_t kProgressY = 284;
constexpr int16_t kProgressW = 140;
constexpr int16_t kProgressH = 4;
}  // namespace

void SplashScreen::invalidate() { painted_ = false; }

void SplashScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    d.fillRect(0, 0, d.width(), 2, theme::kCyan);
    d.fillRect(d.width() - 64, 0, 64, 2, theme::kViolet);

    mandala(d, d.width() / 2, 84, 46);

    const char* wordmark = MUDRA_FW_NAME;
    d.textCentered(d.width() / 2, 140, wordmark, 3, theme::kCyan);
    const char* tagline = "BHARATANATYAM GESTURE RECOGNITION";
    d.textCentered(d.width() / 2, 166, tagline, 1, theme::kTextDim);

    d.hLine(40, 196, d.width() - 80, theme::kStroke);

    d.text(28, 212, "DISPLAY", 1, theme::kTextDim);
    char display[40];
    snprintf(display, sizeof(display), "%s  %dx%d", d.controllerName(), d.width(), d.height());
    d.textRight(d.width() - 28, 212, display, 1, theme::kText);

    drawLinkRow(d, state);

    // Progress track; update() fills it.
    d.fillRoundRect(kProgressX, kProgressY, kProgressW, kProgressH, 2, theme::kSurface2);
    d.fillRoundRect(kProgressX, kProgressY, 1, kProgressH, 2, theme::kCyan);

    char version[40];
    snprintf(version, sizeof(version), "%s  v%s", MUDRA_HW_BOARD, MUDRA_FW_VERSION);
    d.textCentered(d.width() / 2, 302, version, 1, theme::kTextDim);
    d.textCentered(d.width() / 2, 268, "INITIALISING", 1, theme::kCyanDim);

    startMs_ = millis();
    progress_ = 0;
    lastLink_ = state.link;
    lastEverLinked_ = state.everLinked;
    painted_ = true;
}

void SplashScreen::drawLinkRow(display::Driver& d, const AppState& state) {
    d.fillRect(28, 232, d.width() - 56, 10, theme::kBg);
    const bool online = state.link == LinkStatus::Connected;
    const char* text = online ? "CONNECTED" : (state.everLinked ? "RECONNECTING" : "WAITING");
    const uint16_t color = online ? theme::kGreen : theme::kAmber;
    d.text(28, 232, "LAPTOP", 1, theme::kTextDim);
    statusDot(d, d.width() - 28 - static_cast<int16_t>(strlen(text)) * 6 - 9, 236, color);
    d.textRight(d.width() - 28, 232, text, 1, color);
}

void SplashScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) {
        render(d, state);
        return;
    }

    if (state.link != lastLink_ || state.everLinked != lastEverLinked_) {
        lastLink_ = state.link;
        lastEverLinked_ = state.everLinked;
        drawLinkRow(d, state);
    }

    const uint32_t elapsed = millis() - startMs_;
    uint16_t progress = static_cast<uint16_t>((elapsed * 100UL) / ui_cfg::kSplashMs);
    if (progress > 100) progress = 100;
    if (progress != progress_) {
        const int16_t fillW = static_cast<int16_t>((kProgressW * progress) / 100);
        if (fillW > 0) {
            d.fillRoundRect(kProgressX, kProgressY, fillW, kProgressH, 2, theme::kCyan);
        }
        progress_ = progress;
    }
}

UiAction SplashScreen::tap(int16_t, int16_t, const AppState&) { return UiAction::Home; }

}  // namespace ui

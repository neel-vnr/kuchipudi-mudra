#include "navigation_bar.h"

#include "../../config/ui_config.h"
#include "../theme.h"

namespace ui {
namespace {

struct Tab {
    const char* label;
    Screen screen;
    UiAction action;
};

constexpr Tab kTabs[4] = {
    {"HOME", Screen::Home, UiAction::Home},
    {"DETECT", Screen::Detection, UiAction::Detection},
    {"CONFIG", Screen::Settings, UiAction::Settings},
    {"INFO", Screen::About, UiAction::About},
};

void drawIcon(display::Driver& d, Screen screen, int16_t cx, int16_t cy, uint16_t color) {
    switch (screen) {
        case Screen::Home:
            d.fillTriangle(cx - 8, cy - 1, cx, cy - 8, cx + 8, cy - 1, color);
            d.fillRect(cx - 6, cy, 12, 8, color);
            d.fillRect(cx - 1, cy + 3, 3, 5, theme::kBg);
            break;
        case Screen::Detection:
            d.circle(cx, cy, 8, color);
            d.circle(cx, cy, 4, color);
            d.fillCircle(cx, cy, 2, color);
            break;
        case Screen::Settings:
            for (int i = -1; i <= 1; ++i) {
                const int16_t y = static_cast<int16_t>(cy + i * 6);
                d.hLine(cx - 9, y, 18, color);
                d.fillCircle(static_cast<int16_t>(cx + i * 5), y, 2, color);
            }
            break;
        case Screen::About:
            d.circle(cx, cy, 8, color);
            d.fillRect(cx - 1, cy - 4, 2, 2, color);
            d.fillRect(cx - 1, cy - 1, 2, 6, color);
            break;
        default:
            break;
    }
}

}  // namespace

void drawNavigationBar(display::Driver& d, Screen active) {
    const int16_t y = d.height() - ui_cfg::kNavHeight;
    d.fillRect(0, y, d.width(), ui_cfg::kNavHeight, theme::kSurface);
    d.hLine(0, y, d.width(), theme::kStroke);

    const int16_t tabW = d.width() / 4;
    for (int i = 0; i < 4; ++i) {
        const Tab& tab = kTabs[i];
        const bool isActive = tab.screen == active;
        const uint16_t color = isActive ? theme::kCyan : theme::kTextDim;
        const int16_t cx = static_cast<int16_t>(i * tabW + tabW / 2);

        if (isActive) {
            d.fillRect(i * tabW + 10, y, tabW - 20, 2, theme::kCyan);
        }
        drawIcon(d, tab.screen, cx, y + 13, color);
        d.textCentered(cx, y + 26, tab.label, 1, color);
    }
}

UiAction navigationHit(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight) {
    const int16_t navTop = screenHeight - ui_cfg::kNavHeight;
    if (y < navTop) return UiAction::None;
    if (x < 0 || x >= screenWidth) return UiAction::None;
    const int16_t tabW = screenWidth / 4;
    int index = x / tabW;
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return kTabs[index].action;
}

}  // namespace ui

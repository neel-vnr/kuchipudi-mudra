#include "status_indicator.h"

#include "common.h"
#include "../theme.h"

namespace ui {

void drawStatusRow(display::Driver& d, int16_t x, int16_t y, int16_t w, const char* label, const char* value,
                   uint16_t color) {
    statusDot(d, x + 4, y + 4, color);
    d.text(x + 14, y, label, 1, theme::kTextDim);
    d.textRight(x + w, y, value, 1, color);
}

void drawMiniStatus(display::Driver& d, const Rect& area, const char* label, const char* value, uint16_t color) {
    const int16_t cx = area.cx();
    statusDot(d, cx - (display::Driver::textWidth(value, 1) / 2) - 8, area.y + 6, color);
    d.textCentered(cx + 4, area.y + 2, value, 1, color);
    d.textCentered(cx, area.y + 15, label, 1, theme::kTextDim);
}

}  // namespace ui

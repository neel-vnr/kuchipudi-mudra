#include "confidence_bar.h"

#include "../theme.h"

namespace ui {

void drawConfidenceBar(display::Driver& d, int16_t x, int16_t y, int16_t w, int16_t h, float percent, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    const int16_t radius = static_cast<int16_t>(h / 2);
    d.fillRoundRect(x, y, w, h, radius, theme::kSurface2);

    int16_t fillW = static_cast<int16_t>((static_cast<float>(w) * percent) / 100.0f + 0.5f);
    if (fillW > 0) {
        if (fillW < h) fillW = h;  // keep the rounded cap intact
        d.fillRoundRect(x, y, fillW, h, radius, color);
    }
}

}  // namespace ui

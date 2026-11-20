#include "button.h"

#include "../../config/ui_config.h"
#include "../theme.h"

namespace ui {

ButtonStyle primaryButtonStyle() {
    return ButtonStyle{theme::kCyan, theme::kCyan, theme::kTextDark, true};
}
ButtonStyle dangerButtonStyle() {
    return ButtonStyle{theme::kRed, theme::kRed, theme::kTextDark, true};
}
ButtonStyle secondaryButtonStyle() {
    return ButtonStyle{theme::kSurface, theme::kStroke, theme::kText, false};
}
ButtonStyle ghostButtonStyle() {
    return ButtonStyle{theme::kBg, theme::kStroke, theme::kTextDim, false};
}

void drawButton(display::Driver& d, const Rect& r, const char* label, const ButtonStyle& style, bool enabled) {
    if (r.w <= 0 || r.h <= 0) return;
    const uint16_t fill = style.filled ? (enabled ? style.fill : theme::kSurface2) : theme::kSurface;
    const uint16_t border = enabled ? style.border : theme::kStroke;
    const uint16_t textColor = enabled ? style.text : theme::kTextDim;

    if (style.filled) {
        d.fillRoundRect(r.x, r.y, r.w, r.h, ui_cfg::kButtonRadius, fill);
    } else {
        d.fillRoundRect(r.x, r.y, r.w, r.h, ui_cfg::kButtonRadius, fill);
        d.roundRect(r.x, r.y, r.w, r.h, ui_cfg::kButtonRadius, border);
    }

    const int16_t maxWidth = static_cast<int16_t>(r.w - 20);
    uint8_t size = 2;
    while (size > 1 && display::Driver::textWidth(label, size) > maxWidth) --size;

    const int16_t textW = display::Driver::textWidth(label, size);
    const int16_t textH = display::Driver::textHeight(size);
    d.text(r.cx() - textW / 2, r.cy() - textH / 2, label, size, textColor);
}

void drawButtonWithIcon(display::Driver& d, const Rect& r, const char* label, bool playIcon, const ButtonStyle& style) {
    if (r.w <= 0 || r.h <= 0) return;
    d.fillRoundRect(r.x, r.y, r.w, r.h, ui_cfg::kButtonRadius, style.fill);

    const uint8_t size = 2;
    const int16_t textW = display::Driver::textWidth(label, size);
    const int16_t iconW = playIcon ? 22 : 0;
    const int16_t contentW = textW + iconW;
    const int16_t startX = r.cx() - contentW / 2;
    const int16_t textY = r.cy() - display::Driver::textHeight(size) / 2;

    if (playIcon) {
        const int16_t cy = r.cy();
        d.fillTriangle(startX, cy - 7, startX, cy + 7, startX + 12, cy, style.text);
    }
    d.text(startX + iconW, textY, label, size, style.text);
}

}  // namespace ui

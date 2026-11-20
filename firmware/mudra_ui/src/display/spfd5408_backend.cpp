#include "spfd5408_backend.h"

#include <math.h>
#include <string.h>

#include "../config/display_config.h"
#include "font5x7.h"
#include "spfd5408.h"

// Only built when the native SPFD5408 driver is the selected controller.
#if MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408

namespace display {
namespace {
Spfd5408 lcd;
}  // namespace

bool Spfd5408Backend::begin() {
    ready_ = lcd.begin();
    return ready_;
}

int16_t Spfd5408Backend::width() const { return lcd.width(); }
int16_t Spfd5408Backend::height() const { return lcd.height(); }

void Spfd5408Backend::fillScreen(uint16_t color) { lcd.fillScreen(color); }

void Spfd5408Backend::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    lcd.fill(x, y, w, h, color);
}

void Spfd5408Backend::drawPixel(int16_t x, int16_t y, uint16_t color) { lcd.writePixel(x, y, color); }

void Spfd5408Backend::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    lcd.fill(x, y, w, 1, color);
    lcd.fill(x, y + h - 1, w, 1, color);
    lcd.fill(x, y, 1, h, color);
    lcd.fill(x + w - 1, y, 1, h, color);
}

void Spfd5408Backend::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    const int16_t dx = static_cast<int16_t>(abs(x1 - x0));
    const int16_t sx = x0 < x1 ? 1 : -1;
    const int16_t dy = static_cast<int16_t>(-abs(y1 - y0));
    const int16_t sy = y0 < y1 ? 1 : -1;
    int16_t error = static_cast<int16_t>(dx + dy);
    for (;;) {
        lcd.writePixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int16_t e2 = static_cast<int16_t>(error * 2);
        if (e2 >= dy) {
            error = static_cast<int16_t>(error + dy);
            x0 = static_cast<int16_t>(x0 + sx);
        }
        if (e2 <= dx) {
            error = static_cast<int16_t>(error + dx);
            y0 = static_cast<int16_t>(y0 + sy);
        }
    }
}

void Spfd5408Backend::drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) {
        lcd.writePixel(cx, cy, color);
        return;
    }
    int16_t x = r;
    int16_t y = 0;
    int16_t error = static_cast<int16_t>(1 - r);
    while (x >= y) {
        lcd.writePixel(cx + x, cy + y, color);
        lcd.writePixel(cx + y, cy + x, color);
        lcd.writePixel(cx - y, cy + x, color);
        lcd.writePixel(cx - x, cy + y, color);
        lcd.writePixel(cx - x, cy - y, color);
        lcd.writePixel(cx - y, cy - x, color);
        lcd.writePixel(cx + y, cy - x, color);
        lcd.writePixel(cx + x, cy - y, color);
        ++y;
        if (error < 0) {
            error = static_cast<int16_t>(error + 2 * y + 1);
        } else {
            --x;
            error = static_cast<int16_t>(error + 2 * (y - x) + 1);
        }
    }
}

void Spfd5408Backend::fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) {
        lcd.writePixel(cx, cy, color);
        return;
    }
    for (int16_t dy = -r; dy <= r; ++dy) {
        const int16_t dx = static_cast<int16_t>(sqrt(static_cast<float>(r) * r - static_cast<float>(dy) * dy));
        lcd.fill(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

void Spfd5408Backend::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        lcd.fill(x, y, w, h, color);
        return;
    }
    const int16_t limit = static_cast<int16_t>((w < h ? w : h) / 2);
    if (r > limit) r = limit;
    lcd.fill(x + r, y, w - 2 * r, h, color);
    lcd.fill(x, y + r, r, h - 2 * r, color);
    lcd.fill(x + w - r, y + r, r, h - 2 * r, color);
    fillCircle(x + r, y + r, r, color);
    fillCircle(x + w - r - 1, y + r, r, color);
    fillCircle(x + r, y + h - r - 1, r, color);
    fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void Spfd5408Backend::quarterArc(int16_t cx, int16_t cy, int16_t r, int8_t sx, int8_t sy, uint16_t color) {
    int16_t x = r;
    int16_t y = 0;
    int16_t error = static_cast<int16_t>(1 - r);
    while (x >= y) {
        lcd.writePixel(cx + sx * x, cy + sy * y, color);
        lcd.writePixel(cx + sx * y, cy + sy * x, color);
        ++y;
        if (error < 0) {
            error = static_cast<int16_t>(error + 2 * y + 1);
        } else {
            --x;
            error = static_cast<int16_t>(error + 2 * (y - x) + 1);
        }
    }
}

void Spfd5408Backend::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        drawRect(x, y, w, h, color);
        return;
    }
    const int16_t limit = static_cast<int16_t>((w < h ? w : h) / 2);
    if (r > limit) r = limit;
    lcd.fill(x + r, y, w - 2 * r, 1, color);
    lcd.fill(x + r, y + h - 1, w - 2 * r, 1, color);
    lcd.fill(x, y + r, 1, h - 2 * r, color);
    lcd.fill(x + w - 1, y + r, 1, h - 2 * r, color);
    quarterArc(x + r, y + r, r, -1, -1, color);
    quarterArc(x + w - r - 1, y + r, r, 1, -1, color);
    quarterArc(x + r, y + h - r - 1, r, -1, 1, color);
    quarterArc(x + w - r - 1, y + h - r - 1, r, 1, 1, color);
}

void Spfd5408Backend::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                                   uint16_t color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void Spfd5408Backend::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                                   uint16_t color) {
    int16_t minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int16_t maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int16_t minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int16_t maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    const int32_t d1 = (x2 - x1) * (y0 - y1) - (x0 - x1) * (y2 - y1);
    if (d1 == 0) return;  // degenerate

    for (int16_t y = minY; y <= maxY; ++y) {
        for (int16_t x = minX; x <= maxX; ++x) {
            const int32_t a = (x2 - x1) * (y - y1) - (x - x1) * (y2 - y1);
            const int32_t b = (x0 - x2) * (y - y2) - (x - x2) * (y0 - y2);
            const int32_t c = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0);
            const bool inside = (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
            if (inside) lcd.writePixel(x, y, color);
        }
    }
}

void Spfd5408Backend::text(int16_t x, int16_t y, const char* value, uint8_t sizeX, uint8_t sizeY, uint16_t color) {
    if (value == nullptr) return;
    if (sizeX == 0) sizeX = 1;
    if (sizeY == 0) sizeY = 1;

    int16_t cursorX = x;
    for (const char* p = value; *p != '\0'; ++p) {
        const uint8_t ch = static_cast<uint8_t>(*p);
        if (ch == '\n') continue;
        if (ch < 0x20) continue;
        const uint8_t* glyph = &kFont5x7[static_cast<uint16_t>(ch) * 5];
        for (uint8_t col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            uint8_t row = 0;
            while (row < 7) {
                if (bits & (1u << row)) {
                    uint8_t run = 1;
                    while (row + run < 7 && (bits & (1u << (row + run)))) ++run;
                    lcd.fill(static_cast<int16_t>(cursorX + col * sizeX),
                             static_cast<int16_t>(y + row * sizeY), sizeX, static_cast<int16_t>(run * sizeY), color);
                    row = static_cast<uint8_t>(row + run);
                } else {
                    ++row;
                }
            }
        }
        cursorX = static_cast<int16_t>(cursorX + 6 * sizeX);
    }
}

}  // namespace display

#endif  // MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408

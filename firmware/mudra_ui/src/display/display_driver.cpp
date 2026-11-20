#include "display_driver.h"

#include <string.h>

#include "../config/board_config.h"
#include "../config/display_config.h"
#include "../config/pin_config.h"
#include "lcd_probe.h"
#include "spfd5408_backend.h"

#if MUDRA_LCD_CONTROLLER != MUDRA_LCD_CTRL_SPFD5408
#include <Arduino_GFX_Library.h>
#endif

namespace display {
namespace {

#if MUDRA_LCD_CONTROLLER != MUDRA_LCD_CTRL_SPFD5408
// ---------------------------------------------------------------------------
// Arduino_GFX backend for the 8-bit 8080 parallel bus.
// ---------------------------------------------------------------------------

class ArduinoGfxBackend : public Backend {
public:
    ArduinoGfxBackend()
        : bus_(new Arduino_ESP32PAR8(
              board_pins::LCD_DC, board_pins::LCD_CS, board_pins::LCD_WR, board_pins::LCD_RD,
              board_pins::LCD_DATA[0], board_pins::LCD_DATA[1], board_pins::LCD_DATA[2],
              board_pins::LCD_DATA[3], board_pins::LCD_DATA[4], board_pins::LCD_DATA[5],
              board_pins::LCD_DATA[6], board_pins::LCD_DATA[7])),
          panel_(makePanel(bus_)) {}

    bool begin() override {
        if (panel_ == nullptr) {
            return false;
        }
        const bool ok = panel_->begin();
        if (!ok) {
            return false;
        }
#if MUDRA_LCD_INVERT
        panel_->invertDisplay(true);
#else
        panel_->invertDisplay(false);
#endif
        panel_->setTextWrap(false);
        panel_->fillScreen(0x0000);
        return true;
    }

    int16_t width() const override { return panel_->width(); }
    int16_t height() const override { return panel_->height(); }
    void fillScreen(uint16_t c) override { panel_->fillScreen(c); }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { panel_->fillRect(x, y, w, h, c); }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { panel_->drawRect(x, y, w, h, c); }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) override {
        panel_->fillRoundRect(x, y, w, h, r, c);
    }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) override {
        panel_->drawRoundRect(x, y, w, h, r, c);
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) override { panel_->drawLine(x0, y0, x1, y1, c); }
    void drawPixel(int16_t x, int16_t y, uint16_t c) override { panel_->drawPixel(x, y, c); }
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t c) override { panel_->drawCircle(x, y, r, c); }
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) override { panel_->fillCircle(x, y, r, c); }
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) override {
        panel_->drawTriangle(x0, y0, x1, y1, x2, y2, c);
    }
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) override {
        panel_->fillTriangle(x0, y0, x1, y1, x2, y2, c);
    }
    void text(int16_t x, int16_t y, const char* text, uint8_t sx, uint8_t sy, uint16_t c) override {
        panel_->setTextColor(c);
        panel_->setTextSize(sx, sy);
        panel_->setCursor(x, y);
        panel_->print(text);
    }
    void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* data) override {
        panel_->draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), w, h);
    }

private:
    static Arduino_GFX* makePanel(Arduino_DataBus* bus) {
#if MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9341
        return new Arduino_ILI9341(bus, board_pins::LCD_RST, display_cfg::kRotation);
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9342
        return new Arduino_ILI9342(bus, board_pins::LCD_RST, display_cfg::kRotation);
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ST7796
        return new Arduino_ST7796(bus, board_pins::LCD_RST, display_cfg::kRotation);
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9488
        return new Arduino_ILI9488(bus, board_pins::LCD_RST, display_cfg::kRotation);
#else
        return nullptr;
#endif
    }

    Arduino_DataBus* bus_ = nullptr;
    Arduino_GFX* panel_ = nullptr;
};
#endif  // MUDRA_LCD_CONTROLLER != SPFD5408

}  // namespace

Driver::Driver() = default;

Driver::~Driver() { delete backend_; }

bool Driver::begin() {
#if MUDRA_LCD_PROBE
    // Identify the panel before the driver takes over the bus. Diagnostic only.
    probeController(true);
#endif
#if MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408
    backend_ = new Spfd5408Backend();
#else
    backend_ = new ArduinoGfxBackend();
#endif
    ready_ = backend_->begin();
    if (!ready_) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# LCD init FAILED");
#endif
        return false;
    }
    width_ = backend_->width();
    height_ = backend_->height();
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# LCD init OK controller=%s rotation=%u size=%dx%d\n",
                  MUDRA_LCD_CONTROLLER_NAME, MUDRA_LCD_ROTATION, width_, height_);
#endif
    return true;
}

const char* Driver::controllerName() const { return MUDRA_LCD_CONTROLLER_NAME; }
uint8_t Driver::rotation() const { return display_cfg::kRotation; }

void Driver::fill(uint16_t c) {
    if (ready_) backend_->fillScreen(c);
}
void Driver::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (!ready_ || w <= 0 || h <= 0) return;
    backend_->fillRect(x, y, w, h, c);
}
void Driver::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    if (!ready_ || w <= 0 || h <= 0) return;
    backend_->fillRoundRect(x, y, w, h, r, c);
}
void Driver::rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (!ready_ || w <= 0 || h <= 0) return;
    backend_->drawRect(x, y, w, h, c);
}
void Driver::roundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    if (!ready_ || w <= 0 || h <= 0) return;
    backend_->drawRoundRect(x, y, w, h, r, c);
}
void Driver::hLine(int16_t x, int16_t y, int16_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
void Driver::vLine(int16_t x, int16_t y, int16_t h, uint16_t c) { fillRect(x, y, 1, h, c); }
void Driver::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    if (ready_) backend_->drawLine(x0, y0, x1, y1, c);
}
void Driver::pixel(int16_t x, int16_t y, uint16_t c) {
    if (ready_) backend_->drawPixel(x, y, c);
}
void Driver::blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* data) {
    if (!ready_ || data == nullptr || w <= 0 || h <= 0) return;
    backend_->blit565(x, y, w, h, data);
}
void Driver::circle(int16_t x, int16_t y, int16_t r, uint16_t c) {
    if (ready_) backend_->drawCircle(x, y, r, c);
}
void Driver::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) {
    if (ready_) backend_->fillCircle(x, y, r, c);
}
void Driver::triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
    if (ready_) backend_->drawTriangle(x0, y0, x1, y1, x2, y2, c);
}
void Driver::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
    if (ready_) backend_->fillTriangle(x0, y0, x1, y1, x2, y2, c);
}

void Driver::text(int16_t x, int16_t y, const char* value, uint8_t size, uint16_t color) {
    text(x, y, value, size, size, color);
}
void Driver::text(int16_t x, int16_t y, const char* value, uint8_t sizeX, uint8_t sizeY, uint16_t color) {
    if (!ready_ || value == nullptr || *value == '\0') return;
    if (sizeX == 0) sizeX = 1;
    if (sizeY == 0) sizeY = 1;
    backend_->text(x, y, value, sizeX, sizeY, color);
}
void Driver::textCentered(int16_t cx, int16_t y, const char* value, uint8_t size, uint16_t color) {
    if (value == nullptr) return;
    text(cx - textWidth(value, size) / 2, y, value, size, color);
}
void Driver::textRight(int16_t right, int16_t y, const char* value, uint8_t size, uint16_t color) {
    if (value == nullptr) return;
    text(right - textWidth(value, size), y, value, size, color);
}
int16_t Driver::textWidth(const char* value, uint8_t size) {
    return (value == nullptr) ? 0 : static_cast<int16_t>(strlen(value)) * 6 * size;
}
int16_t Driver::textHeight(uint8_t size) { return 8 * size; }
uint8_t Driver::fitSize(const char* value, uint8_t maxSize, int16_t maxWidth) {
    if (value == nullptr || *value == '\0' || maxSize == 0) return 1;
    for (uint8_t s = maxSize; s > 1; --s) {
        if (textWidth(value, s) <= maxWidth) return s;
    }
    return 1;
}

void Driver::runColorTest(uint16_t holdMs) {
    if (!ready_) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# LCDTEST skipped: display not ready");
#endif
        return;
    }
    struct Phase {
        const char* name;
        uint16_t color;
    };
    static const Phase phases[] = {
        {"RED", 0xF800}, {"GREEN", 0x07E0}, {"BLUE", 0x001F}, {"WHITE", 0xFFFF}, {"BLACK", 0x0000},
    };
    for (const Phase& p : phases) {
#if MUDRA_SERIAL_CONSOLE
        Serial.printf("# LCDTEST phase=%s color=0x%04X\n", p.name, p.color);
#endif
        backend_->fillScreen(p.color);
        delay(holdMs);
    }
    runGeometryTest(holdMs * 5);
}

void Driver::runGeometryTest(uint16_t holdMs) {
    if (!ready_) return;
    const int16_t w = width();
    const int16_t h = height();
    backend_->fillScreen(0x0000);

    // 8 vertical colour bars across the top third.
    static const uint16_t bars[] = {0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF};
    const int16_t barW = w / 8;
    for (int i = 0; i < 8; ++i) {
        backend_->fillRect(i * barW, 0, barW, h / 3, bars[i]);
    }

    // Border and centre crosshair prove addressable geometry.
    backend_->drawRect(0, 0, w, h, 0xFFFF);
    backend_->drawRect(1, 1, w - 2, h - 2, 0x7BEF);
    backend_->drawLine(0, h / 2, w - 1, h / 2, 0x07FF);
    backend_->drawLine(w / 2, 0, w / 2, h - 1, 0x07FF);
    backend_->fillCircle(w / 2, h / 2, 6, 0xF800);

    backend_->text(w / 2 - textWidth("MUDRA AI", 3) / 2, h / 3 + 16, "MUDRA AI", 3, 3, 0x07FF);
    char line[40];
    snprintf(line, sizeof(line), "%s %dx%d", MUDRA_LCD_CONTROLLER_NAME, w, h);
    backend_->text(w / 2 - textWidth(line, 1) / 2, h / 3 + 48, line, 1, 1, 0xFFFF);
    backend_->text(w / 2 - textWidth("LCD SELF TEST", 1) / 2, h / 3 + 64, "LCD SELF TEST", 1, 1, 0x7BEF);

    // Corner markers.
    backend_->fillRect(4, 4, 22, 3, 0x07E0);
    backend_->fillRect(4, 4, 3, 22, 0x07E0);
    backend_->fillRect(w - 27, 4, 22, 3, 0xF800);
    backend_->fillRect(w - 7, 4, 3, 22, 0xF800);
    backend_->fillRect(4, h - 7, 22, 3, 0x07FF);
    backend_->fillRect(4, h - 27, 3, 22, 0x07FF);
    backend_->fillRect(w - 27, h - 7, 22, 3, 0xFFE0);
    backend_->fillRect(w - 7, h - 27, 3, 22, 0xFFE0);

#if MUDRA_SERIAL_CONSOLE
    Serial.println("# LCDTEST geometry drawn");
#endif
    delay(holdMs);
}

}  // namespace display

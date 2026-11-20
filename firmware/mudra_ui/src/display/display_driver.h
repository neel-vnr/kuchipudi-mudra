#pragma once

#include <Arduino.h>

#include "display_backend.h"

// ---------------------------------------------------------------------------
// DisplayDriver: device-independent drawing helpers with bounds clamping,
// text fitting and hardware diagnostics. Owns one concrete backend.
// ---------------------------------------------------------------------------

namespace display {

class Driver {
public:
    Driver();
    ~Driver();

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // Initialises the bus, panel controller and clears the panel.
    bool begin();
    bool ready() const { return ready_; }
    const char* controllerName() const;
    uint8_t rotation() const;

    int16_t width() const { return width_; }
    int16_t height() const { return height_; }

    void fill(uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
    void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void roundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
    void hLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void vLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void pixel(int16_t x, int16_t y, uint16_t color);

    // Bulk RGB565 blit (little-endian pixels). Used for reference pictures.
    void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* data);
    void circle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

    // Built-in 5x7 GFX font scaled by integer factors.
    void text(int16_t x, int16_t y, const char* text, uint8_t size, uint16_t color);
    void text(int16_t x, int16_t y, const char* text, uint8_t sizeX, uint8_t sizeY, uint16_t color);
    void textCentered(int16_t cx, int16_t y, const char* text, uint8_t size, uint16_t color);
    void textRight(int16_t right, int16_t y, const char* text, uint8_t size, uint16_t color);

    static int16_t textWidth(const char* text, uint8_t size);
    static int16_t textHeight(uint8_t size);
    // Largest size <= maxSize whose rendered width fits maxWidth (minimum 1).
    static uint8_t fitSize(const char* text, uint8_t maxSize, int16_t maxWidth);

    // Hardware verification: solid colours then a geometry/colour pattern.
    // Blocks for roughly holdMs per colour and logs each phase on Serial.
    void runColorTest(uint16_t holdMs);
    void runGeometryTest(uint16_t holdMs);

private:
    display::Backend* backend_ = nullptr;
    bool ready_ = false;
    int16_t width_ = 0;
    int16_t height_ = 0;
};

}  // namespace display

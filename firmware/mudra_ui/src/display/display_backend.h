#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Thin rendering interface. The UI only depends on this, never on a concrete
// graphics library. A different library (LovyanGFX / TFT_eSPI) can be added by
// implementing one class in display_driver.cpp.
// ---------------------------------------------------------------------------

namespace display {

class Backend {
public:
    virtual ~Backend() = default;

    virtual bool begin() = 0;

    virtual int16_t width() const = 0;
    virtual int16_t height() const = 0;

    virtual void fillScreen(uint16_t color) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) = 0;
    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) = 0;
    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) = 0;
    virtual void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) = 0;
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) = 0;
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
    virtual void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) = 0;
    virtual void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) = 0;
    virtual void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) = 0;
    virtual void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) = 0;
    virtual void text(int16_t x, int16_t y, const char* text, uint8_t sizeX, uint8_t sizeY, uint16_t color) = 0;

    // Bulk RGB565 copy: sets the address window once and streams pixels. Far
    // faster than thousands of fillRect runs when drawing a photograph.
    virtual void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* data) = 0;
};

}  // namespace display

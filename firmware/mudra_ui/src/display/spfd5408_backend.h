#pragma once

#include "display_backend.h"

namespace display {

// Backend implementation for the SPFD5408 / ILI932x parallel panel. All
// drawing is decomposed into window fills and single-pixel writes, which keeps
// the driver small and predictable on the 8-bit bus.
class Spfd5408Backend : public Backend {
public:
    bool begin() override;

    int16_t width() const override;
    int16_t height() const override;

    void fillScreen(uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) override;
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) override;
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) override;
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) override;
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) override;
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) override;
    void text(int16_t x, int16_t y, const char* text, uint8_t sizeX, uint8_t sizeY, uint16_t color) override;

private:
    void quarterArc(int16_t cx, int16_t cy, int16_t r, int8_t sx, int8_t sy, uint16_t color);

    bool ready_ = false;
};

}  // namespace display

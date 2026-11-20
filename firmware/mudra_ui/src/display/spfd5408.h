#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Native SPFD5408 8-bit parallel driver for the ESP32-S3.
//
// The seller-recommended SPFD5408 library runs the Adafruit ILI9341-compatible
// register layout (8-bit command, 16-bit data, 0x2A/0x2B/0x2C windowing), not
// the ILI932x 16-bit register layout. This driver implements that sequence
// exactly, including the power-control and 500 ms display-on timing that a
// minimal init omits.
//
// Writes are bit-banged through the fixed GPIO map in pin_config.h with a
// widened WR strobe, because the shield's 74HC245 buffer and jumper leads do
// not tolerate Arduino_GFX's few-nanosecond register writes.
// ---------------------------------------------------------------------------

namespace display {

class Spfd5408 {
public:
    static constexpr int16_t kWidth = 240;
    static constexpr int16_t kHeight = 320;

    bool begin();

    int16_t width() const { return kWidth; }
    int16_t height() const { return kHeight; }

    // Fills a clipped rectangle with a 16-bit 565 colour.
    void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillScreen(uint16_t color) { fill(0, 0, kWidth, kHeight, color); }

    // Single pixel, clipped here.
    void writePixel(int16_t x, int16_t y, uint16_t color);

private:
    void initPins();
    void resetPanel();
    void writeByte(uint8_t value);
    void writeCommand(uint8_t command);
    void writeRegister8(uint8_t command, uint8_t value);
    void writeRegister16(uint8_t command, uint16_t value);
    uint8_t readRegister8(uint8_t command);
    uint8_t madCtlValue() const;
    void setRotation(uint8_t rotation);
    void setWindow(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
    void startGram();
    void endGram();
    void push(uint16_t color);
    void pushRepeated(uint16_t color, uint32_t count);

    uint8_t entryMode_ = 0;
    bool ready_ = false;
};

}  // namespace display

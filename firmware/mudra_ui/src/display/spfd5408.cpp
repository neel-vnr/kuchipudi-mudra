#include "spfd5408.h"

#include "../config/board_config.h"
#include "../config/display_config.h"
#include "../config/pin_config.h"

// The SPFD5408 bit-banged driver only compiles when it is actually selected.
// It assumes every control pin is below GPIO 32 because it writes GPIO_OUT_W1TS
// directly, which is not true for the current ILI9341/Arduino_GFX build.
#if MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408

namespace display {
namespace {

// The data pins are used as one 8-bit port, so they must stay contiguous.
static_assert(board_pins::LCD_DATA[7] == board_pins::LCD_DATA[0] + 7,
              "SPFD5408 driver expects LCD_D0..D7 on consecutive GPIOs");

constexpr uint32_t kDataShift = static_cast<uint32_t>(board_pins::LCD_DATA[0]);
constexpr uint32_t kCsMask = 1u << board_pins::LCD_CS;
constexpr uint32_t kDcMask = 1u << board_pins::LCD_DC;
constexpr uint32_t kWrMask = 1u << board_pins::LCD_WR;
constexpr uint32_t kRdMask = 1u << board_pins::LCD_RD;

inline void gpioHigh(uint32_t mask) { REG_WRITE(GPIO_OUT_W1TS_REG, mask); }
inline void gpioLow(uint32_t mask) { REG_WRITE(GPIO_OUT_W1TC_REG, mask); }

inline void dataBus(uint8_t value) {
    gpioHigh(static_cast<uint32_t>(value) << kDataShift);
    gpioLow(static_cast<uint32_t>(value ^ 0xFF) << kDataShift);
}

inline void csLow() { gpioLow(kCsMask); }
inline void csHigh() { gpioHigh(kCsMask); }
inline void dcCommand() { gpioLow(kDcMask); }
inline void dcData() { gpioHigh(kDcMask); }

// Widened write strobe: ~70 ns low plus register latency, comfortably inside
// the controller's cycle time and the shield buffer's tolerance.
inline void strobe() {
    gpioLow(kWrMask);
    for (uint8_t i = 0; i < 16; ++i) __asm__ __volatile__("nop");
    gpioHigh(kWrMask);
    for (uint8_t i = 0; i < 4; ++i) __asm__ __volatile__("nop");
}

// Controller commands used by the seller-recommended SPFD5408 sequence.
constexpr uint8_t kSwReset = 0x01;
constexpr uint8_t kDisplayOff = 0x28;
constexpr uint8_t kDisplayOn = 0x29;
constexpr uint8_t kSleepOut = 0x11;
constexpr uint8_t kMadCtl = 0x36;
constexpr uint8_t kPixelFormat = 0x3A;
constexpr uint8_t kFrameControl = 0xB1;
constexpr uint8_t kEntryMode = 0xB7;
constexpr uint8_t kColAddrSet = 0x2A;
constexpr uint8_t kPageAddrSet = 0x2B;
constexpr uint8_t kMemoryWrite = 0x2C;

constexpr uint8_t kMadCtlMy = 0x80;
constexpr uint8_t kMadCtlMx = 0x40;
constexpr uint8_t kMadCtlMv = 0x20;
constexpr uint8_t kMadCtlBgr = 0x08;

}  // namespace

void Spfd5408::initPins() {
    pinMode(board_pins::LCD_CS, OUTPUT);
    pinMode(board_pins::LCD_DC, OUTPUT);
    pinMode(board_pins::LCD_WR, OUTPUT);
    pinMode(board_pins::LCD_RD, OUTPUT);
    pinMode(board_pins::LCD_RST, OUTPUT);
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], OUTPUT);

    csHigh();
    dcData();
    gpioHigh(kWrMask);
    gpioHigh(kRdMask);  // write-only operation: RD stays inactive
    dataBus(0xFF);
}

void Spfd5408::writeByte(uint8_t value) {
    dataBus(value);
    strobe();
}

void Spfd5408::writeCommand(uint8_t command) {
    dcCommand();
    writeByte(command);
    dcData();
}

void Spfd5408::writeRegister8(uint8_t command, uint8_t value) {
    csLow();
    writeCommand(command);
    writeByte(value);
    csHigh();
}

void Spfd5408::writeRegister16(uint8_t command, uint16_t value) {
    csLow();
    writeCommand(command);
    writeByte(static_cast<uint8_t>(value >> 8));
    writeByte(static_cast<uint8_t>(value & 0xFF));
    csHigh();
}

uint8_t Spfd5408::readRegister8(uint8_t command) {
    csLow();
    writeCommand(command);
    // Turn the data bus around; the first byte after a read command is a dummy
    // on this controller family, so clock two bytes and keep the second.
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], INPUT);
    uint8_t value = 0;
    for (uint8_t pass = 0; pass < 2; ++pass) {
        gpioLow(kRdMask);
        delayMicroseconds(2);
        value = 0;
        for (uint8_t i = 0; i < 8; ++i) {
            if (digitalRead(board_pins::LCD_DATA[i]) != LOW) value |= static_cast<uint8_t>(1u << i);
        }
        gpioHigh(kRdMask);
        delayMicroseconds(2);
    }
    csHigh();
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], OUTPUT);
    return value;
}

void Spfd5408::resetPanel() {
    csHigh();
    gpioHigh(kWrMask);
    gpioHigh(kRdMask);
    digitalWrite(board_pins::LCD_RST, LOW);
    delay(3);
    digitalWrite(board_pins::LCD_RST, HIGH);
    delay(50);
}

bool Spfd5408::begin() {
    initPins();
    resetPanel();

    // Exact sequence from the seller-recommended SPFD5408 library
    // (Adafruit_TFTLCD ILI9341 branch, which is what their begin(0x9341) runs).
    writeCommand(kSwReset);
    delay(50);
    writeCommand(kDisplayOff);

    writeRegister8(0xC0, 0x23);      // power control 1
    writeRegister8(0xC1, 0x10);      // power control 2
    writeRegister16(0xC5, 0x2B2B);   // VCOM control 1
    writeRegister8(0xC7, 0xC0);      // VCOM control 2
    writeRegister8(kMadCtl, madCtlValue());
    writeRegister8(kPixelFormat, 0x55);  // 16 bit/pixel
    writeRegister16(kFrameControl, 0x001B);
    writeRegister8(kEntryMode, 0x07);

    writeCommand(kSleepOut);
    delay(150);
    writeCommand(kDisplayOn);
    delay(500);

    setRotation(display_cfg::kRotation);
    setWindow(0, 0, kWidth - 1, kHeight - 1);

    ready_ = true;
    fillScreen(0x0000);

#if MUDRA_SERIAL_CONSOLE
    // Read-back proof that the write path reached the controller.
    Serial.printf("# SPFD5408 readback MADCTL=0x%02X COLMOD=0x%02X RDMODE=0x%02X\n",
                  readRegister8(0x0B), readRegister8(0x0C), readRegister8(0x0A));
#endif
    return true;
}

uint8_t Spfd5408::madCtlValue() const {
    switch (display_cfg::kRotation) {
        case 1: return static_cast<uint8_t>(kMadCtlMx | kMadCtlMy | kMadCtlMv | kMadCtlBgr);
        case 2: return static_cast<uint8_t>(kMadCtlMx | kMadCtlBgr);
        case 3: return static_cast<uint8_t>(kMadCtlMv | kMadCtlBgr);
        default: return static_cast<uint8_t>(kMadCtlMy | kMadCtlBgr);
    }
}

void Spfd5408::setRotation(uint8_t rotation) {
    writeRegister8(kMadCtl, madCtlValue());
    entryMode_ = rotation;
}

void Spfd5408::setWindow(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    csLow();
    writeCommand(kColAddrSet);
    writeByte(static_cast<uint8_t>(x1 >> 8));
    writeByte(static_cast<uint8_t>(x1 & 0xFF));
    writeByte(static_cast<uint8_t>(x2 >> 8));
    writeByte(static_cast<uint8_t>(x2 & 0xFF));

    writeCommand(kPageAddrSet);
    writeByte(static_cast<uint8_t>(y1 >> 8));
    writeByte(static_cast<uint8_t>(y1 & 0xFF));
    writeByte(static_cast<uint8_t>(y2 >> 8));
    writeByte(static_cast<uint8_t>(y2 & 0xFF));

    writeCommand(kMemoryWrite);
}

void Spfd5408::startGram() { csLow(); }

void Spfd5408::endGram() { csHigh(); }

void Spfd5408::push(uint16_t color) {
    writeByte(static_cast<uint8_t>(color >> 8));
    writeByte(static_cast<uint8_t>(color & 0xFF));
}

void Spfd5408::pushRepeated(uint16_t color, uint32_t count) {
    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    while (count-- > 0) {
        writeByte(hi);
        writeByte(lo);
    }
}

void Spfd5408::writePixel(int16_t x, int16_t y, uint16_t color) {
    if (!ready_ || x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    setWindow(x, y, x, y);
    push(color);
    endGram();
}

void Spfd5408::fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!ready_ || w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= kWidth || y >= kHeight) return;
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    if (w <= 0 || h <= 0) return;

    setWindow(x, y, x + w - 1, y + h - 1);
    pushRepeated(color, static_cast<uint32_t>(w) * static_cast<uint32_t>(h));
    endGram();
}

}  // namespace display

#endif  // MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408

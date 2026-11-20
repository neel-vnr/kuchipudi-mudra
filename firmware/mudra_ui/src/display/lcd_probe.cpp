#include "lcd_probe.h"

#include <Arduino.h>

#include "../config/board_config.h"
#include "../config/pin_config.h"

namespace display {
namespace {

// RD is optional. On a board without a spare GPIO the shield's RD pin is tied
// to 3V3 and the read path is simply unavailable; every RD access must then be
// a no-op instead of driving a non-existent pin.
constexpr bool kRdAssigned = board_pins::LCD_RD >= 0;

inline void rdWrite(uint8_t level) {
    if (kRdAssigned) digitalWrite(board_pins::LCD_RD, level);
}

inline void rdSetup() {
    if (kRdAssigned) pinMode(board_pins::LCD_RD, OUTPUT);
}

void dataPinsOutput() {
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], OUTPUT);
}

void dataPinsInput() {
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], INPUT);
}

void writeDataBus(uint8_t value) {
    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(board_pins::LCD_DATA[i], (value >> i) & 0x01);
    }
}

uint8_t readDataBus() {
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (digitalRead(board_pins::LCD_DATA[i]) != LOW) value |= static_cast<uint8_t>(1u << i);
    }
    return value;
}

void pulseWrite() {
    digitalWrite(board_pins::LCD_WR, LOW);
    delayMicroseconds(2);
    digitalWrite(board_pins::LCD_WR, HIGH);
    delayMicroseconds(2);
}

void writeCommand(uint8_t command) {
    digitalWrite(board_pins::LCD_CS, LOW);
    digitalWrite(board_pins::LCD_DC, LOW);
    writeDataBus(command);
    pulseWrite();
    digitalWrite(board_pins::LCD_CS, HIGH);
    digitalWrite(board_pins::LCD_DC, HIGH);
}

uint8_t readByte() {
    rdWrite(LOW);
    delayMicroseconds(2);
    const uint8_t value = readDataBus();
    rdWrite(HIGH);
    delayMicroseconds(2);
    return value;
}

// Command is written with the bus as output, then the bus is turned around and
// the response bytes are clocked out with RD while CS and DC stay asserted.
uint8_t readRegister(uint8_t command, uint8_t* out, uint8_t count) {
    dataPinsOutput();
    writeCommand(command);
    dataPinsInput();
    digitalWrite(board_pins::LCD_DC, HIGH);
    digitalWrite(board_pins::LCD_CS, LOW);
    for (uint8_t i = 0; i < count; ++i) out[i] = readByte();
    digitalWrite(board_pins::LCD_CS, HIGH);
    dataPinsOutput();
    return count;
}

bool allZero(const uint8_t* data, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) {
        if (data[i] != 0x00) return false;
    }
    return true;
}

bool allOnes(const uint8_t* data, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i) {
        if (data[i] != 0xFF) return false;
    }
    return true;
}

}  // namespace

ControllerProbe probeController(bool verbose) {
    ControllerProbe probe;

    // Idle bus state.
    pinMode(board_pins::LCD_CS, OUTPUT);
    pinMode(board_pins::LCD_DC, OUTPUT);
    pinMode(board_pins::LCD_WR, OUTPUT);
    rdSetup();
    pinMode(board_pins::LCD_RST, OUTPUT);
    digitalWrite(board_pins::LCD_CS, HIGH);
    digitalWrite(board_pins::LCD_DC, HIGH);
    digitalWrite(board_pins::LCD_WR, HIGH);
    rdWrite(HIGH);

    // Hardware reset.
    digitalWrite(board_pins::LCD_RST, LOW);
    delay(25);
    digitalWrite(board_pins::LCD_RST, HIGH);
    delay(120);
    probe.resetDone = true;

    // Software reset + sleep out so registers are readable.
    dataPinsOutput();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(150);

    readRegister(0x04, probe.id04, 4);
    readRegister(0x00, probe.id00, 3);
    readRegister(0xD3, probe.idd3, 4);

    const bool zero = allZero(probe.id04, 4) && allZero(probe.id00, 3);
    const bool ones = allOnes(probe.id04, 4) && allOnes(probe.id00, 3);
    probe.readable = !(zero || ones);

    if (verbose) {
#if MUDRA_SERIAL_CONSOLE
        Serial.printf("# LCDPROBE id04=%02X %02X %02X %02X id00=%02X %02X %02X idD3=%02X %02X %02X %02X\n",
                      probe.id04[0], probe.id04[1], probe.id04[2], probe.id04[3], probe.id00[0], probe.id00[1],
                      probe.id00[2], probe.idd3[0], probe.idd3[1], probe.idd3[2], probe.idd3[3]);
        Serial.printf("# LCDPROBE bus %s, guess=%s\n", probe.readable ? "readable" : "not readable",
                      identifyController(probe));
#endif
    }
    return probe;
}

const char* identifyController(const ControllerProbe& probe) {
    const uint8_t* a = probe.id04;
    const uint8_t* b = probe.id00;

    // ILI9341 family: 00 00 93 41 or 00 93 41 xx
    for (uint8_t i = 0; i + 1 < 4; ++i) {
        if (a[i] == 0x93 && a[i + 1] == 0x41) return "ILI9341";
        if (a[i] == 0x93 && a[i + 1] == 0x42) return "ILI9342";
    }
    // ST7789: 85 85 52
    for (uint8_t i = 0; i + 1 < 4; ++i) {
        if (a[i] == 0x85 && a[i + 1] == 0x85) return "ST7789";
    }
    // ILI9488 / ILI9486: 00 94 88 / 00 94 86
    for (uint8_t i = 0; i + 1 < 4; ++i) {
        if (a[i] == 0x94 && a[i + 1] == 0x88) return "ILI9488";
        if (a[i] == 0x94 && a[i + 1] == 0x86) return "ILI9486";
    }
    // ST7796: 77 96
    for (uint8_t i = 0; i + 1 < 4; ++i) {
        if (a[i] == 0x77 && a[i + 1] == 0x96) return "ST7796";
    }
    // ILI9331 / ILI9325 / SPFD5408 families return 93 xx on register 0x00.
    for (uint8_t i = 0; i + 1 < 3; ++i) {
        if (b[i] == 0x93 && b[i + 1] == 0x31) return "ILI9331";
        if (b[i] == 0x93 && b[i + 1] == 0x25) return "ILI9325";
        if (b[i] == 0x93 && b[i + 1] == 0x08) return "SPFD5408";
        if (b[i] == 0x54 && b[i + 1] == 0x08) return "SPFD5408";
    }
    for (uint8_t i = 0; i + 1 < 4; ++i) {
        if (probe.idd3[i] == 0x93 && probe.idd3[i + 1] == 0x41) return "ILI9341";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Write-path verification.
//
// The read path demonstrably works (0xD3 returns 93 41), so the question is not
// "is the bus wired" but "do writes reach the controller". Two bands are written
// with different edge dwell times and then read back out of GRAM with 0x2E:
//
//   * red band  (y  0..19) - 1 us edge dwell (fast path)
//   * blue band (y 20..39) - 5 us edge dwell (slow path)
//
// If the readback contains our colours, writes land and the fault is on the
// display/power side. If it contains FF FF..., writes are not landing.
// ---------------------------------------------------------------------------
namespace {

uint16_t gDwell;

void tPause() {
    if (gDwell > 0) delayMicroseconds(gDwell);
}

void tByte(uint8_t value) {
    writeDataBus(value);
    tPause();
    digitalWrite(board_pins::LCD_WR, LOW);
    tPause();
    digitalWrite(board_pins::LCD_WR, HIGH);
    tPause();
}

void tCommand(uint8_t command) {
    digitalWrite(board_pins::LCD_DC, LOW);
    tPause();
    tByte(command);
    digitalWrite(board_pins::LCD_DC, HIGH);
    tPause();
}

void tCommandData(uint8_t command, const uint8_t* data, uint8_t count) {
    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(command);
    for (uint8_t i = 0; i < count; ++i) tByte(data[i]);
    digitalWrite(board_pins::LCD_CS, HIGH);
}

constexpr uint8_t kRdPulse = 0;
constexpr uint8_t kRdHigh = 1;
constexpr uint8_t kCsLow = 0;
constexpr uint8_t kCsHigh = 1;

// Reads `count` bytes from `command`. rdMode selects whether RD is pulsed
// normally, held high (never asserted) or held low. csMode can deselect the
// chip for the whole transfer. Used to characterise the read path.
void readEx(uint8_t command, uint8_t* out, uint8_t count, uint8_t rdMode, uint8_t csMode) {
    dataPinsOutput();
    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(command);
    if (csMode == kCsHigh) digitalWrite(board_pins::LCD_CS, HIGH);
    dataPinsInput();

    for (uint8_t i = 0; i < count; ++i) {
        if (rdMode == kRdPulse) {
            rdWrite(LOW);
        } else {
            rdWrite(rdMode == kRdHigh ? HIGH : LOW);
        }
        tPause();
        out[i] = readDataBus();
        if (rdMode == kRdPulse) {
            rdWrite(HIGH);
        }
        tPause();
    }

    rdWrite(HIGH);
    digitalWrite(board_pins::LCD_CS, HIGH);
    dataPinsOutput();
}

// Adafruit's ILI9341 power-up table, byte for byte. This is the sequence the
// seller-recommended SPFD5408 library runs for this shield.
[[maybe_unused]] void tInit() {
    dataPinsOutput();
    digitalWrite(board_pins::LCD_CS, HIGH);
    digitalWrite(board_pins::LCD_DC, HIGH);
    digitalWrite(board_pins::LCD_WR, HIGH);
    rdWrite(HIGH);

    digitalWrite(board_pins::LCD_RST, LOW);
    delay(5);
    digitalWrite(board_pins::LCD_RST, HIGH);
    delay(120);

    static const uint8_t ef[] = {0x03, 0x80, 0x02};
    static const uint8_t cf[] = {0x00, 0xC1, 0x30};
    static const uint8_t ed[] = {0x64, 0x03, 0x12, 0x81};
    static const uint8_t e8[] = {0x85, 0x00, 0x78};
    static const uint8_t cb[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
    static const uint8_t f7[] = {0x20};
    static const uint8_t ea[] = {0x00, 0x00};
    static const uint8_t c0[] = {0x23};
    static const uint8_t c1[] = {0x10};
    static const uint8_t c5[] = {0x3E, 0x28};
    static const uint8_t c7[] = {0x86};
    static const uint8_t madctl[] = {0x48};
    static const uint8_t pixfmt[] = {0x55};
    static const uint8_t b1[] = {0x00, 0x18};
    static const uint8_t b6[] = {0x08, 0x82, 0x27};
    static const uint8_t f2[] = {0x00};
    static const uint8_t gammaEnable[] = {0x01};
    static const uint8_t gammaPos[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
                                       0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    static const uint8_t gammaNeg[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
                                       0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};

    tCommandData(0xEF, ef, sizeof(ef));
    tCommandData(0xCF, cf, sizeof(cf));
    tCommandData(0xED, ed, sizeof(ed));
    tCommandData(0xE8, e8, sizeof(e8));
    tCommandData(0xCB, cb, sizeof(cb));
    tCommandData(0xF7, f7, sizeof(f7));
    tCommandData(0xEA, ea, sizeof(ea));
    tCommandData(0xC0, c0, sizeof(c0));
    tCommandData(0xC1, c1, sizeof(c1));
    tCommandData(0xC5, c5, sizeof(c5));
    tCommandData(0xC7, c7, sizeof(c7));
    tCommandData(0x36, madctl, sizeof(madctl));
    tCommandData(0x37, ea, 1);
    tCommandData(0x3A, pixfmt, sizeof(pixfmt));
    tCommandData(0xB1, b1, sizeof(b1));
    tCommandData(0xB6, b6, sizeof(b6));
    tCommandData(0xF2, f2, sizeof(f2));
    tCommandData(0x26, gammaEnable, sizeof(gammaEnable));
    tCommandData(0xE0, gammaPos, sizeof(gammaPos));
    tCommandData(0xE1, gammaNeg, sizeof(gammaNeg));

    tCommandData(0x11, nullptr, 0);  // sleep out
    delay(120);
    tCommandData(0x29, nullptr, 0);  // display on
    delay(100);
}

[[maybe_unused]] void tFillBand(uint16_t y0, uint16_t rows, uint16_t color) {
    const uint8_t caset[4] = {0x00, 0x00, 0x00, 0xEF};
    const uint8_t raset[4] = {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
                              static_cast<uint8_t>((y0 + rows - 1) >> 8),
                              static_cast<uint8_t>((y0 + rows - 1) & 0xFF)};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    for (uint32_t i = 0; i < static_cast<uint32_t>(240) * rows; ++i) {
        tByte(hi);
        tByte(lo);
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// Same fill, but with an explicit number of bytes per pixel. The controller may
// be running 18 bits/pixel (3 bytes) even though COLMOD was written as 16 bits,
// in which case a 2-byte stream is consumed misaligned and only changes the
// overall luminance.
void tFillFormat(uint16_t y0, uint16_t rows, uint16_t color, uint8_t bytesPerPixel) {
    const uint8_t caset[4] = {0x00, 0x00, 0x00, 0xEF};
    const uint8_t raset[4] = {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
                              static_cast<uint8_t>((y0 + rows - 1) >> 8),
                              static_cast<uint8_t>((y0 + rows - 1) & 0xFF)};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(color & 0x1F);
    const uint8_t r8 = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    const uint8_t g8 = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    const uint8_t b8 = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    for (uint32_t i = 0; i < static_cast<uint32_t>(240) * rows; ++i) {
        if (bytesPerPixel == 2) {
            tByte(hi);
            tByte(lo);
        } else {
            tByte(r8);
            tByte(g8);
            tByte(b8);
        }
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

void tColorMode(uint8_t colmod) { tCommandData(0x3A, &colmod, 1); }

// ---------------------------------------------------------------------------
// ILI932x / SPFD5408 register model: 16-bit register index, 16-bit value,
// GRAM written through 0x0022 after setting the 0x0050..0x0053 window and the
// 0x0020/0x0021 start address.
// ---------------------------------------------------------------------------
void tReg16(uint16_t address, uint16_t value) {
    digitalWrite(board_pins::LCD_CS, LOW);
    digitalWrite(board_pins::LCD_DC, LOW);
    tByte(static_cast<uint8_t>(address >> 8));
    tByte(static_cast<uint8_t>(address & 0xFF));
    digitalWrite(board_pins::LCD_DC, HIGH);
    tByte(static_cast<uint8_t>(value >> 8));
    tByte(static_cast<uint8_t>(value & 0xFF));
    digitalWrite(board_pins::LCD_CS, HIGH);
}

void tInit932x() {
    dataPinsOutput();
    digitalWrite(board_pins::LCD_CS, HIGH);
    digitalWrite(board_pins::LCD_DC, HIGH);
    digitalWrite(board_pins::LCD_WR, HIGH);
    rdWrite(HIGH);

    digitalWrite(board_pins::LCD_RST, LOW);
    delay(10);
    digitalWrite(board_pins::LCD_RST, HIGH);
    delay(120);

    tReg16(0x0000, 0x0001);  // start oscillator
    delay(50);
    tReg16(0x0001, 0x0100);  // driver output control
    tReg16(0x0002, 0x0700);  // LCD driving wave control
    tReg16(0x0003, 0x1030);  // entry mode: BGR, address increment
    tReg16(0x0004, 0x0000);
    tReg16(0x0008, 0x0202);
    tReg16(0x0009, 0x0000);
    tReg16(0x000A, 0x0000);
    tReg16(0x000C, 0x0000);
    tReg16(0x000D, 0x0000);
    tReg16(0x000F, 0x0000);
    tReg16(0x0010, 0x0000);
    tReg16(0x0011, 0x0007);
    tReg16(0x0012, 0x0000);
    tReg16(0x0013, 0x0000);
    tReg16(0x0010, 0x1690);  // power control 1
    delay(200);
    tReg16(0x0011, 0x0227);  // power control 2
    tReg16(0x0012, 0x001A);  // power control 3
    delay(50);
    tReg16(0x0013, 0x1800);  // power control 4
    tReg16(0x0029, 0x002A);  // power control 7
    delay(50);
    tReg16(0x0030, 0x0000);
    tReg16(0x0031, 0x0000);
    tReg16(0x0032, 0x0000);
    tReg16(0x0035, 0x0206);
    tReg16(0x0036, 0x0808);
    tReg16(0x0037, 0x0007);
    tReg16(0x0038, 0x0201);
    tReg16(0x0039, 0x0000);
    tReg16(0x003C, 0x0000);
    tReg16(0x003D, 0x0000);
    tReg16(0x0050, 0x0000);  // horizontal start
    tReg16(0x0051, 0x00EF);  // horizontal end (239)
    tReg16(0x0052, 0x0000);  // vertical start
    tReg16(0x0053, 0x013F);  // vertical end (319)
    tReg16(0x0060, 0xA700);
    tReg16(0x0061, 0x0003);
    tReg16(0x006A, 0x0000);
    tReg16(0x0090, 0x0010);
    tReg16(0x0092, 0x0000);
    tReg16(0x0093, 0x0003);
    tReg16(0x0095, 0x1100);
    tReg16(0x0097, 0x0000);
    tReg16(0x0098, 0x0000);
    tReg16(0x0007, 0x0133);  // display control 1: display on
    delay(100);
}

void tFill932x(uint16_t color) {
    tReg16(0x0020, 0x0000);  // GRAM horizontal address
    tReg16(0x0021, 0x0000);  // GRAM vertical address

    digitalWrite(board_pins::LCD_CS, LOW);
    digitalWrite(board_pins::LCD_DC, LOW);
    tByte(0x00);
    tByte(0x22);  // index 0x0022 = write GRAM
    digitalWrite(board_pins::LCD_DC, HIGH);

    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    for (uint32_t i = 0; i < 240UL * 320UL; ++i) {
        tByte(hi);
        tByte(lo);
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// One byte per pixel. If the controller is running an 8-bit colour mode
// (RGB332) this produces real colours; if it is in 16-bit mode it produces the
// same grey dither as the 2-byte stream.
void tFill8(uint8_t level) {
    const uint8_t caset[4] = {0x00, 0x00, 0x00, 0xEF};
    const uint8_t raset[4] = {0x00, 0x00, 0x01, 0x3F};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    for (uint32_t i = 0; i < 240UL * 320UL; ++i) tByte(level);
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// Vertical bar written through the ILI932x window registers (0x50..0x53 with
// the 0x20/0x21 start address and 0x22 GRAM index).
void tBar932x(int16_t x, int16_t w, uint16_t color) {
    tReg16(0x0050, static_cast<uint16_t>(x));
    tReg16(0x0051, static_cast<uint16_t>(x + w - 1));
    tReg16(0x0052, 0x0000);
    tReg16(0x0053, 0x013F);
    tReg16(0x0020, static_cast<uint16_t>(x));
    tReg16(0x0021, 0x0000);

    digitalWrite(board_pins::LCD_CS, LOW);
    digitalWrite(board_pins::LCD_DC, LOW);
    tByte(0x00);
    tByte(0x22);
    digitalWrite(board_pins::LCD_DC, HIGH);

    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    for (uint32_t i = 0; i < static_cast<uint32_t>(w) * 320UL; ++i) {
        tByte(hi);
        tByte(lo);
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// Four-colour quadrants, written pixel by pixel into one full-screen window.
// No window-register changes mid-frame, so this isolates the pixel-write path
// and makes colour vs grey impossible to misread.
void tQuadrants9341() {
    const uint8_t caset[4] = {0x00, 0x00, 0x00, 0xEF};
    const uint8_t raset[4] = {0x00, 0x00, 0x01, 0x3F};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    for (int16_t y = 0; y < 320; ++y) {
        for (int16_t x = 0; x < 240; ++x) {
            uint16_t color;
            if (y < 160) {
                color = (x < 120) ? 0xF800 : 0x001F;  // red | blue
            } else {
                color = (x < 120) ? 0x07E0 : 0xFFFF;  // green | white
            }
            tByte(static_cast<uint8_t>(color >> 8));
            tByte(static_cast<uint8_t>(color & 0xFF));
        }
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// Proves the write path as far as firmware can see it:
//  1. Drives LCD_WR low and high and reads the pad back, so a shorted or dead
//     GPIO is caught immediately (a pin stuck at one level reads the same both
//     ways).
//  2. Writes COLMOD and MADCTL and reads them back through RDCOLM/RDMADCTL.
void tPrintHex(const char* label, const uint8_t* data, uint8_t count);
[[maybe_unused]] void tWritePathProbe() {
    pinMode(board_pins::LCD_WR, OUTPUT);
    digitalWrite(board_pins::LCD_WR, LOW);
    delayMicroseconds(50);
    const int lowRead = digitalRead(board_pins::LCD_WR);
    digitalWrite(board_pins::LCD_WR, HIGH);
    delayMicroseconds(50);
    const int highRead = digitalRead(board_pins::LCD_WR);

    uint8_t colBefore[6] = {0};
    readEx(0x0C, colBefore, sizeof(colBefore), kRdPulse, kCsLow);
    const uint8_t c55 = 0x55;
    tCommandData(0x3A, &c55, 1);
    delay(5);
    uint8_t colAfter55[6] = {0};
    readEx(0x0C, colAfter55, sizeof(colAfter55), kRdPulse, kCsLow);

    const uint8_t c66 = 0x66;
    tCommandData(0x3A, &c66, 1);
    delay(5);
    uint8_t colAfter66[6] = {0};
    readEx(0x0C, colAfter66, sizeof(colAfter66), kRdPulse, kCsLow);

    uint8_t madReadBefore[6] = {0};
    readEx(0x0B, madReadBefore, sizeof(madReadBefore), kRdPulse, kCsLow);
    uint8_t mad68 = 0x68;
    tCommandData(0x36, &mad68, 1);
    delay(5);
    uint8_t madReadAfter[6] = {0};
    readEx(0x0B, madReadAfter, sizeof(madReadAfter), kRdPulse, kCsLow);

    // Leave MADCTL in its normal portrait setting.
    uint8_t mad48 = 0x48;
    tCommandData(0x36, &mad48, 1);

    tPrintHex("# WRPROBE colmod before    :", colBefore, sizeof(colBefore));
    tPrintHex("# WRPROBE colmod after 0x55:", colAfter55, sizeof(colAfter55));
    tPrintHex("# WRPROBE colmod after 0x66:", colAfter66, sizeof(colAfter66));
    tPrintHex("# WRPROBE madctl before    :", madReadBefore, sizeof(madReadBefore));
    tPrintHex("# WRPROBE madctl after 0x68:", madReadAfter, sizeof(madReadAfter));

#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# WRPROBE wr-pin=%d drive-low-reads=%d drive-high-reads=%d %s\n", board_pins::LCD_WR,
                  lowRead, highRead,
                  (lowRead == 0 && highRead == 1) ? "(pin OK)" : "(PIN STUCK OR SHORTED)");
#endif
}

// Writes `count` pixels starting at (x,y) through 0x2C.
void tWritePixels(uint16_t x, uint16_t y, const uint8_t* bytes, uint8_t count) {
    const uint8_t caset[4] = {static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
                              static_cast<uint8_t>((x + count - 1) >> 8),
                              static_cast<uint8_t>((x + count - 1) & 0xFF)};
    const uint8_t raset[4] = {static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
                              static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF)};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);
    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    for (uint8_t i = 0; i < count * 2; ++i) tByte(bytes[i]);
    digitalWrite(board_pins::LCD_CS, HIGH);
    delay(10);
}

// Reads GRAM back through 0x2E from a single pixel.
void tReadGram(uint16_t x, uint16_t y, uint8_t* out, uint8_t count) {
    const uint8_t caset[4] = {static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
                              static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF)};
    const uint8_t raset[4] = {static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
                              static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF)};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);
    readEx(0x2E, out, count, kRdPulse, kCsLow);
}

void tPrintHex(const char* label, const uint8_t* data, uint8_t count) {
#if MUDRA_SERIAL_CONSOLE
    Serial.print(label);
    for (uint8_t i = 0; i < count; ++i) Serial.printf(" %02X", data[i]);
    Serial.println();
#else
    (void)label;
    (void)data;
    (void)count;
#endif
}

// With the bus measured error-free, this settles whether pixel writes reach
// GRAM: write a pattern, read it back, overwrite with black, read again. If
// the two readbacks differ, writes land. If both match an untouched pixel,
// writes never reach memory.
[[maybe_unused]] void tGramProbe() {
    static const uint8_t bright[] = {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF};
    static const uint8_t dark[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint8_t afterBright[19] = {0};
    uint8_t afterDark[19] = {0};
    uint8_t untouched[19] = {0};

    tWritePixels(0, 0, bright, 4);
    tReadGram(0, 0, afterBright, sizeof(afterBright));

    tWritePixels(0, 0, dark, 4);
    tReadGram(0, 0, afterDark, sizeof(afterDark));

    tReadGram(100, 200, untouched, sizeof(untouched));

    tPrintHex("# GRAMPROBE after F800/07E0/001F/FFFF:", afterBright, sizeof(afterBright));
    tPrintHex("# GRAMPROBE after 0000 x4            :", afterDark, sizeof(afterDark));
    tPrintHex("# GRAMPROBE untouched (100,200)      :", untouched, sizeof(untouched));

    bool brightDiffers = false;
    for (uint8_t i = 0; i < sizeof(afterBright); ++i) {
        if (afterBright[i] != afterDark[i]) brightDiffers = true;
    }
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# GRAMPROBE verdict: writes %s GRAM\n",
                  brightDiffers ? "REACH" : "DO NOT REACH");
#endif
}

// Reads the ID register 32 times and counts bit errors per data line, so a
// single flaky jumper can be identified instead of blamed generically.
void tBusErrorReport() {
    uint8_t hiErrors[8] = {0};
    uint8_t loErrors[8] = {0};
    uint8_t exact = 0;
    uint8_t dead = 0;

    for (uint8_t i = 0; i < 32; ++i) {
        uint8_t r[4] = {0};
        readEx(0xD3, r, sizeof(r), kRdPulse, kCsLow);
        const uint8_t hi = r[2];
        const uint8_t lo = r[3];

        if (hi == 0x93 && lo == 0x41) {
            ++exact;
        } else if (hi == 0x00 && lo == 0x00) {
            ++dead;
        }

        const uint8_t hiXor = static_cast<uint8_t>(hi ^ 0x93);
        const uint8_t loXor = static_cast<uint8_t>(lo ^ 0x41);
        for (uint8_t b = 0; b < 8; ++b) {
            if (hiXor & (1u << b)) ++hiErrors[b];
            if (loXor & (1u << b)) ++loErrors[b];
        }
    }

#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# BUSERR reads=32 exact=%u dead=%u\n", exact, dead);
    Serial.printf("# BUSERR high-byte errors D0..D7: %u %u %u %u %u %u %u %u\n", hiErrors[0], hiErrors[1],
                  hiErrors[2], hiErrors[3], hiErrors[4], hiErrors[5], hiErrors[6], hiErrors[7]);
    Serial.printf("# BUSERR low-byte  errors D0..D7: %u %u %u %u %u %u %u %u\n", loErrors[0], loErrors[1],
                  loErrors[2], loErrors[3], loErrors[4], loErrors[5], loErrors[6], loErrors[7]);
#endif
}

// Colour check: white strip, then equal red/green/blue thirds, then a black
// strip. All written into one full-screen window.
void tColorCheck() {
    const uint8_t caset[4] = {0x00, 0x00, 0x00, 0xEF};
    const uint8_t raset[4] = {0x00, 0x00, 0x01, 0x3F};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    for (int16_t y = 0; y < 320; ++y) {
        uint16_t color;
        if (y < 40) {
            color = 0xFFFF;  // white strip
        } else if (y >= 280) {
            color = 0x0000;  // black strip
        } else if (y < 120) {
            color = 0xF800;  // red
        } else if (y < 200) {
            color = 0x07E0;  // green
        } else {
            color = 0x001F;  // blue
        }
        for (int16_t x = 0; x < 240; ++x) {
            tByte(static_cast<uint8_t>(color >> 8));
            tByte(static_cast<uint8_t>(color & 0xFF));
        }
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

// Vertical bar of a given 16-bit colour, 2 bytes per pixel.
void tBar(int16_t x, int16_t w, uint16_t color) {
    const uint8_t caset[4] = {0x00, static_cast<uint8_t>(x),
                              static_cast<uint8_t>((x + w - 1) >> 8),
                              static_cast<uint8_t>((x + w - 1) & 0xFF)};
    const uint8_t raset[4] = {0x00, 0x00, 0x01, 0x3F};
    tCommandData(0x2A, caset, 4);
    tCommandData(0x2B, raset, 4);

    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0x2C);
    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    for (uint32_t i = 0; i < static_cast<uint32_t>(w) * 320UL; ++i) {
        tByte(hi);
        tByte(lo);
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
}

}  // namespace

void runGramTest() {
#if MUDRA_SERIAL_CONSOLE
    Serial.println("# LCDTEST start");
#endif

    gDwell = 1;
    tInit();
    delay(300);

    // Is anything actually driving the data bus?
    //
    // Every read so far used plain INPUT pins. Capacitance can hold a pattern on
    // an undriven bus, which looks exactly like a real answer. Repeating the ID
    // read with the data pins as INPUT_PULLDOWN settles it: a real driver
    // overpowers the pull-down and still returns 93 41, while an undriven bus
    // collapses to 00.
    // Read-back diagnostics need a real RD line. When RD is tied high in
    // hardware there is nothing to strobe, so the probe is skipped rather than
    // reporting meaningless values.
    if (kRdAssigned) {
    digitalWrite(board_pins::LCD_CS, HIGH);
    rdWrite(HIGH);
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], INPUT_PULLUP);
    delayMicroseconds(300);
    const uint8_t idlePullUp = readDataBus();
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], INPUT_PULLDOWN);
    delayMicroseconds(300);
    const uint8_t idlePullDown = readDataBus();

    uint8_t idFloat[4] = {0};
    readEx(0xD3, idFloat, sizeof(idFloat), kRdPulse, kCsLow);

    uint8_t idPullDown[4] = {0};
    dataPinsOutput();
    digitalWrite(board_pins::LCD_CS, LOW);
    tCommand(0xD3);
    for (uint8_t i = 0; i < 8; ++i) pinMode(board_pins::LCD_DATA[i], INPUT_PULLDOWN);
    for (uint8_t i = 0; i < 4; ++i) {
        rdWrite(LOW);
        delayMicroseconds(20);
        idPullDown[i] = readDataBus();
        rdWrite(HIGH);
        delayMicroseconds(20);
    }
    digitalWrite(board_pins::LCD_CS, HIGH);
    dataPinsOutput();

#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# DRIVE idle, pull-up bus   = 0x%02X\n", idlePullUp);
    Serial.printf("# DRIVE idle, pull-down bus= 0x%02X\n", idlePullDown);
    Serial.printf("# DRIVE 0xD3 float   : %02X %02X %02X %02X\n", idFloat[0], idFloat[1], idFloat[2], idFloat[3]);
    Serial.printf("# DRIVE 0xD3 pulldown: %02X %02X %02X %02X\n", idPullDown[0], idPullDown[1], idPullDown[2],
                  idPullDown[3]);
    const bool driven = (idPullDown[0] != 0x00 || idPullDown[1] != 0x00 || idPullDown[2] != 0x00 ||
                         idPullDown[3] != 0x00);
    Serial.printf("# DRIVE verdict: %s\n",
                  driven ? "panel DOES drive the bus" : "NOTHING drives the bus - earlier reads were phantom");
#endif
    } else {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# DRIVE skipped: RD tied high, no read-back path");
#endif
    }

    // Paint immediately so a working bus shows up without another flash.
    tColorCheck();

#if MUDRA_SERIAL_CONSOLE
    Serial.println("# LCDTEST holding strips: white/red/green/blue/black");
#endif

    for (;;) delay(1000);
}

}  // namespace display

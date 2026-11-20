#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// ESP32 (classic, DevKit V1 / WROOM-32) -> 2.4" ILI9341 TFT shield.
//
// Every pin is overridable from platformio.ini with -DMUDRA_LCD_*, so the map
// can be re-routed without editing source. The board target is esp32dev.
//
// Reserved on this chip and never used here:
//   GPIO 6-11  : SPI flash
//   GPIO 1, 3  : UART0 (serial console)
//   GPIO 34-39 : input-only, cannot drive a bus line
//   GPIO 0,12,15 : boot strapping; avoided
//
// Shield header labels: A0=RD, A1=WR, A2=RS/DC, A3=CS, A4=RESET, and the
// data bus is on Arduino-labelled pins D8,D9,D2,D3,D4,D5,D6,D7 for LCD
// D0..D7 (confirmed from MCUFRIEND_kbv's own UNO pin map, which drove this
// exact panel successfully).
// ---------------------------------------------------------------------------

namespace board_pins {

#ifndef MUDRA_LCD_RST
#define MUDRA_LCD_RST 13
#endif
#ifndef MUDRA_LCD_CS
#define MUDRA_LCD_CS 4
#endif
#ifndef MUDRA_LCD_DC
#define MUDRA_LCD_DC 2
#endif
#ifndef MUDRA_LCD_WR
#define MUDRA_LCD_WR 5
#endif
#ifndef MUDRA_LCD_RD
#define MUDRA_LCD_RD 16
#endif

#ifndef MUDRA_LCD_D0
#define MUDRA_LCD_D0 33
#endif
#ifndef MUDRA_LCD_D1
#define MUDRA_LCD_D1 32
#endif
#ifndef MUDRA_LCD_D2
#define MUDRA_LCD_D2 27
#endif
#ifndef MUDRA_LCD_D3
#define MUDRA_LCD_D3 26
#endif
#ifndef MUDRA_LCD_D4
#define MUDRA_LCD_D4 25
#endif
#ifndef MUDRA_LCD_D5
#define MUDRA_LCD_D5 23
#endif
#ifndef MUDRA_LCD_D6
#define MUDRA_LCD_D6 22
#endif
#ifndef MUDRA_LCD_D7
#define MUDRA_LCD_D7 19
#endif

constexpr int8_t LCD_RST = MUDRA_LCD_RST;
constexpr int8_t LCD_CS = MUDRA_LCD_CS;   // active low
constexpr int8_t LCD_DC = MUDRA_LCD_DC;   // high = data, low = command
constexpr int8_t LCD_WR = MUDRA_LCD_WR;   // write strobe, active low
constexpr int8_t LCD_RD = MUDRA_LCD_RD;   // read strobe, kept high

// D0..D7 in order.
constexpr int8_t LCD_DATA[8] = {MUDRA_LCD_D0, MUDRA_LCD_D1, MUDRA_LCD_D2, MUDRA_LCD_D3,
                                MUDRA_LCD_D4, MUDRA_LCD_D5, MUDRA_LCD_D6, MUDRA_LCD_D7};

// ---------------------------------------------------------------------------
// Resistive touchscreen - NOT ASSIGNED yet.
//
// On this shield the touch electrodes are shared with LCD lines:
//   XP = shield D8 = LCD D0,  XM = shield A2 = RS/DC,
//   YP = shield A3 = CS,      YM = shield D9 = LCD D1
// so no extra wires are needed, but touch reads must restore the LCD pin modes
// afterwards. Calibration must be done on this board (different ADC reference
// from the UNO).
// ---------------------------------------------------------------------------

#ifndef TOUCH_PIN_XP
#define TOUCH_PIN_XP -1
#endif
#ifndef TOUCH_PIN_XM
#define TOUCH_PIN_XM -1
#endif
#ifndef TOUCH_PIN_YP
#define TOUCH_PIN_YP -1
#endif
#ifndef TOUCH_PIN_YM
#define TOUCH_PIN_YM -1
#endif

constexpr int8_t TOUCH_XP = TOUCH_PIN_XP;
constexpr int8_t TOUCH_XM = TOUCH_PIN_XM;
constexpr int8_t TOUCH_YP = TOUCH_PIN_YP;
constexpr int8_t TOUCH_YM = TOUCH_PIN_YM;

constexpr bool touchPinsAssigned() {
    return TOUCH_XP >= 0 && TOUCH_XM >= 0 && TOUCH_YP >= 0 && TOUCH_YM >= 0;
}

}  // namespace board_pins

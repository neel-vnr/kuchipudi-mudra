#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// LCD controller selection.
//
// RESOLVED: the panel reports readID 0x9341 (240x320) when driven by
// MCUFRIEND_kbv on an Arduino UNO, and renders colours there. The seller's
// "SPFD5408" label is wrong; this is a genuine ILI9341. ILI9341 is therefore the
// correct setting, not a guess.
// Controller-specific code lives only in display_driver.cpp.
// ---------------------------------------------------------------------------

#define MUDRA_LCD_CTRL_ILI9341 1
#define MUDRA_LCD_CTRL_ILI9342 2
#define MUDRA_LCD_CTRL_ST7796 3
#define MUDRA_LCD_CTRL_ILI9488 4
#define MUDRA_LCD_CTRL_SPFD5408 5

#ifndef MUDRA_LCD_CONTROLLER
#define MUDRA_LCD_CONTROLLER MUDRA_LCD_CTRL_ILI9341
#endif

#ifndef MUDRA_LCD_ROTATION
#define MUDRA_LCD_ROTATION 0
#endif

#ifndef MUDRA_LCD_INVERT
#define MUDRA_LCD_INVERT 0
#endif

// Bit-banged controller ID probe at boot (diagnostic; see lcd_probe.cpp).
// Disabled by default because SPFD5408 answers ILI9341 ID bytes and the probe
// sends ILI9341-style commands, which must not run before the 932x init.
#ifndef MUDRA_LCD_PROBE
#define MUDRA_LCD_PROBE 0
#endif

// GRAM write/read-back verification at boot (diagnostic; see lcd_probe.cpp).
#ifndef MUDRA_LCD_GRAM_TEST
#define MUDRA_LCD_GRAM_TEST 0
#endif

#if MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9341
#define MUDRA_LCD_CONTROLLER_NAME "ILI9341"
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9342
#define MUDRA_LCD_CONTROLLER_NAME "ILI9342"
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ST7796
#define MUDRA_LCD_CONTROLLER_NAME "ST7796"
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_ILI9488
#define MUDRA_LCD_CONTROLLER_NAME "ILI9488"
#elif MUDRA_LCD_CONTROLLER == MUDRA_LCD_CTRL_SPFD5408
#define MUDRA_LCD_CONTROLLER_NAME "SPFD5408"
#else
#error "Unsupported MUDRA_LCD_CONTROLLER"
#endif

namespace display_cfg {
constexpr int16_t kPanelWidth = 240;
constexpr int16_t kPanelHeight = 320;
constexpr uint8_t kRotation = MUDRA_LCD_ROTATION;
constexpr bool kInvert = MUDRA_LCD_INVERT != 0;
// Delay after hardware reset before the first command.
constexpr uint32_t kResetDelayMs = 20;
constexpr uint32_t kResetSettleMs = 150;
}  // namespace display_cfg

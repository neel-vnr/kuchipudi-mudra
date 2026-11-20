#pragma once

// ---------------------------------------------------------------------------
// Board / build identity. Everything here is compiled in; nothing is probed.
// ---------------------------------------------------------------------------

#define MUDRA_FW_NAME "MUDRA AI"
#define MUDRA_FW_VERSION "2.1.0"
#define MUDRA_FW_STAGE "embedded-ui"

#define MUDRA_HW_BOARD "ESP32-DevKit-V1"
#define MUDRA_HW_MCU "ESP32 (WROOM-32) / 240MHz / 4MB flash"
#define MUDRA_HW_ROLE "TFT display + touch controller"

// Serial link. USB CDC is disabled in platformio.ini so Serial == UART0.
// 921600 is used so a full 240x180 RGB565 reference (86 KB) crosses the link in
// about two seconds instead of eight.
#define MUDRA_SERIAL_BAUD 921600
// A laptop that has not spoken for this long is considered disconnected.
// The bridge sends a heartbeat every 1.5 s, so this is deliberately generous:
// the indicator stays ON for the whole time the bridge process is alive rather
// than flickering off during quiet stretches.
#define MUDRA_LINK_TIMEOUT_MS 15000
// Largest accepted newline-delimited JSON line.
#define MUDRA_SERIAL_LINE_MAX 640
// Minimum spacing between outbound commands (prevents command flooding).
#define MUDRA_TX_MIN_INTERVAL_MS 40

// A recognised mudra below this confidence (0..100) is shown as LOW CONFIDENCE.
#define MUDRA_LOW_CONFIDENCE 60.0f

// Touch is compiled out until the four wires are physically identified.
#ifndef MUDRA_ENABLE_TOUCH
#define MUDRA_ENABLE_TOUCH 0
#endif

// Boot straight into the colour/geometry test instead of the application.
#ifndef MUDRA_FORCE_LCD_TEST
#define MUDRA_FORCE_LCD_TEST 0
#endif

// Emit human-readable lines prefixed with '#' on the serial link. The Python
// bridge ignores anything that is not JSON or a known command, so these are safe.
#ifndef MUDRA_SERIAL_CONSOLE
#define MUDRA_SERIAL_CONSOLE 1
#endif

// Stream raw touch x/y/z over serial for threshold and axis diagnosis.
#ifndef MUDRA_TOUCH_DEBUG
#define MUDRA_TOUCH_DEBUG 0
#endif

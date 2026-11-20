#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Controller identification over the 8080 8-bit parallel bus.
//
// Arduino_GFX's Arduino_ESP32PAR8 only writes, so this probe bit-bangs the
// read cycle directly on the configured pins. It must run before the panel is
// initialised; afterwards the normal GFX driver takes over the bus again.
//
// A readable bus means the wiring (including the shield's level shifter) is
// sound. If the bus reads back 0x00 or 0xFF the shield is write-only (DIR
// strapped) or the data lines are not reaching the panel.
// ---------------------------------------------------------------------------

namespace display {

struct ControllerProbe {
    bool resetDone = false;
    uint8_t id04[4] = {0, 0, 0, 0};  // Read ID4 (0x04): ILI9341/ST7789/ILI948x
    uint8_t id00[3] = {0, 0, 0};     // Read ID (0x00): ILI932x/SPFD5408/ILI9331
    uint8_t idd3[4] = {0, 0, 0, 0};  // Read ID4 (0xD3) used by some ILI parts
    bool readable = false;
};

// Pulses hardware reset, wakes the panel and reads the ID registers.
ControllerProbe probeController(bool verbose);

// Diagnostic: writes two known colour bands into GRAM and reads them back with
// 0x2E. Proves whether the write path reaches the controller.
void runGramTest();

// Best-effort controller name from the probe bytes. Never returns nullptr.
const char* identifyController(const ControllerProbe& probe);

}  // namespace display

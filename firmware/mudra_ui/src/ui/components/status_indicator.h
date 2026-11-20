#pragma once

#include <stdint.h>

#include "../../display/display_driver.h"
#include "../ui_state.h"

namespace ui {

// A labelled row: status dot, uppercase label on the left, colour-coded value
// on the right. Used by the home dashboard and settings.
void drawStatusRow(display::Driver& d, int16_t x, int16_t y, int16_t w, const char* label, const char* value,
                   uint16_t color);

// Compact three-column indicator used on the detection screen.
void drawMiniStatus(display::Driver& d, const Rect& area, const char* label, const char* value, uint16_t color);

}  // namespace ui

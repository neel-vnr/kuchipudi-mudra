#pragma once

#include <stdint.h>

#include "../../display/display_driver.h"

namespace ui {

// Rounded progress bar. `percent` is clamped to 0..100.
void drawConfidenceBar(display::Driver& d, int16_t x, int16_t y, int16_t w, int16_t h, float percent, uint16_t color);

}  // namespace ui

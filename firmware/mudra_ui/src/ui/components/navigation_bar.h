#pragma once

#include <stdint.h>

#include "../../display/display_driver.h"
#include "../ui_state.h"

namespace ui {

// Bottom navigation bar with four tabs. Not drawn on the detection screen.
void drawNavigationBar(display::Driver& d, Screen active);

// Returns the action for a tap inside the nav bar, or None.
UiAction navigationHit(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight);

}  // namespace ui

#pragma once

#include <stdint.h>

#include "../../display/display_driver.h"
#include "../ui_state.h"

namespace ui {

struct ButtonStyle {
    uint16_t fill;
    uint16_t border;
    uint16_t text;
    bool filled;
};

ButtonStyle primaryButtonStyle();
ButtonStyle dangerButtonStyle();
ButtonStyle secondaryButtonStyle();
ButtonStyle ghostButtonStyle();

// Draws a full-width action button with vertically centred text. The text size
// is automatically reduced when it would not fit.
void drawButton(display::Driver& d, const Rect& r, const char* label, const ButtonStyle& style, bool enabled = true);

// Draws a button with an optional leading glyph (e.g. play triangle).
void drawButtonWithIcon(display::Driver& d, const Rect& r, const char* label, bool playIcon, const ButtonStyle& style);

}  // namespace ui

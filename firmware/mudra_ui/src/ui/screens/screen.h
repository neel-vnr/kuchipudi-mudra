#pragma once

#include "../../application/application_state.h"
#include "../../display/display_driver.h"
#include "../ui_state.h"

namespace ui {

// Base class for the five screens. Screens are stateless with respect to the
// application; they only cache what they last painted so update() can redraw
// the smallest possible region.
class ScreenView {
public:
    virtual ~ScreenView() = default;

    // Full repaint. Must leave the screen visually complete.
    virtual void render(display::Driver& d, const AppState& state) = 0;

    // Incremental repaint; called frequently. Must return immediately when
    // nothing relevant changed.
    virtual void update(display::Driver& d, const AppState& state) = 0;

    // Hit test. Returns the action triggered by a tap, or UiAction::None.
    virtual UiAction tap(int16_t x, int16_t y, const AppState& state) = 0;

    // Invalidate cached state so the next update() repaints everything.
    virtual void invalidate() = 0;
};

}  // namespace ui

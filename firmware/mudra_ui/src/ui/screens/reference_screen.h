#pragma once

#include "screen.h"

namespace ui {

// Full-picture reference for the mudra currently being detected. Opened from
// the detection screen's VIEW MUDRA button; the laptop streams the image on
// request, so this can open before the pixels have arrived.
class ReferenceScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    void drawPicture(display::Driver& d, int16_t originX, int16_t originY);

    bool painted_ = false;
    bool drewImage_ = false;
};

}  // namespace ui

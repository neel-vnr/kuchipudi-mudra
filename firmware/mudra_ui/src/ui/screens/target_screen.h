#pragma once

#include "screen.h"

namespace ui {

// Presents the task target before detection starts:
//   learn     -> reference picture (and the name), to copy
//   challenge -> the mudra name, as the challenge
//   exam      -> the mudra name to perform
//   practice  -> free rehearsal
class TargetScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    void drawPicture(display::Driver& d, int16_t x, int16_t y);
    void drawName(display::Driver& d, const char* name);

    bool painted_ = false;
    bool drewImage_ = false;
};

}  // namespace ui

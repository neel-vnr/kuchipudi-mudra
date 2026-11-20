#pragma once

#include "screen.h"

namespace ui {

// Four task buttons shown after "Start Detection": Learn, Practice, Challenge,
// Exam. Selecting one asks the laptop to launch the matching task and moves on
// to the detection screen.
class ModeSelectScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    bool painted_ = false;
};

}  // namespace ui

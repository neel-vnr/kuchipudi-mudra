#pragma once

#include "screen.h"

namespace ui {

class SettingsScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

    void setTouchAvailable(bool available) { touchAvailable_ = available; }

private:
    void drawConnectionCard(display::Driver& d, const AppState& state);

    bool painted_ = false;
    bool touchAvailable_ = false;
    LinkStatus lastLink_ = LinkStatus::Offline;
    uint32_t lastRxMessages_ = 0;
};

}  // namespace ui

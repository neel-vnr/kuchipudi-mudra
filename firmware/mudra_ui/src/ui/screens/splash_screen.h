#pragma once

#include "screen.h"

namespace ui {

class SplashScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    void drawLinkRow(display::Driver& d, const AppState& state);

    bool painted_ = false;
    uint32_t startMs_ = 0;
    uint16_t progress_ = 0;
    LinkStatus lastLink_ = LinkStatus::Offline;
    bool lastEverLinked_ = false;
};

}  // namespace ui

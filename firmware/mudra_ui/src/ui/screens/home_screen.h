#pragma once

#include "screen.h"

namespace ui {

class HomeScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    void drawPrimary(display::Driver& d, const AppState& state);

    bool painted_ = false;
    AppMode lastMode_ = AppMode::Boot;
    LinkStatus lastLink_ = LinkStatus::Offline;
    CameraStatus lastCamera_ = CameraStatus::Unknown;
    ModelStatus lastModel_ = ModelStatus::Unknown;
};

}  // namespace ui

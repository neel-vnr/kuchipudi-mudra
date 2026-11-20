#pragma once

#include "screen.h"

namespace ui {

class DetectionScreen : public ScreenView {
public:
    void render(display::Driver& d, const AppState& state) override;
    void update(display::Driver& d, const AppState& state) override;
    UiAction tap(int16_t x, int16_t y, const AppState& state) override;
    void invalidate() override;

private:
    void drawHeader(display::Driver& d, const AppState& state);
    void drawTop(display::Driver& d, const AppState& state);
    void drawCandidates(display::Driver& d, const AppState& state);
    void drawSuggestion(display::Driver& d, const AppState& state);
    void drawRow(display::Driver& d, uint8_t index, const char* name, float percent, uint16_t color);
    void drawAction(display::Driver& d, const AppState& state);

    bool candidatesChanged(const AppState& state) const;
    void rememberCandidates(const AppState& state);

    bool painted_ = false;
    char lastName_[40] = "";
    int16_t lastConfidenceTenths_ = -1;
    DetectionStatus lastStatus_ = DetectionStatus::Stopped;
    AppMode lastMode_ = AppMode::Boot;
    LinkStatus lastLink_ = LinkStatus::Offline;
    CameraStatus lastCamera_ = CameraStatus::Unknown;
    ModelStatus lastModel_ = ModelStatus::Unknown;

    // Signature of the ranked list, used to avoid full repaints.
    char lastCand_[AppState::kMaxCandidates][24] = {};
    int16_t lastCandTenths_[AppState::kMaxCandidates] = {-1, -1, -1};
    uint8_t lastCandCount_ = 0;
    char lastSuggestion_[64] = "";
};

}  // namespace ui

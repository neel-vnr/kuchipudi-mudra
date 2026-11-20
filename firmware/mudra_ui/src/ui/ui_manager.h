#pragma once

#include <functional>

#include "../application/application_state.h"
#include "../display/display_driver.h"
#include "../touch/touch_driver.h"
#include "screens/about_screen.h"
#include "screens/detection_screen.h"
#include "screens/home_screen.h"
#include "screens/mode_select_screen.h"
#include "screens/reference_screen.h"
#include "screens/settings_screen.h"
#include "screens/splash_screen.h"
#include "screens/target_screen.h"
#include "ui_state.h"

namespace ui {

// Owns the screen instances, the touch driver and the redraw policy. It does
// not know about the serial protocol: actionable taps are returned to the
// application through a callback.
class UiManager {
public:
    using ActionHandler = std::function<void(UiAction)>;

    UiManager(display::Driver& display, AppState& state);

    void setActionHandler(ActionHandler handler) { handler_ = std::move(handler); }

    void begin(bool displayReady);
    void tick(uint32_t now);

    void show(Screen screen);
    Screen currentScreen() const { return current_; }

    bool displayReady() const { return displayReady_; }
    bool touchAvailable() const { return touch_.available(); }
    bool calibrationActive() const { return calibrationPhase_ != CalibrationPhase::Idle; }

    // Serial fallback: synthesise a tap at the given screen coordinate.
    void simulateTap(int16_t x, int16_t y);
    void startCalibration();
    void cancelCalibration();

    // Force a full repaint of the current screen.
    void repaint() { renderCurrent(); }

    void runDisplayTest(uint16_t holdMs);

private:
    enum class CalibrationPhase : uint8_t { Idle, Target, Result };

    ScreenView* screenFor(Screen id);
    void renderCurrent();
    void handlePoint(const touch::Point& point);
    void handleCalibrationTap(const touch::Point& point);
    void drawCalibrationTarget();
    void drawCalibrationResult();

    display::Driver& display_;
    AppState& state_;
    touch::Driver touch_;

    SplashScreen splash_;
    HomeScreen home_;
    DetectionScreen detection_;
    SettingsScreen settings_;
    AboutScreen about_;
    ModeSelectScreen modeSelect_;
    TargetScreen target_;
    ReferenceScreen reference_;
    ScreenView* screens_[static_cast<size_t>(Screen::Count)] = {};

    Screen current_ = Screen::Splash;
    ActionHandler handler_;
    bool started_ = false;
    bool displayReady_ = false;
    uint32_t splashStartMs_ = 0;

    CalibrationPhase calibrationPhase_ = CalibrationPhase::Idle;
    uint8_t calibrationIndex_ = 0;
    bool calibrationOk_ = false;
    uint32_t calibrationMessageMs_ = 0;

    static constexpr int16_t kCalibrationInset = 30;
    static constexpr int16_t kCalibrationTargetY = 70;
};

}  // namespace ui

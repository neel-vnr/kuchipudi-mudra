#include "ui_manager.h"

#include <stdio.h>

#include "../config/board_config.h"
#include "../config/ui_config.h"
#include "components/button.h"
#include "components/common.h"
#include "theme.h"

namespace ui {
namespace {
constexpr uint32_t kCalibrationResultMs = 1400;
}

UiManager::UiManager(display::Driver& display, AppState& state) : display_(display), state_(state) {
    screens_[static_cast<size_t>(Screen::Splash)] = &splash_;
    screens_[static_cast<size_t>(Screen::Home)] = &home_;
    screens_[static_cast<size_t>(Screen::Detection)] = &detection_;
    screens_[static_cast<size_t>(Screen::Settings)] = &settings_;
    screens_[static_cast<size_t>(Screen::About)] = &about_;
    screens_[static_cast<size_t>(Screen::ModeSelect)] = &modeSelect_;
    screens_[static_cast<size_t>(Screen::Target)] = &target_;
    screens_[static_cast<size_t>(Screen::Reference)] = &reference_;
}

void UiManager::begin(bool displayReady) {
    displayReady_ = displayReady;
    const bool touchOk = touch_.begin();
    settings_.setTouchAvailable(touchOk);

#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# touch: %s\n", touchOk ? "enabled" : "not assigned/disabled");
#endif

    if (displayReady_) {
        show(Screen::Splash);
        splashStartMs_ = millis();
    }
    started_ = true;
}

ScreenView* UiManager::screenFor(Screen id) {
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(Screen::Count)) return nullptr;
    return screens_[index];
}

void UiManager::renderCurrent() {
    ScreenView* screen = screenFor(current_);
    if (screen == nullptr || !displayReady_) return;
    screen->invalidate();
    screen->render(display_, state_);
}

void UiManager::show(Screen screen) {
    if (screen == Screen::Count || screen == current_) {
        if (screen == current_ && displayReady_) {
            ScreenView* s = screenFor(current_);
            if (s != nullptr) s->update(display_, state_);
        }
        return;
    }
    current_ = screen;
    calibrationPhase_ = CalibrationPhase::Idle;

    ScreenView* s = screenFor(current_);
    if (s == nullptr || !displayReady_) return;
    s->invalidate();
    s->render(display_, state_);
}

void UiManager::tick(uint32_t now) {
    if (!started_) return;

    if (displayReady_) {
        if (current_ == Screen::Splash) {
            splash_.update(display_, state_);
            if (now - splashStartMs_ >= ui_cfg::kSplashMs) {
                show(Screen::Home);
            }
        } else if (calibrationPhase_ == CalibrationPhase::Idle) {
            ScreenView* s = screenFor(current_);
            if (s != nullptr) s->update(display_, state_);
        }

        if (calibrationPhase_ == CalibrationPhase::Result) {
            if (now - calibrationMessageMs_ >= kCalibrationResultMs) {
                calibrationPhase_ = CalibrationPhase::Idle;
                show(Screen::Settings);
            }
        }
    }

    touch_.poll(now);
    touch::Point point;
    while (touch_.takeTap(point)) {
        handlePoint(point);
    }
}

void UiManager::handlePoint(const touch::Point& point) {
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# tap raw=%d,%d -> %d,%d phase=%u\n", point.rawX, point.rawY, point.x, point.y,
                  static_cast<unsigned>(calibrationPhase_));
#endif
    if (calibrationPhase_ == CalibrationPhase::Target) {
        handleCalibrationTap(point);
        return;
    }
    if (calibrationPhase_ == CalibrationPhase::Result) return;

    ScreenView* s = screenFor(current_);
    if (s == nullptr) return;
    const UiAction action = s->tap(point.x, point.y, state_);
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# action=%u screen=%u\n", static_cast<unsigned>(action), static_cast<unsigned>(current_));
#endif
    if (action == UiAction::Calibrate) {
        startCalibration();
        return;
    }
    if (action == UiAction::None) return;

    // The application owns navigation, start/stop and any side effects.
    if (handler_) handler_(action);
}

void UiManager::simulateTap(int16_t x, int16_t y) {
    touch::Point point;
    point.x = x;
    point.y = y;
    point.rawX = 0;
    point.rawY = 0;
    handlePoint(point);
}

void UiManager::startCalibration() {
    if (!touch_.available() || !displayReady_) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# calibration rejected: touch unavailable");
#endif
        return;
    }
    calibrationPhase_ = CalibrationPhase::Target;
    calibrationIndex_ = 0;
    calibrationOk_ = false;
    touch_.beginCalibration();
    drawCalibrationTarget();
#if MUDRA_SERIAL_CONSOLE
    Serial.println("# calibration started: tap target 1 of 4");
#endif
}

void UiManager::cancelCalibration() {
    if (calibrationPhase_ == CalibrationPhase::Idle) return;
    touch_.cancelCalibration();
    calibrationPhase_ = CalibrationPhase::Idle;
    show(Screen::Settings);
}

void UiManager::handleCalibrationTap(const touch::Point& point) {
    // Cancel zone: top-right corner.
    if (point.y < 32 && point.x > display_.width() - 80) {
        cancelCalibration();
        return;
    }

    const int16_t x = (calibrationIndex_ % 2 == 0) ? kCalibrationInset
                                                   : static_cast<int16_t>(display_.width() - kCalibrationInset);
    const int16_t y = (calibrationIndex_ < 2) ? kCalibrationTargetY
                                              : static_cast<int16_t>(display_.height() - kCalibrationTargetY);

    const bool ok = touch_.addCalibrationPoint(x, y);
    const bool complete = !touch_.calibrating();
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# cal point %u target=%d,%d raw=%d,%d\n", calibrationIndex_, x, y, point.rawX, point.rawY);
#endif
    if (complete) {
        calibrationOk_ = ok;
        calibrationPhase_ = CalibrationPhase::Result;
        calibrationMessageMs_ = millis();
        drawCalibrationResult();
#if MUDRA_SERIAL_CONSOLE
        Serial.printf("# touch calibration %s\n", ok ? "OK" : "FAILED");
#endif
        return;
    }
    calibrationIndex_++;
    drawCalibrationTarget();
}

void UiManager::drawCalibrationTarget() {
    display_.fill(theme::kBg);
    display_.fillRect(0, 0, display_.width(), 2, theme::kCyan);
    display_.text(14, 12, "TOUCH CALIBRATION", 1, theme::kCyan);
    display_.textRight(display_.width() - 12, 12, "CANCEL", 1, theme::kAmber);

    const int16_t x = (calibrationIndex_ % 2 == 0) ? kCalibrationInset
                                                   : static_cast<int16_t>(display_.width() - kCalibrationInset);
    const int16_t y = (calibrationIndex_ < 2) ? kCalibrationTargetY
                                              : static_cast<int16_t>(display_.height() - kCalibrationTargetY);

    display_.circle(x, y, 16, theme::kCyan);
    display_.circle(x, y, 7, theme::kCyanDim);
    display_.hLine(static_cast<int16_t>(x - 22), y, 44, theme::kCyan);
    display_.vLine(x, static_cast<int16_t>(y - 22), 44, theme::kCyan);
    display_.fillCircle(x, y, 2, theme::kCyan);

    char step[32];
    snprintf(step, sizeof(step), "TAP TARGET %u OF %u", calibrationIndex_ + 1,
             touch_cfg::kCalibrationPoints);
    display_.textCentered(display_.width() / 2, display_.height() - 48, step, 1, theme::kText);
    display_.textCentered(display_.width() / 2, display_.height() - 32, "TOUCH EACH CORNER IN TURN", 1,
                          theme::kTextDim);
}

void UiManager::drawCalibrationResult() {
    display_.fill(theme::kBg);
    display_.fillRect(0, 0, display_.width(), 2, calibrationOk_ ? theme::kGreen : theme::kRed);
    const uint16_t color = calibrationOk_ ? theme::kGreen : theme::kRed;
    display_.textCentered(display_.width() / 2, 140, calibrationOk_ ? "CALIBRATED" : "CALIBRATION FAILED", 2,
                          color);
    display_.textCentered(display_.width() / 2, 172, calibrationOk_ ? "TOUCH INPUT READY" : "CHECK WIRING AND RETRY",
                          1, theme::kTextDim);
}

void UiManager::runDisplayTest(uint16_t holdMs) {
    if (!displayReady_) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# LCDTEST unavailable: display not ready");
#endif
        return;
    }
    display_.runColorTest(holdMs);
    renderCurrent();
}

}  // namespace ui

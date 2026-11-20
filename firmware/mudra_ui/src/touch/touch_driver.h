#pragma once

#include <Arduino.h>

#include "../config/board_config.h"
#include "../config/pin_config.h"
#include "touch_calibration.h"

namespace touch {

struct Point {
    int16_t x = 0;
    int16_t y = 0;
    int16_t rawX = 0;
    int16_t rawY = 0;
};

// 4-wire resistive touchscreen reader with debouncing, tap detection and a
// persistent affine calibration. When the driver is unavailable every method
// is a safe no-op.
class Driver {
public:
    bool begin();
    bool available() const { return available_; }
    bool calibrating() const { return calibrating_; }

    // Samples the panel. Call frequently from loop().
    void poll(uint32_t now);

    // Retrieves one completed tap, if any.
    bool takeTap(Point& out);

    // --- Calibration -------------------------------------------------------
    void beginCalibration();
    void cancelCalibration();
    // Records the raw sample of the most recent tap against a known screen
    // position. Returns true once the affine transform was solved and stored.
    bool addCalibrationPoint(int16_t screenX, int16_t screenY);
    uint8_t calibrationPoints() const { return calibrationCount_; }
    void resetCalibration();

    int16_t rawX() const { return lastRawX_; }
    int16_t rawY() const { return lastRawY_; }

private:
    // One full 4-wire measurement: X, Y and pressure (all 10-bit, 0..1023).
    void measure(int16_t& x, int16_t& y, int16_t& z);
    bool readRaw(int16_t& rawX, int16_t& rawY);
    uint16_t readSense();
    void mapRaw(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) const;
    void applyDefaultCalibration();
    bool loadCalibration();
    bool saveCalibration();
    bool solveAffine();

    bool available_ = false;
    bool calibrating_ = false;

    // Affine model: x = a*rx + b*ry + c ; y = d*rx + e*ry + f
    float a_ = 0.062f;
    float b_ = 0.0f;
    float c_ = -12.4f;
    float d_ = 0.0f;
    float e_ = 0.083f;
    float f_ = -16.6f;

    uint8_t pressCount_ = 0;
    uint8_t releaseCount_ = 0;
    bool pressed_ = false;
    bool moved_ = false;
    uint32_t pressStartMs_ = 0;
    uint32_t lastSampleMs_ = 0;
    int16_t pressRawX_ = 0;
    int16_t pressRawY_ = 0;
    int16_t lastRawX_ = 0;
    int16_t lastRawY_ = 0;

    bool hasPending_ = false;
    Point pending_;

    // Calibration samples: raw reading and the screen point it targeted.
    int16_t calRawX_[touch_cfg::kCalibrationPoints] = {};
    int16_t calRawY_[touch_cfg::kCalibrationPoints] = {};
    int16_t calScrX_[touch_cfg::kCalibrationPoints] = {};
    int16_t calScrY_[touch_cfg::kCalibrationPoints] = {};
    uint8_t calibrationCount_ = 0;
    int16_t lastTapRawX_ = 0;
    int16_t lastTapRawY_ = 0;
};

}  // namespace touch

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Touch calibration defaults and sampling behaviour.
//
// The four wires of the resistive panel are NOT assigned by this project.
// See src/config/pin_config.h. Everything below only takes effect when
// MUDRA_ENABLE_TOUCH=1 and four valid ADC pins are provided.
// ---------------------------------------------------------------------------

namespace touch_cfg {

// Raw ADC expectations for an uncalibrated panel. Only used until the user
// runs the on-device 4-point calibration, which stores an affine transform.
// Values are 10-bit (0..1023) to match the library-compatible measurement in
// touch_driver.cpp, which scales the ESP32's 12-bit ADC down by 4.
constexpr uint16_t kDefaultRawMin = 60;
constexpr uint16_t kDefaultRawMax = 960;

// Contact detection: 10-bit pressure counts. A press must exceed this.
// Idle sits around 150-200, a firm press reads 600+.
constexpr uint16_t kTouchThreshold = 300;

// A short press produces a tap. Longer holds are ignored (prevents accidental
// taps during cleaning or holding the panel).
constexpr uint32_t kPressMinMs = 25;
constexpr uint32_t kPressMaxMs = 1600;
constexpr uint16_t kMoveToleranceRaw = 150;

// 4-point calibration targets, inset from each corner.
constexpr int16_t kCalibrationInset = 22;
constexpr uint8_t kCalibrationPoints = 4;

// NVS storage.
constexpr const char* kNvsNamespace = "mudra_touch";
constexpr const char* kNvsKey = "affine";

}  // namespace touch_cfg

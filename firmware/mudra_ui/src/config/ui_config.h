#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// UI layout and behaviour constants.
// ---------------------------------------------------------------------------

namespace ui_cfg {

constexpr int16_t kMargin = 14;
constexpr int16_t kHeaderHeight = 44;
constexpr int16_t kNavHeight = 40;
constexpr int16_t kMinTouchTarget = 40;
constexpr int16_t kCardRadius = 12;
constexpr int16_t kButtonRadius = 12;

constexpr uint32_t kSplashMs = 1900;
constexpr uint32_t kSplashTickMs = 33;

// Confidence below this (0..100) is shown as LOW CONFIDENCE.
constexpr float kLowConfidence = 60.0f;
// Ignore confidence changes smaller than this to avoid needless redraws.
constexpr float kConfidenceRedrawStep = 0.2f;

// Touch sampling.
constexpr uint32_t kTouchSampleMs = 12;
constexpr uint32_t kTapLockoutMs = 220;
constexpr uint16_t kTouchThreshold = 500;  // 12-bit ADC, 0..4095
constexpr uint8_t kTouchDebounceSamples = 3;

}  // namespace ui_cfg

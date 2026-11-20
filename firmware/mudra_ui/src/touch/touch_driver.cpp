#include "touch_driver.h"

#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "../config/pin_config.h"
#include "../config/ui_config.h"

namespace touch {
namespace {

// On this shield the four touch electrodes share nets with the LCD bus
// (XP=D0, XM=RS/DC, YP=CS, YM=D1). Any pin left in an input mode after a touch
// sample would leave the 8080 bus floating, so every read ends by returning the
// shared pins to their idle output state.
void restoreBus() {
    pinMode(board_pins::TOUCH_XP, OUTPUT);
    digitalWrite(board_pins::TOUCH_XP, HIGH);
    pinMode(board_pins::TOUCH_XM, OUTPUT);
    digitalWrite(board_pins::TOUCH_XM, HIGH);
    pinMode(board_pins::TOUCH_YP, OUTPUT);
    digitalWrite(board_pins::TOUCH_YP, HIGH);
    pinMode(board_pins::TOUCH_YM, OUTPUT);
    digitalWrite(board_pins::TOUCH_YM, HIGH);
}

// 3x3 linear system solved with partial-pivot Gaussian elimination.
bool solve3x3(double m[3][4], double out[3]) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row) {
            if (fabs(m[row][col]) > fabs(m[pivot][col])) pivot = row;
        }
        if (fabs(m[pivot][col]) < 1e-9) return false;
        if (pivot != col) {
            for (int k = 0; k < 4; ++k) {
                const double tmp = m[col][k];
                m[col][k] = m[pivot][k];
                m[pivot][k] = tmp;
            }
        }
        const double diag = m[col][col];
        for (int k = col; k < 4; ++k) m[col][k] /= diag;
        for (int row = 0; row < 3; ++row) {
            if (row == col) continue;
            const double factor = m[row][col];
            if (factor == 0.0) continue;
            for (int k = col; k < 4; ++k) m[row][k] -= factor * m[col][k];
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(m[i][3])) return false;
        out[i] = m[i][3];
    }
    return true;
}

}  // namespace

// Raw uncooked ADC counts from the last measurement, for diagnostics only.
static int gRawXwiper = 0;
static int gRawYwiper = 0;
static int gRawZ1 = 0;
static int gRawZ2 = 0;

bool Driver::begin() {
#if MUDRA_ENABLE_TOUCH && defined(TOUCH_PIN_XP)
    if (!board_pins::touchPinsAssigned()) {
        available_ = false;
        return false;
    }
    pinMode(board_pins::TOUCH_XP, INPUT);
    pinMode(board_pins::TOUCH_XM, INPUT);
    pinMode(board_pins::TOUCH_YP, INPUT);
    pinMode(board_pins::TOUCH_YM, INPUT);
    analogReadResolution(12);
    applyDefaultCalibration();
    loadCalibration();
    restoreBus();  // never leave the shared LCD lines floating
    available_ = true;
    return true;
#else
    available_ = false;
    return false;
#endif
}

void Driver::poll(uint32_t now) {
    if (!available_) return;
    if (now - lastSampleMs_ < ui_cfg::kTouchSampleMs) return;
    lastSampleMs_ = now;

#if MUDRA_TOUCH_DEBUG
    {
        int16_t dx = 0;
        int16_t dy = 0;
        int16_t dz = 0;
        measure(dx, dy, dz);
        static int peakZ = 0;
        static int peakZ1 = 0;
        static int peakZ2 = 0;
        static int peakXw = 0;
        static uint32_t lastPrint = 0;
        if (dz > peakZ) {
            peakZ = dz;
            peakZ1 = gRawZ1;
            peakZ2 = gRawZ2;
            peakXw = gRawXwiper;
        }
        if (millis() - lastPrint >= 1000) {
            lastPrint = millis();
            Serial.printf("# touchpeak z=%d z1=%d z2=%d xw=%d (now z=%d)\n", peakZ, peakZ1, peakZ2, peakXw, dz);
        }
    }
#endif

    const bool down = readSense() > touch_cfg::kTouchThreshold;

    if (down) {
        releaseCount_ = 0;
        if (pressCount_ < 255) pressCount_++;

        if (!pressed_ && pressCount_ >= ui_cfg::kTouchDebounceSamples) {
            pressed_ = true;
            moved_ = false;
            pressStartMs_ = now;
            int16_t rx = 0;
            int16_t ry = 0;
            if (readRaw(rx, ry)) {
                pressRawX_ = rx;
                pressRawY_ = ry;
            }
        } else if (pressed_) {
            int16_t rx = 0;
            int16_t ry = 0;
            if (readRaw(rx, ry)) {
                if (abs(rx - pressRawX_) > touch_cfg::kMoveToleranceRaw ||
                    abs(ry - pressRawY_) > touch_cfg::kMoveToleranceRaw) {
                    moved_ = true;
                }
            }
        }
    } else {
        pressCount_ = 0;
        if (releaseCount_ < 255) releaseCount_++;
        if (pressed_ && releaseCount_ >= ui_cfg::kTouchDebounceSamples) {
            pressed_ = false;
            const uint32_t held = now - pressStartMs_;
            if (!moved_ && held >= touch_cfg::kPressMinMs && held <= touch_cfg::kPressMaxMs) {
                int16_t rx = pressRawX_;
                int16_t ry = pressRawY_;
                lastRawX_ = rx;
                lastRawY_ = ry;
                lastTapRawX_ = rx;
                lastTapRawY_ = ry;

                Point p;
                p.rawX = rx;
                p.rawY = ry;
                mapRaw(rx, ry, p.x, p.y);
                pending_ = p;
                hasPending_ = true;
            }
        }
    }
}

bool Driver::takeTap(Point& out) {
    if (!hasPending_) return false;
    out = pending_;
    hasPending_ = false;
    return true;
}
uint16_t Driver::readSense() {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    measure(x, y, z);
    return static_cast<uint16_t>(z);
}

bool Driver::readRaw(int16_t& rawX, int16_t& rawY) {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    measure(x, y, z);
    rawX = static_cast<int16_t>(constrain(x, 0, 1023));
    rawY = static_cast<int16_t>(constrain(y, 0, 1023));
    return true;
}

// The four-wire sequence used by the library that drives this exact panel on an
// Arduino UNO. A single absolute reading of one pin cannot detect a press here,
// because that pin (YP) is also the panel's CS line and idles pulled high; only
// a differential pressure measurement distinguishes touch from idle.
void Driver::measure(int16_t& x, int16_t& y, int16_t& z) {
    constexpr uint8_t kSamples = 3;

    // --- X: drive the X sheet, read the Y+ wiper -------------------------
    pinMode(board_pins::TOUCH_YP, INPUT);
    pinMode(board_pins::TOUCH_YM, INPUT);
    digitalWrite(board_pins::TOUCH_YP, LOW);
    digitalWrite(board_pins::TOUCH_YM, LOW);
    pinMode(board_pins::TOUCH_XP, OUTPUT);
    digitalWrite(board_pins::TOUCH_XP, HIGH);
    pinMode(board_pins::TOUCH_XM, OUTPUT);
    digitalWrite(board_pins::TOUCH_XM, LOW);
    delayMicroseconds(80);
    int sum = 0;
    for (uint8_t i = 0; i < kSamples; ++i) sum += analogRead(board_pins::TOUCH_YP);
    gRawXwiper = sum / kSamples;
    x = static_cast<int16_t>(1023 - (sum / kSamples) / 4);

    // --- Y: drive the Y sheet, read the X- wiper -------------------------
    pinMode(board_pins::TOUCH_XP, INPUT);
    pinMode(board_pins::TOUCH_XM, INPUT);
    digitalWrite(board_pins::TOUCH_XP, LOW);
    digitalWrite(board_pins::TOUCH_XM, LOW);
    pinMode(board_pins::TOUCH_YP, OUTPUT);
    digitalWrite(board_pins::TOUCH_YP, HIGH);
    pinMode(board_pins::TOUCH_YM, OUTPUT);
    digitalWrite(board_pins::TOUCH_YM, LOW);
    delayMicroseconds(80);
    sum = 0;
    for (uint8_t i = 0; i < kSamples; ++i) sum += analogRead(board_pins::TOUCH_XM);
    gRawYwiper = sum / kSamples;
    y = static_cast<int16_t>(1023 - (sum / kSamples) / 4);

    // --- Pressure: X+ = GND, Y- = 3V3, X- and Y+ high impedance ----------
    pinMode(board_pins::TOUCH_XP, OUTPUT);
    digitalWrite(board_pins::TOUCH_XP, LOW);
    pinMode(board_pins::TOUCH_YM, OUTPUT);
    digitalWrite(board_pins::TOUCH_YM, HIGH);
    digitalWrite(board_pins::TOUCH_XM, LOW);
    pinMode(board_pins::TOUCH_XM, INPUT);
    digitalWrite(board_pins::TOUCH_YP, LOW);
    pinMode(board_pins::TOUCH_YP, INPUT);
    delayMicroseconds(80);
    const int z1 = analogRead(board_pins::TOUCH_XM) / 4;
    const int z2 = analogRead(board_pins::TOUCH_YP) / 4;
    gRawZ1 = analogRead(board_pins::TOUCH_XM);
    gRawZ2 = analogRead(board_pins::TOUCH_YP);
    z = static_cast<int16_t>(constrain(1023 - (z2 - z1), 0, 1023));

    restoreBus();
}

void Driver::mapRaw(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) const {
    const float fx = a_ * rawX + b_ * rawY + c_;
    const float fy = d_ * rawX + e_ * rawY + f_;
    x = static_cast<int16_t>(constrain(lroundf(fx), -8L, 400L));
    y = static_cast<int16_t>(constrain(lroundf(fy), -8L, 400L));
}

void Driver::applyDefaultCalibration() {
    // Measured on this panel from three on-screen calibration targets:
    //   target (30,70)   <- raw (223,774)
    //   target (210,70)  <- raw (821,775)
    //   target (30,250)  <- raw (232,368)
    // The Y axis is inverted (raw Y falls as screen Y rises). These are the
    // factory defaults for this board; running CAL:START replaces them with a
    // 4-point fit stored in NVS.
    a_ = 0.3010f;
    b_ = 0.00667f;
    c_ = -42.3f;
    d_ = 0.000741f;
    e_ = -0.4433f;
    f_ = 412.9f;
}

bool Driver::loadCalibration() {
    Preferences prefs;
    if (!prefs.begin(touch_cfg::kNvsNamespace, true)) return false;
    float values[6] = {};
    const size_t read = prefs.getBytes(touch_cfg::kNvsKey, values, sizeof(values));
    prefs.end();
    if (read != sizeof(values)) return false;
    a_ = values[0];
    b_ = values[1];
    c_ = values[2];
    d_ = values[3];
    e_ = values[4];
    f_ = values[5];
    return true;
}

bool Driver::saveCalibration() {
    Preferences prefs;
    if (!prefs.begin(touch_cfg::kNvsNamespace, false)) return false;
    const float values[6] = {a_, b_, c_, d_, e_, f_};
    const size_t written = prefs.putBytes(touch_cfg::kNvsKey, values, sizeof(values));
    prefs.end();
    return written == sizeof(values);
}

void Driver::resetCalibration() {
    applyDefaultCalibration();
    Preferences prefs;
    if (prefs.begin(touch_cfg::kNvsNamespace, false)) {
        prefs.remove(touch_cfg::kNvsKey);
        prefs.end();
    }
}

void Driver::beginCalibration() {
    calibrating_ = true;
    calibrationCount_ = 0;
    hasPending_ = false;
    memset(calRawX_, 0, sizeof(calRawX_));
    memset(calRawY_, 0, sizeof(calRawY_));
    memset(calScrX_, 0, sizeof(calScrX_));
    memset(calScrY_, 0, sizeof(calScrY_));
}

void Driver::cancelCalibration() {
    calibrating_ = false;
    calibrationCount_ = 0;
}

bool Driver::addCalibrationPoint(int16_t screenX, int16_t screenY) {
    if (!calibrating_) return false;
    if (calibrationCount_ >= touch_cfg::kCalibrationPoints) return false;
    const uint8_t i = calibrationCount_;
    calRawX_[i] = lastTapRawX_;
    calRawY_[i] = lastTapRawY_;
    calScrX_[i] = screenX;
    calScrY_[i] = screenY;
    calibrationCount_++;

    if (calibrationCount_ == touch_cfg::kCalibrationPoints) {
        const bool ok = solveAffine();
        if (ok) saveCalibration();
        calibrating_ = false;
        calibrationCount_ = 0;
        return ok;
    }
    return false;
}

bool Driver::solveAffine() {
    // Least-squares affine fit from the four raw/screen point pairs.
    double ata[3][3] = {};
    double atx[3] = {};
    double aty[3] = {};

    for (uint8_t i = 0; i < touch_cfg::kCalibrationPoints; ++i) {
        const double row[3] = {static_cast<double>(calRawX_[i]), static_cast<double>(calRawY_[i]), 1.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) ata[r][c] += row[r] * row[c];
            atx[r] += row[r] * static_cast<double>(calScrX_[i]);
            aty[r] += row[r] * static_cast<double>(calScrY_[i]);
        }
    }

    double mx[3][4];
    double my[3][4];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            mx[r][c] = ata[r][c];
            my[r][c] = ata[r][c];
        }
        mx[r][3] = atx[r];
        my[r][3] = aty[r];
    }

    double cx[3];
    double cy[3];
    if (!solve3x3(mx, cx) || !solve3x3(my, cy)) return false;

    // Reject implausible fits (degenerate raw data / bad taps).
    const double sx = fabs(cx[0]) + fabs(cx[1]);
    const double sy = fabs(cy[0]) + fabs(cy[1]);
    if (sx < 1e-5 || sy < 1e-5) return false;

    double maxError = 0.0;
    for (uint8_t i = 0; i < touch_cfg::kCalibrationPoints; ++i) {
        const double px = cx[0] * calRawX_[i] + cx[1] * calRawY_[i] + cx[2];
        const double py = cy[0] * calRawX_[i] + cy[1] * calRawY_[i] + cy[2];
        maxError = fmax(maxError, fmax(fabs(px - calScrX_[i]), fabs(py - calScrY_[i])));
    }
    if (maxError > 45.0) return false;

    a_ = static_cast<float>(cx[0]);
    b_ = static_cast<float>(cx[1]);
    c_ = static_cast<float>(cx[2]);
    d_ = static_cast<float>(cy[0]);
    e_ = static_cast<float>(cy[1]);
    f_ = static_cast<float>(cy[2]);
    return true;
}

}  // namespace touch

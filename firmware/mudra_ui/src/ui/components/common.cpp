#include "common.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "../../config/board_config.h"
#include "../../config/ui_config.h"

namespace ui {
namespace {

constexpr int16_t kHeaderTitleX = 16;
constexpr int16_t kHeaderBaseline = 15;

}  // namespace

void paintBackground(display::Driver& d) { d.fill(theme::kBg); }

void header(display::Driver& d, const char* title, bool online, bool showBack) {
    // Top accent: cyan fading into the violet secondary accent.
    d.fillRect(0, 0, d.width(), 2, theme::kCyan);
    d.fillRect(d.width() - 56, 0, 56, 2, theme::kViolet);

    if (showBack) {
        const int16_t x = 20;
        const int16_t y = 22;
        d.line(x + 4, y - 6, x - 2, y, theme::kText);
        d.line(x - 2, y, x + 4, y + 6, theme::kText);
        d.text(kHeaderTitleX + 16, kHeaderBaseline, title, 2, theme::kText);
    } else {
        d.text(kHeaderTitleX, kHeaderBaseline, title, 2, theme::kCyan);
    }

    // Link indicator, right aligned.
    const char* label = online ? "ONLINE" : "OFFLINE";
    const uint16_t color = online ? theme::kGreen : theme::kAmber;
    const int16_t textRight = d.width() - ui_cfg::kMargin;
    d.textRight(textRight, 19, label, 1, color);
    const int16_t dotX = textRight - static_cast<int16_t>(strlen(label)) * 6 - 9;
    statusDot(d, dotX, 22, color);

    d.hLine(0, ui_cfg::kHeaderHeight - 1, d.width(), theme::kStroke);
}

void sectionLabel(display::Driver& d, int16_t x, int16_t y, const char* text) {
    d.text(x, y, text, 1, theme::kTextDim);
}

void card(display::Driver& d, int16_t x, int16_t y, int16_t w, int16_t h) {
    d.fillRoundRect(x, y, w, h, ui_cfg::kCardRadius, theme::kSurface);
    d.roundRect(x, y, w, h, ui_cfg::kCardRadius, theme::kStroke);
}

void statusDot(display::Driver& d, int16_t cx, int16_t cy, uint16_t color) {
    d.fillCircle(cx, cy, 3, color);
}

void statusChip(display::Driver& d, int16_t cx, int16_t y, const char* label, uint16_t color) {
    const uint8_t size = 1;
    const int16_t textW = display::Driver::textWidth(label, size);
    const int16_t chipW = textW + 26;
    const int16_t chipH = 22;
    const int16_t x = cx - chipW / 2;
    d.fillRoundRect(x, y, chipW, chipH, chipH / 2, theme::kSurface2);
    d.roundRect(x, y, chipW, chipH, chipH / 2, color);
    statusDot(d, x + 11, y + chipH / 2, color);
    d.text(x + 19, y + 8, label, size, color);
}

void mandala(display::Driver& d, int16_t cx, int16_t cy, int16_t radius) {
    d.circle(cx, cy, radius, theme::kCyanDim);
    d.circle(cx, cy, radius - 7, theme::kViolet);
    d.circle(cx, cy, radius - 16, theme::kStroke);
    for (int i = 0; i < 12; ++i) {
        const float angle = static_cast<float>(i) * 6.2831853f / 12.0f;
        const int16_t x0 = cx + static_cast<int16_t>(cosf(angle) * (radius + 7));
        const int16_t y0 = cy + static_cast<int16_t>(sinf(angle) * (radius + 7));
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * (radius + 2));
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * (radius + 2));
        d.line(x0, y0, x1, y1, i % 3 == 0 ? theme::kCyan : theme::kStroke);
    }
    d.fillCircle(cx, cy, 2, theme::kCyan);
}

void textSpaced(display::Driver& d, int16_t x, int16_t y, const char* text, uint8_t size, int16_t spacing, uint16_t color) {
    if (text == nullptr) return;
    int16_t cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        char one[2] = {*p, '\0'};
        d.text(cursor, y, one, size, color);
        cursor = static_cast<int16_t>(cursor + 6 * size + spacing);
    }
}

int16_t textSpacedWidth(const char* text, uint8_t size, int16_t spacing) {
    if (text == nullptr) return 0;
    const int16_t n = static_cast<int16_t>(strlen(text));
    if (n == 0) return 0;
    return static_cast<int16_t>(n * (6 * size + spacing) - spacing);
}

uint16_t linkColor(LinkStatus status) {
    return status == LinkStatus::Connected ? theme::kGreen : theme::kRed;
}
const char* linkText(LinkStatus status) { return status == LinkStatus::Connected ? "ONLINE" : "OFFLINE"; }

uint16_t cameraColor(CameraStatus status) {
    switch (status) {
        case CameraStatus::Active: return theme::kGreen;
        case CameraStatus::Inactive: return theme::kRed;
        default: return theme::kAmber;
    }
}
const char* cameraText(CameraStatus status) {
    switch (status) {
        case CameraStatus::Active: return "ACTIVE";
        case CameraStatus::Inactive: return "OFFLINE";
        default: return "UNKNOWN";
    }
}

uint16_t modelColor(ModelStatus status) {
    switch (status) {
        case ModelStatus::Ready: return theme::kGreen;
        case ModelStatus::Offline: return theme::kRed;
        default: return theme::kAmber;
    }
}
const char* modelText(ModelStatus status) {
    switch (status) {
        case ModelStatus::Ready: return "READY";
        case ModelStatus::Offline: return "OFFLINE";
        default: return "UNKNOWN";
    }
}

uint16_t detectionColor(DetectionStatus status) {
    switch (status) {
        case DetectionStatus::Recognized: return theme::kGreen;
        case DetectionStatus::LowConfidence: return theme::kAmber;
        case DetectionStatus::NoHand:
        case DetectionStatus::Waiting: return theme::kAmber;
        case DetectionStatus::CameraDisconnected:
        case DetectionStatus::ModelDisconnected:
        case DetectionStatus::LinkLost: return theme::kRed;
        case DetectionStatus::Stopped: return theme::kTextDim;
    }
    return theme::kTextDim;
}

const char* detectionLabel(DetectionStatus status) {
    switch (status) {
        case DetectionStatus::Recognized: return "RECOGNIZED";
        case DetectionStatus::LowConfidence: return "LOW CONFIDENCE";
        case DetectionStatus::NoHand: return "NO HAND";
        case DetectionStatus::Waiting: return "WAITING FOR INPUT";
        case DetectionStatus::Stopped: return "DETECTION STOPPED";
        case DetectionStatus::CameraDisconnected: return "CAMERA OFFLINE";
        case DetectionStatus::ModelDisconnected: return "MODEL OFFLINE";
        case DetectionStatus::LinkLost: return "LINK LOST";
    }
    return "WAITING";
}

void displayMudraName(const char* raw, char* out, size_t outSize) {
    if (outSize == 0) return;
    out[0] = '\0';
    if (raw == nullptr) return;

    const char* start = raw;
    // Strip a leading "<number>-" class prefix.
    if (isdigit(static_cast<unsigned char>(*start))) {
        const char* p = start;
        while (isdigit(static_cast<unsigned char>(*p))) ++p;
        if (*p == '-') start = p + 1;
    }

    size_t i = 0;
    while (*start != '\0' && i + 1 < outSize) {
        char c = *start++;
        if (c == '_') c = ' ';
        out[i++] = c;
    }
    out[i] = '\0';
}

}  // namespace ui

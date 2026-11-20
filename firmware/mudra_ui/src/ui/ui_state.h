#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// UI-only types. Nothing here knows about the display driver or the protocol.
// ---------------------------------------------------------------------------

namespace ui {

enum class Screen : uint8_t {
    Splash = 0,
    Home,
    Detection,
    Settings,
    About,
    ModeSelect,
    Target,
    Reference,
    Count
};

enum class UiAction : uint8_t {
    None,
    Home,
    Detection,
    Settings,
    About,
    Back,
    Start,
    Stop,
    Calibrate,
    // Session tasks chosen on the mode-select screen.
    ModeLearn,
    ModePractice,
    ModeChallenge,
    ModeExam,
    // Start the prepared session from the target screen.
    BeginSession,
    // Ask the laptop to show the reference for the current mudra.
    ViewMudra,
    // Ask the laptop to open the full studio UI.
    OpenStudio,
};

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    constexpr bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    constexpr int16_t cx() const { return static_cast<int16_t>(x + w / 2); }
    constexpr int16_t cy() const { return static_cast<int16_t>(y + h / 2); }
    constexpr int16_t right() const { return static_cast<int16_t>(x + w); }
    constexpr int16_t bottom() const { return static_cast<int16_t>(y + h); }
};

inline Screen screenFromAction(UiAction action) {
    switch (action) {
        case UiAction::Home: return Screen::Home;
        case UiAction::Detection: return Screen::Detection;
        case UiAction::Settings: return Screen::Settings;
        case UiAction::About: return Screen::About;
        default: return Screen::Count;
    }
}

}  // namespace ui

#include "target_screen.h"

#include <string.h>

#include "../../config/ui_config.h"
#include "../bitmap_store.h"
#include "../components/button.h"
#include "../components/common.h"
#include "../theme.h"

namespace ui {
namespace {

constexpr Rect kBack{0, 0, 56, 44};
constexpr Rect kPicture{0, 74, 240, 180};
constexpr Rect kNameArea{20, 84, 200, 150};
constexpr Rect kAction{14, 266, 212, 44};

const char* modeTitle(const AppState& state) {
    if (strcmp(state.targetMode, "learn") == 0) return "LEARN";
    if (strcmp(state.targetMode, "detect") == 0) return "DETECT";
    if (strcmp(state.targetMode, "challenge") == 0) return "CHALLENGE";
    if (strcmp(state.targetMode, "exam") == 0) return "EXAM";
    return "SESSION";
}

const char* modeInstruction(const AppState& state) {
    if (strcmp(state.targetMode, "learn") == 0) return "COPY THIS MUDRA";
    if (strcmp(state.targetMode, "detect") == 0) return "SHOW ANY MUDRA";
    if (strcmp(state.targetMode, "challenge") == 0) return "CAN YOU MATCH IT?";
    if (strcmp(state.targetMode, "exam") == 0) return "PERFORM THIS MUDRA";
    return "";
}

}  // namespace

void TargetScreen::invalidate() { painted_ = false; }

void TargetScreen::drawPicture(display::Driver& d, int16_t originX, int16_t originY) {
    const int16_t w = bitmap::width();
    const int16_t h = bitmap::height();
    if (w <= 0 || h <= 0) return;
    d.blit565(originX, originY, w, h, bitmap::pixels());
    drewImage_ = true;
}

void TargetScreen::drawName(display::Driver& d, const char* name) {
    d.fillRect(kNameArea.x, kNameArea.y, kNameArea.w, kNameArea.h, theme::kBg);
    const uint8_t size = display::Driver::fitSize(name, 3, kNameArea.w - 8);
    const int16_t y =
        static_cast<int16_t>(kNameArea.y + (kNameArea.h - display::Driver::textHeight(size)) / 2);
    d.textCentered(d.width() / 2, y, name, size, theme::kText);
}

void TargetScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    drewImage_ = false;  // recomputed below; never assume it survived a redraw
    header(d, modeTitle(state), state.link == LinkStatus::Connected, true);

    d.textCentered(d.width() / 2, 52, modeInstruction(state), 1, theme::kCyan);

    if (!state.hasTarget && strcmp(state.targetMode, "detect") != 0) {
        d.textCentered(d.width() / 2, 160, "WAITING FOR LAPTOP", 1, theme::kTextDim);
        d.textCentered(d.width() / 2, 176, "preparing your target ...", 1, theme::kTextDim);
    } else if (!state.hasTarget) {
        // DETECT has no target: the learner can start immediately.
        d.textCentered(d.width() / 2, 140, "SHOW ANY MUDRA", 2, theme::kText);
        d.textCentered(d.width() / 2, 176, "the name appears on screen", 1, theme::kTextDim);
        d.textCentered(d.width() / 2, 192, "as soon as it is recognised", 1, theme::kTextDim);
    } else {
        char name[40];
        displayMudraName(state.targetMudra, name, sizeof(name));

        const bool showPicture = state.targetShowsImage && bitmap::valid();
        if (showPicture) {
            // Name sits above the picture so the learner sees both.
            d.textCentered(d.width() / 2, 62, name, 1, theme::kText);
            drawPicture(d, kPicture.x, kPicture.y);
        } else {
            drawName(d, name);
        }
    }

    drawButtonWithIcon(d, kAction, "START", true, primaryButtonStyle());
    painted_ = true;
}

void TargetScreen::update(display::Driver& d, const AppState& state) {
    // The target and the picture arrive from the laptop after the first paint,
    // so repaint whenever either changes state.
    if (!painted_) {
        render(d, state);
        return;
    }
    const bool pictureNow = state.targetShowsImage && bitmap::valid();
    if (pictureNow != drewImage_ && (pictureNow || drewImage_)) {
        render(d, state);
    }
}

UiAction TargetScreen::tap(int16_t x, int16_t y, const AppState& state) {
    if (kBack.contains(x, y)) return UiAction::Back;
    if (kAction.contains(x, y)) {
        // Practice has no target to wait for.
        if (state.hasTarget || strcmp(state.targetMode, "detect") == 0) {
            return UiAction::BeginSession;
        }
    }
    return UiAction::None;
}

}  // namespace ui

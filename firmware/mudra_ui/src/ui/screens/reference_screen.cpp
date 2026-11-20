#include "reference_screen.h"

#include <string.h>

#include "../bitmap_store.h"
#include "../components/button.h"
#include "../components/common.h"
#include "../theme.h"

namespace ui {
namespace {

constexpr Rect kBack{0, 0, 56, 44};
constexpr Rect kPicture{0, 78, 240, 180};
constexpr Rect kClose{14, 272, 212, 40};

}  // namespace

void ReferenceScreen::invalidate() { painted_ = false; }

void ReferenceScreen::drawPicture(display::Driver& d, int16_t originX, int16_t originY) {
    const int16_t w = bitmap::width();
    const int16_t h = bitmap::height();
    if (w <= 0 || h <= 0) return;
    d.blit565(originX, originY, w, h, bitmap::pixels());
    drewImage_ = true;
}

void ReferenceScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    drewImage_ = false;
    header(d, "MUDRA REFERENCE", state.link == LinkStatus::Connected, true);

    char name[40];
    displayMudraName(state.mudraOrPlaceholder(), name, sizeof(name));
    d.textCentered(d.width() / 2, 56, name, 2, theme::kCyan);

    if (bitmap::valid()) {
        drawPicture(d, kPicture.x, kPicture.y);
    } else {
        d.textCentered(d.width() / 2, 160, "LOADING PICTURE", 1, theme::kTextDim);
        d.textCentered(d.width() / 2, 176, "from the laptop ...", 1, theme::kTextDim);
    }

    drawButton(d, kClose, "BACK", secondaryButtonStyle());
    painted_ = true;
}

void ReferenceScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) {
        render(d, state);
        return;
    }
    // The picture arrives row by row; repaint once it is complete.
    const bool pictureNow = bitmap::valid();
    if (pictureNow && !drewImage_) {
        render(d, state);
    }
}

UiAction ReferenceScreen::tap(int16_t x, int16_t y, const AppState& state) {
    (void)state;
    if (kBack.contains(x, y) || kClose.contains(x, y)) return UiAction::Back;
    return UiAction::None;
}

}  // namespace ui

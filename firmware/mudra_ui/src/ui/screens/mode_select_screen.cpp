#include "mode_select_screen.h"

#include "../../config/ui_config.h"
#include "../components/common.h"
#include "../theme.h"

namespace ui {
namespace {

struct TaskTile {
    Rect rect;
    UiAction action;
    const char* title;
    const char* subtitle;
    uint16_t accent;
};

const Rect kBack{0, 0, 56, 44};
const Rect kStudio{14, 290, 212, 26};

constexpr TaskTile kTiles[] = {
    {{14, 68, 212, 48}, UiAction::ModeLearn, "LEARN", "Guided session, step by step", theme::kCyan},
    {{14, 124, 212, 48}, UiAction::ModePractice, "DETECT", "Identify any mudra you show", theme::kViolet},
    {{14, 180, 212, 48}, UiAction::ModeChallenge, "CHALLENGE", "Random mudra, can you match it?", theme::kAmber},
    {{14, 236, 212, 48}, UiAction::ModeExam, "EXAM", "Scored session, results at the end", theme::kGreen},
};

void drawTile(display::Driver& d, const TaskTile& tile) {
    const Rect& r = tile.rect;
    d.fillRoundRect(r.x, r.y, r.w, r.h, 10, theme::kSurface2);
    d.roundRect(r.x, r.y, r.w, r.h, 10, theme::kStroke);
    d.fillRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 12), 3, 24, tile.accent);

    d.text(static_cast<int16_t>(r.x + 16), static_cast<int16_t>(r.y + 9), tile.title, 2, theme::kText);
    d.text(static_cast<int16_t>(r.x + 16), static_cast<int16_t>(r.y + 31), tile.subtitle, 1,
           theme::kTextDim);

    // Chevron on the right hints that the tile is actionable.
    const int16_t cx = static_cast<int16_t>(r.x + r.w - 18);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    d.line(cx - 3, cy - 5, cx + 2, cy, tile.accent);
    d.line(cx + 2, cy, cx - 3, cy + 5, tile.accent);
}

}  // namespace

void ModeSelectScreen::invalidate() { painted_ = false; }

void ModeSelectScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    header(d, "START A SESSION", state.link == LinkStatus::Connected, true);

    sectionLabel(d, 14, 50, "PICK A TASK");

    for (const TaskTile& tile : kTiles) {
        drawTile(d, tile);
    }

    d.fillRoundRect(kStudio.x, kStudio.y, kStudio.w, kStudio.h, 8, theme::kSurface2);
    d.roundRect(kStudio.x, kStudio.y, kStudio.w, kStudio.h, 8, theme::kStroke);
    d.textCentered(kStudio.cx(), static_cast<int16_t>(kStudio.y + 9), "OPEN LAPTOP STUDIO", 1,
                   theme::kViolet);

    painted_ = true;
}

void ModeSelectScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) render(d, state);
}

UiAction ModeSelectScreen::tap(int16_t x, int16_t y, const AppState& state) {
    (void)state;
    if (kBack.contains(x, y)) return UiAction::Back;
    if (kStudio.contains(x, y)) return UiAction::OpenStudio;
    for (const TaskTile& tile : kTiles) {
        if (tile.rect.contains(x, y)) return tile.action;
    }
    return UiAction::None;
}

}  // namespace ui

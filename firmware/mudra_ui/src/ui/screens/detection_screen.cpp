#include "detection_screen.h"

#include <stdio.h>
#include <string.h>

#include "../../config/ui_config.h"
#include "../components/button.h"
#include "../components/common.h"
#include "../components/confidence_bar.h"
#include "../components/status_indicator.h"
#include "../theme.h"

namespace ui {
namespace {

// 240x320 portrait. Card holds the top-1 result and the two runner-up
// candidates; below it the coaching suggestion and the two actions.
constexpr Rect kCard{14, 46, 212, 190};
constexpr Rect kChip{58, 52, 124, 20};
constexpr Rect kNameArea{22, 76, 196, 30};
constexpr Rect kConfidenceArea{22, 110, 196, 28};
constexpr Rect kBar{26, 142, 188, 8};

constexpr int16_t kDividerY = 156;
constexpr int16_t kRowX = 20;
constexpr int16_t kRowY = 162;
constexpr int16_t kRowStep = 23;
constexpr int16_t kRowNameX = 44;
constexpr int16_t kRowRight = 218;
constexpr int16_t kRowBarW = 174;

constexpr int16_t kSuggestionY = 240;
constexpr Rect kView{14, 270, 128, 40};
constexpr Rect kStop{150, 270, 76, 40};
constexpr Rect kBack{0, 0, 56, 44};

bool confidenceVisible(DetectionStatus status) {
    return status == DetectionStatus::Recognized || status == DetectionStatus::LowConfidence;
}

void formatConfidence(const AppState& state, char* out, size_t outSize) {
    if (confidenceVisible(state.detection)) {
        snprintf(out, outSize, "%.1f%%", static_cast<double>(state.confidence));
    } else {
        snprintf(out, outSize, "--");
    }
}

uint16_t rankColor(uint8_t index, DetectionStatus status) {
    if (index == 0) return detectionColor(status);
    if (index == 1) return theme::kViolet;
    return theme::kTextDim;
}

// In challenge and exam the headline is the mudra the learner was ASKED for,
// not whatever the camera just recognised (that stays in the candidate list).
bool showsAskedMudra(const AppState& state) {
    if (!state.hasTarget) return false;
    return strcmp(state.targetMode, "challenge") == 0 || strcmp(state.targetMode, "exam") == 0;
}

const char* headlineMudra(const AppState& state) {
    return showsAskedMudra(state) ? state.targetMudra : state.mudraOrPlaceholder();
}

// Breaks the hint into at most two lines of 35 characters.
void wrapTwoLines(const char* text, char* line1, size_t s1, char* line2, size_t s2) {
    line1[0] = '\0';
    line2[0] = '\0';
    if (text == nullptr || *text == '\0') return;

    const size_t limit = 35;
    const size_t len = strlen(text);
    if (len <= limit) {
        strncpy(line1, text, s1 - 1);
        line1[s1 - 1] = '\0';
        return;
    }

    size_t split = limit;
    while (split > 0 && text[split] != ' ') --split;
    if (split == 0) split = limit;

    size_t n = (split < s1 - 1) ? split : s1 - 1;
    memcpy(line1, text, n);
    line1[n] = '\0';

    const char* rest = text + split;
    while (*rest == ' ') ++rest;
    strncpy(line2, rest, s2 - 1);
    line2[s2 - 1] = '\0';
}

}  // namespace

void DetectionScreen::invalidate() { painted_ = false; }

void DetectionScreen::drawHeader(display::Driver& d, const AppState& state) {
    d.fillRect(0, 0, d.width(), ui_cfg::kHeaderHeight, theme::kBg);
    header(d, "DETECTION", state.link == LinkStatus::Connected, true);
}

void DetectionScreen::drawTop(display::Driver& d, const AppState& state) {
    d.fillRect(kChip.x, kChip.y, kChip.w, kChip.h, theme::kSurface);
    statusChip(d, kChip.cx(), kChip.y, detectionLabel(state.detection), detectionColor(state.detection));

    char name[40];
    displayMudraName(headlineMudra(state), name, sizeof(name));

    d.fillRect(kNameArea.x, kNameArea.y, kNameArea.w, kNameArea.h, theme::kSurface);
    const uint8_t size = display::Driver::fitSize(name, 3, kNameArea.w - 8);
    const int16_t nameY =
        static_cast<int16_t>(kNameArea.y + (kNameArea.h - display::Driver::textHeight(size)) / 2);
    d.textCentered(120, nameY, name, size, theme::kText);

    d.fillRect(kConfidenceArea.x, kConfidenceArea.y, kConfidenceArea.w, kConfidenceArea.h, theme::kSurface);
    char text[12];
    formatConfidence(state, text, sizeof(text));
    const bool visible = confidenceVisible(state.detection);
    const uint16_t color = visible ? detectionColor(state.detection) : theme::kTextDim;
    const int16_t confY =
        static_cast<int16_t>(kConfidenceArea.y + (kConfidenceArea.h - display::Driver::textHeight(3)) / 2);
    d.textCentered(120, confY, text, 3, color);

    const float percent = visible ? state.confidence : 0.0f;
    drawConfidenceBar(d, kBar.x, kBar.y, kBar.w, kBar.h, percent, visible ? color : theme::kSurface2);
}

void DetectionScreen::drawRow(display::Driver& d, uint8_t index, const char* name, float percent,
                              uint16_t color) {
    const int16_t y = static_cast<int16_t>(kRowY + index * kRowStep);

    char rank[4];
    snprintf(rank, sizeof(rank), "%u", static_cast<unsigned>(index + 1));
    d.fillRoundRect(kRowX, static_cast<int16_t>(y + 2), 16, 16, 5,
                    (index == 0) ? color : theme::kSurface2);
    d.textCentered(static_cast<int16_t>(kRowX + 8), static_cast<int16_t>(y + 6), rank, 1,
                   (index == 0) ? theme::kTextDark : theme::kTextDim);

    d.text(kRowNameX, static_cast<int16_t>(y + 6), name, 1, theme::kText);

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(percent + 0.5f));
    d.textRight(kRowRight, static_cast<int16_t>(y + 6), percentText, 1, color);

    d.fillRect(kRowNameX, static_cast<int16_t>(y + 17), kRowBarW, 3, theme::kSurface2);
    const int16_t filled = static_cast<int16_t>((percent / 100.0f) * static_cast<float>(kRowBarW));
    if (filled > 0) {
        d.fillRect(kRowNameX, static_cast<int16_t>(y + 17), filled, 3, color);
    }
}

void DetectionScreen::drawCandidates(display::Driver& d, const AppState& state) {
    d.fillRect(0, kDividerY, d.width(), 1, theme::kStroke);

    for (uint8_t i = 0; i < AppState::kMaxCandidates; ++i) {
        const int16_t y = static_cast<int16_t>(kRowY + i * kRowStep);
        d.fillRect(kRowX - 6, static_cast<int16_t>(y - 2), 210, kRowStep, theme::kSurface);
    }

    const uint8_t count = (state.candidateCount > AppState::kMaxCandidates)
                              ? AppState::kMaxCandidates
                              : state.candidateCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (state.candidates[i].mudra[0] == '\0') continue;
        drawRow(d, i, state.candidates[i].mudra, state.candidates[i].confidence,
                rankColor(i, state.detection));
    }
}

void DetectionScreen::drawSuggestion(display::Driver& d, const AppState& state) {
    d.fillRect(0, static_cast<int16_t>(kSuggestionY - 4), d.width(), 30, theme::kBg);
    if (state.suggestion[0] == '\0') return;

    char line1[40];
    char line2[40];
    wrapTwoLines(state.suggestion, line1, sizeof(line1), line2, sizeof(line2));

    d.textCentered(d.width() / 2, kSuggestionY, line1, 1, theme::kAmber);
    if (line2[0] != '\0') {
        d.textCentered(d.width() / 2, static_cast<int16_t>(kSuggestionY + 12), line2, 1, theme::kAmber);
    }
}

void DetectionScreen::drawAction(display::Driver& d, const AppState& state) {
    d.fillRect(0, static_cast<int16_t>(kView.y - 8), d.width(), 56, theme::kBg);

    drawButton(d, kView, "VIEW MUDRA", secondaryButtonStyle(), state.mudra[0] != '\0');
    if (state.mode == AppMode::Detecting) {
        drawButton(d, kStop, "STOP", dangerButtonStyle());
    } else {
        drawButton(d, kStop, "START", primaryButtonStyle());
    }
}

bool DetectionScreen::candidatesChanged(const AppState& state) const {
    const uint8_t count = (state.candidateCount > AppState::kMaxCandidates)
                              ? AppState::kMaxCandidates
                              : state.candidateCount;
    if (count != lastCandCount_) return true;
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(state.candidates[i].mudra, lastCand_[i]) != 0) return true;
        const int16_t tenths = static_cast<int16_t>(state.candidates[i].confidence * 10.0f + 0.5f);
        if (tenths != lastCandTenths_[i]) return true;
    }
    return false;
}

void DetectionScreen::rememberCandidates(const AppState& state) {
    const uint8_t count = (state.candidateCount > AppState::kMaxCandidates)
                              ? AppState::kMaxCandidates
                              : state.candidateCount;
    lastCandCount_ = count;
    for (uint8_t i = 0; i < AppState::kMaxCandidates; ++i) {
        if (i < count) {
            strncpy(lastCand_[i], state.candidates[i].mudra, sizeof(lastCand_[i]) - 1);
            lastCand_[i][sizeof(lastCand_[i]) - 1] = '\0';
            lastCandTenths_[i] = static_cast<int16_t>(state.candidates[i].confidence * 10.0f + 0.5f);
        } else {
            lastCand_[i][0] = '\0';
            lastCandTenths_[i] = -1;
        }
    }
}

void DetectionScreen::render(display::Driver& d, const AppState& state) {
    paintBackground(d);
    drawHeader(d, state);

    card(d, kCard.x, kCard.y, kCard.w, kCard.h);
    drawTop(d, state);
    drawCandidates(d, state);
    drawSuggestion(d, state);
    drawAction(d, state);

    painted_ = true;
    char name[40];
    displayMudraName(headlineMudra(state), name, sizeof(name));
    strncpy(lastName_, name, sizeof(lastName_) - 1);
    lastName_[sizeof(lastName_) - 1] = '\0';
    lastConfidenceTenths_ = static_cast<int16_t>(state.confidence * 10.0f + 0.5f);
    lastStatus_ = state.detection;
    lastMode_ = state.mode;
    lastLink_ = state.link;
    lastCamera_ = state.camera;
    lastModel_ = state.model;
    strncpy(lastSuggestion_, state.suggestion, sizeof(lastSuggestion_) - 1);
    lastSuggestion_[sizeof(lastSuggestion_) - 1] = '\0';
    rememberCandidates(state);
}

void DetectionScreen::update(display::Driver& d, const AppState& state) {
    if (!painted_) {
        render(d, state);
        return;
    }

    char name[40];
    displayMudraName(headlineMudra(state), name, sizeof(name));
    const int16_t confidenceTenths = static_cast<int16_t>(state.confidence * 10.0f + 0.5f);
    const bool statusChanged = state.detection != lastStatus_;
    const bool nameChanged = strcmp(name, lastName_) != 0;
    const bool linkChanged = state.link != lastLink_;
    const bool listChanged = candidatesChanged(state);
    const bool suggestionChanged = strcmp(state.suggestion, lastSuggestion_) != 0;
    const bool confidenceChanged =
        statusChanged || nameChanged || abs(confidenceTenths - lastConfidenceTenths_) >= 2;

    if (linkChanged) {
        lastLink_ = state.link;
        drawHeader(d, state);
    }
    if (nameChanged || statusChanged || confidenceChanged) {
        drawTop(d, state);
        if (nameChanged) {
            strncpy(lastName_, name, sizeof(lastName_) - 1);
            lastName_[sizeof(lastName_) - 1] = '\0';
            // The view button enables/disables with the name, so redraw it too.
            drawAction(d, state);
        }
        lastConfidenceTenths_ = confidenceTenths;
        lastStatus_ = state.detection;
    }
    if (listChanged) {
        drawCandidates(d, state);
        rememberCandidates(state);
    }
    if (suggestionChanged) {
        drawSuggestion(d, state);
        strncpy(lastSuggestion_, state.suggestion, sizeof(lastSuggestion_) - 1);
        lastSuggestion_[sizeof(lastSuggestion_) - 1] = '\0';
    }
    lastCamera_ = state.camera;
    lastModel_ = state.model;
    if (state.mode != lastMode_) {
        lastMode_ = state.mode;
        drawAction(d, state);
    }
}

UiAction DetectionScreen::tap(int16_t x, int16_t y, const AppState& state) {
    if (kBack.contains(x, y)) return UiAction::Back;
    if (kView.contains(x, y)) {
        return state.mudra[0] != '\0' ? UiAction::ViewMudra : UiAction::None;
    }
    if (kStop.contains(x, y)) {
        return state.mode == AppMode::Detecting ? UiAction::Stop : UiAction::Start;
    }
    return UiAction::None;
}

}  // namespace ui

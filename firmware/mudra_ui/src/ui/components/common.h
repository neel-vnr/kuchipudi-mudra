#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../application/application_state.h"
#include "../../display/display_driver.h"
#include "../theme.h"
#include "../ui_state.h"

namespace ui {

// Shared drawing helpers and status vocabulary.
void paintBackground(display::Driver& d);
void header(display::Driver& d, const char* title, bool online, bool showBack);
void sectionLabel(display::Driver& d, int16_t x, int16_t y, const char* text);
void card(display::Driver& d, int16_t x, int16_t y, int16_t w, int16_t h);
void statusChip(display::Driver& d, int16_t cx, int16_t y, const char* label, uint16_t color);
void statusDot(display::Driver& d, int16_t cx, int16_t cy, uint16_t color);
void mandala(display::Driver& d, int16_t cx, int16_t cy, int16_t radius);
void textSpaced(display::Driver& d, int16_t x, int16_t y, const char* text, uint8_t size, int16_t spacing, uint16_t color);
int16_t textSpacedWidth(const char* text, uint8_t size, int16_t spacing);

uint16_t linkColor(LinkStatus status);
const char* linkText(LinkStatus status);
uint16_t cameraColor(CameraStatus status);
const char* cameraText(CameraStatus status);
uint16_t modelColor(ModelStatus status);
const char* modelText(ModelStatus status);
uint16_t detectionColor(DetectionStatus status);
const char* detectionLabel(DetectionStatus status);

// "12-Katakamukha_1" -> "Katakamukha 1" (fits the small display).
void displayMudraName(const char* raw, char* out, size_t outSize);

}  // namespace ui

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// MUDRA AI palette (RGB565).
// Dark charcoal base, cyan primary accent, muted violet secondary accent.
// ---------------------------------------------------------------------------

namespace theme {

constexpr uint16_t kBg = 0x0862;        // #0B0F14
constexpr uint16_t kSurface = 0x10C4;   // #121A22
constexpr uint16_t kSurface2 = 0x1926;  // #1A2530
constexpr uint16_t kStroke = 0x21A8;    // #263440
constexpr uint16_t kText = 0xE77E;      // #E6EDF3
constexpr uint16_t kTextDim = 0x7C53;   // #7D8B99
constexpr uint16_t kTextDark = 0x0083;  // #06121A

constexpr uint16_t kCyan = 0x269D;      // #22D3EE
constexpr uint16_t kCyanDim = 0x0BB2;   // #0E7490
constexpr uint16_t kViolet = 0x12FE;    // #8B5CF6
constexpr uint16_t kGreen = 0x2637;     // #22C55E
constexpr uint16_t kAmber = 0xF4E2;     // #F59E0B
constexpr uint16_t kRed = 0xEA28;       // #EF4444

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;

}  // namespace theme

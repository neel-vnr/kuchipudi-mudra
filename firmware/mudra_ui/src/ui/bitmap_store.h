#pragma once

#include <stdint.h>

namespace ui {
namespace bitmap {

// Reference picture sent from the laptop as RGB565, little-endian, in chunks.
// Sized for the worst case the target/reference screens use so nothing is
// allocated at runtime.
constexpr int16_t kMaxWidth = 240;
constexpr int16_t kMaxHeight = 180;
constexpr uint32_t kMaxBytes = static_cast<uint32_t>(kMaxWidth) * kMaxHeight * 2;

// Chunks per row the laptop splits each row into (hex line length limit).
constexpr uint8_t kChunksPerRow = 2;

void begin(int16_t width, int16_t height);
// Appends `hex` (2 chars per byte) at byte offset `offset` inside row `y`.
void bytes(int16_t y, uint16_t offset, const char* hex);
void end();
void clear();

bool valid();
int16_t width();
int16_t height();

// Raw RGB565 colour of a pixel, 0 when out of range.
uint16_t pixel(int16_t x, int16_t y);

// Contiguous RGB565 pixel buffer, for bulk blits.
const uint16_t* pixels();

// Direct access for the binary transfer path: the UART task writes bytes
// straight into the buffer, and markComplete()/takeComplete() tell the UI task
// when a full picture has arrived.
uint8_t* rawBuffer();
uint32_t rawByteCount();
void markComplete();
bool takeComplete();

}  // namespace bitmap
}  // namespace ui

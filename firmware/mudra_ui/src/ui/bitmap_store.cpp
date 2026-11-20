#include "bitmap_store.h"

#include <string.h>

namespace ui {
namespace bitmap {
namespace {

uint8_t gBuffer[kMaxBytes];           // RGB565, two bytes per pixel
int16_t gWidth = 0;
int16_t gHeight = 0;
uint8_t gChunks[kMaxHeight];          // chunks received per row
bool gCompleted = false;              // set when a raw binary picture finished

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t rowBytes() { return static_cast<uint32_t>(gWidth) * 2; }

}  // namespace

void begin(int16_t width, int16_t height) {
    if (width <= 0 || height <= 0 || width > kMaxWidth || height > kMaxHeight) {
        clear();
        return;
    }
    gWidth = width;
    gHeight = height;
    memset(gChunks, 0, sizeof(gChunks));
}

void bytes(int16_t y, uint16_t offset, const char* hex) {
    if (hex == nullptr || gWidth <= 0 || gHeight <= 0) return;
    if (y < 0 || y >= gHeight) return;
    if (offset >= rowBytes()) return;

    uint8_t* dst = gBuffer + static_cast<uint32_t>(y) * rowBytes() + offset;
    const uint32_t room = rowBytes() - offset;
    uint32_t written = 0;
    while (written < room && hex[written * 2] != '\0' && hex[written * 2 + 1] != '\0') {
        const int hi = hexNibble(hex[written * 2]);
        const int lo = hexNibble(hex[written * 2 + 1]);
        if (hi < 0 || lo < 0) break;
        dst[written] = static_cast<uint8_t>((hi << 4) | lo);
        ++written;
    }

    // Count this row as complete once every byte has been filled.
    if (offset + written >= rowBytes() && gChunks[y] < 255) {
        gChunks[y] = 255;
    } else if (gChunks[y] < 254) {
        ++gChunks[y];
    }
}

void end() {
    for (int16_t y = 0; y < gHeight; ++y) {
        if (gChunks[y] != 255) {
            gWidth = 0;
            gHeight = 0;
            return;
        }
    }
}

void clear() {
    gWidth = 0;
    gHeight = 0;
    memset(gChunks, 0, sizeof(gChunks));
}

bool valid() {
    if (gWidth <= 0 || gHeight <= 0) return false;
    for (int16_t y = 0; y < gHeight; ++y) {
        if (gChunks[y] != 255) return false;
    }
    return true;
}

int16_t width() { return gWidth; }
int16_t height() { return gHeight; }

uint16_t pixel(int16_t x, int16_t y) {
    if (!valid() || x < 0 || y < 0 || x >= gWidth || y >= gHeight) return 0;
    const uint8_t* p = gBuffer + (static_cast<uint32_t>(y) * rowBytes()) + static_cast<uint32_t>(x) * 2;
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint8_t* rawBuffer() { return gBuffer; }

const uint16_t* pixels() { return reinterpret_cast<const uint16_t*>(gBuffer); }
uint32_t rawByteCount() {
    return (gWidth > 0 && gHeight > 0) ? static_cast<uint32_t>(gWidth) * gHeight * 2 : 0;
}

void markComplete() {
    if (gHeight > 0) {
        memset(gChunks, 255, gHeight);
        gCompleted = true;
    }
}

bool takeComplete() {
    const bool was = gCompleted;
    gCompleted = false;
    return was;
}

}  // namespace bitmap
}  // namespace ui

// ---------------------------------------------------------------------------
// MUDRA AI - 2.4" TFT shield reference test for Arduino UNO / MEGA
//
// Purpose: prove the panel itself is good, independently of the ESP32-S3.
// The shield plugs straight into the UNO headers, so there are no jumper wires
// and no 3.3 V/5 V level questions. If this sketch draws colours here, the
// panel is healthy and the fault is in the ESP32 wiring (most likely the WR
// line). If it also fails here, the panel is faulty.
//
// Library: "MCUFRIEND_kbv" by David Prentice, install via Library Manager.
//          It auto-detects the controller, so it works whether this is really
//          an SPFD5408/ILI9325 or an ILI9341.
//
// Board: Arduino UNO (or MEGA 2560). Serial Monitor: 9600 baud.
// ---------------------------------------------------------------------------

#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

// Six full-height bars, same pattern the ESP32 firmware tried to draw.
static const uint16_t bars[] = {0x0000, 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x8410};
static const char* const names[] = {"black", "red", "green", "blue", "white", "grey"};

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 3000) {
        // wait for the USB serial port, up to 3 s
    }

    Serial.println(F("=== MUDRA UNO reference test ==="));

    // readID() talks to the panel before any driver is bound to it.
    uint16_t id = tft.readID();
    Serial.print(F("readID   = 0x"));
    Serial.println(id, HEX);

    tft.begin(id);  // selects the matching driver
    tft.setRotation(0);

    Serial.print(F("driver   = 0x"));
    Serial.println(id, HEX);
    Serial.print(F("size     = "));
    Serial.print(tft.width());
    Serial.print(F("x"));
    Serial.println(tft.height());
    Serial.println(F("drawing bars: black red green blue white grey"));

    tft.fillScreen(0x0000);

    const int16_t w = tft.width();
    const int16_t h = tft.height();
    const int16_t barW = w / 6;

    for (uint8_t i = 0; i < 6; ++i) {
        tft.fillRect(i * barW, 0, (i == 5) ? (w - 5 * barW) : barW, h, bars[i]);
    }

    // A frame plus two labels, so a partially working panel is still readable.
    tft.drawRect(0, 0, w, h, 0xF800);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(8, h / 2 - 20);
    tft.print(F("MUDRA"));
    tft.setCursor(8, h / 2 + 4);
    tft.print(F("UNO TEST"));

    Serial.println(F("done - screen should show 6 bars + MUDRA UNO TEST"));
}

void loop() {
    // Cycle a full-screen colour every 2 s so an unattended panel still shows
    // that writes are landing.
    static uint8_t i = 0;
    static uint32_t last = 0;
    if (millis() - last < 2000) return;
    last = millis();
    i = (i + 1) % 6;

    const int16_t w = tft.width();
    const int16_t h = tft.height();
    const int16_t barW = w / 6;
    for (uint8_t b = 0; b < 6; ++b) {
        tft.fillRect(b * barW, 0, (b == 5) ? (w - 5 * barW) : barW, h, bars[(b + i) % 6]);
    }
}

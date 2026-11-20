// ---------------------------------------------------------------------------
// Resistive-touch peak detector for the 2.4" shield on an Arduino UNO.
//
// Prints and latches the strongest press seen since reset, so the result can be
// read at any time afterwards - no need to press during a specific window.
//
// Two candidate pin mappings are polled because the pin diagnosis is ambiguous
// about which analogue pin is X and which is Y:
//   A: XP=D8  XM=A2  YP=A3  YM=D9      (library's derivation)
//   B: XP=A3  XM=D8  YP=D9  YM=A2      (analogue pins swapped)
// ---------------------------------------------------------------------------

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include "TouchScreen_kbv.h"

MCUFRIEND_kbv tft;

TouchScreen_kbv tsA(8, A3, A2, 9, 300);   // XP, YP, XM, YM
TouchScreen_kbv tsB(A3, 9, 8, A2, 300);

const int PINS[4] = {8, 9, A2, A3};
const int PINS_B[4] = {A3, 9, 8, A2};

struct Peak {
    int16_t z;
    int16_t x;
    int16_t y;
    int16_t idleZ;
};
Peak peakA = {0, 0, 0, 0};
Peak peakB = {0, 0, 0, 0};

// Touch electrodes share nets with the LCD bus; return all four to idle.
void restoreBus(const int *pins) {
    for (uint8_t i = 0; i < 4; ++i) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], HIGH);
    }
}

void sample(TouchScreen_kbv &ts, const int *pins, Peak &peak) {
    TSPoint_kbv p = ts.getPoint();
    restoreBus(pins);
    if (p.z > peak.z) {
        peak.z = p.z;
        peak.x = p.x;
        peak.y = p.y;
    }
}

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 3000) {
    }

    tft.begin(0x9341);
    tft.setRotation(0);
    tft.fillScreen(0x0000);
    restoreBus(PINS);
    restoreBus(PINS_B);

    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print(F("PRESS THE"));
    tft.setCursor(10, 40);
    tft.print(F("SCREEN"));
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print(F("then read Serial"));

    Serial.println(F("=== touch peak detector ==="));
    Serial.println(F("A: XP=D8 XM=A2 YP=A3 YM=D9"));
    Serial.println(F("B: XP=A3 XM=D8 YP=D9 YM=A2"));
    Serial.println(F("press the panel, then look at the peak values"));
}

void loop() {
    sample(tsA, PINS, peakA);
    sample(tsB, PINS_B, peakB);

    static uint32_t last = 0;
    if (millis() - last >= 500) {
        last = millis();
        Serial.print(F("curA z="));
        Serial.print(peakA.idleZ);
        Serial.print(F("  PEAK A z="));
        Serial.print(peakA.z);
        Serial.print(F(" x="));
        Serial.print(peakA.x);
        Serial.print(F(" y="));
        Serial.print(peakA.y);
        Serial.print(F("   PEAK B z="));
        Serial.print(peakB.z);
        Serial.print(F(" x="));
        Serial.print(peakB.x);
        Serial.print(F(" y="));
        Serial.println(peakB.y);

        // Show progress on the panel itself. Mapping B saturates at idle, so
        // only mapping A is allowed to report a press.
        if (peakA.z > 250) {
            tft.fillRect(10, 110, 220, 40, 0x07E0);
            tft.setTextColor(0x0000, 0x07E0);
            tft.setTextSize(2);
            tft.setCursor(20, 120);
            tft.print(F("TOUCH OK"));
        }
    }
    delay(15);
}

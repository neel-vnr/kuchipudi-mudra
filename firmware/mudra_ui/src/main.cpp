// ---------------------------------------------------------------------------
// MUDRA AI - ESP32-S3 embedded display controller
//
// The laptop runs the MediaPipe + SVM Bharatanatyam mudra pipeline. This
// firmware owns the 2.4" TFT, the touchscreen, the application state and the
// newline-delimited JSON link. It never runs the ML model.
// ---------------------------------------------------------------------------

#include <Arduino.h>

#include "application/application_state.h"
#include "application/event_manager.h"
#include "communication/serial_manager.h"
#include "config/board_config.h"
#include "config/display_config.h"
#include "config/pin_config.h"
#include "display/display_driver.h"
#include "display/lcd_probe.h"
#include "ui/ui_manager.h"

namespace {

display::Driver gDisplay;
AppState gState;
communication::SerialManager gSerial;
ui::UiManager gUi(gDisplay, gState);
application::EventManager gEvents(gState, gSerial, gUi);

bool initDisplay() {
    if (gDisplay.begin()) return true;
#if MUDRA_SERIAL_CONSOLE
    Serial.println("# retrying LCD initialisation once");
#endif
    delay(200);
    return gDisplay.begin();
}

}  // namespace

void setup() {
    // Serial first so every later diagnostic is visible. Serial is UART0 on the
    // CH343 bridge (USB CDC is disabled in platformio.ini).
    Serial.begin(MUDRA_SERIAL_BAUD);
    Serial.setTimeout(20);
    delay(60);

    gState.begin(millis());

    gUi.setActionHandler([](ui::UiAction action) { gEvents.handleUiAction(action); });
    gSerial.setLineHandler([](const char* line, size_t length) { gEvents.onLine(line, length); });
    gSerial.begin(MUDRA_SERIAL_BAUD);

#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# boot %s v%s\n", MUDRA_FW_NAME, MUDRA_FW_VERSION);
    Serial.printf("# LCD pins: rst=%d cs=%d dc=%d wr=%d rd=%d\n", board_pins::LCD_RST,
                  board_pins::LCD_CS, board_pins::LCD_DC, board_pins::LCD_WR, board_pins::LCD_RD);
    Serial.printf("# LCD data: d0..d7=%d %d %d %d %d %d %d %d\n", board_pins::LCD_DATA[0],
                  board_pins::LCD_DATA[1], board_pins::LCD_DATA[2], board_pins::LCD_DATA[3],
                  board_pins::LCD_DATA[4], board_pins::LCD_DATA[5], board_pins::LCD_DATA[6],
                  board_pins::LCD_DATA[7]);
#endif

    const bool displayReady = initDisplay();

#if MUDRA_LCD_GRAM_TEST
    // Diagnostic build: prove whether GRAM writes reach the controller and show
    // two colour bands before the UI paints over them.
    if (displayReady) display::runGramTest();
#endif

    gUi.begin(displayReady);
    gEvents.begin(displayReady);

#if MUDRA_FORCE_LCD_TEST
    // Build-time hardware verification mode.
    if (displayReady) {
        gDisplay.runColorTest(450);
        gUi.show(ui::Screen::Home);
    }
#endif
}

void loop() {
    const uint32_t now = millis();

    gSerial.poll();
    gState.tick(now);
    gEvents.tick(now);
    gUi.tick(now);

    delay(1);
}

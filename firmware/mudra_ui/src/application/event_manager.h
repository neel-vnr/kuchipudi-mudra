#pragma once

#include <Arduino.h>

#include "../application/application_state.h"
#include "../communication/protocol.h"
#include "../communication/serial_manager.h"
#include "../ui/ui_manager.h"
#include "../ui/ui_state.h"

namespace application {

// Translates the serial protocol into application state/UI changes and turns
// UI actions into commands for the laptop. This is the only place where the
// communication, application and UI layers meet.
class EventManager {
public:
    EventManager(AppState& state, communication::SerialManager& serial, ui::UiManager& ui);

    void begin(bool displayReady);
    void onLine(const char* line, size_t length);
    void handleUiAction(ui::UiAction action);
    void tick(uint32_t now);

private:
    void handleEvent(const protocol::Event& event);
    void handlePlainCommand(const char* line);
    void sendStatusSnapshot();
    void sendDeviceInfo();
    void sendDeviceEvent(const char* status, const char* detail);
    void requestStatus(uint32_t now, bool force);

    AppState& state_;
    communication::SerialManager& serial_;
    ui::UiManager& ui_;

    bool statusRequested_ = false;
    bool lastLinkConnected_ = false;
    uint32_t lastStatusRequestMs_ = 0;
    uint32_t ignoredLines_ = 0;
};

}  // namespace application

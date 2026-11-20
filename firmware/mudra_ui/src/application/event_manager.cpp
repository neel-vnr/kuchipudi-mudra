#include "event_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../communication/json_parser.h"
#include "../config/board_config.h"
#include "../config/display_config.h"
#include "../ui/bitmap_store.h"

namespace application {
namespace {

bool iequals(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a != '\0' && *b != '\0') {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool startsWithIgnoreCase(const char* text, const char* prefix) {
    if (text == nullptr || prefix == nullptr) return false;
    while (*prefix != '\0') {
        if (*text == '\0') return false;
        if (tolower(static_cast<unsigned char>(*text)) != tolower(static_cast<unsigned char>(*prefix))) return false;
        ++text;
        ++prefix;
    }
    return true;
}

void escapeJson(const char* src, char* dst, size_t dstSize) {
    if (dstSize == 0) return;
    size_t out = 0;
    for (const char* p = src; p != nullptr && *p != '\0' && out + 2 < dstSize; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') {
            dst[out++] = '\\';
            dst[out++] = static_cast<char>(c);
        } else if (c < 0x20) {
            dst[out++] = ' ';
        } else {
            dst[out++] = static_cast<char>(c);
        }
    }
    dst[out] = '\0';
}

const char* modeToken(AppMode mode) {
    switch (mode) {
        case AppMode::Detecting: return "detecting";
        case AppMode::Idle: return "idle";
        default: return "boot";
    }
}

const char* cameraToken(CameraStatus status) {
    switch (status) {
        case CameraStatus::Active: return "active";
        case CameraStatus::Inactive: return "inactive";
        default: return "unknown";
    }
}

const char* modelToken(ModelStatus status) {
    switch (status) {
        case ModelStatus::Ready: return "ready";
        case ModelStatus::Offline: return "offline";
        default: return "unknown";
    }
}

}  // namespace

EventManager::EventManager(AppState& state, communication::SerialManager& serial, ui::UiManager& ui)
    : state_(state), serial_(serial), ui_(ui) {}

void EventManager::begin(bool displayReady) {
#if MUDRA_SERIAL_CONSOLE
    Serial.printf("# %s v%s | %s\n", MUDRA_FW_NAME, MUDRA_FW_VERSION, MUDRA_HW_BOARD);
    Serial.printf("# LCD %s %dx%d | touch %s\n", ui_.displayReady() ? "ready" : "FAILED", 240, 320,
                  ui_.touchAvailable() ? "enabled" : "not assigned");
    Serial.println("# nav: NAV:HOME NAV:DETECT NAV:SETTINGS NAV:ABOUT NAV:BACK");
    Serial.println("# cmd: CMD:START CMD:STOP CMD:STATUS TOUCH:x,y LCDTEST PING HELP");
#endif
    sendDeviceInfo();
    if (!displayReady) {
        sendDeviceEvent("display_error", "LCD controller did not initialise");
    }
}

void EventManager::tick(uint32_t now) {
    (void)now;
    // A raw picture arrived from the laptop: repaint the screen showing it.
    if (ui::bitmap::takeComplete()) {
        ui_.repaint();
    }
    const bool connected = state_.link == LinkStatus::Connected;
    if (lastLinkConnected_ && !connected) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# laptop link lost");
#endif
        statusRequested_ = false;
    }
    lastLinkConnected_ = connected;
}

void EventManager::onLine(const char* line, size_t length) {
    if (line == nullptr || length == 0) return;

    protocol::Event event;
    const protocol::ParseResult result = communication::parseEventLine(line, length, event);

    switch (result) {
        case protocol::ParseResult::Ok:
            state_.noteRx(millis());
            handleEvent(event);
            break;
        case protocol::ParseResult::NotJson:
            handlePlainCommand(line);
            break;
        case protocol::ParseResult::Invalid:
        case protocol::ParseResult::TooLong:
            state_.invalidMessages++;
            ignoredLines_++;
            if (MUDRA_SERIAL_CONSOLE && (ignoredLines_ <= 3 || ignoredLines_ % 25 == 0)) {
                Serial.printf("# ignored malformed line (total %lu)\n", static_cast<unsigned long>(ignoredLines_));
            }
            break;
    }
}

void EventManager::handleEvent(const protocol::Event& event) {
    using protocol::EventType;
    const uint32_t now = millis();

    switch (event.type) {
        case EventType::Connection: {
            const bool connected = !iequals(event.status, "disconnected") && !iequals(event.status, "offline") &&
                                   !iequals(event.status, "false");
            state_.applyConnection(connected);
            if (connected) {
                requestStatus(now, false);
            } else {
                statusRequested_ = false;
            }
            break;
        }
        case EventType::Camera: {
            if (iequals(event.status, "active") || iequals(event.status, "on") ||
                iequals(event.status, "connected") || iequals(event.status, "running")) {
                state_.applyCamera(CameraStatus::Active);
            } else if (iequals(event.status, "inactive") || iequals(event.status, "off") ||
                       iequals(event.status, "disconnected") || iequals(event.status, "error")) {
                state_.applyCamera(CameraStatus::Inactive);
            } else {
                state_.applyCamera(CameraStatus::Unknown);
            }
            break;
        }
        case EventType::Model: {
            if (iequals(event.status, "connected") || iequals(event.status, "ready") ||
                iequals(event.status, "loaded") || iequals(event.status, "active")) {
                state_.applyModel(ModelStatus::Ready);
            } else if (iequals(event.status, "disconnected") || iequals(event.status, "offline") ||
                       iequals(event.status, "error") || iequals(event.status, "not_loaded")) {
                state_.applyModel(ModelStatus::Offline);
            } else {
                state_.applyModel(ModelStatus::Unknown);
            }
            break;
        }
        case EventType::Detection: {
            const DetectionStatus status = detectionStatusFromToken(event.status);
            float confidence = 0.0f;
            if (event.hasConfidence) {
                confidence = event.confidence;
            } else if (status == DetectionStatus::Recognized || status == DetectionStatus::LowConfidence) {
                confidence = state_.confidence;
            }
            AppState::Candidate ranked[AppState::kMaxCandidates];
            const uint8_t rankedCount = (event.candidateCount > AppState::kMaxCandidates)
                                            ? AppState::kMaxCandidates
                                            : event.candidateCount;
            for (uint8_t i = 0; i < rankedCount; ++i) {
                strncpy(ranked[i].mudra, event.candidates[i].mudra, sizeof(ranked[i].mudra) - 1);
                ranked[i].mudra[sizeof(ranked[i].mudra) - 1] = '\0';
                ranked[i].confidence = event.candidates[i].confidence;
            }
            strncpy(state_.suggestion, event.suggestion, sizeof(state_.suggestion) - 1);
            state_.suggestion[sizeof(state_.suggestion) - 1] = '\0';
            state_.applyDetection(event.mudra, confidence, status, now, ranked, rankedCount);
            if (state_.mode == AppMode::Detecting &&
                (ui_.currentScreen() == ui::Screen::Home || ui_.currentScreen() == ui::Screen::Splash)) {
                ui_.show(ui::Screen::Detection);
            }
            break;
        }
        case EventType::Target: {
            strncpy(state_.targetMudra, event.mudra, sizeof(state_.targetMudra) - 1);
            state_.targetMudra[sizeof(state_.targetMudra) - 1] = '\0';
            strncpy(state_.targetMode, event.mode, sizeof(state_.targetMode) - 1);
            state_.targetMode[sizeof(state_.targetMode) - 1] = '\0';
            state_.hasTarget = event.mudra[0] != '\0';
            state_.targetShowsImage = event.showImage;
            ui_.show(ui::Screen::Target);
            ui_.repaint();
            break;
        }
        case EventType::Status: {
            if (iequals(event.status, "detecting") || iequals(event.status, "started") ||
                iequals(event.status, "running")) {
                if (state_.mode != AppMode::Detecting) {
                    state_.startLocalDetection(now);
                }
            } else if (iequals(event.status, "stopped") || iequals(event.status, "idle")) {
                state_.stopLocalDetection(now);
            }
            break;
        }
        case EventType::Ui: {
            if (iequals(event.action, "start")) {
                handleUiAction(ui::UiAction::Start);
            } else if (iequals(event.action, "stop")) {
                handleUiAction(ui::UiAction::Stop);
            } else if (iequals(event.action, "home")) {
                handleUiAction(ui::UiAction::Home);
            } else if (iequals(event.action, "detect") || iequals(event.action, "detection")) {
                handleUiAction(ui::UiAction::Detection);
            } else if (iequals(event.action, "settings")) {
                handleUiAction(ui::UiAction::Settings);
            } else if (iequals(event.action, "about")) {
                handleUiAction(ui::UiAction::About);
            } else if (iequals(event.action, "back")) {
                handleUiAction(ui::UiAction::Back);
            }
            break;
        }
        case EventType::Ping:
            serial_.sendLine("{\"event\":\"pong\",\"status\":\"ok\"}");
            break;
        case EventType::Hello:
            sendDeviceInfo();
            break;
        case EventType::Unknown:
        case EventType::None:
            break;
    }
}

void EventManager::handlePlainCommand(const char* line) {
    if (line == nullptr) return;

    char buffer[560];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    // Trim leading/trailing whitespace.
    char* text = buffer;
    while (*text == ' ' || *text == '\t') ++text;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    if (*text == '\0') return;

    bool recognized = true;
    if (iequals(text, "PING")) {
        serial_.sendLine("{\"event\":\"pong\",\"status\":\"ok\"}");
    } else if (iequals(text, "HELP")) {
#if MUDRA_SERIAL_CONSOLE
        Serial.println("# NAV:HOME|DETECT|SETTINGS|ABOUT|BACK CMD:START|STOP|STATUS TOUCH:x,y LCDTEST");
#endif
    } else if (iequals(text, "LCDTEST")) {
        ui_.runDisplayTest(400);
    } else if (iequals(text, "GET_STATUS") || iequals(text, "CMD:STATUS")) {
        sendStatusSnapshot();
    } else if (iequals(text, "CMD:START")) {
        handleUiAction(ui::UiAction::Start);
    } else if (iequals(text, "CMD:STOP")) {
        handleUiAction(ui::UiAction::Stop);
    } else if (iequals(text, "CAL:START")) {
        ui_.startCalibration();
    } else if (iequals(text, "CAL:CANCEL")) {
        ui_.cancelCalibration();
    } else if (startsWithIgnoreCase(text, "IMGBEGIN:")) {
        int w = 0;
        int h = 0;
        if (sscanf(text + 9, "%d,%d", &w, &h) == 2) {
            ui::bitmap::begin(static_cast<int16_t>(w), static_cast<int16_t>(h));
        }
    } else if (startsWithIgnoreCase(text, "IMGROW:")) {
        // IMGROW:<row>:<byteOffset>:<hex...>
        int y = 0;
        int offset = 0;
        const char* body = text + 7;
        const char* firstColon = strchr(body, ':');
        if (firstColon != nullptr && sscanf(body, "%d:%d", &y, &offset) == 2) {
            const char* hex = strchr(firstColon + 1, ':');
            if (hex != nullptr) {
                ui::bitmap::bytes(static_cast<int16_t>(y), static_cast<uint16_t>(offset), hex + 1);
            }
        }
    } else if (iequals(text, "IMGEND")) {
        ui::bitmap::end();
        ui_.repaint();
    } else if (startsWithIgnoreCase(text, "NAV:")) {
        const char* target = text + 4;
        if (iequals(target, "HOME")) {
            handleUiAction(ui::UiAction::Home);
        } else if (iequals(target, "DETECT") || iequals(target, "DETECTION")) {
            handleUiAction(ui::UiAction::Detection);
        } else if (iequals(target, "SETTINGS") || iequals(target, "CONFIG")) {
            handleUiAction(ui::UiAction::Settings);
        } else if (iequals(target, "ABOUT") || iequals(target, "INFO")) {
            handleUiAction(ui::UiAction::About);
        } else if (iequals(target, "BACK")) {
            handleUiAction(ui::UiAction::Back);
        } else {
            recognized = false;
        }
    } else if (startsWithIgnoreCase(text, "TOUCH:")) {
        int x = 0;
        int y = 0;
        if (sscanf(text + 6, "%d,%d", &x, &y) == 2) {
            ui_.simulateTap(static_cast<int16_t>(x), static_cast<int16_t>(y));
        } else {
            recognized = false;
        }
    } else {
        recognized = false;
    }

    if (recognized) {
        state_.noteRx(millis());
    }
}

void EventManager::handleUiAction(ui::UiAction action) {
    const uint32_t now = millis();
    switch (action) {
        case ui::UiAction::Home:
            ui_.show(ui::Screen::Home);
            break;
        case ui::UiAction::Detection:
            ui_.show(ui::Screen::Detection);
            break;
        case ui::UiAction::Settings:
            ui_.show(ui::Screen::Settings);
            requestStatus(now, true);
            break;
        case ui::UiAction::About:
            ui_.show(ui::Screen::About);
            break;
        case ui::UiAction::Back:
            if (ui_.currentScreen() != ui::Screen::Home) {
                ui_.show(ui::Screen::Home);
            }
            break;
        case ui::UiAction::Start:
            // "Start Detection" now asks which of the four tasks to run.
            ui_.show(ui::Screen::ModeSelect);
            break;
        case ui::UiAction::ModeLearn:
        case ui::UiAction::ModePractice:
        case ui::UiAction::ModeChallenge:
        case ui::UiAction::ModeExam: {
            const char* mode = "learn";
            if (action == ui::UiAction::ModePractice) {
                mode = "detect";
            } else if (action == ui::UiAction::ModeChallenge) {
                mode = "challenge";
            } else if (action == ui::UiAction::ModeExam) {
                mode = "exam";
            }
            // Ask the laptop to prepare a target for this task; the camera only
            // starts once the learner confirms with START.
            strncpy(state_.targetMode, mode, sizeof(state_.targetMode) - 1);
            state_.targetMode[sizeof(state_.targetMode) - 1] = '\0';
            state_.targetMudra[0] = '\0';
            state_.hasTarget = false;
            state_.targetShowsImage = false;

            char command[40];
            snprintf(command, sizeof(command), "%s%s", protocol::kCmdStartMode, mode);
            serial_.sendCommand(command);
            ui_.show(ui::Screen::Target);
            break;
        }
        case ui::UiAction::BeginSession:
            serial_.sendCommand(protocol::kCmdBeginSession);
            state_.startLocalDetection(now);
            ui_.show(ui::Screen::Detection);
            break;
        case ui::UiAction::OpenStudio:
            serial_.sendLine("KIOSK:START");
#if MUDRA_SERIAL_CONSOLE
            Serial.println("# asked laptop for the studio UI");
#endif
            break;
        case ui::UiAction::ViewMudra: {
            if (state_.mudra[0] == '\0') break;
            // Show the reference view first, then ask the laptop for the pixels.
            ui::bitmap::clear();
            ui_.show(ui::Screen::Reference);
            char command[64];
            snprintf(command, sizeof(command), "REFERENCE:%s", state_.mudra);
            serial_.sendLine(command);
            break;
        }
        case ui::UiAction::Stop:
            if (state_.mode == AppMode::Detecting) {
                state_.stopLocalDetection(now);
                serial_.sendCommand(protocol::kCmdStopDetection);
                if (ui_.currentScreen() == ui::Screen::Detection) {
                    ui_.show(ui::Screen::Home);
                }
            }
            break;
        case ui::UiAction::Calibrate:
            ui_.startCalibration();
            break;
        case ui::UiAction::None:
            break;
    }
}

void EventManager::requestStatus(uint32_t now, bool force) {
    if (!force && statusRequested_) return;
    if (lastStatusRequestMs_ != 0 && (now - lastStatusRequestMs_) < 750) return;
    lastStatusRequestMs_ = now;
    statusRequested_ = true;
    serial_.sendCommand(protocol::kCmdGetStatus);
}

void EventManager::sendStatusSnapshot() {
    char mudra[48];
    escapeJson(state_.mudra, mudra, sizeof(mudra));
    char line[260];
    snprintf(line, sizeof(line),
             "{\"event\":\"device_status\",\"mode\":\"%s\",\"link\":\"%s\",\"camera\":\"%s\","
             "\"model\":\"%s\",\"detection\":\"%s\",\"mudra\":\"%s\",\"confidence\":%.1f,"
             "\"display\":\"%s\",\"touch\":%s}",
             modeToken(state_.mode), state_.link == LinkStatus::Connected ? "connected" : "offline",
             cameraToken(state_.camera), modelToken(state_.model), detectionStatusToken(state_.detection),
             mudra, static_cast<double>(state_.confidence), MUDRA_LCD_CONTROLLER_NAME,
             ui_.touchAvailable() ? "true" : "false");
    serial_.sendLine(line);
}

void EventManager::sendDeviceInfo() {
    char line[240];
    snprintf(line, sizeof(line),
             "{\"event\":\"device\",\"status\":\"ready\",\"fw\":\"%s\",\"board\":\"%s\","
             "\"display\":\"%s\",\"width\":240,\"height\":320,\"touch\":%s}",
             MUDRA_FW_VERSION, MUDRA_HW_BOARD, MUDRA_LCD_CONTROLLER_NAME,
             ui_.touchAvailable() ? "true" : "false");
    serial_.sendLine(line);
}

void EventManager::sendDeviceEvent(const char* status, const char* detail) {
    char safe[64];
    escapeJson(detail, safe, sizeof(safe));
    char line[160];
    snprintf(line, sizeof(line), "{\"event\":\"device\",\"status\":\"%s\",\"detail\":\"%s\"}", status, safe);
    serial_.sendLine(line);
}

}  // namespace application

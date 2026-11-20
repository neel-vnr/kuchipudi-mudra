#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Serial protocol constants.
//
// Laptop -> device: newline-delimited JSON objects.
// Device -> laptop: newline-delimited plain-text commands (START_DETECTION,
// STOP_DETECTION, GET_STATUS) plus occasional JSON device events.
// ---------------------------------------------------------------------------

namespace protocol {

constexpr const char* kCmdStartDetection = "START_DETECTION";
constexpr const char* kCmdStopDetection = "STOP_DETECTION";
constexpr const char* kCmdGetStatus = "GET_STATUS";
// Followed by the task name: START_MODE:learn | practice | challenge | exam
constexpr const char* kCmdStartMode = "START_MODE:";
// Sent when the learner confirms the target and wants the camera to start.
constexpr const char* kCmdBeginSession = "BEGIN_SESSION";

constexpr const char* kEventConnection = "connection";
constexpr const char* kEventCamera = "camera";
constexpr const char* kEventModel = "model";
constexpr const char* kEventDetection = "detection";
constexpr const char* kEventStatus = "status";
constexpr const char* kEventUi = "ui";
constexpr const char* kEventPing = "ping";
constexpr const char* kEventHello = "hello";
constexpr const char* kEventDevice = "device";
constexpr const char* kEventTarget = "target";

enum class EventType : uint8_t {
    None,
    Connection,
    Camera,
    Model,
    Detection,
    Status,
    Ui,
    Ping,
    Hello,
    Target,
    Unknown,
};

enum class ParseResult : uint8_t {
    Ok,
    NotJson,
    Invalid,
    TooLong,
};

// Ranked alternatives shown on the detection screen.
constexpr uint8_t kMaxCandidates = 3;
constexpr size_t kCandidateNameMax = 24;

struct Candidate {
    char mudra[kCandidateNameMax] = "";
    float confidence = 0.0f;
};

struct Event {
    EventType type = EventType::None;
    char status[24] = "";
    char mudra[40] = "";
    char action[24] = "";
    char mode[12] = "";
    bool showImage = false;
    char suggestion[64] = "";
    float confidence = 0.0f;
    bool hasConfidence = false;
    // Ranked alternatives, best first. candidateCount == 1 is the normal case
    // when the laptop sends only a top-1 result.
    Candidate candidates[kMaxCandidates];
    uint8_t candidateCount = 0;
};

// Maximum accepted mudra/status/action lengths (mirror of the struct fields).
constexpr size_t kStatusMax = sizeof(Event::status);
constexpr size_t kMudraMax = sizeof(Event::mudra);
constexpr size_t kActionMax = sizeof(Event::action);

}  // namespace protocol

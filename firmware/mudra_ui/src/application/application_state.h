#pragma once

#include <Arduino.h>

#include "../config/board_config.h"

// ---------------------------------------------------------------------------
// Application state owned by the device. The laptop drives the content of
// this state through the serial protocol; the UI only reads it.
// ---------------------------------------------------------------------------

enum class LinkStatus : uint8_t { Offline, Connected };
enum class CameraStatus : uint8_t { Unknown, Active, Inactive };
enum class ModelStatus : uint8_t { Unknown, Ready, Offline };
enum class AppMode : uint8_t { Boot, Idle, Detecting };

enum class DetectionStatus : uint8_t {
    Stopped,
    Waiting,
    NoHand,
    Recognized,
    LowConfidence,
    CameraDisconnected,
    ModelDisconnected,
    LinkLost,
};

struct AppState {
    AppMode mode = AppMode::Boot;
    LinkStatus link = LinkStatus::Offline;
    CameraStatus camera = CameraStatus::Unknown;
    ModelStatus model = ModelStatus::Unknown;
    DetectionStatus detection = DetectionStatus::Stopped;

    char mudra[40] = "";
    float confidence = 0.0f;

    // Target prepared by the laptop for the chosen task: the mudra to perform
    // (exam/challenge) or the reference to copy (learn).
    char targetMudra[40] = "";
    char targetMode[12] = "";
    bool hasTarget = false;
    bool targetShowsImage = false;

    // One-line coaching hint from the laptop, e.g. "Adjust your hand".
    char suggestion[64] = "";

    // Ranked alternatives, best first. candidates[0] mirrors mudra/confidence.
    static constexpr uint8_t kMaxCandidates = 3;
    struct Candidate {
        char mudra[24] = "";
        float confidence = 0.0f;
    };
    Candidate candidates[kMaxCandidates];
    uint8_t candidateCount = 0;

    uint32_t bootMs = 0;
    uint32_t lastRxMs = 0;
    uint32_t lastDetectionMs = 0;
    uint32_t rxMessages = 0;
    uint32_t invalidMessages = 0;
    bool everLinked = false;

    void begin(uint32_t now);
    void tick(uint32_t now);

    void noteRx(uint32_t now);
    void applyConnection(bool connected);
    void applyCamera(CameraStatus status);
    void applyModel(ModelStatus status);
    void applyDetection(const char* name, float confidencePercent, DetectionStatus status, uint32_t now);
    // Overload that also carries ranked alternatives from the laptop.
    void applyDetection(const char* name, float confidencePercent, DetectionStatus status, uint32_t now,
                        const Candidate* ranked, uint8_t rankedCount);

    // Local (device-initiated) start/stop. The laptop confirms via events.
    void startLocalDetection(uint32_t now);
    void stopLocalDetection(uint32_t now);

    bool linkAlive(uint32_t now) const;
    const char* mudraOrPlaceholder() const;
    bool hasMudra() const { return mudra[0] != '\0'; }
};

DetectionStatus detectionStatusFromToken(const char* token);
const char* detectionStatusToken(DetectionStatus status);

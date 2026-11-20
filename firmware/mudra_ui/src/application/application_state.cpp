#include "application_state.h"

#include <string.h>

namespace {

bool tokenIs(const char* value, const char* expected) {
    return value != nullptr && strcmp(value, expected) == 0;
}

void copyTrimmed(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;
    size_t n = strlen(src);
    if (n >= dstSize) n = dstSize - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

DetectionStatus detectionStatusFromToken(const char* token) {
    if (token == nullptr) return DetectionStatus::Waiting;
    if (tokenIs(token, "recognized") || tokenIs(token, "detected") || tokenIs(token, "ok")) {
        return DetectionStatus::Recognized;
    }
    if (tokenIs(token, "low_confidence") || tokenIs(token, "low")) return DetectionStatus::LowConfidence;
    if (tokenIs(token, "no_hand") || tokenIs(token, "no-hand")) return DetectionStatus::NoHand;
    if (tokenIs(token, "waiting")) return DetectionStatus::Waiting;
    if (tokenIs(token, "stopped") || tokenIs(token, "idle")) return DetectionStatus::Stopped;
    if (tokenIs(token, "camera_disconnected") || tokenIs(token, "camera_offline")) {
        return DetectionStatus::CameraDisconnected;
    }
    if (tokenIs(token, "model_disconnected") || tokenIs(token, "model_offline")) {
        return DetectionStatus::ModelDisconnected;
    }
    if (tokenIs(token, "link_lost")) return DetectionStatus::LinkLost;
    return DetectionStatus::Waiting;
}

const char* detectionStatusToken(DetectionStatus status) {
    switch (status) {
        case DetectionStatus::Recognized: return "recognized";
        case DetectionStatus::LowConfidence: return "low_confidence";
        case DetectionStatus::NoHand: return "no_hand";
        case DetectionStatus::Waiting: return "waiting";
        case DetectionStatus::Stopped: return "stopped";
        case DetectionStatus::CameraDisconnected: return "camera_disconnected";
        case DetectionStatus::ModelDisconnected: return "model_disconnected";
        case DetectionStatus::LinkLost: return "link_lost";
    }
    return "waiting";
}

void AppState::begin(uint32_t now) {
    bootMs = now;
    mode = AppMode::Idle;
    link = LinkStatus::Offline;
    camera = CameraStatus::Unknown;
    model = ModelStatus::Unknown;
    detection = DetectionStatus::Stopped;
    mudra[0] = '\0';
    confidence = 0.0f;
}

void AppState::tick(uint32_t now) {
    if (link == LinkStatus::Connected && !linkAlive(now)) {
        link = LinkStatus::Offline;
        camera = CameraStatus::Unknown;
        model = ModelStatus::Unknown;
        if (mode == AppMode::Detecting) {
            detection = DetectionStatus::LinkLost;
        }
    }
}

void AppState::noteRx(uint32_t now) {
    lastRxMs = now;
    rxMessages++;
    if (link != LinkStatus::Connected) {
        link = LinkStatus::Connected;
        everLinked = true;
    }
}

void AppState::applyConnection(bool connected) {
    link = connected ? LinkStatus::Connected : LinkStatus::Offline;
    if (connected) {
        everLinked = true;
    } else {
        camera = CameraStatus::Unknown;
        model = ModelStatus::Unknown;
        if (mode == AppMode::Detecting) detection = DetectionStatus::LinkLost;
    }
}

void AppState::applyCamera(CameraStatus status) {
    camera = status;
    if (status == CameraStatus::Inactive && mode == AppMode::Detecting) {
        detection = DetectionStatus::CameraDisconnected;
    }
}

void AppState::applyModel(ModelStatus status) {
    model = status;
    if (status == ModelStatus::Offline && mode == AppMode::Detecting) {
        detection = DetectionStatus::ModelDisconnected;
    }
}

void AppState::applyDetection(const char* name, float confidencePercent, DetectionStatus status, uint32_t now) {
    applyDetection(name, confidencePercent, status, now, nullptr, 0);
}

void AppState::applyDetection(const char* name, float confidencePercent, DetectionStatus status, uint32_t now,
                              const Candidate* ranked, uint8_t rankedCount) {
    lastDetectionMs = now;
    confidence = confidencePercent;
    if (name != nullptr && *name != '\0') {
        copyTrimmed(mudra, sizeof(mudra), name);
    } else if (status != DetectionStatus::Recognized && status != DetectionStatus::LowConfidence) {
        mudra[0] = '\0';
    }

    if (status == DetectionStatus::Stopped) {
        mode = AppMode::Idle;
    } else {
        mode = AppMode::Detecting;
    }

    if ((status == DetectionStatus::Recognized) && confidence > 0.0f &&
        confidence < MUDRA_LOW_CONFIDENCE) {
        status = DetectionStatus::LowConfidence;
    }
    detection = status;

    // Ranked alternatives, best first. Nothing is shown when there is no
    // recognised result, so stale rows can never linger on screen.
    candidateCount = 0;
    if (detection == DetectionStatus::Recognized || detection == DetectionStatus::LowConfidence) {
        if (ranked != nullptr && rankedCount > 0) {
            const uint8_t n = (rankedCount > kMaxCandidates) ? kMaxCandidates : rankedCount;
            for (uint8_t i = 0; i < n; ++i) {
                copyTrimmed(candidates[i].mudra, sizeof(candidates[i].mudra), ranked[i].mudra);
                candidates[i].confidence = ranked[i].confidence;
            }
            candidateCount = n;
        } else if (mudra[0] != '\0') {
            copyTrimmed(candidates[0].mudra, sizeof(candidates[0].mudra), mudra);
            candidates[0].confidence = confidence;
            candidateCount = 1;
        }
    } else {
        for (uint8_t i = 0; i < kMaxCandidates; ++i) {
            candidates[i].mudra[0] = '\0';
            candidates[i].confidence = 0.0f;
        }
    }
}

void AppState::startLocalDetection(uint32_t now) {
    mode = AppMode::Detecting;
    detection = DetectionStatus::Waiting;
    mudra[0] = '\0';
    confidence = 0.0f;
    lastDetectionMs = now;
}

void AppState::stopLocalDetection(uint32_t now) {
    mode = AppMode::Idle;
    detection = DetectionStatus::Stopped;
    confidence = 0.0f;
    mudra[0] = '\0';
    lastDetectionMs = now;
}

bool AppState::linkAlive(uint32_t now) const {
    return lastRxMs != 0 && (now - lastRxMs) <= MUDRA_LINK_TIMEOUT_MS;
}

const char* AppState::mudraOrPlaceholder() const {
    if (mudra[0] != '\0') return mudra;
    switch (detection) {
        case DetectionStatus::NoHand: return "NO HAND";
        case DetectionStatus::Waiting: return "WAITING";
        case DetectionStatus::Stopped: return "STOPPED";
        case DetectionStatus::CameraDisconnected: return "CAMERA OFFLINE";
        case DetectionStatus::ModelDisconnected: return "MODEL OFFLINE";
        case DetectionStatus::LinkLost: return "LINK LOST";
        case DetectionStatus::Recognized:
        case DetectionStatus::LowConfidence: return "UNKNOWN";
    }
    return "WAITING";
}

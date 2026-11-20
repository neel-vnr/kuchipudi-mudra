#include "json_parser.h"

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

namespace communication {
namespace {

void copyBounded(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;
    size_t n = strlen(src);
    if (n >= dstSize) n = dstSize - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool isJsonStart(const char* line, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return c == '{';
    }
    return false;
}

}  // namespace

protocol::ParseResult parseEventLine(const char* line, size_t length, protocol::Event& out) {
    if (line == nullptr) return protocol::ParseResult::Invalid;
    if (length == 0) return protocol::ParseResult::Invalid;
    if (!isJsonStart(line, length)) return protocol::ParseResult::NotJson;

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, line, length);
    if (error) {
        return (error == DeserializationError::NoMemory) ? protocol::ParseResult::TooLong
                                                         : protocol::ParseResult::Invalid;
    }

    JsonObject obj = doc.as<JsonObject>();
    if (obj.isNull()) return protocol::ParseResult::Invalid;

    const char* event = obj["event"] | "";
    if (*event == '\0') return protocol::ParseResult::Invalid;

    protocol::Event parsed;
    parsed.type = protocol::EventType::Unknown;

    if (strcmp(event, protocol::kEventConnection) == 0) {
        parsed.type = protocol::EventType::Connection;
        const char* status = obj["status"] | "connected";
        copyBounded(parsed.status, sizeof(parsed.status), status);
    } else if (strcmp(event, protocol::kEventCamera) == 0) {
        parsed.type = protocol::EventType::Camera;
        const char* status = obj["status"] | "active";
        copyBounded(parsed.status, sizeof(parsed.status), status);
    } else if (strcmp(event, protocol::kEventModel) == 0) {
        parsed.type = protocol::EventType::Model;
        const char* status = obj["status"] | "connected";
        copyBounded(parsed.status, sizeof(parsed.status), status);
    } else if (strcmp(event, protocol::kEventDetection) == 0) {
        parsed.type = protocol::EventType::Detection;
        const char* mudra = obj["mudra"] | "";
        const char* status = obj["status"] | "";
        copyBounded(parsed.mudra, sizeof(parsed.mudra), mudra);
        copyBounded(parsed.status, sizeof(parsed.status), status);

        if (obj["confidence"].is<float>() || obj["confidence"].is<int>() || obj["confidence"].is<double>()) {            float confidence = obj["confidence"].as<float>();
            if (isnan(confidence) || isinf(confidence)) {
                parsed.confidence = 0.0f;
            } else {
                // Accept either 0..1 fractions or 0..100 percentages.
                if (confidence >= 0.0f && confidence <= 1.0f) confidence *= 100.0f;
                if (confidence < 0.0f) confidence = 0.0f;
                if (confidence > 100.0f) confidence = 100.0f;
                parsed.confidence = confidence;
                parsed.hasConfidence = true;
            }
        }
        copyBounded(parsed.suggestion, sizeof(parsed.suggestion), obj["suggestion"] | "");

        if (parsed.status[0] == '\0') {
            copyBounded(parsed.status, sizeof(parsed.status),
                        parsed.mudra[0] != '\0' ? "recognized" : "no_hand");
        }

        // Optional ranked alternatives:
        //   "candidates":[{"mudra":"Pataka","confidence":94.2}, ...]
        // Confidences accept either 0..1 fractions or 0..100 percentages.
        JsonArray alternatives = obj["candidates"].as<JsonArray>();
        if (!alternatives.isNull()) {
            for (JsonVariant item : alternatives) {
                if (parsed.candidateCount >= protocol::kMaxCandidates) break;
                JsonObject entry = item.as<JsonObject>();
                if (entry.isNull()) continue;
                const char* candidateName = entry["mudra"] | "";
                if (*candidateName == '\0') continue;
                float candidateConfidence = entry["confidence"] | 0.0f;
                if (isnan(candidateConfidence) || isinf(candidateConfidence)) continue;
                if (candidateConfidence >= 0.0f && candidateConfidence <= 1.0f) {
                    candidateConfidence *= 100.0f;
                }
                if (candidateConfidence < 0.0f) candidateConfidence = 0.0f;
                if (candidateConfidence > 100.0f) candidateConfidence = 100.0f;
                copyBounded(parsed.candidates[parsed.candidateCount].mudra,
                            protocol::kCandidateNameMax, candidateName);
                parsed.candidates[parsed.candidateCount].confidence = candidateConfidence;
                parsed.candidateCount++;
            }
        }

        // Fall back to the top-1 fields when no array was supplied.
        if (parsed.candidateCount == 0 && parsed.mudra[0] != '\0') {
            copyBounded(parsed.candidates[0].mudra, protocol::kCandidateNameMax, parsed.mudra);
            parsed.candidates[0].confidence = parsed.confidence;
            parsed.candidateCount = 1;
        }
    } else if (strcmp(event, protocol::kEventStatus) == 0) {
        parsed.type = protocol::EventType::Status;
        copyBounded(parsed.status, sizeof(parsed.status), obj["status"] | "alive");
    } else if (strcmp(event, protocol::kEventUi) == 0) {
        parsed.type = protocol::EventType::Ui;
        copyBounded(parsed.action, sizeof(parsed.action), obj["action"] | "");
    } else if (strcmp(event, protocol::kEventPing) == 0) {
        parsed.type = protocol::EventType::Ping;
    } else if (strcmp(event, protocol::kEventHello) == 0) {
        parsed.type = protocol::EventType::Hello;
        copyBounded(parsed.status, sizeof(parsed.status), obj["device"] | "laptop");
    } else if (strcmp(event, protocol::kEventTarget) == 0) {
        parsed.type = protocol::EventType::Target;
        copyBounded(parsed.mudra, sizeof(parsed.mudra), obj["mudra"] | "");
        copyBounded(parsed.mode, sizeof(parsed.mode), obj["mode"] | "");
        parsed.showImage = obj["image"] | false;
    } else {
        // Unknown event names still prove the link is alive; ignore content.
        parsed.type = protocol::EventType::Unknown;
    }

    out = parsed;
    return protocol::ParseResult::Ok;
}

}  // namespace communication

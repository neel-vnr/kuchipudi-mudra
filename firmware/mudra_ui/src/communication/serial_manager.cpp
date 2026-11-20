#include "serial_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/bitmap_store.h"

namespace communication {
namespace {

constexpr size_t kQueueDepth = 24;
constexpr BaseType_t kRxCore = 0;      // UI task (Arduino loop) lives on core 1
constexpr UBaseType_t kRxPriority = 3;

}  // namespace

void SerialManager::begin(uint32_t baud) {
    Serial.begin(baud);
    Serial.setTimeout(20);
    delay(50);

    if (rxQueue_ == nullptr) {
        rxQueue_ = xQueueCreate(kQueueDepth, sizeof(char*));
    }
    if (rxQueue_ != nullptr) {
        xTaskCreatePinnedToCore(rxTaskEntry, "uart_rx", 4096, this, kRxPriority, nullptr, kRxCore);
    }

#if MUDRA_SERIAL_CONSOLE
    Serial.println();
    Serial.println("# MUDRA AI serial link up");
#endif
}

void SerialManager::rxTaskEntry(void* arg) {
    static_cast<SerialManager*>(arg)->rxTask();
}

// Runs forever on core 0: nothing here blocks the UI or draws.
void SerialManager::rxTask() {
    char line[MUDRA_SERIAL_LINE_MAX + 1];
    size_t len = 0;
    bool discarding = false;

    for (;;) {
        int budget = 512;  // bound the work per scheduling slice
        while (budget-- > 0 && Serial.available() > 0) {
            const int raw = Serial.read();
            if (raw < 0) break;
            const char c = static_cast<char>(raw);

            if (c == '\n') {
                if (discarding) {
                    discarding = false;
                    len = 0;
                    droppedLines_++;
                    continue;
                }
                line[len] = '\0';
                while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) {
                    line[--len] = '\0';
                }
                if (len > 0) {
                    // Binary picture payload: the next w*h*2 bytes are raw RGB565,
                    // written straight into the bitmap buffer. This is roughly
                    // twice as fast as the hex path.
                    if (strncmp(line, "IMGBIN:", 7) == 0) {
                        int w = 0;
                        int h = 0;
                        if (sscanf(line + 7, "%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
                            ui::bitmap::begin(static_cast<int16_t>(w), static_cast<int16_t>(h));
                            uint32_t remaining = ui::bitmap::rawByteCount();
                            uint8_t* buffer = ui::bitmap::rawBuffer();
                            uint32_t index = 0;
                            while (remaining > 0 && buffer != nullptr) {
                                const int available = Serial.available();
                                if (available > 0) {
                                    const uint32_t want = (remaining < static_cast<uint32_t>(available))
                                                              ? remaining
                                                              : static_cast<uint32_t>(available);
                                    const int got = Serial.readBytes(buffer + index, want);
                                    if (got > 0) {
                                        index += static_cast<uint32_t>(got);
                                        remaining -= static_cast<uint32_t>(got);
                                    }
                                } else {
                                    vTaskDelay(1);
                                }
                            }
                            ui::bitmap::markComplete();
                        }
                        len = 0;
                        continue;
                    }

                    char* copy = static_cast<char*>(malloc(len + 1));
                    if (copy != nullptr) {
                        memcpy(copy, line, len + 1);
                        if (xQueueSend(rxQueue_, &copy, 0) != pdTRUE) {
                            free(copy);
                            droppedLines_++;
                        }
                    } else {
                        droppedLines_++;
                    }
                }
                len = 0;
                continue;
            }

            if (discarding) continue;
            if (len >= MUDRA_SERIAL_LINE_MAX) {
                len = 0;
                discarding = true;
                continue;
            }
            line[len++] = c;
        }
        vTaskDelay(1);  // 1 tick: yields without adding noticeable latency
    }
}

void SerialManager::poll() {
    const uint32_t now = millis();

    // Drain a bounded number of lines per call so a burst cannot stall the UI.
    if (rxQueue_ != nullptr) {
        for (int i = 0; i < 12; ++i) {
            char* line = nullptr;
            if (xQueueReceive(rxQueue_, &line, 0) != pdTRUE) break;
            if (line != nullptr) {
                dispatch(line, strlen(line));
                free(line);
            }
        }
    }

    flushPending(now);
}

void SerialManager::dispatch(char* line, size_t length) {
    lastRxMs_ = millis();
    rxLines_++;
    if (handler_) {
        handler_(line, length);
    }
}

bool SerialManager::sendCommand(const char* command) {
    if (command == nullptr || *command == '\0') return false;
    const uint32_t now = millis();
    if (lastTxMs_ != 0 && (now - lastTxMs_) < MUDRA_TX_MIN_INTERVAL_MS) {
        strncpy(pending_, command, sizeof(pending_) - 1);
        pending_[sizeof(pending_) - 1] = '\0';
        hasPending_ = true;
        return true;
    }
    writeLine(command);
    return true;
}

void SerialManager::sendLine(const char* line) {
    if (line == nullptr || *line == '\0') return;
    writeLine(line);
}

void SerialManager::writeLine(const char* line) {
    Serial.print(line);
    Serial.print('\n');
    lastTxMs_ = millis();
    txLines_++;
}

void SerialManager::flushPending(uint32_t now) {
    if (!hasPending_) return;
    if (lastTxMs_ != 0 && (now - lastTxMs_) < MUDRA_TX_MIN_INTERVAL_MS) return;
    hasPending_ = false;
    writeLine(pending_);
}

}  // namespace communication

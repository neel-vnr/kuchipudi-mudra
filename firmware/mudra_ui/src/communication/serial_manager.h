#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <functional>

#include "../config/board_config.h"

namespace communication {

// Non-blocking newline-delimited line reader/writer for the laptop link.
// - Never blocks on read.
// - Caps line length and resynchronises on overflow.
// - Rate-limits outbound commands and keeps only the latest pending command.
class SerialManager {
public:
    using LineHandler = std::function<void(const char* line, size_t length)>;

    void begin(uint32_t baud = MUDRA_SERIAL_BAUD);
    void setLineHandler(LineHandler handler) { handler_ = std::move(handler); }

    // Reads available bytes and dispatches complete lines.
    void poll();

    // Queues a command for the laptop. Returns false only if the link is not
    // ready. The most recent command wins if calls are closer than the minimum
    // interval.
    bool sendCommand(const char* command);

    // Writes a complete line immediately (used for rare JSON device events).
    void sendLine(const char* line);

    uint32_t lastRxMs() const { return lastRxMs_; }
    uint32_t rxLines() const { return rxLines_; }
    uint32_t droppedLines() const { return droppedLines_; }
    uint32_t txLines() const { return txLines_; }

private:
    void dispatch(char* line, size_t length);
    void writeLine(const char* line);
    void flushPending(uint32_t now);

    // UART reception runs in its own task on core 0 and hands complete lines to
    // the UI task through this queue. Without it a 921600 image stream starves
    // the render loop and bytes are lost.
    static void rxTaskEntry(void* arg);
    void rxTask();

    LineHandler handler_;
    QueueHandle_t rxQueue_ = nullptr;
    char buffer_[MUDRA_SERIAL_LINE_MAX + 1] = {};
    size_t length_ = 0;
    bool discarding_ = false;

    char pending_[40] = {};
    bool hasPending_ = false;
    uint32_t lastRxMs_ = 0;
    uint32_t lastTxMs_ = 0;
    uint32_t rxLines_ = 0;
    uint32_t droppedLines_ = 0;
    uint32_t txLines_ = 0;
};

}  // namespace communication

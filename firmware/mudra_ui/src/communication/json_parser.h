#pragma once

#include <stddef.h>

#include "protocol.h"

namespace communication {

// Parses exactly one newline-delimited JSON object. Never throws; malformed
// input returns a failure result and leaves `out` untouched.
protocol::ParseResult parseEventLine(const char* line, size_t length, protocol::Event& out);

}  // namespace communication

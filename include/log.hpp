#pragma once

#include "common.hpp"

namespace Log {

void init();
void close();

/// Appends to the SD log and to an in-memory ring buffer shown on the progress screen.
void printf(const char *fmt, ...);
const std::deque<std::string> &recent();
void clearRecent();

}  // namespace Log

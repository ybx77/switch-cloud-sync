#pragma once

#include "common.hpp"
#include "config.hpp"
#include "http.hpp"

class State;

/// Uploads new or changed album screenshots/videos. Local files are kept by default.
bool albumSyncUpload(const Config &cfg, HttpClient &http, State &state, SyncStats *stats,
                     std::string *err);

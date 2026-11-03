#pragma once

#include "common.hpp"
#include "config.hpp"
#include "http.hpp"

class State;

/// Pings the server and compares its instance id with the one stored locally.
/// When the server changed (new database, different backend), the local
/// "already uploaded" index is wiped so the next sync re-uploads everything.
void syncServerIdentity(const Config &cfg, HttpClient &http, State &state);

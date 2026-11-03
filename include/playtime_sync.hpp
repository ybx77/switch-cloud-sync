#pragma once

#include "common.hpp"
#include "config.hpp"
#include "http.hpp"

class TitleCache;
class State;

struct PlayLogEvent {
    std::string type;  ///< "Launched" / "Gained Focus" / "Lost Focus" / "Account Login" / ...
    long long clock = 0;
    long long steady = 0;
};

struct PlayLogTitle {
    u64 title_id = 0;
    std::string name;
    long long first_played = 0;
    long long last_played = 0;
    u64 playtime_ns = 0;
    u64 launches = 0;
    std::vector<PlayLogEvent> events;
};

struct PlayLogUser {
    std::string id;    ///< 32 hex chars
    std::string name;  ///< profile nickname (UTF-8)
    std::vector<PlayLogTitle> titles;
};

/// Collects the system play log (sessions, account events) plus per-title summaries.
bool playLogCollect(TitleCache &titles, std::vector<PlayLogUser> *out, std::string *err);

/// Serializes to the same shape as NX-Activity-Log's export (summary.playtime is seconds).
std::string playLogToJson(const std::vector<PlayLogUser> &users, const std::string &device);

/// Uploads the compact summary TSV and (when enabled) the detailed JSON play log.
bool playtimeSyncUpload(const Config &cfg, HttpClient &http, TitleCache &titles, State &state,
                        int *rows, u64 *total_seconds, std::string *err);

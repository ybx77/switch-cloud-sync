#pragma once

#include "common.hpp"
#include "config.hpp"
#include "http.hpp"

class State;
class TitleCache;

/// Backs up local save data and uploads only the entries that changed.
bool saveSyncUpload(const Config &cfg, HttpClient &http, State &state, TitleCache &titles,
                    SyncStats *stats, std::string *err);

bool saveSyncListRemote(const Config &cfg, HttpClient &http, State &state,
                        std::vector<RemoteSave> *out, std::string *err);

bool saveSyncRestore(const Config &cfg, HttpClient &http, State &state, const RemoteSave &remote,
                     std::string *err);

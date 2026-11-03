#pragma once

#include "common.hpp"

struct Config {
    // [server]
    std::string base_url;
    std::string token;
    std::string ca_bundle;
    bool insecure_tls = false;
    long timeout_sec = 60;

    // [device]
    std::string device_id_config;
    std::string device_name;

    // [saves]
    bool saves_enabled = true;
    bool games_only = true;
    long max_upload_mb = 256;

    // [sync]
    /// When true, saves/album entries that did not change since the last upload are skipped.
    bool only_changed = true;

    // [album]
    bool album_enabled = true;
    bool album_delete_after_upload = false;

    // [playtime]
    bool playtime_enabled = true;
    bool playtime_detailed = true;

    bool load(std::string *err);
    bool writeDefault(std::string *err);
    bool save(std::string *err) const;

    std::string api(const std::string &path) const;
    bool ready() const { return !base_url.empty(); }
};

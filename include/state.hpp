#pragma once

#include "common.hpp"

struct Config;

struct SaveIndexEntry {
    std::string sha;
    long long timestamp = 0;
    u64 commit = 0;
};

/// Local sync bookkeeping stored on the SD card.
class State {
public:
    bool init(std::string *err);

    const std::string &deviceId() const { return device_id_; }

    bool getSaveEntry(const std::string &title_id, const std::string &user_id, std::string *sha,
                      long long *timestamp, u64 *commit) const;
    void setSaveEntry(const std::string &title_id, const std::string &user_id, const std::string &sha,
                      long long timestamp, u64 commit);

    bool albumUploaded(const std::string &path, u64 size, long long mtime) const;
    void markAlbum(const std::string &path, u64 size, long long mtime);

    bool flush(std::string *err = nullptr);

    size_t saveCount() const { return saves_.size(); }
    size_t albumCount() const { return album_.size(); }

    void setLastSync(const std::string &value) { last_sync_ = value; }
    const std::string &lastSync() const { return last_sync_; }

private:
    struct AlbumIndexEntry {
        u64 size = 0;
        long long mtime = 0;
    };

    std::string device_id_;
    std::map<std::string, SaveIndexEntry> saves_;
    std::map<std::string, AlbumIndexEntry> album_;
    std::string last_sync_;
};

std::string effectiveDeviceId(const Config &cfg, const State &state);

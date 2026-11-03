#include "state.hpp"

#include "config.hpp"

namespace {

std::string saveKey(const std::string &title_id, const std::string &user_id) {
    return title_id + "/" + user_id;
}

std::string makeDeviceId() {
    u64 seed = ((u64)time(nullptr) << 32) ^ (u64)rand();
    seed ^= (u64)(uintptr_t)&seed;
    return util::format("switch-%08llx%08llx", (unsigned long long)(seed >> 32),
                        (unsigned long long)(seed & 0xFFFFFFFFULL));
}

}  // namespace

bool State::init(std::string *err) {
    util::ensureDir(SD_ROOT);
    util::ensureDir(SD_STATE_DIR);
    util::ensureDir(SD_TMP_DIR);

    std::string text;
    if (util::readFile(SD_STATE_DEVICE, &text)) {
        device_id_ = util::trim(text);
    }
    if (device_id_.empty()) {
        srand((unsigned)time(nullptr));
        device_id_ = makeDeviceId();
        if (!util::writeFile(SD_STATE_DEVICE, device_id_ + "\n")) {
            if (err) *err = util::format("cannot write %s", SD_STATE_DEVICE);
        }
    }

    text.clear();
    if (util::readFile(SD_STATE_SAVES, &text)) {
        std::vector<std::string> lines = util::split(text, '\n');
        for (size_t i = 0; i < lines.size(); ++i) {
            std::vector<std::string> f = util::split(lines[i], '\t');
            if (f.size() < 4) continue;
            SaveIndexEntry entry;
            entry.sha = f[2];
            entry.timestamp = atoll(f[3].c_str());
            if (f.size() >= 5) entry.commit = strtoull(f[4].c_str(), nullptr, 10);
            saves_[saveKey(f[0], f[1])] = entry;
        }
    }

    text.clear();
    if (util::readFile(SD_STATE_ALBUM, &text)) {
        std::vector<std::string> lines = util::split(text, '\n');
        for (size_t i = 0; i < lines.size(); ++i) {
            std::vector<std::string> f = util::split(lines[i], '\t');
            if (f.size() != 3) continue;
            AlbumIndexEntry entry;
            entry.size = strtoull(f[1].c_str(), nullptr, 10);
            entry.mtime = atoll(f[2].c_str());
            album_[f[0]] = entry;
        }
    }

    text.clear();
    if (util::readFile(SD_STATE_META, &text)) {
        std::vector<std::string> lines = util::split(text, '\n');
        for (size_t i = 0; i < lines.size(); ++i) {
            if (util::startsWith(lines[i], "last_sync=")) {
                last_sync_ = util::trim(lines[i].substr(strlen("last_sync=")));
            } else if (util::startsWith(lines[i], "server_id=")) {
                server_id_ = util::trim(lines[i].substr(strlen("server_id=")));
            }
        }
    }

    return true;
}

void State::clearIndexes() {
    saves_.clear();
    album_.clear();
}

bool State::getSaveEntry(const std::string &title_id, const std::string &user_id,
                         std::string *sha, long long *timestamp, u64 *commit) const {
    std::map<std::string, SaveIndexEntry>::const_iterator it =
        saves_.find(saveKey(title_id, user_id));
    if (it == saves_.end()) return false;
    if (sha) *sha = it->second.sha;
    if (timestamp) *timestamp = it->second.timestamp;
    if (commit) *commit = it->second.commit;
    return true;
}

void State::setSaveEntry(const std::string &title_id, const std::string &user_id,
                         const std::string &sha, long long timestamp, u64 commit) {
    SaveIndexEntry entry;
    entry.sha = sha;
    entry.timestamp = timestamp;
    entry.commit = commit;
    saves_[saveKey(title_id, user_id)] = entry;
}

bool State::albumUploaded(const std::string &path, u64 size, long long mtime) const {
    std::map<std::string, AlbumIndexEntry>::const_iterator it = album_.find(path);
    if (it == album_.end()) return false;
    return it->second.size == size && it->second.mtime == mtime;
}

void State::markAlbum(const std::string &path, u64 size, long long mtime) {
    AlbumIndexEntry entry;
    entry.size = size;
    entry.mtime = mtime;
    album_[path] = entry;
}

bool State::flush(std::string *err) {
    std::string out;
    for (std::map<std::string, SaveIndexEntry>::const_iterator it = saves_.begin();
         it != saves_.end(); ++it) {
        size_t slash = it->first.find('/');
        if (slash == std::string::npos) continue;
        std::string title_id = it->first.substr(0, slash);
        std::string user_id = it->first.substr(slash + 1);
        out += title_id + "\t" + user_id + "\t" + it->second.sha + "\t" +
               util::format("%lld", it->second.timestamp) + "\t" +
               util::format("%llu", (unsigned long long)it->second.commit) + "\n";
    }
    if (!util::writeFile(SD_STATE_SAVES, out)) {
        if (err) *err = util::format("cannot write %s", SD_STATE_SAVES);
        return false;
    }

    out.clear();
    for (std::map<std::string, AlbumIndexEntry>::const_iterator it = album_.begin();
         it != album_.end(); ++it) {
        out += it->first + "\t" + util::format("%llu", (unsigned long long)it->second.size) + "\t" +
               util::format("%lld", it->second.mtime) + "\n";
    }
    if (!util::writeFile(SD_STATE_ALBUM, out)) {
        if (err) *err = util::format("cannot write %s", SD_STATE_ALBUM);
        return false;
    }

    std::string meta = "last_sync=" + last_sync_ + "\nserver_id=" + server_id_ + "\n";
    util::writeFile(SD_STATE_META, meta);
    return true;
}

std::string effectiveDeviceId(const Config &cfg, const State &state) {
    if (!cfg.device_id_config.empty()) return cfg.device_id_config;
    return state.deviceId();
}

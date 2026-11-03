#include "save_sync.hpp"

#include <unistd.h>

#include "log.hpp"
#include "saves.hpp"
#include "sha256.hpp"
#include "state.hpp"
#include "titles.hpp"
#include "ui.hpp"

namespace {

std::string sanitizeText(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        out.push_back((c < 0x20 || c == 0x7F) ? ' ' : (char)c);
    }
    return util::trim(out);
}

std::string canonUserId(const std::string &raw) {
    std::string s = util::normHexId(raw);
    if (s.empty() || s == "0") return std::string("0");
    return util::padHexId(s, 32);
}

void splitTab(const std::string &line, size_t max_fields, std::vector<std::string> *out) {
    out->clear();
    size_t start = 0;
    while (out->size() + 1 < max_fields) {
        size_t pos = line.find('\t', start);
        if (pos == std::string::npos) break;
        out->push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    out->push_back(line.substr(start));
}

std::string httpError(long status) {
    if (status == 401 || status == 403) return util::format("HTTP %ld（检查 token）", status);
    if (status == 404) return util::format("HTTP 404（端点不存在或没有数据）");
    if (status == 413) return "HTTP 413（超过服务端体积限制）";
    return util::format("HTTP %ld", status);
}

bool findLocalSave(const RemoteSave &remote, SaveEntry *out, std::string *err) {
    std::vector<SaveEntry> saves;
    if (!enumerateSaves(&saves, err)) return false;

    const std::string want_title = util::padHexId(remote.title_id, 16);
    const std::string want_user = canonUserId(remote.user_id);

    for (size_t i = 0; i < saves.size(); ++i) {
        if (util::toHexU64(saves[i].app_id, 16) == want_title &&
            saves[i].userIdStr() == want_user) {
            *out = saves[i];
            return true;
        }
    }
    if (err) *err = "本地没有对应的存档（游戏未安装或用其他用户）";
    return false;
}

}  // namespace

bool saveSyncUpload(const Config &cfg, HttpClient &http, State &state, TitleCache &titles,
                    SyncStats *stats, std::string *err) {
    *stats = SyncStats();
    if (!cfg.saves_enabled) {
        if (err) *err = "设置里已关闭存档上传";
        return false;
    }

    std::vector<SaveEntry> all;
    if (!enumerateSaves(&all, err)) return false;

    std::string title_err;
    if (!titles.ensureLoaded(&title_err)) {
        Log::printf("警告: %s（游戏名会缺失）", title_err.c_str());
    }

    std::vector<SaveEntry> list;
    for (size_t i = 0; i < all.size(); ++i) {
        if (cfg.games_only && all[i].app_id < APP_ID_GAME_MIN) continue;
        list.push_back(all[i]);
    }

    stats->total = (int)list.size();
    Log::printf("找到 %u 个存档（过滤后 %u 个）", (unsigned)all.size(), (unsigned)list.size());
    if (list.empty()) return true;

    UI::progressPhase("存档备份", (int)list.size());

    const std::string device = effectiveDeviceId(cfg, state);
    const long long max_bytes = (long long)cfg.max_upload_mb * 1024LL * 1024LL;
    util::ensureDir(SD_TMP_DIR);

    for (size_t i = 0; i < list.size(); ++i) {
        if (!UI::pump()) {
            if (err) *err = "已取消";
            return false;
        }

        const SaveEntry &entry = list[i];
        const std::string title_id = util::toHexU64(entry.app_id, 16);
        const std::string user_id = entry.userIdStr();

        std::string name = sanitizeText(titles.nameFor(entry.app_id));
        if (name.empty()) name = "未知游戏";
        UI::progressStep((int)i + 1, name);

        // Cheap check first: the save-data commit id changes whenever the game writes it.
        std::string prev_sha;
        long long prev_ts = 0;
        u64 prev_commit = 0;
        bool have_prev = state.getSaveEntry(title_id, user_id, &prev_sha, &prev_ts, &prev_commit);

        FsSaveDataExtraData extra;
        std::string extra_err;
        bool have_extra = readSaveExtraData(entry, &extra, &extra_err);

        if (cfg.only_changed && have_prev && have_extra && prev_commit != 0 &&
            extra.commit_id == prev_commit) {
            Log::printf("  跳过（未变化）%s", name.c_str());
            ++stats->skipped;
            continue;
        }

        FsFileSystem fs;
        std::string open_err;
        if (!openSaveData(entry, &fs, &open_err)) {
            Log::printf("  失败 %s: %s", name.c_str(), open_err.c_str());
            ++stats->failed;
            continue;
        }

        const std::string tmp_path =
            std::string(SD_TMP_DIR) + "/save-" + title_id + "-" + user_id + ".zip";
        std::string sha;
        long long zip_bytes = 0;
        long long raw_bytes = 0;
        std::string build_err;
        bool built =
            buildSaveArchive(&fs, entry, name, tmp_path, &sha, &zip_bytes, &raw_bytes, &build_err);
        closeSaveData(&fs);

        if (!built) {
            Log::printf("  失败 %s: %s", name.c_str(), build_err.c_str());
            ++stats->failed;
            continue;
        }

        if (raw_bytes > 0) {
            Log::printf("  压缩 %s → %s（%lld%%）", util::formatBytes(raw_bytes).c_str(),
                        util::formatBytes(zip_bytes).c_str(),
                        (long long)(zip_bytes * 100 / (raw_bytes > 0 ? raw_bytes : 1)));
        }

        if (cfg.only_changed && have_prev && prev_sha == sha) {
            // Remember the commit id so next time this save is skipped without reading it.
            if (have_extra) state.setSaveEntry(title_id, user_id, prev_sha, prev_ts, extra.commit_id);
            Log::printf("  跳过（内容相同）%s", name.c_str());
            ++stats->skipped;
            unlink(tmp_path.c_str());
            continue;
        }

        if (zip_bytes > max_bytes) {
            Log::printf("  跳过 %s：压缩后 %s 超过上限 %ld MB", name.c_str(),
                        util::formatBytes(zip_bytes).c_str(), cfg.max_upload_mb);
            ++stats->failed;
            unlink(tmp_path.c_str());
            continue;
        }
        FileUploadSource src(tmp_path);
        if (!src.ok()) {
            Log::printf("  失败 %s：无法重新打开备份文件", name.c_str());
            ++stats->failed;
            unlink(tmp_path.c_str());
            continue;
        }

        long long timestamp = util::nowUnix();
        std::string url = cfg.api("/saves") + "?device=" + util::urlEncode(device) +
                          "&title_id=" + title_id + "&user_id=" + user_id +
                          "&timestamp=" + util::format("%lld", timestamp) +
                          "&title_name=" + util::urlEncode(name);

        std::vector<std::string> headers;
        headers.push_back("X-Content-SHA256: " + sha);
        headers.push_back("X-Device-Name: " + util::urlEncode(cfg.device_name));

        Log::printf("  上传 %s（%s）", name.c_str(), util::formatBytes(zip_bytes).c_str());

        HttpResponse resp;
        std::string http_err;
        bool ok = http.put(url, &src, "application/zip", headers, &resp, &http_err);
        unlink(tmp_path.c_str());

        if (!ok) {
            Log::printf("  失败 %s: %s", name.c_str(), http_err.c_str());
            ++stats->failed;
            if (g_abort || g_exit) break;
            continue;
        }
        if (resp.status < 200 || resp.status >= 300) {
            Log::printf("  失败 %s: %s", name.c_str(), httpError(resp.status).c_str());
            ++stats->failed;
            continue;
        }

        state.setSaveEntry(title_id, user_id, sha, timestamp,
                           have_extra ? extra.commit_id : 0);
        ++stats->uploaded;
        Log::printf("  完成 %s", name.c_str());
        if (stats->uploaded % 10 == 0) state.flush();
    }

    state.setLastSync(util::format("存档 %s", util::formatUnixTime(util::nowUnix()).c_str()));
    state.flush();
    return !(g_abort || g_exit);
}

bool saveSyncListRemote(const Config &cfg, HttpClient &http, State &state,
                        std::vector<RemoteSave> *out, std::string *err) {
    out->clear();
    const std::string device = effectiveDeviceId(cfg, state);

    std::string url = cfg.api("/saves") + "?device=" + util::urlEncode(device);

    HttpResponse resp;
    std::string http_err;
    if (!http.getText(url, &resp, &http_err)) {
        if (err) *err = http_err;
        return false;
    }
    if (resp.status != 200) {
        if (err) *err = httpError(resp.status);
        return false;
    }

    std::vector<std::string> lines = util::split(resp.body, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = util::trim(lines[i]);
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> f;
        splitTab(line, 6, &f);
        if (f.size() != 6) continue;

        RemoteSave save;
        save.title_id = util::padHexId(f[0], 16);
        save.user_id = canonUserId(f[1]);
        save.timestamp = atoll(f[2].c_str());
        save.size = atoll(f[3].c_str());
        save.sha256 = util::toLower(util::trim(f[4]));
        save.title_name = sanitizeText(f[5]);
        out->push_back(save);
    }

    std::sort(out->begin(), out->end(), [](const RemoteSave &a, const RemoteSave &b) {
        if (a.title_name != b.title_name) return a.title_name < b.title_name;
        return a.title_id < b.title_id;
    });
    return true;
}

bool saveSyncRestore(const Config &cfg, HttpClient &http, State &state, const RemoteSave &remote,
                     std::string *err) {
    util::ensureDir(SD_TMP_DIR);
    const std::string tmp_path = std::string(SD_TMP_DIR) + "/restore.zip";

    UI::progressPhase("下载存档", 1);
    std::string url = cfg.api("/saves/latest") + "?device=" +
                      util::urlEncode(effectiveDeviceId(cfg, state)) +
                      "&title_id=" + util::urlEncode(remote.title_id) +
                      "&user_id=" + util::urlEncode(remote.user_id);

    FileDownloadSink sink(tmp_path);
    if (!sink.ok()) {
        if (err) *err = util::format("无法创建 %s", tmp_path.c_str());
        return false;
    }

    HttpResponse resp;
    std::string http_err;
    bool ok = http.download(url, &sink, &resp, &http_err);
    bool closed = sink.close();

    if (!ok) {
        unlink(tmp_path.c_str());
        if (err) *err = http_err;
        return false;
    }
    if (resp.status != 200) {
        unlink(tmp_path.c_str());
        if (err) *err = httpError(resp.status);
        return false;
    }
    if (!closed) {
        unlink(tmp_path.c_str());
        if (err) *err = "写入 SD 卡失败";
        return false;
    }

    UI::progressPhase("校验并写入", 1);
    std::string sha = sha256File(tmp_path.c_str());
    if (!remote.sha256.empty() && sha != remote.sha256) {
        unlink(tmp_path.c_str());
        if (err) *err = util::format("校验失败（本地 %s / 服务器 %s）", sha.c_str(),
                                     remote.sha256.c_str());
        return false;
    }

    SaveEntry entry;
    if (!findLocalSave(remote, &entry, err)) {
        unlink(tmp_path.c_str());
        return false;
    }

    FsFileSystem fs;
    std::string open_err;
    if (!openSaveData(entry, &fs, &open_err)) {
        unlink(tmp_path.c_str());
        if (err) *err = open_err;
        return false;
    }

    if (!UI::pump()) {
        closeSaveData(&fs);
        unlink(tmp_path.c_str());
        if (err) *err = "已取消";
        return false;
    }

    bool restored = restoreSaveArchive(&fs, tmp_path, err);
    closeSaveData(&fs);
    unlink(tmp_path.c_str());
    if (!restored) return false;

    FsSaveDataExtraData extra;
    std::string extra_err;
    bool have_extra = readSaveExtraData(entry, &extra, &extra_err);

    state.setSaveEntry(remote.title_id, remote.user_id, sha,
                       remote.timestamp > 0 ? remote.timestamp : util::nowUnix(),
                       have_extra ? extra.commit_id : 0);
    state.setLastSync(util::format("恢复 %s", util::formatUnixTime(util::nowUnix()).c_str()));
    state.flush();
    Log::printf("恢复完成: %s", remote.title_id.c_str());
    return true;
}

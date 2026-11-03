#include "album_sync.hpp"

#include "log.hpp"
#include "state.hpp"
#include "ui.hpp"

namespace {

const char kAlbumSdmcPath[] = "Nintendo/Album";

struct AlbumRoot {
    FsFileSystem fs;
    bool open = false;
    std::string prefix;
};

struct AlbumFile {
    std::string path;
    u64 size = 0;
    long long mtime = 0;
};

bool isMediaFile(const std::string &name) {
    return util::endsWithNoCase(name, ".jpg") || util::endsWithNoCase(name, ".jpeg") ||
           util::endsWithNoCase(name, ".mp4");
}

std::string contentTypeFor(const std::string &name) {
    if (util::endsWithNoCase(name, ".mp4")) return "video/mp4";
    if (util::endsWithNoCase(name, ".jpg") || util::endsWithNoCase(name, ".jpeg")) {
        return "image/jpeg";
    }
    return "application/octet-stream";
}

bool openAlbum(AlbumRoot *root, std::string *err) {
    Result rc = fsOpenImageDirectoryFileSystem(&root->fs, FsImageDirectoryId_Sd);
    if (R_SUCCEEDED(rc)) {
        root->open = true;
        root->prefix.clear();
        Log::printf("相册: ImageDirectory 挂载成功");
        return true;
    }
    Log::printf("ImageDirectory 挂载失败 (0x%x)，改用 sdmc", rc);

    rc = fsOpenSdCardFileSystem(&root->fs);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("无法打开 SD 卡文件系统 (0x%x)", rc);
        return false;
    }

    const std::string album_root = std::string("/") + kAlbumSdmcPath;
    FsDirEntryType type;
    rc = fsFsGetEntryType(&root->fs, album_root.c_str(), &type);
    if (R_FAILED(rc) || type != FsDirEntryType_Dir) {
        fsFsClose(&root->fs);
        if (err) *err = util::format("找不到 sdmc:/%s", kAlbumSdmcPath);
        return false;
    }

    root->open = true;
    root->prefix = kAlbumSdmcPath;
    Log::printf("相册: sdmc:/%s", kAlbumSdmcPath);
    return true;
}

void closeAlbum(AlbumRoot *root) {
    if (root->open) {
        fsFsClose(&root->fs);
        root->open = false;
    }
}

std::string albumPath(const std::string &prefix, const std::string &rel) {
    std::string p = "/";
    if (!prefix.empty()) {
        p += prefix;
        if (!rel.empty()) p += "/";
    }
    p += rel;
    return p;
}

void walkAlbum(FsFileSystem *fs, const std::string &prefix, const std::string &rel,
               std::vector<AlbumFile> *out, int depth) {
    if (depth > 12) return;

    std::string dir_path = albumPath(prefix, rel);
    FsDir dir;
    Result rc = fsFsOpenDirectory(fs, dir_path.c_str(),
                                 FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
    if (R_FAILED(rc)) {
        Log::printf("  跳过 %s (0x%x)", dir_path.c_str(), rc);
        return;
    }

    FsDirectoryEntry entries[16];
    for (;;) {
        s64 total = 0;
        rc = fsDirRead(&dir, &total, 16, entries);
        if (R_FAILED(rc) || total <= 0) break;

        for (s64 i = 0; i < total; ++i) {
            std::string name(entries[i].name);
            if (name.empty() || name == "." || name == "..") continue;
            std::string child = rel.empty() ? name : (rel + "/" + name);

            if (entries[i].type == FsDirEntryType_Dir) {
                walkAlbum(fs, prefix, child, out, depth + 1);
                continue;
            }
            if (!isMediaFile(name)) continue;

            AlbumFile file;
            file.path = child;
            file.size = entries[i].file_size > 0 ? (u64)entries[i].file_size : 0;

            FsTimeStampRaw ts;
            memset(&ts, 0, sizeof(ts));
            if (R_SUCCEEDED(fsFsGetFileTimeStampRaw(fs, albumPath(prefix, child).c_str(), &ts)) &&
                ts.is_valid) {
                file.mtime = (long long)ts.modified;
            }
            out->push_back(file);
        }
    }
    fsDirClose(&dir);
}

/// Streams a file from an FsFileSystem into libcurl.
class FsFileUploadSource : public UploadSource {
public:
    FsFileUploadSource(FsFileSystem *fs, const std::string &path) {
        if (R_FAILED(fsFsOpenFile(fs, path.c_str(), FsOpenMode_Read, &file_))) return;
        s64 size = 0;
        if (R_FAILED(fsFileGetSize(&file_, &size)) || size < 0) {
            fsFileClose(&file_);
            return;
        }
        size_ = size;
        open_ = true;
    }

    ~FsFileUploadSource() override {
        if (open_) fsFileClose(&file_);
    }

    bool ok() const { return open_; }
    long long size() override { return size_; }

    size_t read(char *buf, size_t cap) override {
        if (!open_ || offset_ >= size_) return 0;
        s64 left = size_ - offset_;
        size_t want = (size_t)(left < (s64)cap ? left : (s64)cap);
        u64 got = 0;
        if (R_FAILED(fsFileRead(&file_, offset_, buf, want, FsReadOption_None, &got))) {
            return CURL_READFUNC_ABORT;
        }
        offset_ += (s64)got;
        return (size_t)got;
    }

private:
    FsFile file_;
    s64 size_ = 0;
    s64 offset_ = 0;
    bool open_ = false;
};

}  // namespace

bool albumSyncUpload(const Config &cfg, HttpClient &http, State &state, SyncStats *stats,
                     std::string *err) {
    *stats = SyncStats();
    if (!cfg.album_enabled) {
        if (err) *err = "设置里已关闭相册上传";
        return false;
    }

    AlbumRoot root;
    if (!openAlbum(&root, err)) return false;

    std::vector<AlbumFile> files;
    walkAlbum(&root.fs, root.prefix, std::string(), &files, 0);
    std::sort(files.begin(), files.end(),
              [](const AlbumFile &a, const AlbumFile &b) { return a.path < b.path; });

    stats->total = (int)files.size();
    Log::printf("找到 %u 个相册文件", (unsigned)files.size());
    if (files.empty()) {
        closeAlbum(&root);
        return true;
    }

    UI::progressPhase("相册上传", (int)files.size());
    const std::string device = effectiveDeviceId(cfg, state);

    for (size_t i = 0; i < files.size(); ++i) {
        if (!UI::pump()) {
            closeAlbum(&root);
            if (err) *err = "已取消";
            return false;
        }

        const AlbumFile &file = files[i];
        if (cfg.only_changed && state.albumUploaded(file.path, file.size, file.mtime)) {
            ++stats->skipped;
            continue;
        }

        UI::progressStep((int)i + 1, util::baseName(file.path));
        Log::printf("  %s（%s）", file.path.c_str(),
                    util::formatBytes((long long)file.size).c_str());

        std::string full = albumPath(root.prefix, file.path);

        FsFileUploadSource src(&root.fs, full);
        if (!src.ok()) {
            Log::printf("  失败：无法打开文件");
            ++stats->failed;
            continue;
        }

        std::string url = cfg.api("/album") + "?device=" + util::urlEncode(device) +
                          "&path=" + util::urlEncode(file.path);
        std::vector<std::string> headers;
        headers.push_back(util::format("X-File-Size: %llu", (unsigned long long)file.size));
        headers.push_back(util::format("X-File-Mtime: %lld", (long long)file.mtime));
        headers.push_back("X-Device-Name: " + util::urlEncode(cfg.device_name));

        HttpResponse resp;
        std::string http_err;
        bool ok = http.put(url, &src, contentTypeFor(file.path), headers, &resp, &http_err);

        if (!ok) {
            Log::printf("  失败: %s", http_err.c_str());
            ++stats->failed;
            if (g_abort || g_exit) break;
            continue;
        }
        if (resp.status < 200 || resp.status >= 300) {
            Log::printf("  失败: HTTP %ld", resp.status);
            ++stats->failed;
            continue;
        }

        state.markAlbum(file.path, file.size, file.mtime);
        ++stats->uploaded;

        if (cfg.album_delete_after_upload) {
            Result rc = fsFsDeleteFile(&root.fs, full.c_str());
            if (R_FAILED(rc)) Log::printf("  已上传，但删除本地文件失败 (0x%x)", rc);
        }

        if (stats->uploaded % 50 == 0) state.flush();
    }

    state.setLastSync(util::format("相册 %s", util::formatUnixTime(util::nowUnix()).c_str()));
    state.flush();
    closeAlbum(&root);
    return !(g_abort || g_exit);
}

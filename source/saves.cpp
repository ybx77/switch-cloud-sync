#include "saves.hpp"

#include <minizip/unzip.h>
#include <minizip/zip.h>
#include <zlib.h>

#include <map>

#include "log.hpp"
#include "sha256.hpp"
#include "ui.hpp"

namespace {

const char kLegacyMagic[6] = {'S', 'C', 'S', 'A', '1', '\n'};
const char kMetaEntry[] = ".__scs__/meta.txt";
const char kIndexEntry[] = ".__scs__/index.txt";
const u32 kLegacyMaxMetaLen = 0x10000;
const u32 kLegacyMaxNameLen = 0x400;
const u64 kMaxFileSize = 0x1000000000ULL;  // 64 GiB sanity limit

u8 g_copy_buf[0x10000];

// ---------------------------------------------------------------------------
// legacy SCSA1 helpers
// ---------------------------------------------------------------------------

bool readU32le(FILE *f, u32 *v) {
    u8 b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return true;
}

bool readU64le(FILE *f, u64 *v) {
    u8 b[8];
    if (fread(b, 1, 8, f) != 8) return false;
    u64 out = 0;
    for (int i = 7; i >= 0; --i) out = (out << 8) | (u64)b[i];
    *v = out;
    return true;
}

bool safeRelPath(const std::string &p) {
    if (p.empty() || p.size() > 0x300) return false;
    if (p[0] == '/') return false;
    if (p.find('\\') != std::string::npos) return false;

    size_t start = 0;
    for (;;) {
        size_t end = p.find('/', start);
        std::string segment =
            end == std::string::npos ? p.substr(start) : p.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// walking the save data filesystem
// ---------------------------------------------------------------------------

struct FileRef {
    std::string path;
    bool concat = false;
    u64 dir_size = 0;
};

struct WalkResult {
    std::vector<FileRef> files;
    std::vector<std::string> dirs;
};

bool walkDir(FsFileSystem *fs, const std::string &rel, FsDir *dir, WalkResult *out, int depth,
             std::string *err) {
    if (depth > 16) {
        if (err) *err = "存档目录层级过深";
        return false;
    }

    FsDirectoryEntry entries[16];
    for (;;) {
        s64 total = 0;
        Result rc = fsDirRead(dir, &total, 16, entries);
        if (R_FAILED(rc)) {
            if (err) *err = util::format("读取目录 %s 失败 (0x%x)", rel.c_str(), rc);
            return false;
        }
        if (total <= 0) break;

        for (s64 i = 0; i < total; ++i) {
            std::string name(entries[i].name);
            if (name.empty() || name == "." || name == "..") continue;
            std::string child = rel.empty() ? name : (rel + "/" + name);
            std::string full = "/" + child;

            if (entries[i].type == FsDirEntryType_Dir) {
                // A concatenation file is a directory with the archive bit set. It can be
                // opened as a *file* but not as a directory. Probe the file handle first;
                // a real directory never opens as a file.
                FsFile probe;
                Result rc_file = fsFsOpenFile(fs, full.c_str(), FsOpenMode_Read, &probe);
                if (R_SUCCEEDED(rc_file)) {
                    fsFileClose(&probe);
                    FileRef ref;
                    ref.path = child;
                    ref.concat = true;
                    out->files.push_back(ref);
                    continue;
                }

                FsDir sub;
                Result rc_dir = fsFsOpenDirectory(
                    fs, full.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &sub);
                if (R_FAILED(rc_dir)) {
                    if (err) {
                        *err = util::format("无法读取 %s（目录 0x%x / 文件 0x%x，条目类型 %d）",
                                            child.c_str(), rc_dir, rc_file,
                                            (int)entries[i].type);
                    }
                    return false;
                }
                out->dirs.push_back(child);
                if (!walkDir(fs, child, &sub, out, depth + 1, err)) {
                    fsDirClose(&sub);
                    return false;
                }
                fsDirClose(&sub);
                continue;
            }

            if (entries[i].type == FsDirEntryType_File) {
                FileRef ref;
                ref.path = child;
                ref.concat = false;
                ref.dir_size = entries[i].file_size > 0 ? (u64)entries[i].file_size : 0;
                out->files.push_back(ref);
                continue;
            }

            if (err) *err = util::format("不支持的条目类型 %d: %s", (int)entries[i].type,
                                         child.c_str());
            return false;
        }
    }
    return true;
}

bool walkSaveData(FsFileSystem *fs, WalkResult *out, std::string *err) {
    FsDir root;
    Result rc = fsFsOpenDirectory(fs, "/", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &root);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("无法打开存档根目录 (0x%x)", rc);
        return false;
    }
    bool ok = walkDir(fs, std::string(), &root, out, 0, err);
    fsDirClose(&root);

    std::sort(out->files.begin(), out->files.end(),
              [](const FileRef &a, const FileRef &b) { return a.path < b.path; });
    std::sort(out->dirs.begin(), out->dirs.end());
    return ok;
}

void ensureFsDirs(FsFileSystem *fs, const std::string &rel) {
    size_t pos = 0;
    while ((pos = rel.find('/', pos)) != std::string::npos) {
        std::string dir = rel.substr(0, pos);
        if (!dir.empty()) fsFsCreateDirectory(fs, ("/" + dir).c_str());
        ++pos;
    }
}

bool wipeSaveData(FsFileSystem *fs, std::string *err) {
    FsDir dir;
    Result rc = fsFsOpenDirectory(fs, "/", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("无法打开存档根目录 (0x%x)", rc);
        return false;
    }

    std::vector<std::pair<std::string, bool> > entries;
    FsDirectoryEntry buf[16];
    for (;;) {
        s64 total = 0;
        rc = fsDirRead(&dir, &total, 16, buf);
        if (R_FAILED(rc) || total <= 0) break;
        for (s64 i = 0; i < total; ++i) {
            std::string name(buf[i].name);
            if (name.empty() || name == "." || name == "..") continue;
            entries.push_back(std::make_pair(name, buf[i].type == FsDirEntryType_Dir));
        }
    }
    fsDirClose(&dir);

    for (size_t i = 0; i < entries.size(); ++i) {
        std::string path = "/" + entries[i].first;
        if (entries[i].second) {
            fsFsDeleteDirectoryRecursively(fs, path.c_str());
        } else {
            fsFsDeleteFile(fs, path.c_str());
        }
    }
    return true;
}

bool createFileForRestore(FsFileSystem *fs, const std::string &rel, u64 size, bool concat,
                          std::string *err) {
    ensureFsDirs(fs, rel);
    std::string full = "/" + rel;
    u32 option = concat ? FsCreateOption_BigFile : 0;

    Result rc = fsFsCreateFile(fs, full.c_str(), (s64)size, option);
    if (R_FAILED(rc)) {
        fsFsDeleteFile(fs, full.c_str());
        rc = fsFsCreateFile(fs, full.c_str(), (s64)size, option);
    }
    if (R_FAILED(rc)) {
        if (err) *err = util::format("创建 %s 失败 (0x%x)", rel.c_str(), rc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// legacy SCSA1 restore (kept so old backups can still be restored)
// ---------------------------------------------------------------------------

bool restoreLegacyArchive(FsFileSystem *fs, const std::string &archive_path, std::string *err) {
    FILE *f = fopen(archive_path.c_str(), "rb");
    if (!f) {
        if (err) *err = util::format("无法打开 %s", archive_path.c_str());
        return false;
    }

    char magic[6];
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, kLegacyMagic, sizeof(magic)) != 0) {
        fclose(f);
        if (err) *err = "无效的旧版归档";
        return false;
    }

    u32 meta_len = 0;
    if (!readU32le(f, &meta_len) || meta_len > kLegacyMaxMetaLen) {
        fclose(f);
        if (err) *err = "无效的旧版归档（元数据）";
        return false;
    }
    std::string meta(meta_len, '\0');
    if (meta_len > 0 && fread(&meta[0], 1, meta_len, f) != meta_len) {
        fclose(f);
        if (err) *err = "无效的旧版归档（元数据截断）";
        return false;
    }

    long start = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, start, SEEK_SET);
    if (start < 0 || end < start) {
        fclose(f);
        if (err) *err = "无效的旧版归档";
        return false;
    }

    long pos = start;
    while (pos < end) {
        u32 name_len = 0;
        u64 size = 0;
        if (!readU32le(f, &name_len) || name_len == 0 || name_len > kLegacyMaxNameLen) {
            fclose(f);
            if (err) *err = "无效的旧版归档（文件名）";
            return false;
        }
        std::string name(name_len, '\0');
        if (fread(&name[0], 1, name_len, f) != name_len || !safeRelPath(name)) {
            fclose(f);
            if (err) *err = "无效的旧版归档（路径）";
            return false;
        }
        if (!readU64le(f, &size) || size > kMaxFileSize) {
            fclose(f);
            if (err) *err = "无效的旧版归档（大小）";
            return false;
        }
        long cur = ftell(f);
        if (cur < 0 || (long long)size > (long long)(end - cur)) {
            fclose(f);
            if (err) *err = "无效的旧版归档（数据截断）";
            return false;
        }
        if (fseek(f, (long)size, SEEK_CUR) != 0) {
            fclose(f);
            if (err) *err = "无效的旧版归档";
            return false;
        }
        pos = ftell(f);
    }

    if (!wipeSaveData(fs, err)) {
        fclose(f);
        return false;
    }

    fseek(f, start, SEEK_SET);
    pos = start;
    int restored = 0;
    bool ok = true;
    while (pos < end && ok) {
        u32 name_len = 0;
        u64 size = 0;
        readU32le(f, &name_len);
        std::string name(name_len, '\0');
        fread(&name[0], 1, name_len, f);
        readU64le(f, &size);

        if (!createFileForRestore(fs, name, size, false, err)) {
            ok = false;
            break;
        }
        if (size > 0) {
            FsFile out;
            Result rc = fsFsOpenFile(fs, ("/" + name).c_str(), FsOpenMode_Write, &out);
            if (R_FAILED(rc)) {
                if (err) *err = util::format("打开 %s 失败 (0x%x)", name.c_str(), rc);
                ok = false;
                break;
            }
            u64 remaining = size;
            s64 offset = 0;
            while (remaining > 0) {
                size_t chunk = (size_t)(remaining < sizeof(g_copy_buf) ? remaining
                                                                      : sizeof(g_copy_buf));
                size_t got = fread(g_copy_buf, 1, chunk, f);
                if (got == 0) {
                    if (err) *err = "旧版归档被截断";
                    ok = false;
                    break;
                }
                rc = fsFileWrite(&out, offset, g_copy_buf, got, FsWriteOption_None);
                if (R_FAILED(rc)) {
                    if (err) *err = util::format("写入 %s 失败 (0x%x)", name.c_str(), rc);
                    ok = false;
                    break;
                }
                offset += (s64)got;
                remaining -= got;
            }
            fsFileClose(&out);
            if (!ok) break;
        }
        ++restored;
        pos = ftell(f);
    }
    fclose(f);
    if (!ok) return false;

    Result rc = fsFsCommit(fs);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("fsFsCommit 失败 (0x%x)", rc);
        return false;
    }
    Log::printf("  已恢复 %d 个文件（旧格式）", restored);
    return true;
}

// ---------------------------------------------------------------------------
// ZIP restore
// ---------------------------------------------------------------------------

bool readZipEntry(unzFile uf, std::string *name, unz_file_info *info) {
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    if (unzGetCurrentFileInfo(uf, info, buf, sizeof(buf), nullptr, 0, nullptr, 0) != UNZ_OK) {
        return false;
    }
    *name = std::string(buf);
    return true;
}

bool restoreZipArchive(FsFileSystem *fs, const std::string &archive_path, std::string *err) {
    unzFile uf = unzOpen(archive_path.c_str());
    if (!uf) {
        if (err) *err = "无法打开 ZIP 归档";
        return false;
    }

    std::map<std::string, bool> concat;
    std::vector<std::string> dirs;
    int entry_count = 0;

    // Pass 1: validate everything and read the concatenation index.
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            std::string name;
            unz_file_info info;
            memset(&info, 0, sizeof(info));
            if (!readZipEntry(uf, &name, &info)) {
                unzClose(uf);
                if (err) *err = "ZIP 条目损坏";
                return false;
            }

            if (name == kIndexEntry) {
                if (unzOpenCurrentFile(uf) != UNZ_OK) {
                    unzClose(uf);
                    if (err) *err = "无法读取 ZIP 索引";
                    return false;
                }
                std::string body;
                char tmp[4096];
                int n = 0;
                while ((n = unzReadCurrentFile(uf, tmp, sizeof(tmp))) > 0) body.append(tmp, n);
                unzCloseCurrentFile(uf);

                std::vector<std::string> lines = util::split(body, '\n');
                for (size_t i = 0; i < lines.size(); ++i) {
                    std::string line = util::trim(lines[i]);
                    if (line.empty()) continue;
                    size_t tab = line.find('\t');
                    if (tab == std::string::npos) continue;
                    std::string path = line.substr(0, tab);
                    if (!safeRelPath(path)) continue;
                    concat[path] = util::trim(line.substr(tab + 1)) == "1";
                }
                continue;
            }
            if (name == kMetaEntry) continue;

            if (!name.empty() && name[name.size() - 1] == '/') {
                std::string dir = name.substr(0, name.size() - 1);
                if (!dir.empty() && safeRelPath(dir)) dirs.push_back(dir);
                continue;
            }
            if (!safeRelPath(name)) {
                unzClose(uf);
                if (err) *err = util::format("ZIP 中包含非法路径: %s", name.c_str());
                return false;
            }
            if (info.uncompressed_size > kMaxFileSize) {
                unzClose(uf);
                if (err) *err = util::format("ZIP 条目过大: %s", name.c_str());
                return false;
            }
            ++entry_count;
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }

    Log::printf("  ZIP OK: %d 个文件, %u 个目录, %u 个拼接文件", entry_count,
                (unsigned)dirs.size(), (unsigned)concat.size());

    if (!wipeSaveData(fs, err)) {
        unzClose(uf);
        return false;
    }

    // Pass 2: extract.
    int restored = 0;
    bool ok = true;
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            std::string name;
            unz_file_info info;
            memset(&info, 0, sizeof(info));
            if (!readZipEntry(uf, &name, &info)) {
                ok = false;
                if (err) *err = "ZIP 条目损坏";
                break;
            }
            if (name == kMetaEntry || name == kIndexEntry) continue;
            if (!name.empty() && name[name.size() - 1] == '/') {
                std::string dir = name.substr(0, name.size() - 1);
                if (!dir.empty()) ensureFsDirs(fs, dir + "/x");
                continue;
            }

            bool is_concat = concat.find(name) != concat.end() && concat[name];
            u64 size = (u64)info.uncompressed_size;
            if (!createFileForRestore(fs, name, size, is_concat, err)) {
                ok = false;
                break;
            }
            if (unzOpenCurrentFile(uf) != UNZ_OK) {
                if (err) *err = util::format("无法解压 %s", name.c_str());
                ok = false;
                break;
            }

            FsFile out;
            Result rc = fsFsOpenFile(fs, ("/" + name).c_str(), FsOpenMode_Write, &out);
            if (R_FAILED(rc)) {
                unzCloseCurrentFile(uf);
                if (err) *err = util::format("打开 %s 失败 (0x%x)", name.c_str(), rc);
                ok = false;
                break;
            }

            s64 offset = 0;
            for (;;) {
                int n = unzReadCurrentFile(uf, g_copy_buf, sizeof(g_copy_buf));
                if (n < 0) {
                    if (err) *err = util::format("解压 %s 失败 (%d)", name.c_str(), n);
                    ok = false;
                    break;
                }
                if (n == 0) break;
                if ((u64)offset + (u64)n > size) {
                    size = (u64)offset + (u64)n;
                    fsFileSetSize(&out, (s64)size);
                }
                rc = fsFileWrite(&out, offset, g_copy_buf, (u64)n, FsWriteOption_None);
                if (R_FAILED(rc)) {
                    if (err) *err = util::format("写入 %s 失败 (0x%x)", name.c_str(), rc);
                    ok = false;
                    break;
                }
                offset += n;
                if (!UI::pump()) {
                    if (err) *err = "已取消";
                    ok = false;
                    break;
                }
            }
            fsFileClose(&out);
            unzCloseCurrentFile(uf);
            if (!ok) break;
            ++restored;
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }

    unzClose(uf);
    if (!ok) return false;

    Result rc = fsFsCommit(fs);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("fsFsCommit 失败 (0x%x)", rc);
        return false;
    }
    Log::printf("  已恢复 %d 个文件", restored);
    return true;
}

}  // namespace

std::string SaveEntry::userIdStr() const {
    if (!isAccount()) return std::string("0");
    return util::accountUidHex(uid);
}

bool enumerateSaves(std::vector<SaveEntry> *out, std::string *err) {
    out->clear();

    const FsSaveDataSpaceId spaces[] = {FsSaveDataSpaceId_User, FsSaveDataSpaceId_SdUser};
    bool any_reader_opened = false;

    for (size_t s = 0; s < sizeof(spaces) / sizeof(spaces[0]); ++s) {
        FsSaveDataInfoReader reader;
        Result rc = fsOpenSaveDataInfoReader(&reader, spaces[s]);
        if (R_FAILED(rc)) {
            if (s == 0 && err) {
                *err = util::format("无法枚举存档 (0x%x)", rc);
            }
            continue;
        }
        any_reader_opened = true;

        FsSaveDataInfo infos[16];
        for (;;) {
            s64 total = 0;
            rc = fsSaveDataInfoReaderRead(&reader, infos, 16, &total);
            if (R_FAILED(rc) || total <= 0) break;

            for (s64 i = 0; i < total; ++i) {
                const FsSaveDataInfo &info = infos[i];
                u8 type = info.save_data_type;
                if (type != FsSaveDataType_Account && type != FsSaveDataType_Device) continue;
                if (info.application_id == 0) continue;

                bool duplicate = false;
                for (size_t j = 0; j < out->size(); ++j) {
                    const SaveEntry &e = (*out)[j];
                    if (e.app_id == info.application_id && e.type == type &&
                        e.index == info.save_data_index &&
                        memcmp(&e.uid, &info.uid, sizeof(AccountUid)) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                SaveEntry e;
                e.save_data_id = info.save_data_id;
                e.space_id = info.save_data_space_id;
                e.type = type;
                e.rank = info.save_data_rank;
                e.index = info.save_data_index;
                e.app_id = info.application_id;
                e.system_save_data_id = info.system_save_data_id;
                e.uid = info.uid;
                e.size = info.size;
                out->push_back(e);
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }

    if (!any_reader_opened && err && err->empty()) {
        *err = "无法打开任何存档枚举器";
        return false;
    }
    return true;
}

bool openSaveData(const SaveEntry &entry, FsFileSystem *fs, std::string *err) {
    FsSaveDataAttribute attr;
    memset(&attr, 0, sizeof(attr));
    attr.application_id = entry.app_id;
    attr.uid = entry.uid;
    attr.system_save_data_id = entry.system_save_data_id;
    attr.save_data_type = entry.type;
    attr.save_data_rank = entry.rank;
    attr.save_data_index = entry.index;

    Result rc = fsOpenSaveDataFileSystem(fs, (FsSaveDataSpaceId)entry.space_id, &attr);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("挂载存档失败 0x%x（游戏是否在运行？）", rc);
        return false;
    }
    return true;
}

void closeSaveData(FsFileSystem *fs) { fsFsClose(fs); }

bool readSaveExtraData(const SaveEntry &entry, FsSaveDataExtraData *out, std::string *err) {
    memset(out, 0, sizeof(*out));
    Result rc = fsReadSaveDataFileSystemExtraData(out, sizeof(*out), entry.save_data_id);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("extra data 0x%x", rc);
        return false;
    }
    return true;
}

bool buildSaveArchive(FsFileSystem *fs, const SaveEntry &entry, const std::string &title_name,
                      const std::string &out_path, std::string *sha_hex, long long *out_zip_bytes,
                      long long *out_raw_bytes, std::string *err) {
    if (sha_hex) sha_hex->clear();
    if (out_zip_bytes) *out_zip_bytes = 0;
    if (out_raw_bytes) *out_raw_bytes = 0;

    WalkResult walk;
    if (!walkSaveData(fs, &walk, err)) return false;

    util::ensureParentDirs(out_path);
    zipFile zf = zipOpen(out_path.c_str(), APPEND_STATUS_CREATE);
    if (!zf) {
        if (err) *err = util::format("无法创建 %s", out_path.c_str());
        return false;
    }

    zip_fileinfo zi;
    memset(&zi, 0, sizeof(zi));

    auto addText = [&](const char *name, const std::string &text) -> bool {
        if (zipOpenNewFileInZip(zf, name, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
                                Z_BEST_SPEED) != ZIP_OK) {
            return false;
        }
        if (!text.empty() &&
            zipWriteInFileInZip(zf, text.data(), (unsigned)text.size()) != ZIP_OK) {
            return false;
        }
        return zipCloseFileInZip(zf) == ZIP_OK;
    };

    std::string clean_name = title_name;
    for (size_t i = 0; i < clean_name.size(); ++i) {
        if (clean_name[i] == '\n' || clean_name[i] == '\r' || clean_name[i] == '\t') {
            clean_name[i] = ' ';
        }
    }

    std::string meta = util::format(
        "format=switchcloud-zip1\ntitle_id=%s\nuser_id=%s\ntitle_name=%s\nsave_data_type=%u\n"
        "file_count=%u\n",
        util::toHexU64(entry.app_id, 16).c_str(), entry.userIdStr().c_str(), clean_name.c_str(),
        (unsigned)entry.type, (unsigned)walk.files.size());

    std::string index;
    for (size_t i = 0; i < walk.files.size(); ++i) {
        index += walk.files[i].path;
        index += walk.files[i].concat ? "\t1\n" : "\t0\n";
    }

    bool ok = addText(kMetaEntry, meta) && addText(kIndexEntry, index);

    for (size_t i = 0; ok && i < walk.dirs.size(); ++i) {
        std::string name = walk.dirs[i] + "/";
        if (zipOpenNewFileInZip(zf, name.c_str(), &zi, nullptr, 0, nullptr, 0, nullptr, 0, 0) !=
            ZIP_OK) {
            ok = false;
            break;
        }
        if (zipCloseFileInZip(zf) != ZIP_OK) ok = false;
    }

    u64 raw_total = 0;
    for (size_t i = 0; ok && i < walk.files.size(); ++i) {
        const FileRef &ref = walk.files[i];
        if (!UI::pump()) {
            if (err) *err = "已取消";
            ok = false;
            break;
        }
        UI::progressPercent((int)((i + 1) * 100 / walk.files.size()));

        std::string full = "/" + ref.path;
        FsFile file;
        Result rc = fsFsOpenFile(fs, full.c_str(), FsOpenMode_Read, &file);
        if (R_FAILED(rc)) {
            if (err) *err = util::format("无法打开 %s (0x%x)", ref.path.c_str(), rc);
            ok = false;
            break;
        }
        s64 actual = 0;
        rc = fsFileGetSize(&file, &actual);
        if (R_FAILED(rc) || actual < 0) {
            fsFileClose(&file);
            if (err) *err = util::format("无法读取 %s 大小 (0x%x)", ref.path.c_str(), rc);
            ok = false;
            break;
        }
        if (!ref.concat && ref.dir_size > 0 && (u64)actual != ref.dir_size) {
            fsFileClose(&file);
            if (err) *err = util::format("%s 在备份过程中发生变化，请重试", ref.path.c_str());
            ok = false;
            break;
        }
        raw_total += (u64)actual;

        if (zipOpenNewFileInZip(zf, ref.path.c_str(), &zi, nullptr, 0, nullptr, 0, nullptr,
                                Z_DEFLATED, Z_BEST_SPEED) != ZIP_OK) {
            fsFileClose(&file);
            if (err) *err = util::format("无法写入 ZIP 条目 %s", ref.path.c_str());
            ok = false;
            break;
        }

        s64 offset = 0;
        size_t chunk_count = 0;
        while (offset < actual) {
            s64 left = actual - offset;
            size_t want = (size_t)(left < (s64)sizeof(g_copy_buf) ? left : (s64)sizeof(g_copy_buf));
            u64 got = 0;
            rc = fsFileRead(&file, offset, g_copy_buf, want, FsReadOption_None, &got);
            if (R_FAILED(rc) || got == 0) {
                if (err) *err = util::format("读取 %s 失败 (0x%x)", ref.path.c_str(), rc);
                ok = false;
                break;
            }
            if (zipWriteInFileInZip(zf, g_copy_buf, (unsigned)got) != ZIP_OK) {
                if (err) *err = "写入 ZIP 失败（SD 卡空间不足？）";
                ok = false;
                break;
            }
            offset += (s64)got;
            if ((++chunk_count % 64) == 0 && !UI::pump()) {
                if (err) *err = "已取消";
                ok = false;
                break;
            }
        }
        fsFileClose(&file);
        if (!ok || zipCloseFileInZip(zf) != ZIP_OK) {
            if (ok && err) *err = "写入 ZIP 失败（SD 卡空间不足？）";
            ok = false;
            break;
        }
    }

    if (zipClose(zf, nullptr) != ZIP_OK) {
        if (ok && err) *err = "关闭 ZIP 失败";
        ok = false;
    }

    if (!ok) {
        remove(out_path.c_str());
        return false;
    }

    long long zip_size = 0;
    FILE *f = fopen(out_path.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        zip_size = (long long)ftell(f);
        fclose(f);
    }

    if (sha_hex) *sha_hex = sha256File(out_path.c_str());
    if (out_zip_bytes) *out_zip_bytes = zip_size;
    if (out_raw_bytes) *out_raw_bytes = (long long)raw_total;
    return true;
}

bool restoreSaveArchive(FsFileSystem *fs, const std::string &archive_path, std::string *err) {
    FILE *f = fopen(archive_path.c_str(), "rb");
    if (!f) {
        if (err) *err = util::format("无法打开 %s", archive_path.c_str());
        return false;
    }
    u8 magic[6];
    size_t n = fread(magic, 1, sizeof(magic), f);
    fclose(f);

    if (n >= sizeof(kLegacyMagic) && memcmp(magic, kLegacyMagic, sizeof(kLegacyMagic)) == 0) {
        return restoreLegacyArchive(fs, archive_path, err);
    }
    if (n >= 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4) {
        return restoreZipArchive(fs, archive_path, err);
    }
    if (err) *err = "未知的归档格式";
    return false;
}

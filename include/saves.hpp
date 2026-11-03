#pragma once

#include "common.hpp"

struct SaveEntry {
    u64 save_data_id = 0;
    u8 space_id = 0;
    u8 type = 0;
    u8 rank = 0;
    u16 index = 0;
    u64 app_id = 0;
    u64 system_save_data_id = 0;
    AccountUid uid{};
    u64 size = 0;

    bool isAccount() const { return type == FsSaveDataType_Account; }
    std::string userIdStr() const;
};

bool enumerateSaves(std::vector<SaveEntry> *out, std::string *err);

bool openSaveData(const SaveEntry &entry, FsFileSystem *fs, std::string *err);
void closeSaveData(FsFileSystem *fs);

/// Reads the save data extra data (commit id + timestamp), used to skip unchanged saves.
bool readSaveExtraData(const SaveEntry &entry, FsSaveDataExtraData *out, std::string *err);

/// Writes a ZIP archive of the mounted save data (deflate). `sha_hex` is the SHA-256 of the file,
/// `out_zip_bytes` the size on disk and `out_raw_bytes` the uncompressed total.
bool buildSaveArchive(FsFileSystem *fs, const SaveEntry &entry, const std::string &title_name,
                      const std::string &out_path, std::string *sha_hex, long long *out_zip_bytes,
                      long long *out_raw_bytes, std::string *err);

/// Replaces the content of the mounted save data with the archive and commits it.
bool restoreSaveArchive(FsFileSystem *fs, const std::string &archive_path, std::string *err);

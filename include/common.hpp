#pragma once

#include <switch.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define APP_VERSION "1.1.0"
#define APP_NAME "Switch 云存档"
#define APP_USER_AGENT "SwitchCloudSync/" APP_VERSION " (libnx)"

#define SD_ROOT "sdmc:/switch/cloud-sync"
#define SD_CONFIG SD_ROOT "/config.ini"
#define SD_LOG_FILE SD_ROOT "/logs/sync.log"
#define SD_STATE_DIR SD_ROOT "/state"
#define SD_STATE_DEVICE SD_STATE_DIR "/device_id.txt"
#define SD_STATE_SAVES SD_STATE_DIR "/saves.tsv"
#define SD_STATE_ALBUM SD_STATE_DIR "/album.tsv"
#define SD_STATE_META SD_STATE_DIR "/meta.ini"
#define SD_TMP_DIR SD_ROOT "/tmp"
#define SD_CA_BUNDLE SD_ROOT "/ca-bundle.pem"

/// Application ids below this value belong to applets / system titles, not games.
#define APP_ID_GAME_MIN 0x0100000000003000ULL

/// Shared controller state. Only touched from the main thread.
extern PadState g_pad;
/// Set when the user aborts the current operation (B during a transfer, etc).
extern volatile bool g_abort;
/// Set when the application has to exit (HOME button, hbmenu).
extern volatile bool g_exit;

struct SyncStats {
    int total = 0;
    int uploaded = 0;
    int skipped = 0;
    int failed = 0;
};

/// One save entry as reported by the server (used by the restore picker).
struct RemoteSave {
    std::string title_id;
    std::string user_id;
    std::string sha256;
    std::string title_name;
    long long timestamp = 0;
    long long size = 0;
};

namespace util {

std::string format(const char *fmt, ...);
std::string format(const std::string &fmt, ...);
std::string trim(const std::string &s);
std::string toLower(std::string s);
std::string toUpper(std::string s);
bool startsWith(const std::string &s, const char *prefix);
bool endsWithNoCase(const std::string &s, const char *suffix);

std::string accountUidHex(const AccountUid &uid);
std::string toHexU64(u64 v, int digits);
std::string normHexId(const std::string &id);
std::string padHexId(const std::string &id, size_t width);
bool parseU64(const std::string &s, u64 *out);

std::string urlEncode(const std::string &in);
std::string hexEncode(const void *data, size_t len);
std::string jsonEscape(const std::string &in);

std::string formatBytes(long long bytes);
std::string formatDuration(u64 seconds);
std::string formatDurationShort(u64 seconds);
std::string formatUnixTime(long long unix_seconds);
long long nowUnix();

bool ensureDir(const char *path);
bool ensureParentDirs(const std::string &path);
bool readFile(const char *path, std::string *out);
bool writeFile(const char *path, const std::string &data);

std::vector<std::string> split(const std::string &s, char sep);
std::string joinPath(const std::string &a, const std::string &b);
std::string baseName(const std::string &p);

std::string ellipsizeUtf8(const std::string &s, size_t max_bytes);

}  // namespace util

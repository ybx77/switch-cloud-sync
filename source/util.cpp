#include "common.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace util {

std::string format(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c - 'A' + 'a');
    }
    return s;
}

std::string toUpper(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') s[i] = (char)(c - 'a' + 'A');
    }
    return s;
}

bool startsWith(const std::string &s, const char *prefix) {
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool endsWithNoCase(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    if (s.size() < n) return false;
    return toLower(s.substr(s.size() - n)) == toLower(std::string(suffix));
}

std::string accountUidHex(const AccountUid &uid) {
    return format("%016llx%016llx", (unsigned long long)uid.uid[0],
                  (unsigned long long)uid.uid[1]);
}

std::string toHexU64(u64 v, int digits) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*llx", digits, (unsigned long long)v);
    return std::string(buf);
}

std::string normHexId(const std::string &id) {
    std::string s = toLower(trim(id));
    if (s.size() > 2 && s[0] == '0' && s[1] == 'x') s = s.substr(2);
    return s;
}

std::string padHexId(const std::string &id, size_t width) {
    std::string s = normHexId(id);
    if (s.size() >= width) return s;
    return std::string(width - s.size(), '0') + s;
}

bool parseU64(const std::string &s, u64 *out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    int base = 10;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) base = 16;
    errno = 0;
    char *end = nullptr;
    unsigned long long v = strtoull(t.c_str(), &end, base);
    if (errno != 0 || end == t.c_str() || (end && *end != '\0')) return false;
    *out = (u64)v;
    return true;
}

std::string urlEncode(const std::string &in) {
    static const char *kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string hexEncode(const void *data, size_t len) {
    static const char *kHex = "0123456789abcdef";
    const u8 *p = (const u8 *)data;
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[p[i] >> 4]);
        out.push_back(kHex[p[i] & 0x0F]);
    }
    return out;
}

std::string jsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += format("\\u%04x", (unsigned)c);
                } else {
                    out.push_back((char)c);
                }
                break;
        }
    }
    return out;
}

std::string formatBytes(long long bytes) {
    if (bytes < 0) return "-";
    double v = (double)bytes;
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    if (u == 0) return format("%lld B", bytes);
    return format("%.1f %s", v, units[u]);
}

std::string formatDuration(u64 seconds) {
    u64 h = seconds / 3600;
    u64 m = (seconds % 3600) / 60;
    if (h > 0) return format("%llu 小时 %02llu 分", (unsigned long long)h,
                             (unsigned long long)m);
    return format("%llu 分钟", (unsigned long long)m);
}

std::string formatDurationShort(u64 seconds) {
    u64 h = seconds / 3600;
    u64 m = (seconds % 3600) / 60;
    if (h > 0) return format("%lluh%02llum", (unsigned long long)h, (unsigned long long)m);
    return format("%llum", (unsigned long long)m);
}

std::string formatUnixTime(long long unix_seconds) {
    time_t t = (time_t)unix_seconds;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    if (gmtime_r(&t, &tmv) == nullptr) return "?";
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return std::string(buf);
}

long long nowUnix() {
    return (long long)time(nullptr);
}

bool ensureDir(const char *path) {
    if (mkdir(path, 0777) == 0) return true;
    return errno == EEXIST;
}

bool ensureParentDirs(const std::string &path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            ensureDir(path.substr(0, i).c_str());
        }
    }
    return true;
}

bool readFile(const char *path, std::string *out) {
    out->clear();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out->append(buf, n);
    }
    fclose(f);
    return true;
}

bool writeFile(const char *path, const std::string &data) {
    ensureParentDirs(path);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t written = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    bool ok = written == data.size();
    fclose(f);
    return ok;
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a[a.size() - 1] == '/') return a + b;
    return a + "/" + b;
}

std::string baseName(const std::string &p) {
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

std::string ellipsizeUtf8(const std::string &s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t cut = max_bytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "...";
}

}  // namespace util

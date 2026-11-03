#include "i18n.hpp"
#include "log.hpp"

#include <sys/stat.h>
#include <unistd.h>

namespace {

FILE *g_log = nullptr;
unsigned g_line_count = 0;
std::deque<std::string> g_recent;
const size_t kMaxRecent = 200;

void writeLine(const char *line) {
    g_recent.push_back(std::string(line));
    while (g_recent.size() > kMaxRecent) g_recent.pop_front();

    if (g_log) {
        time_t t = time(nullptr);
        struct tm tmv;
        memset(&tmv, 0, sizeof(tmv));
        gmtime_r(&t, &tmv);
        fprintf(g_log, "[%02d:%02d:%02d] %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, line);
        if ((++g_line_count % 16) == 0) fflush(g_log);
    }
}

}  // namespace

namespace Log {

void init() {
    util::ensureDir(SD_ROOT);
    util::ensureDir(SD_ROOT "/logs");

    struct stat st;
    if (stat(SD_LOG_FILE, &st) == 0 && st.st_size > 1024 * 1024) {
        unlink(SD_LOG_FILE ".1");
        rename(SD_LOG_FILE, SD_LOG_FILE ".1");
    }

    g_log = fopen(SD_LOG_FILE, "ab");
    printf("%s %s", APP_NAME, APP_VERSION);
    printf(T("日志文件: %s"), SD_LOG_FILE);
}

void close() {
    if (g_log) {
        fflush(g_log);
        fclose(g_log);
        g_log = nullptr;
    }
}

void printf(const char *fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    writeLine(line);
}

void printf(const std::string &fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt.c_str(), ap);
    va_end(ap);
    writeLine(line);
}

const std::deque<std::string> &recent() { return g_recent; }

void clearRecent() { g_recent.clear(); }

}  // namespace Log

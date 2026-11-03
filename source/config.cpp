#include "config.hpp"

namespace {

std::string stripLine(const std::string &raw) {
    std::string s = util::trim(raw);
    if (s.empty() || s[0] == '#' || s[0] == ';') return std::string();

    for (size_t i = 1; i < s.size(); ++i) {
        if ((s[i] == '#' || s[i] == ';') && (s[i - 1] == ' ' || s[i - 1] == '\t')) {
            s = util::trim(s.substr(0, i));
            break;
        }
    }
    return s;
}

bool asBool(const std::string &v) {
    std::string s = util::toLower(util::trim(v));
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

long asLong(const std::string &v, long fallback) {
    std::string s = util::trim(v);
    if (s.empty()) return fallback;
    char *end = nullptr;
    long value = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || (end && *end != '\0')) return fallback;
    return value;
}

std::string get(const std::map<std::string, std::string> &kv, const char *key,
                const std::string &fallback = std::string()) {
    std::map<std::string, std::string>::const_iterator it = kv.find(key);
    if (it == kv.end()) return fallback;
    return it->second;
}

const char *b(bool v) { return v ? "1" : "0"; }

}  // namespace

bool Config::load(std::string *err) {
    std::string text;
    if (!util::readFile(SD_CONFIG, &text)) {
        if (!writeDefault(err)) return false;
        if (err) *err = util::format("no config found, created %s", SD_CONFIG);
        return false;
    }

    if (text.size() >= 3 && (u8)text[0] == 0xEF && (u8)text[1] == 0xBB && (u8)text[2] == 0xBF) {
        text = text.substr(3);
    }

    std::map<std::string, std::string> kv;
    std::string section;
    std::vector<std::string> lines = util::split(text, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string s = stripLine(lines[i]);
        if (s.empty()) continue;
        if (s[0] == '[') {
            size_t close = s.find(']');
            section = close == std::string::npos ? std::string() : util::trim(s.substr(1, close - 1));
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = util::trim(s.substr(0, eq));
        std::string value = util::trim(s.substr(eq + 1));
        if (key.empty()) continue;
        kv[(section.empty() ? std::string() : section + ".") + key] = value;
    }

    base_url = get(kv, "server.base_url");
    while (!base_url.empty() && base_url[base_url.size() - 1] == '/') {
        base_url.erase(base_url.size() - 1);
    }
    token = get(kv, "server.token");
    ca_bundle = get(kv, "server.ca_bundle");
    insecure_tls = asBool(get(kv, "server.insecure_tls", "0"));
    timeout_sec = asLong(get(kv, "server.timeout_sec", "60"), 60);
    if (timeout_sec < 5) timeout_sec = 5;

    device_id_config = get(kv, "device.id");
    device_name = get(kv, "device.name", "switch");

    saves_enabled = asBool(get(kv, "saves.enabled", "1"));
    games_only = asBool(get(kv, "saves.games_only", "1"));
    max_upload_mb = asLong(get(kv, "saves.max_upload_mb", "256"), 256);
    if (max_upload_mb <= 0) max_upload_mb = 256;

    only_changed = asBool(get(kv, "sync.only_changed", "1"));

    album_enabled = asBool(get(kv, "album.enabled", "1"));
    album_delete_after_upload = asBool(get(kv, "album.delete_after_upload", "0"));

    playtime_enabled = asBool(get(kv, "playtime.enabled", "1"));
    playtime_detailed = asBool(get(kv, "playtime.detailed", "1"));

    return true;
}

bool Config::writeDefault(std::string *err) {
    Config defaults;
    defaults.device_name = "my-switch";
    return defaults.save(err);
}

bool Config::save(std::string *err) const {
    std::string text;
    text += "# Switch 云存档同步 / Switch Cloud Sync\n";
    text += "# 修改后可在应用的「设置」里直接编辑，或改完选 Reload。\n";
    text += "\n[server]\n";
    text += "# 服务器地址，结尾不要带斜杠。例: https://cloud.example.com\n";
    text += "base_url = " + base_url + "\n";
    text += "\n# 以 Authorization: Bearer <token> 发送\n";
    text += "token = " + token + "\n";
    text += "\n# 自签 HTTPS 用的 CA PEM；留空时自动使用 sdmc:/switch/cloud-sync/ca-bundle.pem\n";
    text += "ca_bundle = " + ca_bundle + "\n";
    text += "\n# 1 = 跳过 TLS 证书校验（仅内网调试）\n";
    text += util::format("insecure_tls = %s\n", b(insecure_tls));
    text += util::format("timeout_sec = %ld\n", timeout_sec);
    text += "\n[device]\n";
    text += "# 留空则自动生成并保存在 state/device_id.txt\n";
    text += "id = " + device_id_config + "\n";
    text += "name = " + device_name + "\n";
    text += "\n[saves]\n";
    text += util::format("enabled = %s\n", b(saves_enabled));
    text += "# 1 = 跳过系统 applet（应用 ID 低于 0x0100000000003000）\n";
    text += util::format("games_only = %s\n", b(games_only));
    text += "# 单个存档归档的上传上限（MB）\n";
    text += util::format("max_upload_mb = %ld\n", max_upload_mb);
    text += "\n[sync]\n";
    text += "# 1 = 只上传有变化的存档 / 相册文件（推荐）\n";
    text += util::format("only_changed = %s\n", b(only_changed));
    text += "\n[album]\n";
    text += util::format("enabled = %s\n", b(album_enabled));
    text += "# 0 = 上传后保留本地文件（推荐）\n";
    text += util::format("delete_after_upload = %s\n", b(album_delete_after_upload));
    text += "\n[playtime]\n";
    text += util::format("enabled = %s\n", b(playtime_enabled));
    text += "# 1 = 同时上传详细游玩事件（启动/退出/焦点/账号切换）\n";
    text += util::format("detailed = %s\n", b(playtime_detailed));

    if (!util::writeFile(SD_CONFIG, text)) {
        if (err) *err = util::format("cannot write %s", SD_CONFIG);
        return false;
    }
    return true;
}

std::string Config::api(const std::string &path) const {
    std::string base = base_url;
    while (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
    std::string p = path;
    if (!p.empty() && p[0] != '/') p = "/" + p;
    return base + "/api/v1" + p;
}

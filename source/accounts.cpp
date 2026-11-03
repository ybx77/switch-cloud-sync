#include "accounts.hpp"

#include "log.hpp"

namespace {

bool g_ready = false;

}  // namespace

bool ensureAccount() {
    if (g_ready) return true;
    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) {
        Log::printf("accountInitialize failed (0x%x)", rc);
        return false;
    }
    g_ready = true;
    return true;
}

void shutdownAccount() {
    if (g_ready) {
        accountExit();
        g_ready = false;
    }
}

std::string accountUserName(const AccountUid &uid) {
    if (!ensureAccount()) return std::string();

    AccountProfile profile;
    if (R_FAILED(accountGetProfile(&profile, uid))) return std::string();

    AccountUserData userdata;
    AccountProfileBase base;
    memset(&userdata, 0, sizeof(userdata));
    memset(&base, 0, sizeof(base));

    std::string name;
    if (R_SUCCEEDED(accountProfileGet(&profile, &userdata, &base))) {
        name = util::trim(
            std::string(base.nickname, strnlen(base.nickname, sizeof(base.nickname))));
    }
    accountProfileClose(&profile);
    return name;
}

std::string userIdLabel(const std::string &uid_hex) {
    if (uid_hex.empty() || uid_hex == "0") return "设备存档";

    std::string short_id = uid_hex.size() > 6 ? uid_hex.substr(uid_hex.size() - 6) : uid_hex;
    std::string fallback = "用户 " + short_id;

    if (uid_hex.size() < 32) return fallback;

    AccountUid uid;
    memset(&uid, 0, sizeof(uid));
    uid.uid[0] = strtoull(uid_hex.substr(0, 16).c_str(), nullptr, 16);
    uid.uid[1] = strtoull(uid_hex.substr(16, 16).c_str(), nullptr, 16);

    std::string name = accountUserName(uid);
    return name.empty() ? fallback : name;
}

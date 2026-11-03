#pragma once

#include "common.hpp"

/// Shared account service (kept initialized for the app lifetime).
bool ensureAccount();
void shutdownAccount();

/// Nickname for a local profile, or an empty string.
std::string accountUserName(const AccountUid &uid);

/// "设备存档" for user "0", the nickname when available, else "用户 xxxxxx".
std::string userIdLabel(const std::string &uid_hex);

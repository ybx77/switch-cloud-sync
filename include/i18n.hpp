#pragma once

#include "common.hpp"

enum class Lang {
    Zh,
    En,
};

/// setting: "auto" (follow the system language), "zh" or "en".
void i18nInit(const std::string &setting);
Lang i18nCurrent();
std::string i18nSettingForDisplay();  // "auto" / "zh" / "en"

/// Translates a Chinese source string to English when the UI language is English.
/// Unknown strings fall back to the Chinese text.
std::string T(const char *zh);

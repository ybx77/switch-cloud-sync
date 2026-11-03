#pragma once

#include "common.hpp"

/// UTF-8 text rendering using FreeType + the console shared fonts (CJK included).
namespace Font {

bool init(std::string *err);
void shutdown();

int lineHeight(int px);
int measure(const std::string &utf8, int px);
int drawText(int x, int y, const std::string &utf8, int px, u32 color);
/// Draws at most max_lines lines, breaking on '\n' and at max_width.
int drawWrapped(int x, int y, int max_width, const std::string &utf8, int px, u32 color,
                int max_lines);
std::string ellipsize(const std::string &utf8, int px, int max_width);

}  // namespace Font

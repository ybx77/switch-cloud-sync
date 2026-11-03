#pragma once

#include "common.hpp"

/// Minimal 2D renderer on top of the libnx framebuffer (RGBA8888).
namespace Gfx {

const int W = 1280;
const int H = 720;

bool init(std::string *err);
void shutdown();

void begin();
void end();

inline u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

void clear(u32 color);
void fillRect(int x, int y, int w, int h, u32 color);
void fillRectAlpha(int x, int y, int w, int h, u32 color, u8 alpha);
void fillRoundRect(int x, int y, int w, int h, int radius, u32 color);
void fillRoundRectBorder(int x, int y, int w, int h, int radius, int thickness, u32 fill,
                         u32 border);
void fillCircle(int cx, int cy, int radius, u32 color);
void drawLine(int x0, int y0, int x1, int y1, int thickness, u32 color);
/// Alpha-blends an 8-bit glyph mask.
void drawMask(int x, int y, int w, int h, const u8 *mask, int pitch, u32 color);
/// Semi-transparent full screen dimmer.
void dim(u8 alpha);

u64 nowMs();

}  // namespace Gfx

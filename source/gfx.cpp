#include "gfx.hpp"

#include <cmath>

namespace {

Framebuffer g_fb;
u32 *g_pixels = nullptr;
u32 g_stride = 0;  // in pixels
bool g_ready = false;

inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline void putPixel(int x, int y, u32 color) {
    if (x < 0 || y < 0 || x >= Gfx::W || y >= Gfx::H) return;
    g_pixels[(size_t)y * g_stride + x] = color;
}

inline void blendPixel(int x, int y, u8 r, u8 g, u8 b, u8 a) {
    if (x < 0 || y < 0 || x >= Gfx::W || y >= Gfx::H || a == 0) return;
    u32 *p = &g_pixels[(size_t)y * g_stride + x];
    u32 d = *p;
    u32 ia = 255 - a;
    u8 dr = (u8)(d & 0xFF), dg = (u8)((d >> 8) & 0xFF), db = (u8)((d >> 16) & 0xFF);
    u8 rr = (u8)(((u32)r * a + (u32)dr * ia) / 255);
    u8 gg = (u8)(((u32)g * a + (u32)dg * ia) / 255);
    u8 bb = (u8)(((u32)b * a + (u32)db * ia) / 255);
    *p = Gfx::rgba(rr, gg, bb, 255);
}

}  // namespace

namespace Gfx {

bool init(std::string *err) {
    Result rc = framebufferCreate(&g_fb, nwindowGetDefault(), W, H, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("framebufferCreate failed (0x%x)", rc);
        return false;
    }
    rc = framebufferMakeLinear(&g_fb);
    if (R_FAILED(rc)) {
        framebufferClose(&g_fb);
        if (err) *err = util::format("framebufferMakeLinear failed (0x%x)", rc);
        return false;
    }
    g_ready = true;
    return true;
}

void shutdown() {
    if (g_ready) {
        g_ready = false;
        framebufferClose(&g_fb);
    }
}

void begin() {
    if (!g_ready) return;
    u32 stride_bytes = 0;
    g_pixels = (u32 *)framebufferBegin(&g_fb, &stride_bytes);
    g_stride = stride_bytes / sizeof(u32);
}

void end() {
    if (!g_ready) return;
    framebufferEnd(&g_fb);
    g_pixels = nullptr;
}

void clear(u32 color) {
    if (!g_pixels) return;
    for (int y = 0; y < H; ++y) {
        u32 *row = g_pixels + (size_t)y * g_stride;
        for (int x = 0; x < W; ++x) row[x] = color;
    }
}

void fillRect(int x, int y, int w, int h, u32 color) {
    if (!g_pixels || w <= 0 || h <= 0) return;
    int x0 = clamp(x, 0, W), x1 = clamp(x + w, 0, W);
    int y0 = clamp(y, 0, H), y1 = clamp(y + h, 0, H);
    for (int yy = y0; yy < y1; ++yy) {
        u32 *row = g_pixels + (size_t)yy * g_stride;
        for (int xx = x0; xx < x1; ++xx) row[xx] = color;
    }
}

void fillRectAlpha(int x, int y, int w, int h, u32 color, u8 alpha) {
    if (!g_pixels) return;
    u8 r = (u8)(color & 0xFF), g = (u8)((color >> 8) & 0xFF), b = (u8)((color >> 16) & 0xFF);
    int x0 = clamp(x, 0, W), x1 = clamp(x + w, 0, W);
    int y0 = clamp(y, 0, H), y1 = clamp(y + h, 0, H);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) blendPixel(xx, yy, r, g, b, alpha);
    }
}

void fillRoundRect(int x, int y, int w, int h, int radius, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) {
        fillRect(x, y, w, h, color);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    for (int yy = 0; yy < h; ++yy) {
        int dy = 0;
        if (yy < radius) {
            dy = radius - yy;
        } else if (yy >= h - radius) {
            dy = yy - (h - radius - 1);
        }
        int inset = 0;
        if (dy > 0) {
            float v = (float)(radius * radius - dy * dy);
            if (v < 0) v = 0;
            inset = radius - (int)sqrtf(v);
        }
        fillRect(x + inset, y + yy, w - inset * 2, 1, color);
    }
}

void fillRoundRectBorder(int x, int y, int w, int h, int radius, int thickness, u32 fill,
                         u32 border) {
    fillRoundRect(x, y, w, h, radius, border);
    if (thickness < 1) thickness = 1;
    fillRoundRect(x + thickness, y + thickness, w - thickness * 2, h - thickness * 2,
                  radius > thickness ? radius - thickness : 0, fill);
}

void fillCircle(int cx, int cy, int radius, u32 color) {
    if (!g_pixels || radius <= 0) return;
    for (int dy = -radius; dy <= radius; ++dy) {
        int span = (int)sqrtf((float)(radius * radius - dy * dy));
        fillRect(cx - span, cy + dy, span * 2 + 1, 1, color);
    }
}

void drawLine(int x0, int y0, int x1, int y1, int thickness, u32 color) {
    if (thickness < 1) thickness = 1;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fillCircle(x0, y0, thickness / 2 > 0 ? thickness / 2 : 1, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void drawMask(int x, int y, int w, int h, const u8 *mask, int pitch, u32 color) {
    if (!g_pixels || !mask) return;
    u8 r = (u8)(color & 0xFF), g = (u8)((color >> 8) & 0xFF), b = (u8)((color >> 16) & 0xFF);
    for (int yy = 0; yy < h; ++yy) {
        const u8 *row = mask + (pitch >= 0 ? (size_t)yy * pitch : -(size_t)yy * pitch);
        int py = y + yy;
        for (int xx = 0; xx < w; ++xx) {
            u8 a = row[xx];
            if (a) blendPixel(x + xx, py, r, g, b, a);
        }
    }
}

void dim(u8 alpha) { fillRectAlpha(0, 0, W, H, rgba(0, 0, 0), alpha); }

u64 nowMs() { return svcGetSystemTick() / 19200ULL; }

}  // namespace Gfx

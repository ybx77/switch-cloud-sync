#include "font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "gfx.hpp"
#include "log.hpp"

namespace {

FT_Library g_lib = nullptr;
bool g_ready = false;

struct FaceSlot {
    FT_Face face = nullptr;
    int size = 0;
};

std::vector<FaceSlot> g_faces;

struct Glyph {
    int w = 0;
    int h = 0;
    int left = 0;
    int top = 0;
    int advance = 0;
    int baseline = 0;
    int pitch = 0;
    std::vector<u8> mask;
};

std::map<u64, Glyph> g_cache;

/// Decodes one UTF-8 codepoint; returns 0 on invalid input (and advances by 1).
uint32_t nextCodepoint(const std::string &s, size_t *i) {
    if (*i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[*i];
    uint32_t cp = 0;
    size_t len = 0;
    if (c < 0x80) {
        cp = c;
        len = 1;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        len = 4;
    } else {
        ++(*i);
        return 0xFFFD;
    }
    if (*i + len > s.size()) {
        ++(*i);
        return 0xFFFD;
    }
    for (size_t k = 1; k < len; ++k) {
        unsigned char cc = (unsigned char)s[*i + k];
        if ((cc & 0xC0) != 0x80) {
            ++(*i);
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *i += len;
    return cp;
}

const Glyph *glyphFor(uint32_t cp, int px) {
    u64 key = ((u64)px << 32) | (u64)cp;
    std::map<u64, Glyph>::iterator it = g_cache.find(key);
    if (it != g_cache.end()) return &it->second;

    Glyph glyph;  // empty default: zero advance is bad, use half width
    for (size_t f = 0; f < g_faces.size(); ++f) {
        FT_Face face = g_faces[f].face;
        if (!face) continue;
        if (FT_Get_Char_Index(face, cp) == 0) continue;

        if (g_faces[f].size != px) {
            if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0) continue;
            g_faces[f].size = px;
        }
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        glyph.w = (int)slot->bitmap.width;
        glyph.h = (int)slot->bitmap.rows;
        glyph.left = slot->bitmap_left;
        glyph.top = slot->bitmap_top;
        glyph.advance = (int)(slot->advance.x >> 6);
        glyph.baseline = (int)(face->size->metrics.ascender >> 6);
        glyph.pitch = (int)slot->bitmap.pitch;
        if (glyph.w > 0 && glyph.h > 0) {
            glyph.mask.resize((size_t)glyph.w * glyph.h);
            for (int yy = 0; yy < glyph.h; ++yy) {
                const u8 *src = slot->bitmap.buffer + (ptrdiff_t)yy * glyph.pitch;
                memcpy(&glyph.mask[(size_t)yy * glyph.w], src, (size_t)glyph.w);
            }
            glyph.pitch = glyph.w;
        }
        break;
    }
    if (glyph.advance == 0) glyph.advance = px / 2;
    if (glyph.baseline == 0) glyph.baseline = px;

    std::pair<std::map<u64, Glyph>::iterator, bool> ins = g_cache.insert(std::make_pair(key, glyph));
    return &ins.first->second;
}

int drawRun(int x, int y, const std::string &s, int px, u32 color, int max_width) {
    int pen = x;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = nextCodepoint(s, &i);
        if (cp == 0) break;
        const Glyph *g = glyphFor(cp, px);
        if (!g) continue;
        if (max_width > 0 && pen + g->advance > x + max_width) break;
        if (g->w > 0 && g->h > 0) {
            Gfx::drawMask(pen + g->left, y + g->baseline - g->top, g->w, g->h, g->mask.data(),
                          g->pitch, color);
        }
        pen += g->advance;
    }
    return pen - x;
}

}  // namespace

namespace Font {

bool init(std::string *err) {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) {
        if (err) *err = util::format("plInitialize failed (0x%x)", rc);
    }
    if (FT_Init_FreeType(&g_lib) != 0) {
        if (err) *err = "FT_Init_FreeType failed";
        return false;
    }

    static const PlSharedFontType kOrder[] = {
        PlSharedFontType_ChineseSimplified, PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_Standard, PlSharedFontType_ChineseTraditional, PlSharedFontType_KO};
    for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i) {
        PlFontData data;
        memset(&data, 0, sizeof(data));
        if (R_FAILED(plGetSharedFontByType(&data, kOrder[i]))) continue;
        if (!data.address || data.size == 0) continue;

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(g_lib, (const FT_Byte *)data.address, data.size, 0, &face) != 0) {
            continue;
        }
        FaceSlot slot;
        slot.face = face;
        slot.size = 0;
        g_faces.push_back(slot);
    }

    if (g_faces.empty()) {
        if (err) *err = "no usable shared font found";
        return false;
    }
    g_ready = true;
    Log::printf("Fonts: %u faces loaded", (unsigned)g_faces.size());
    return true;
}

void shutdown() {
    g_cache.clear();
    for (size_t i = 0; i < g_faces.size(); ++i) {
        if (g_faces[i].face) FT_Done_Face(g_faces[i].face);
    }
    g_faces.clear();
    if (g_lib) {
        FT_Done_FreeType(g_lib);
        g_lib = nullptr;
    }
    if (g_ready) {
        plExit();
        g_ready = false;
    }
}

int lineHeight(int px) { return px + px / 2; }

int measure(const std::string &utf8, int px) {
    int width = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, &i);
        if (cp == 0) break;
        const Glyph *g = glyphFor(cp, px);
        if (g) width += g->advance;
    }
    return width;
}

int drawText(int x, int y, const std::string &utf8, int px, u32 color) {
    return drawRun(x, y, utf8, px, color, 0);
}

std::string ellipsize(const std::string &utf8, int px, int max_width) {
    if (max_width <= 0) return std::string();
    if (measure(utf8, px) <= max_width) return utf8;

    int dots = measure("…", px);
    if (dots <= 0) dots = measure("...", px);
    const char *suffix = (measure("…", px) > 0) ? "…" : "...";
    int budget = max_width - dots;
    if (budget < 0) return std::string();

    std::string out;
    size_t i = 0;
    int width = 0;
    while (i < utf8.size()) {
        size_t start = i;
        uint32_t cp = nextCodepoint(utf8, &i);
        if (cp == 0) break;
        const Glyph *g = glyphFor(cp, px);
        int adv = g ? g->advance : 0;
        if (width + adv > budget) break;
        out.append(utf8, start, i - start);
        width += adv;
    }
    return out + suffix;
}

int drawWrapped(int x, int y, int max_width, const std::string &utf8, int px, u32 color,
                int max_lines) {
    if (max_lines <= 0) return 0;

    int line = 0;
    int pen_y = y;
    size_t pos = 0;
    const int lh = lineHeight(px);

    while (pos <= utf8.size() && line < max_lines) {
        size_t newline = utf8.find('\n', pos);
        std::string para = newline == std::string::npos ? utf8.substr(pos)
                                                        : utf8.substr(pos, newline - pos);

        size_t start = 0;
        while (start <= para.size() && line < max_lines) {
            int width = 0;
            size_t i = start;
            size_t best = start;
            bool broke = false;
            while (i < para.size()) {
                size_t char_start = i;
                uint32_t cp = nextCodepoint(para, &i);
                if (cp == 0) break;
                const Glyph *g = glyphFor(cp, px);
                int adv = g ? g->advance : 0;
                if (width + adv > max_width && i > start) {
                    broke = true;
                    i = char_start;
                    break;
                }
                width += adv;
                best = i;
            }

            std::string piece = para.substr(start, best - start);
            if (piece.empty() && !broke) break;
            if (broke && best == start) {
                // A single codepoint wider than max_width: consume it to avoid a loop.
                size_t j = start;
                nextCodepoint(para, &j);
                best = (j > start) ? j : start + 1;
                piece = para.substr(start, best - start);
            }
            drawRun(x, pen_y, piece, px, color, 0);
            pen_y += lh;
            ++line;
            if (!broke) break;
            start = best;
        }

        if (newline == std::string::npos) break;
        pos = newline + 1;
    }

    return pen_y - y;
}

}  // namespace Font

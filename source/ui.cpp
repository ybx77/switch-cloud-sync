#include "ui.hpp"

#include <cmath>

#include "accounts.hpp"
#include "font.hpp"
#include "gfx.hpp"
#include "http.hpp"
#include "log.hpp"
#include "save_sync.hpp"
#include "state.hpp"

namespace {

const u32 C_BG = Gfx::rgba(15, 17, 21);
const u32 C_BG2 = Gfx::rgba(20, 26, 36);
const u32 C_CARD = Gfx::rgba(27, 34, 48);
const u32 C_CARD_SEL = Gfx::rgba(35, 50, 74);
const u32 C_BORDER = Gfx::rgba(42, 54, 72);
const u32 C_ACCENT = Gfx::rgba(76, 125, 255);
const u32 C_ACCENT2 = Gfx::rgba(53, 196, 138);
const u32 C_TEXT = Gfx::rgba(232, 237, 246);
const u32 C_MUTED = Gfx::rgba(147, 161, 184);
const u32 C_DANGER = Gfx::rgba(255, 107, 107);
const u32 C_WARN = Gfx::rgba(255, 190, 90);

const int MARGIN = 56;
const int CONTENT_W = Gfx::W - MARGIN * 2;

enum Icon {
    ICON_SYNC,
    ICON_SAVE,
    ICON_RESTORE,
    ICON_ALBUM,
    ICON_CLOCK,
    ICON_GEAR,
    ICON_POWER,
    ICON_CHECK,
    ICON_WARN,
};

struct ProgressState {
    bool active = false;
    std::string title;
    std::string phase;
    std::string label;
    int total = 0;
    int done = 0;
    int percent = -1;
    u64 last_draw = 0;
};

ProgressState g_prog;

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

void drawArc(int cx, int cy, int radius, int deg0, int deg1, int thickness, u32 color) {
    if (deg1 < deg0) std::swap(deg0, deg1);
    for (int deg = deg0; deg <= deg1; deg += 3) {
        float rad = (float)deg * 3.14159265f / 180.0f;
        int x = cx + (int)(cosf(rad) * radius);
        int y = cy + (int)(sinf(rad) * radius);
        Gfx::fillCircle(x, y, thickness / 2 > 0 ? thickness / 2 : 1, color);
    }
}

void drawIcon(Icon icon, int cx, int cy, int size, u32 color) {
    int r = size / 2;
    switch (icon) {
        case ICON_SYNC: {
            drawArc(cx, cy, r - 2, 200, 340, 4, color);
            drawArc(cx, cy, r - 2, 20, 160, 4, color);
            Gfx::drawLine(cx + r - 6, cy - 6, cx + r - 1, cy + 3, 3, color);
            Gfx::drawLine(cx + r - 1, cy + 3, cx + r - 11, cy + 6, 3, color);
            Gfx::drawLine(cx - r + 6, cy + 6, cx - r + 1, cy - 3, 3, color);
            Gfx::drawLine(cx - r + 1, cy - 3, cx - r + 11, cy - 6, 3, color);
            break;
        }
        case ICON_SAVE: {
            Gfx::fillRoundRectBorder(cx - r, cy - r, size, size, 6, 2, 0, color);
            Gfx::fillRect(cx - r + 8, cy - r + 8, size - 16, 7, color);
            Gfx::fillRect(cx - r + 8, cy + 2, size - 16, 2, color);
            Gfx::fillRect(cx - r + 8, cy + 9, size - 16, 2, color);
            break;
        }
        case ICON_RESTORE: {
            Gfx::fillRect(cx - 2, cy - r, 4, size - 10, color);
            Gfx::drawLine(cx - 9, cy + r - 14, cx, cy + r - 4, 4, color);
            Gfx::drawLine(cx + 9, cy + r - 14, cx, cy + r - 4, 4, color);
            Gfx::fillRect(cx - r, cy + r - 3, size, 3, color);
            break;
        }
        case ICON_ALBUM: {
            Gfx::fillRoundRectBorder(cx - r, cy - r + 2, size, size - 4, 5, 2, 0, color);
            Gfx::fillCircle(cx - r / 3, cy - r / 3, 3, color);
            Gfx::drawLine(cx - r + 5, cy + r - 8, cx - 2, cy + 2, 3, color);
            Gfx::drawLine(cx - 2, cy + 2, cx + r - 5, cy + r - 8, 3, color);
            break;
        }
        case ICON_CLOCK: {
            drawArc(cx, cy, r - 2, 0, 360, 3, color);
            Gfx::drawLine(cx, cy, cx, cy - r + 8, 3, color);
            Gfx::drawLine(cx, cy, cx + r / 2, cy + 2, 3, color);
            break;
        }
        case ICON_GEAR: {
            drawArc(cx, cy, r - 4, 0, 360, 4, color);
            for (int i = 0; i < 8; ++i) {
                float rad = (float)i * 3.14159265f / 4.0f;
                int x = cx + (int)(cosf(rad) * (r - 1));
                int y = cy + (int)(sinf(rad) * (r - 1));
                Gfx::fillCircle(x, y, 3, color);
            }
            Gfx::fillCircle(cx, cy, 3, color);
            break;
        }
        case ICON_POWER: {
            drawArc(cx, cy, r - 2, 300, 600, 4, color);
            Gfx::fillRect(cx - 2, cy - r, 4, r / 2 + 4, color);
            break;
        }
        case ICON_CHECK: {
            Gfx::drawLine(cx - r + 4, cy + 1, cx - 3, cy + r - 6, 5, color);
            Gfx::drawLine(cx - 3, cy + r - 6, cx + r - 4, cy - r + 6, 5, color);
            break;
        }
        case ICON_WARN: {
            Gfx::drawLine(cx, cy - r, cx - r, cy + r - 4, 3, color);
            Gfx::drawLine(cx, cy - r, cx + r, cy + r - 4, 3, color);
            Gfx::drawLine(cx - r, cy + r - 4, cx + r, cy + r - 4, 3, color);
            Gfx::fillRect(cx - 2, cy - r / 2, 4, r - 2, color);
            Gfx::fillCircle(cx, cy + r - 12, 3, color);
            break;
        }
    }
}

void drawBackground() {
    for (int y = 0; y < Gfx::H; ++y) {
        float t = (float)y / (float)Gfx::H;
        u8 r = (u8)(15 + t * 6);
        u8 g = (u8)(17 + t * 10);
        u8 b = (u8)(21 + t * 16);
        Gfx::fillRect(0, y, Gfx::W, 1, Gfx::rgba(r, g, b));
    }
    Gfx::fillCircle(Gfx::W - 90, 40, 220, Gfx::rgba(30, 52, 92));
    Gfx::fillRectAlpha(Gfx::W - 320, 0, 320, 260, Gfx::rgba(20, 35, 70), 120);
}

void drawHeader(const std::string &title, const std::string &subtitle, const ServerStatus &status) {
    Font::drawText(MARGIN, 26, title, 32, C_TEXT);
    Font::drawText(MARGIN, 78, subtitle, 17, C_MUTED);

    int pill_w = 190;
    int x = Gfx::W - MARGIN - pill_w;
    u32 dot = C_MUTED;
    u32 fill = Gfx::rgba(32, 40, 54);
    u32 text_color = C_MUTED;
    if (status.configured) {
        if (!status.checked) {
            dot = C_WARN;
            fill = Gfx::rgba(52, 44, 26);
            text_color = C_TEXT;
        } else if (status.ok) {
            dot = C_ACCENT2;
            fill = Gfx::rgba(24, 52, 44);
            text_color = C_TEXT;
        } else {
            dot = C_DANGER;
            fill = Gfx::rgba(58, 30, 34);
            text_color = C_TEXT;
        }
    }
    Gfx::fillRoundRect(x, 34, pill_w, 44, 22, fill);
    Gfx::fillCircle(x + 24, 56, 7, dot);
    Font::drawText(x + 42, 43, Font::ellipsize(status.text, 18, pill_w - 56), 18, text_color);
}

void drawKeyHint(int x, int y, const std::string &key, const std::string &label) {
    Gfx::fillCircle(x + 14, y + 14, 14, Gfx::rgba(45, 57, 76));
    int kw = Font::measure(key, 16);
    Font::drawText(x + 14 - kw / 2, y + 4, key, 16, C_TEXT);
    Font::drawText(x + 36, y + 4, label, 17, C_MUTED);
}

void drawFooter(const std::vector<std::pair<std::string, std::string> > &hints) {
    int x = MARGIN;
    for (size_t i = 0; i < hints.size(); ++i) {
        drawKeyHint(x, Gfx::H - 50, hints[i].first, hints[i].second);
        x += 40 + Font::measure(hints[i].second, 17) + 34;
    }
}

void drawCard(int x, int y, int w, int h, bool selected, Icon icon, const std::string &title,
              const std::string &subtitle, bool primary = false) {
    u32 fill = primary ? Gfx::rgba(33, 62, 120) : (selected ? C_CARD_SEL : C_CARD);
    if (primary && !selected) fill = Gfx::rgba(30, 52, 96);
    u32 border = selected ? C_ACCENT : (primary ? Gfx::rgba(58, 92, 158) : C_BORDER);
    Gfx::fillRoundRectBorder(x, y, w, h, 18, selected ? 2 : 1, fill, border);

    int icon_cx = x + 52;
    int icon_cy = y + h / 2;
    Gfx::fillCircle(icon_cx, icon_cy, 24, primary ? Gfx::rgba(76, 125, 255) : Gfx::rgba(38, 48, 66));
    drawIcon(icon, icon_cx, icon_cy, 26, primary ? C_TEXT : C_ACCENT);

    Font::drawText(x + 96, y + (subtitle.empty() ? h / 2 - 14 : h / 2 - 30), title, 24, C_TEXT);
    if (!subtitle.empty()) {
        Font::drawText(x + 96, y + h / 2 + 8, subtitle, 16, primary ? Gfx::rgba(200, 215, 245) : C_MUTED);
    }

    if (!primary) {
        int cx = x + w - 34;
        int cy = y + h / 2;
        Gfx::drawLine(cx - 5, cy - 8, cx + 3, cy, 3, selected ? C_ACCENT : C_MUTED);
        Gfx::drawLine(cx + 3, cy, cx - 5, cy + 8, 3, selected ? C_ACCENT : C_MUTED);
    }
}

struct CardDef {
    HomeAction action;
    Icon icon;
    std::string title;
    std::string subtitle;
};

std::vector<CardDef> homeCards() {
    std::vector<CardDef> cards;
    cards.push_back({HomeAction::UploadAll, ICON_SYNC, "一键上传全部", "存档 · 相册 · 游玩记录"});
    cards.push_back({HomeAction::UploadSaves, ICON_SAVE, "上传存档", "只传有变化的存档"});
    cards.push_back({HomeAction::Restore, ICON_RESTORE, "恢复存档", "从服务器选择并写回"});
    cards.push_back({HomeAction::UploadAlbum, ICON_ALBUM, "上传相册", "新增的截图与视频"});
    cards.push_back({HomeAction::UploadPlaytime, ICON_CLOCK, "上传游玩记录", "汇总与详细事件"});
    cards.push_back({HomeAction::Settings, ICON_GEAR, "设置", "服务器、令牌、同步选项"});
    cards.push_back({HomeAction::Exit, ICON_POWER, "退出", "返回 hbmenu"});
    return cards;
}

void cardRect(int index, int *x, int *y, int *w, int *h) {
    const int gap = 24;
    const int half = (CONTENT_W - gap) / 2;
    if (index == 0) {
        *x = MARGIN;
        *y = 140;
        *w = CONTENT_W;
        *h = 104;
    } else if (index <= 2) {
        *x = MARGIN + (index - 1) * (half + gap);
        *y = 262;
        *w = half;
        *h = 118;
    } else if (index <= 4) {
        *x = MARGIN + (index - 3) * (half + gap);
        *y = 396;
        *w = half;
        *h = 118;
    } else {
        *x = MARGIN + (index - 5) * (half + gap);
        *y = 530;
        *w = half;
        *h = 74;
    }
}

int rowOf(int index) {
    if (index == 0) return 0;
    if (index <= 2) return 1;
    if (index <= 4) return 2;
    return 3;
}

int colOf(int index) {
    if (index == 0) return 0;
    return (index - 1) % 2;
}

int indexFor(int row, int col) {
    if (row == 0) return 0;
    if (row == 1) return 1 + col;
    if (row == 2) return 3 + col;
    return 5 + col;
}

// ---------------------------------------------------------------------------
// progress screen
// ---------------------------------------------------------------------------

void drawProgress() {
    int pct = 0;
    if (g_prog.percent >= 0) {
        pct = g_prog.percent;
    } else if (g_prog.total > 0) {
        pct = g_prog.done * 100 / g_prog.total;
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    Gfx::begin();
    drawBackground();
    Font::drawText(MARGIN, 34, g_prog.title, 34, C_TEXT);
    if (!g_prog.phase.empty()) {
        Font::drawText(MARGIN, 84, "阶段: " + g_prog.phase, 18, C_MUTED);
    }
    Font::drawText(MARGIN, 150, Font::ellipsize(g_prog.label, 26, CONTENT_W - 140), 26, C_TEXT);
    Font::drawText(Gfx::W - MARGIN - 90, 150, util::format("%d%%", pct), 30, C_ACCENT);

    Gfx::fillRoundRect(MARGIN, 200, CONTENT_W, 16, 8, Gfx::rgba(32, 40, 54));
    if (pct > 0) {
        int bar_w = CONTENT_W * pct / 100;
        if (bar_w < 16) bar_w = 16;
        Gfx::fillRoundRect(MARGIN, 200, bar_w, 16, 8, C_ACCENT);
    }

    std::string counter;
    if (g_prog.total > 0) {
        counter = util::format("%d / %d", g_prog.done < g_prog.total ? g_prog.done : g_prog.total,
                               g_prog.total);
    }
    Font::drawText(MARGIN, 228, counter, 16, C_MUTED);

    Gfx::fillRoundRectBorder(MARGIN, 272, CONTENT_W, 356, 16, 1, Gfx::rgba(19, 24, 33), C_BORDER);
    const std::deque<std::string> &lines = Log::recent();
    int shown = 0;
    int max_lines = 13;
    int start = (int)lines.size() - max_lines;
    if (start < 0) start = 0;
    for (int i = start; i < (int)lines.size(); ++i) {
        Font::drawText(MARGIN + 22, 292 + shown * 24,
                       Font::ellipsize(lines[i], 14, CONTENT_W - 44), 14, C_MUTED);
        ++shown;
    }

    drawFooter({{"B", "取消"}});
    Gfx::end();
    g_prog.last_draw = Gfx::nowMs();
}

void drawProgressThrottled() {
    u64 now = Gfx::nowMs();
    if (now - g_prog.last_draw < 40) return;
    drawProgress();
}

void drawMessageScreen(const std::string &title, const std::vector<std::string> &lines, int scroll,
                       bool error, const std::string &hint) {
    Gfx::begin();
    drawBackground();
    Font::drawText(MARGIN, 40, title, 34, C_TEXT);

    const int card_h = 470;
    Gfx::fillRoundRectBorder(MARGIN, 120, CONTENT_W, card_h, 18, 1, C_CARD, C_BORDER);

    Icon icon = error ? ICON_WARN : ICON_CHECK;
    Gfx::fillCircle(MARGIN + 56, 178, 26, error ? Gfx::rgba(70, 34, 38) : Gfx::rgba(24, 56, 46));
    drawIcon(icon, MARGIN + 56, 178, 28, error ? C_DANGER : C_ACCENT2);

    int y = 154;
    int drawn = 0;
    for (size_t i = scroll; i < lines.size() && drawn < 12; ++i, ++drawn) {
        Font::drawText(MARGIN + 108, y, Font::ellipsize(lines[i], 20, CONTENT_W - 140), 20, C_TEXT);
        y += 32;
    }
    drawFooter({{"A", hint}, {"B", "返回"}});
    Gfx::end();
}

}  // namespace

namespace UI {

bool init(std::string *err) {
    if (!Gfx::init(err)) return false;
    std::string font_err;
    if (!Font::init(&font_err)) {
        Log::printf("字体初始化失败: %s", font_err.c_str());
    }
    return true;
}

void shutdown() {
    Font::shutdown();
    Gfx::shutdown();
}

HomeAction home(const Config &cfg, const State &state, const ServerStatus &status) {
    g_abort = false;
    std::vector<CardDef> cards = homeCards();
    int sel = 0;

    for (;;) {
        if (!appletMainLoop()) {
            g_exit = true;
            return HomeAction::Exit;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);

        int row = rowOf(sel), col = colOf(sel);
        if (down & HidNpadButton_Down) {
            if (row < 3) sel = indexFor(row + 1, col);
        }
        if (down & HidNpadButton_Up) {
            if (row > 0) sel = indexFor(row - 1, col);
        }
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            if (row > 0) sel = indexFor(row, 1 - col);
        }
        if (down & HidNpadButton_A) return cards[sel].action;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return HomeAction::Exit;

        Gfx::begin();
        drawBackground();
        drawHeader(APP_NAME " " APP_VERSION,
                   cfg.ready() ? cfg.base_url : "还没有配置服务器地址", status);

        std::string counts = util::format("设备 %s   ·   已记录 %u 个存档索引 / %u 个相册索引",
                                          effectiveDeviceId(cfg, state).c_str(),
                                          (unsigned)state.saveCount(),
                                          (unsigned)state.albumCount());
        Font::drawText(MARGIN, 104, counts, 15, C_MUTED);

        for (int i = 0; i < (int)cards.size(); ++i) {
            int x, y, w, h;
            cardRect(i, &x, &y, &w, &h);
            drawCard(x, y, w, h, i == sel, cards[i].icon, cards[i].title, cards[i].subtitle,
                     i == 0);
        }

        drawFooter({{"A", "选择"}, {"方向键", "移动"}, {"B", "退出"}});
        Gfx::end();
        svcSleepThread(16000000ULL);
    }
}

namespace {

enum FieldId {
    F_URL,
    F_TOKEN,
    F_DEVNAME,
    F_DEVID,
    F_CABUNDLE,
    F_TIMEOUT,
    F_MAXMB,
    F_SAVES,
    F_ONLYCHANGED,
    F_GAMESONLY,
    F_ALBUM,
    F_ALBUMDEL,
    F_PLAYTIME,
    F_DETAILED,
    F_INSECURE,
    F_TEST,
    F_SAVE,
    F_RELOAD,
    F_BACK,
};

struct SetRow {
    std::string label;
    std::string value;
    int kind = 0;  // 0 text, 1 number, 2 toggle, 3 action
    int id = 0;
    bool on = false;
};

const char *onOff(bool v) { return v ? "开启" : "关闭"; }

std::string maskToken(const std::string &token) {
    if (token.empty()) return "未设置";
    if (token.size() <= 4) return "已设置";
    return token.substr(0, 2) + "…" + token.substr(token.size() - 2);
}

std::vector<SetRow> buildRows(const Config &cfg) {
    std::vector<SetRow> rows;
    SetRow r;

    r.label = "服务器地址";
    r.value = cfg.base_url.empty() ? "未设置" : cfg.base_url;
    r.kind = 0;
    r.id = F_URL;
    rows.push_back(r);

    r.label = "访问令牌";
    r.value = maskToken(cfg.token);
    r.id = F_TOKEN;
    rows.push_back(r);

    r.label = "设备名称";
    r.value = cfg.device_name.empty() ? "未设置" : cfg.device_name;
    r.id = F_DEVNAME;
    rows.push_back(r);

    r.label = "设备 ID";
    r.value = cfg.device_id_config.empty() ? "自动生成" : cfg.device_id_config;
    r.id = F_DEVID;
    rows.push_back(r);

    r.label = "CA 证书路径";
    r.value = cfg.ca_bundle.empty() ? "自动检测" : cfg.ca_bundle;
    r.id = F_CABUNDLE;
    rows.push_back(r);

    r.label = "网络超时（秒）";
    r.value = util::format("%ld", cfg.timeout_sec);
    r.kind = 1;
    r.id = F_TIMEOUT;
    rows.push_back(r);

    r.label = "单个存档上限（MB）";
    r.value = util::format("%ld", cfg.max_upload_mb);
    r.id = F_MAXMB;
    rows.push_back(r);

    r.kind = 2;
    r.label = "上传存档";
    r.on = cfg.saves_enabled;
    r.value = onOff(cfg.saves_enabled);
    r.id = F_SAVES;
    rows.push_back(r);

    r.label = "只上传有变化的内容";
    r.on = cfg.only_changed;
    r.value = onOff(cfg.only_changed);
    r.id = F_ONLYCHANGED;
    rows.push_back(r);

    r.label = "仅游戏存档（跳过系统）";
    r.on = cfg.games_only;
    r.value = onOff(cfg.games_only);
    r.id = F_GAMESONLY;
    rows.push_back(r);

    r.label = "上传相册";
    r.on = cfg.album_enabled;
    r.value = onOff(cfg.album_enabled);
    r.id = F_ALBUM;
    rows.push_back(r);

    r.label = "上传后删除本地相册";
    r.on = cfg.album_delete_after_upload;
    r.value = onOff(cfg.album_delete_after_upload);
    r.id = F_ALBUMDEL;
    rows.push_back(r);

    r.label = "上传游玩记录";
    r.on = cfg.playtime_enabled;
    r.value = onOff(cfg.playtime_enabled);
    r.id = F_PLAYTIME;
    rows.push_back(r);

    r.label = "详细游玩事件";
    r.on = cfg.playtime_detailed;
    r.value = onOff(cfg.playtime_detailed);
    r.id = F_DETAILED;
    rows.push_back(r);

    r.label = "跳过 TLS 校验（内网）";
    r.on = cfg.insecure_tls;
    r.value = onOff(cfg.insecure_tls);
    r.id = F_INSECURE;
    rows.push_back(r);

    r.kind = 3;
    r.label = "测试服务器连接";
    r.value = "执行";
    r.id = F_TEST;
    rows.push_back(r);

    r.label = "保存设置";
    r.value = "保存";
    r.id = F_SAVE;
    rows.push_back(r);

    r.label = "重新读取配置文件";
    r.value = "重新读取";
    r.id = F_RELOAD;
    rows.push_back(r);

    r.label = "返回";
    r.value = "";
    r.id = F_BACK;
    rows.push_back(r);
    return rows;
}

bool editText(const char *header, const char *guide, const std::string &initial, bool numeric,
              std::string *out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetType(&kbd, numeric ? SwkbdType_NumPad : SwkbdType_Normal);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, numeric ? 8 : 512);
    swkbdConfigSetBlurBackground(&kbd, 1);

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;

    *out = std::string(buf);
    return true;
}

void drawSwitch(int x, int y, bool on) {
    Gfx::fillRoundRect(x, y, 56, 28, 14, on ? C_ACCENT : Gfx::rgba(52, 62, 78));
    int kx = on ? x + 56 - 17 : x + 17;
    Gfx::fillCircle(kx, y + 14, 11, C_TEXT);
}

void drawSettings(const Config &cfg, const std::vector<SetRow> &rows, int sel, int top,
                  const std::string &toast) {
    (void)cfg;
    Gfx::begin();
    drawBackground();
    Font::drawText(MARGIN, 34, "设置", 34, C_TEXT);
    Font::drawText(MARGIN, 84, "所有参数都会写回 sdmc:/switch/cloud-sync/config.ini", 17, C_MUTED);

    const int row_h = 52;
    const int visible = 9;
    Gfx::fillRoundRectBorder(MARGIN, 128, CONTENT_W, visible * row_h + 16, 18, 1, C_CARD,
                             C_BORDER);

    for (int i = top; i < (int)rows.size() && i < top + visible; ++i) {
        int y = 144 + (i - top) * row_h;
        if (i == sel) {
            Gfx::fillRoundRect(MARGIN + 8, y - 4, CONTENT_W - 16, row_h - 4, 12, C_CARD_SEL);
            Gfx::fillRoundRect(MARGIN + 8, y - 4, 4, row_h - 4, 2, C_ACCENT);
        }
        u32 label_color = i == sel ? C_TEXT : C_MUTED;
        Font::drawText(MARGIN + 28, y + 10, rows[i].label, 18, label_color);

        if (rows[i].kind == 2) {
            drawSwitch(MARGIN + CONTENT_W - 56 - 28, y + 4, rows[i].on);
            Font::drawText(MARGIN + CONTENT_W - 56 - 28 - 8 - Font::measure(rows[i].value, 16),
                           y + 12, rows[i].value, 16, C_MUTED);
        } else if (!rows[i].value.empty()) {
            std::string value = Font::ellipsize(rows[i].value, 18, CONTENT_W / 2);
            int w = Font::measure(value, 18);
            Font::drawText(MARGIN + CONTENT_W - 28 - w, y + 10, value, 18,
                           rows[i].kind == 3 ? C_ACCENT : C_TEXT);
        }
    }

    if (!toast.empty()) {
        Gfx::fillRoundRect(MARGIN, 128 + visible * row_h + 20, CONTENT_W, 40, 12,
                           Gfx::rgba(30, 40, 56));
        Font::drawText(MARGIN + 20, 128 + visible * row_h + 28, toast, 17, C_ACCENT2);
    }

    drawFooter({{"A", "编辑 / 切换"}, {"X", "保存"}, {"Y", "重读"}, {"B", "返回"}});
    Gfx::end();
}

}  // namespace

void refreshStatus(const Config &cfg, ServerStatus *status) {
    if (!status) return;
    status->configured = cfg.ready();
    status->checked = false;
    status->ok = false;
    status->text = status->configured ? "已配置" : "未配置";
}

bool settings(Config &cfg, const State &state, ServerStatus *status, std::string *err) {
    (void)state;
    (void)err;
    g_abort = false;
    std::string toast;
    u64 toast_until = 0;
    bool saved = false;
    int sel = 0;
    int top = 0;
    const int visible = 9;

    for (;;) {
        std::vector<SetRow> rows = buildRows(cfg);
        if (sel >= (int)rows.size()) sel = (int)rows.size() - 1;
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;

        drawSettings(cfg, rows, sel, top, toast);

        if (!appletMainLoop()) {
            g_exit = true;
            return saved;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);

        if (down & HidNpadButton_Down) sel = (sel + 1) % (int)rows.size();
        if (down & HidNpadButton_Up) sel = (sel + (int)rows.size() - 1) % (int)rows.size();

        bool do_save = (down & HidNpadButton_X) != 0;
        bool do_reload = (down & HidNpadButton_Y) != 0;
        bool activate = (down & HidNpadButton_A) != 0;

        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return saved;

        if (activate) {
            SetRow &row = rows[sel];
            switch (row.id) {
                case F_URL: {
                    std::string value;
                    if (editText("服务器地址", "例如 https://cloud.example.com", cfg.base_url, false,
                                 &value)) {
                        cfg.base_url = util::trim(value);
                        while (!cfg.base_url.empty() &&
                               cfg.base_url[cfg.base_url.size() - 1] == '/') {
                            cfg.base_url.erase(cfg.base_url.size() - 1);
                        }
                    }
                    break;
                }
                case F_TOKEN: {
                    std::string value;
                    if (editText("访问令牌", "留空表示不发送 Authorization", cfg.token, false, &value)) {
                        cfg.token = util::trim(value);
                    }
                    break;
                }
                case F_DEVNAME: {
                    std::string value;
                    if (editText("设备名称", "显示在服务器端", cfg.device_name, false, &value)) {
                        cfg.device_name = util::trim(value);
                    }
                    break;
                }
                case F_DEVID: {
                    std::string value;
                    if (editText("设备 ID", "留空则自动生成", cfg.device_id_config, false, &value)) {
                        cfg.device_id_config = util::trim(value);
                    }
                    break;
                }
                case F_CABUNDLE: {
                    std::string value;
                    if (editText("CA 证书路径", "留空则自动使用 ca-bundle.pem", cfg.ca_bundle, false,
                                 &value)) {
                        cfg.ca_bundle = util::trim(value);
                    }
                    break;
                }
                case F_TIMEOUT: {
                    std::string value;
                    if (editText("网络超时（秒）", "5 - 3600", util::format("%ld", cfg.timeout_sec),
                                 true, &value)) {
                        long v = atol(value.c_str());
                        if (v < 5) v = 5;
                        if (v > 3600) v = 3600;
                        cfg.timeout_sec = v;
                    }
                    break;
                }
                case F_MAXMB: {
                    std::string value;
                    if (editText("单个存档上限（MB）", "1 - 4096",
                                 util::format("%ld", cfg.max_upload_mb), true, &value)) {
                        long v = atol(value.c_str());
                        if (v < 1) v = 1;
                        if (v > 4096) v = 4096;
                        cfg.max_upload_mb = v;
                    }
                    break;
                }
                case F_SAVES:
                    cfg.saves_enabled = !cfg.saves_enabled;
                    break;
                case F_ONLYCHANGED:
                    cfg.only_changed = !cfg.only_changed;
                    break;
                case F_GAMESONLY:
                    cfg.games_only = !cfg.games_only;
                    break;
                case F_ALBUM:
                    cfg.album_enabled = !cfg.album_enabled;
                    break;
                case F_ALBUMDEL:
                    cfg.album_delete_after_upload = !cfg.album_delete_after_upload;
                    break;
                case F_PLAYTIME:
                    cfg.playtime_enabled = !cfg.playtime_enabled;
                    break;
                case F_DETAILED:
                    cfg.playtime_detailed = !cfg.playtime_detailed;
                    break;
                case F_INSECURE:
                    cfg.insecure_tls = !cfg.insecure_tls;
                    break;
                case F_TEST: {
                    if (!cfg.ready()) {
                        toast = "请先填写服务器地址";
                    } else {
                        progressBegin("测试服务器连接", 1);
                        progressStep(1, "正在连接 " + cfg.base_url);
                        HttpClient tmp;
                        std::string http_err;
                        if (!tmp.init(cfg, &http_err)) {
                            toast = "初始化失败: " + http_err;
                        } else {
                            HttpResponse resp;
                            if (tmp.getText(cfg.api("/ping"), &resp, &http_err)) {
                                toast = util::format("连接成功 HTTP %ld", resp.status);
                                if (status) {
                                    status->configured = true;
                                    status->checked = true;
                                    status->ok = (resp.status == 200);
                                    status->text = status->ok
                                                       ? "已连接"
                                                       : util::format("HTTP %ld", resp.status);
                                }
                            } else {
                                toast = "连接失败: " + http_err;
                                if (status) {
                                    status->configured = true;
                                    status->checked = true;
                                    status->ok = false;
                                    status->text = "连接失败";
                                }
                            }
                        }
                        progressEnd();
                    }
                    toast_until = Gfx::nowMs() + 4000;
                    break;
                }
                case F_SAVE:
                    do_save = true;
                    break;
                case F_RELOAD:
                    do_reload = true;
                    break;
                case F_BACK:
                    return saved;
                default:
                    break;
            }
        }

        if (do_save) {
            std::string save_err;
            if (cfg.save(&save_err)) {
                saved = true;
                toast = "设置已保存";
            } else {
                toast = "保存失败: " + save_err;
            }
            refreshStatus(cfg, status);
            toast_until = Gfx::nowMs() + 4000;
        }

        if (do_reload) {
            std::string load_err;
            if (cfg.load(&load_err)) {
                toast = "已重新读取配置";
            } else {
                toast = load_err;
            }
            refreshStatus(cfg, status);
            toast_until = Gfx::nowMs() + 4000;
        }

        if (!toast.empty() && toast_until && Gfx::nowMs() > toast_until) {
            toast.clear();
            toast_until = 0;
        }

        svcSleepThread(16000000ULL);
    }
}

void message(const std::string &title, const std::vector<std::string> &lines, bool error) {
    g_abort = false;
    int scroll = 0;
    for (;;) {
        drawMessageScreen(title, lines, scroll, error, "返回");
        if (!appletMainLoop()) {
            g_exit = true;
            return;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_Down) {
            if ((int)lines.size() > 12 && scroll + 12 < (int)lines.size()) ++scroll;
        }
        if (down & HidNpadButton_Up) {
            if (scroll > 0) --scroll;
        }
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) return;
        svcSleepThread(16000000ULL);
    }
}

bool confirm(const std::string &title, const std::vector<std::string> &lines) {
    for (;;) {
        Gfx::begin();
        drawBackground();
        Font::drawText(MARGIN, 60, title, 32, C_TEXT);
        Gfx::fillRoundRectBorder(MARGIN, 140, CONTENT_W, 360, 18, 1, C_CARD, C_BORDER);
        int y = 176;
        for (size_t i = 0; i < lines.size() && i < 9; ++i) {
            Font::drawText(MARGIN + 28, y, Font::ellipsize(lines[i], 20, CONTENT_W - 56), 20,
                           C_TEXT);
            y += 34;
        }
        drawFooter({{"A", "确认"}, {"B", "取消"}});
        Gfx::end();

        if (!appletMainLoop()) {
            g_exit = true;
            return false;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_A) return true;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return false;
        svcSleepThread(16000000ULL);
    }
}

int selectRemote(const std::vector<RemoteSave> &saves, const std::string &subtitle) {
    if (saves.empty()) return -1;
    int sel = 0;
    int top = 0;
    const int visible = 7;

    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;

        Gfx::begin();
        drawBackground();
        Font::drawText(MARGIN, 34, "恢复存档", 34, C_TEXT);
        Font::drawText(MARGIN, 84, util::format("%s   ·   共 %u 份", subtitle.c_str(),
                                                (unsigned)saves.size()),
                       17, C_MUTED);

        Gfx::fillRoundRectBorder(MARGIN, 128, CONTENT_W, visible * 66 + 16, 18, 1, C_CARD,
                                 C_BORDER);
        for (int i = top; i < (int)saves.size() && i < top + visible; ++i) {
            int y = 144 + (i - top) * 66;
            if (i == sel) {
                Gfx::fillRoundRect(MARGIN + 8, y - 4, CONTENT_W - 16, 60, 12, C_CARD_SEL);
                Gfx::fillRoundRect(MARGIN + 8, y - 4, 4, 60, 2, C_ACCENT);
            }
            const RemoteSave &s = saves[i];
            std::string name = s.title_name.empty() ? "未知游戏" : s.title_name;
            Font::drawText(MARGIN + 28, y + 4, Font::ellipsize(name, 22, CONTENT_W - 300), 22,
                           C_TEXT);
            std::string meta = util::format("%s   ·   %s   ·   %s   ·   %s",
                                            userIdLabel(s.user_id).c_str(), s.title_id.c_str(),
                                            util::formatBytes(s.size).c_str(),
                                            util::formatUnixTime(s.timestamp).c_str());
            Font::drawText(MARGIN + 28, y + 32, meta, 15, C_MUTED);
        }
        drawFooter({{"A", "选择"}, {"B", "返回"}});
        Gfx::end();

        if (!appletMainLoop()) {
            g_exit = true;
            return -1;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_Down) sel = (sel + 1) % (int)saves.size();
        if (down & HidNpadButton_Up) sel = (sel + (int)saves.size() - 1) % (int)saves.size();
        if (down & HidNpadButton_A) return sel;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return -1;
        svcSleepThread(16000000ULL);
    }
}

void result(const std::string &title, const std::vector<std::string> &lines, bool ok) {
    g_abort = false;
    drawMessageScreen(title, lines, 0, !ok, "返回");
    for (;;) {
        if (!appletMainLoop()) {
            g_exit = true;
            return;
        }
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) return;
        svcSleepThread(16000000ULL);
    }
}

void progressBegin(const std::string &title, int total) {
    g_prog = ProgressState();
    g_prog.active = true;
    g_prog.title = title;
    g_prog.total = total;
    g_prog.label = "准备中…";
    g_abort = false;
    drawProgress();
}

void progressPhase(const std::string &name, int total) {
    if (!g_prog.active) return;
    g_prog.phase = name;
    g_prog.total = total;
    g_prog.done = 0;
    g_prog.percent = -1;
    g_prog.label = name;
    drawProgress();
}

void progressStep(int done, const std::string &label) {
    if (!g_prog.active) return;
    g_prog.done = done;
    g_prog.label = label;
    g_prog.percent = -1;
    drawProgressThrottled();
}

void progressPercent(int percent) {
    if (!g_prog.active) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == g_prog.percent) return;
    g_prog.percent = percent;
    drawProgressThrottled();
}

void progressEnd() {
    g_prog.active = false;
    g_prog.percent = -1;
}

bool pump() {
    if (!appletMainLoop()) {
        g_exit = true;
        return false;
    }
    padUpdate(&g_pad);
    u64 down = padGetButtonsDown(&g_pad);
    if (down & (HidNpadButton_B | HidNpadButton_Plus)) g_abort = true;
    if (g_abort || g_exit) return false;
    if (g_prog.active) drawProgressThrottled();
    return true;
}

bool isAborted() { return g_abort || g_exit; }

void sleepFrame() { svcSleepThread(16000000ULL); }

}  // namespace UI

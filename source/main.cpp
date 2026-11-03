#include "common.hpp"

#include "accounts.hpp"
#include "album_sync.hpp"
#include "config.hpp"
#include "http.hpp"
#include "log.hpp"
#include "playtime_sync.hpp"
#include "save_sync.hpp"
#include "state.hpp"
#include "titles.hpp"
#include "ui.hpp"

PadState g_pad;
volatile bool g_abort = false;
volatile bool g_exit = false;

namespace {

ServerStatus configuredStatus(const Config &cfg) {
    ServerStatus status;
    status.configured = cfg.ready();
    status.checked = false;
    status.ok = false;
    status.text = status.configured ? "已配置" : "未配置";
    return status;
}

void addLine(std::vector<std::string> *lines, const std::string &phase, const SyncStats &s) {
    lines->push_back(util::format("%s：上传 %d，跳过 %d，失败 %d", phase.c_str(), s.uploaded,
                                  s.skipped, s.failed));
}

bool ensureConfigured(const Config &cfg) {
    if (cfg.ready()) return true;
    UI::message("还没有配置服务器",
                {"请先在「设置」里填写：",
                 "",
                 "服务器地址（例如 http://192.168.1.10:8420）",
                 "访问令牌（和服务器约定）",
                 "",
                 "保存后可以点「测试服务器连接」验证。"},
                false);
    return false;
}

void actionUploadAll(const Config &cfg, HttpClient &http, State &state, TitleCache &titles) {
    UI::progressBegin("一键上传全部", 3);

    std::vector<std::string> lines;
    bool ok = true;

    if (cfg.saves_enabled) {
        SyncStats s;
        std::string err;
        if (saveSyncUpload(cfg, http, state, titles, &s, &err)) {
            addLine(&lines, "存档", s);
        } else {
            ok = false;
            lines.push_back("存档：" + err);
        }
    } else {
        lines.push_back("存档：已在设置中关闭");
    }

    if (!g_abort && !g_exit) {
        if (cfg.album_enabled) {
            SyncStats s;
            std::string err;
            if (albumSyncUpload(cfg, http, state, &s, &err)) {
                addLine(&lines, "相册", s);
            } else {
                ok = false;
                lines.push_back("相册：" + err);
            }
        } else {
            lines.push_back("相册：已在设置中关闭");
        }
    }

    if (!g_abort && !g_exit) {
        if (cfg.playtime_enabled) {
            int rows = 0;
            u64 seconds = 0;
            std::string err;
            if (playtimeSyncUpload(cfg, http, titles, state, &rows, &seconds, &err)) {
                lines.push_back(util::format("游玩记录：%d 条，共 %s", rows,
                                             util::formatDuration(seconds).c_str()));
            } else {
                ok = false;
                lines.push_back("游玩记录：" + err);
            }
        } else {
            lines.push_back("游玩记录：已在设置中关闭");
        }
    }

    UI::progressEnd();
    if (g_abort) {
        lines.push_back("");
        lines.push_back("操作已取消。");
    }
    UI::result(ok ? "上传完成" : "部分任务失败", lines, ok);
}

void actionUploadSaves(const Config &cfg, HttpClient &http, State &state, TitleCache &titles) {
    UI::progressBegin("上传存档", 0);
    SyncStats s;
    std::string err;
    bool ok = saveSyncUpload(cfg, http, state, titles, &s, &err);
    UI::progressEnd();

    std::vector<std::string> lines;
    addLine(&lines, "存档", s);
    if (!ok) lines.push_back(err);
    UI::result(ok ? "存档上传完成" : "存档上传失败", lines, ok);
}

void actionRestore(const Config &cfg, HttpClient &http, State &state) {
    UI::progressBegin("读取服务器存档列表", 1);
    std::vector<RemoteSave> list;
    std::string err;
    bool ok = saveSyncListRemote(cfg, http, state, &list, &err);
    UI::progressEnd();

    if (!ok) {
        UI::message("读取列表失败", {err}, true);
        return;
    }
    if (list.empty()) {
        UI::message("服务器上没有存档", {"这台设备还没有上传过任何存档。", "",
                                         "先做一次「上传存档」再回来。"},
                    false);
        return;
    }

    int sel = UI::selectRemote(list, "选择要覆盖到本机的存档");
    if (sel < 0) return;

    const RemoteSave &chosen = list[sel];
    std::vector<std::string> lines;
    lines.push_back("游戏：" + (chosen.title_name.empty() ? std::string("未知游戏")
                                                         : chosen.title_name));
    lines.push_back("ID：" + chosen.title_id);
    lines.push_back("用户：" + userIdLabel(chosen.user_id));
    lines.push_back("时间：" + util::formatUnixTime(chosen.timestamp) + " UTC");
    lines.push_back("大小：" + util::formatBytes(chosen.size));
    lines.push_back("");
    lines.push_back("这会覆盖本地存档，且不可撤销。");
    lines.push_back("请确认该游戏已经关闭。");

    if (!UI::confirm("覆盖本地存档？", lines)) return;

    UI::progressBegin("恢复存档", 1);
    bool restored = saveSyncRestore(cfg, http, state, chosen, &err);
    UI::progressEnd();

    if (restored) {
        UI::result("恢复完成", {"存档已经写回本机。", "", "现在可以启动游戏了。"}, true);
    } else {
        UI::message("恢复失败", {err}, true);
    }
}

void actionAlbum(const Config &cfg, HttpClient &http, State &state) {
    UI::progressBegin("上传相册", 0);
    SyncStats s;
    std::string err;
    bool ok = albumSyncUpload(cfg, http, state, &s, &err);
    UI::progressEnd();

    std::vector<std::string> lines;
    addLine(&lines, "相册", s);
    if (!ok) lines.push_back(err);
    UI::result(ok ? "相册上传完成" : "相册上传失败", lines, ok);
}

void actionPlaytime(const Config &cfg, HttpClient &http, State &state, TitleCache &titles) {
    UI::progressBegin("上传游玩记录", 0);
    int rows = 0;
    u64 seconds = 0;
    std::string err;
    bool ok = playtimeSyncUpload(cfg, http, titles, state, &rows, &seconds, &err);
    UI::progressEnd();

    std::vector<std::string> lines;
    if (ok) {
        lines.push_back(util::format("共 %d 条记录，%s", rows,
                                     util::formatDuration(seconds).c_str()));
        lines.push_back("");
        lines.push_back(cfg.playtime_detailed ? "已上传汇总 + 详细事件。"
                                              : "已上传汇总（详细事件已关闭）。");
    } else {
        lines.push_back(err);
    }
    UI::result(ok ? "游玩记录上传完成" : "游玩记录上传失败", lines, ok);
}

}  // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    util::ensureDir(SD_ROOT);
    Log::init();

    std::string err;
    if (!UI::init(&err)) {
        Log::printf("界面初始化失败: %s", err.c_str());
        Log::close();
        return 1;
    }

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) Log::printf("socketInitializeDefault failed (0x%x)", rc);

    Config cfg;
    if (!cfg.load(&err)) Log::printf("配置: %s", err.c_str());

    State state;
    if (!state.init(&err)) Log::printf("状态: %s", err.c_str());

    HttpClient http;
    if (!http.init(cfg, &err)) Log::printf("HTTP: %s", err.c_str());

    TitleCache titles;
    ServerStatus status = configuredStatus(cfg);

    for (;;) {
        HomeAction action = UI::home(cfg, state, status);
        if (action == HomeAction::Exit || g_exit) break;

        if (action != HomeAction::Settings && !ensureConfigured(cfg)) continue;

        switch (action) {
            case HomeAction::UploadAll:
                actionUploadAll(cfg, http, state, titles);
                break;
            case HomeAction::UploadSaves:
                actionUploadSaves(cfg, http, state, titles);
                break;
            case HomeAction::Restore:
                actionRestore(cfg, http, state);
                break;
            case HomeAction::UploadAlbum:
                actionAlbum(cfg, http, state);
                break;
            case HomeAction::UploadPlaytime:
                actionPlaytime(cfg, http, state, titles);
                break;
            case HomeAction::Settings: {
                bool saved = UI::settings(cfg, state, &status, &err);
                if (saved) {
                    if (!http.init(cfg, &err)) Log::printf("HTTP: %s", err.c_str());
                }
                break;
            }
            default:
                break;
        }
    }

    titles.shutdown();
    http.cleanup();
    shutdownAccount();
    Log::printf("退出。");
    Log::close();
    socketExit();
    UI::shutdown();
    return 0;
}

#include "i18n.hpp"

#include <map>

namespace {

Lang g_lang = Lang::En;

const std::map<std::string, std::string> &enTable() {
    static const std::map<std::string, std::string> table = {
        {"一键上传全部", "Upload everything"},
        {"存档 · 相册 · 游玩记录", "Saves · Album · Play activity"},
        {"上传存档", "Upload saves"},
        {"只传有变化的存档", "Only changed saves"},
        {"恢复存档", "Restore saves"},
        {"从服务器选择并写回", "Pick a backup and write it back"},
        {"上传相册", "Upload album"},
        {"新增的截图与视频", "New screenshots and videos"},
        {"上传游玩记录", "Upload play activity"},
        {"汇总与详细事件", "Summary and detailed events"},
        {"设置", "Settings"},
        {"服务器、令牌、同步选项", "Server, token, sync options"},
        {"退出", "Exit"},
        {"返回 hbmenu", "Back to hbmenu"},
        {"阶段: ", "Stage: "},
        {"取消", "Cancel"},
        {"返回", "Back"},
        {"字体初始化失败: %s", "Font init failed: %s"},
        {"还没有配置服务器地址", "No server configured yet"},
        {"设备 %s   ·   已记录 %u 个存档索引 / %u 个相册索引",
         "Device %s   ·   %u saves indexed / %u album files indexed"},
        {"选择", "Select"},
        {"方向键", "D-pad"},
        {"移动", "Move"},
        {"开启", "On"},
        {"关闭", "Off"},
        {"未设置", "Not set"},
        {"已设置", "Set"},
        {"服务器地址", "Server URL"},
        {"访问令牌", "Access token"},
        {"设备名称", "Device name"},
        {"设备 ID", "Device ID"},
        {"自动生成", "Generated automatically"},
        {"CA 证书路径", "CA bundle path"},
        {"自动检测", "Auto-detect"},
        {"网络超时（秒）", "Network timeout (sec)"},
        {"单个存档上限（MB）", "Save size limit (MB)"},
        {"只上传有变化的内容", "Upload only changes"},
        {"仅游戏存档（跳过系统）", "Games only (skip system)"},
        {"上传后删除本地相册", "Delete local album after upload"},
        {"详细游玩事件", "Detailed play events"},
        {"跳过 TLS 校验（内网）", "Skip TLS verify (LAN)"},
        {"测试服务器连接", "Test server connection"},
        {"执行", "Run"},
        {"保存设置", "Save settings"},
        {"保存", "Save"},
        {"重新读取配置文件", "Reload config file"},
        {"重新读取", "Reload"},
        {"所有参数都会写回 sdmc:/switch/cloud-sync/config.ini",
         "All settings are saved to sdmc:/switch/cloud-sync/config.ini"},
        {"编辑 / 切换", "Edit / toggle"},
        {"重读", "Reload"},
        {"已配置", "Configured"},
        {"未配置", "Not configured"},
        {"例如 https://cloud.example.com", "e.g. https://cloud.example.com"},
        {"留空表示不发送 Authorization", "Leave empty to send no Authorization header"},
        {"显示在服务器端", "Shown on the server"},
        {"留空则自动生成", "Leave empty to auto-generate"},
        {"留空则自动使用 ca-bundle.pem", "Leave empty to auto-detect ca-bundle.pem"},
        {"请先填写服务器地址", "Fill in the server URL first"},
        {"正在连接 ", "Connecting "},
        {"初始化失败: ", "Init failed: "},
        {"连接成功 HTTP %ld", "Connected, HTTP %ld"},
        {"已连接", "Connected"},
        {"连接失败: ", "Connection failed: "},
        {"连接失败", "Connection failed"},
        {"设置已保存", "Settings saved"},
        {"保存失败: ", "Save failed: "},
        {"已重新读取配置", "Configuration reloaded"},
        {"确认", "Confirm"},
        {"%s   ·   共 %u 份", "%s   ·   %u backups"},
        {"未知游戏", "Unknown game"},
        {"准备中…", "Preparing…"},
        {"%s：上传 %d，跳过 %d，失败 %d",
         "%s: uploaded %d, skipped %d, failed %d"},
        {"还没有配置服务器", "Server is not configured"},
        {"请先在「设置」里填写：", "Open Settings and fill in:"},
        {"服务器地址（例如 http://192.168.1.10:8420）",
         "Server URL (e.g. http://192.168.1.10:8420)"},
        {"访问令牌（和服务器约定）", "Access token (from your server)"},
        {"保存后可以点「测试服务器连接」验证。",
         "Save, then use \"Test server connection\"."},
        {"存档", "Saves"},
        {"存档：", "Saves: "},
        {"存档：已在设置中关闭", "Saves: disabled in Settings"},
        {"相册", "Album"},
        {"相册：", "Album: "},
        {"相册：已在设置中关闭", "Album: disabled in Settings"},
        {"游玩记录：%d 条，共 %s", "Play activity: %d records, %s total"},
        {"游玩记录：", "Play activity: "},
        {"游玩记录：已在设置中关闭", "Play activity: disabled in Settings"},
        {"操作已取消。", "Operation cancelled."},
        {"上传完成", "Upload complete"},
        {"部分任务失败", "Some tasks failed"},
        {"存档上传完成", "Save upload complete"},
        {"存档上传失败", "Save upload failed"},
        {"读取服务器存档列表", "Reading server save list"},
        {"读取列表失败", "Failed to read the list"},
        {"服务器上没有存档", "No saves on the server"},
        {"这台设备还没有上传过任何存档。", "This device has not uploaded any save yet."},
        {"先做一次「上传存档」再回来。", "Run \"Upload saves\" first."},
        {"选择要覆盖到本机的存档", "Choose a save to restore"},
        {"游戏：", "Game: "},
        {"用户：", "User: "},
        {"时间：", "Time: "},
        {"大小：", "Size: "},
        {"这会覆盖本地存档，且不可撤销。",
         "This overwrites the local save and cannot be undone."},
        {"请确认该游戏已经关闭。", "Make sure the game is closed."},
        {"覆盖本地存档？", "Overwrite the local save?"},
        {"恢复完成", "Restore complete"},
        {"存档已经写回本机。", "The save has been written back."},
        {"现在可以启动游戏了。", "You can start the game now."},
        {"恢复失败", "Restore failed"},
        {"相册上传完成", "Album upload complete"},
        {"相册上传失败", "Album upload failed"},
        {"共 %d 条记录，%s", "%d records, %s total"},
        {"已上传汇总 + 详细事件。", "Uploaded summary and detailed events."},
        {"已上传汇总（详细事件已关闭）。", "Uploaded summary (detailed events disabled)."},
        {"游玩记录上传完成", "Play activity upload complete"},
        {"游玩记录上传失败", "Play activity upload failed"},
        {"界面初始化失败: %s", "UI init failed: %s"},
        {"配置: %s", "Config: %s"},
        {"状态: %s", "State: %s"},
        {"退出。", "Bye."},
        {"HTTP %ld（检查 token）", "HTTP %ld (check token)"},
        {"HTTP 404（端点不存在或没有数据）", "HTTP 404 (endpoint missing or no data)"},
        {"HTTP 413（超过服务端体积限制）", "HTTP 413 (server size limit exceeded)"},
        {"本地没有对应的存档（游戏未安装或用其他用户）",
         "No matching local save (game not installed or other user)"},
        {"设置里已关闭存档上传", "Save upload is disabled in Settings"},
        {"警告: %s（游戏名会缺失）", "Warning: %s (game names will be missing)"},
        {"找到 %u 个存档（过滤后 %u 个）", "Found %u save entries (%u after filtering)"},
        {"存档备份", "Save backup"},
        {"已取消", "Cancelled"},
        {"  跳过（未变化）%s", "  skipped (unchanged) %s"},
        {"  失败 %s: %s", "  failed %s: %s"},
        {"  压缩 %s → %s（%lld%%）", "  compressed %s -> %s (%lld%%)"},
        {"  跳过（内容相同）%s", "  skipped (same content) %s"},
        {"  跳过 %s：压缩后 %s 超过上限 %ld MB",
         "  skipped %s: compressed %s exceeds %ld MB limit"},
        {"  失败 %s：无法重新打开备份文件", "  failed %s: cannot reopen the backup file"},
        {"  上传 %s（%s）", "  uploading %s (%s)"},
        {"  完成 %s", "  done %s"},
        {"存档 %s", "saves %s"},
        {"下载存档", "Download save"},
        {"无法创建 %s", "Cannot create %s"},
        {"写入 SD 卡失败", "Failed to write the SD card"},
        {"校验并写入", "Verify and write"},
        {"校验失败（本地 %s / 服务器 %s）", "Checksum mismatch (local %s / server %s)"},
        {"恢复 %s", "restore %s"},
        {"恢复完成: %s", "Restore complete: %s"},
        {"相册: ImageDirectory 挂载成功", "Album: mounted via ImageDirectory"},
        {"ImageDirectory 挂载失败 (0x%x)，改用 sdmc",
         "ImageDirectory mount failed (0x%x), using sdmc"},
        {"无法打开 SD 卡文件系统 (0x%x)", "Cannot open the SD card filesystem (0x%x)"},
        {"找不到 sdmc:/%s", "sdmc:/%s not found"},
        {"相册: sdmc:/%s", "Album: sdmc:/%s"},
        {"  跳过 %s (0x%x)", "  skipped %s (0x%x)"},
        {"设置里已关闭相册上传", "Album upload is disabled in Settings"},
        {"找到 %u 个相册文件", "Found %u album files"},
        {"相册上传", "Album upload"},
        {"  失败：无法打开文件", "  failed: cannot open the file"},
        {"  失败: %s", "  failed: %s"},
        {"  失败: HTTP %ld", "  failed: HTTP %ld"},
        {"  已上传，但删除本地文件失败 (0x%x)",
         "  uploaded, but deleting the local file failed (0x%x)"},
        {"相册 %s", "album %s"},
        {"account 服务不可用", "account service is unavailable"},
        {"读取游玩汇总", "Reading play summaries"},
        {"读取详细事件", "Reading play events"},
        {"上传游玩汇总", "Uploading summaries"},
        {"上传详细记录", "Uploading detailed events"},
        {"设备存档", "Device save"},
        {"用户 ", "User "},
        {"服务器实例已变化，重建本地去重索引（本次会完整上传一次）",
         "Server instance changed; rebuilding the local index (one full upload)"},
        {"日志文件: %s", "Log file: %s"},
        {"语言", "Language"},
        {"自动", "Auto"},
        {"中文", "Chinese"},
    };
    return table;
}

bool systemPrefersChinese() {
    SetLanguage language = SetLanguage_ENUS;
    if (R_FAILED(setInitialize())) return false;
    u64 code = 0;
    if (R_FAILED(setGetSystemLanguage(&code)) || R_FAILED(setMakeLanguage(code, &language))) {
        setExit();
        return false;
    }
    setExit();
    return language == SetLanguage_ZHCN || language == SetLanguage_ZHTW ||
           language == SetLanguage_ZHHANS || language == SetLanguage_ZHHANT;
}

}  // namespace

void i18nInit(const std::string &setting) {
    if (setting == "zh") {
        g_lang = Lang::Zh;
    } else if (setting == "en") {
        g_lang = Lang::En;
    } else {
        g_lang = systemPrefersChinese() ? Lang::Zh : Lang::En;
    }
}

Lang i18nCurrent() { return g_lang; }

std::string i18nSettingForDisplay() {
    return g_lang == Lang::Zh ? "zh" : "en";
}

std::string T(const char *zh) {
    if (!zh) return std::string();
    if (g_lang == Lang::Zh) return std::string(zh);
    const std::map<std::string, std::string> &table = enTable();
    std::map<std::string, std::string>::const_iterator it = table.find(zh);
    if (it == table.end()) return std::string(zh);
    return it->second;
}

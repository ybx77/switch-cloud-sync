#include "playtime_sync.hpp"

#include <cmath>

#include "accounts.hpp"
#include "log.hpp"
#include "state.hpp"
#include "titles.hpp"
#include "ui.hpp"

namespace {

const u8 kAppletIdApplication = 0x01;

struct RawEvent {
    bool account = false;
    std::string type;  ///< "Launched" / "Closed" / "Gained Focus" / "Lost Focus" / login / logout
    u64 title_id = 0;
    std::string user_id;
    long long clock = 0;
    long long steady = 0;
};

struct TitleAgg {
    u64 title_id = 0;
    std::string user_id;
    std::string name;
    long long first_played = 0;
    long long last_played = 0;
    u64 playtime_ns = 0;
    u64 launches = 0;
    bool has_summary = false;
    std::vector<PlayLogEvent> events;
};

std::string userName(AccountUid uid) { return accountUserName(uid); }

std::string appletEventName(u8 et) {
    switch (et) {
        case PdmAppletEventType_Launch:
            return "Launched";
        case PdmAppletEventType_Exit:
        case PdmAppletEventType_Exit5:
        case PdmAppletEventType_Exit6:
            return "Closed";
        case PdmAppletEventType_InFocus:
            return "Gained Focus";
        case PdmAppletEventType_OutOfFocus:
        case PdmAppletEventType_OutOfFocus4:
            return "Lost Focus";
        default:
            return std::string();
    }
}

bool readAllEvents(std::vector<RawEvent> *out, std::string *err) {
    const s32 kBatch = 512;
    PdmPlayEvent *buf = new PdmPlayEvent[kBatch];
    s32 offset = 0;
    s32 total = 0;
    size_t guard = 0;

    for (;;) {
        if (g_abort || g_exit) {
            delete[] buf;
            if (err) *err = "aborted";
            return false;
        }
        total = 0;
        Result rc = pdmqryQueryPlayEvent(offset, buf, kBatch, &total);
        if (R_FAILED(rc)) {
            if (err) *err = util::format("pdmqryQueryPlayEvent failed (0x%x)", rc);
            delete[] buf;
            return false;
        }
        if (total <= 0) break;
        offset += total;

        for (s32 i = 0; i < total; ++i) {
            const PdmPlayEvent &e = buf[i];
            if (e.play_event_type == PdmPlayEventType_Applet) {
                if (e.event_data.applet.log_policy != PdmPlayLogPolicy_All) continue;
                if (e.event_data.applet.applet_id != kAppletIdApplication) continue;

                u64 title = ((u64)e.event_data.applet.program_id[0] << 32) |
                            (u64)e.event_data.applet.program_id[1];
                if (title < APP_ID_GAME_MIN) continue;
                std::string type = appletEventName(e.event_data.applet.event_type);
                if (type.empty()) continue;

                RawEvent ev;
                ev.account = false;
                ev.type = type;
                ev.title_id = title;
                ev.clock = (long long)e.timestamp_user;
                ev.steady = (long long)e.timestamp_steady;
                out->push_back(ev);
            } else if (e.play_event_type == PdmPlayEventType_Account) {
                u8 t = e.event_data.account.type;
                if (t > 1) continue;

                RawEvent ev;
                ev.account = true;
                ev.type = (t == 0) ? "Account Login" : "Account Logout";
                AccountUid uid;
                memset(&uid, 0, sizeof(uid));
                uid.uid[0] = ((u64)e.event_data.account.uid[0] << 32) | e.event_data.account.uid[1];
                uid.uid[1] = ((u64)e.event_data.account.uid[2] << 32) | e.event_data.account.uid[3];
                ev.user_id = util::accountUidHex(uid);
                ev.clock = (long long)e.timestamp_user;
                ev.steady = (long long)e.timestamp_steady;
                out->push_back(ev);
            }
        }

        if (++guard > 4096) break;  // 2M events: give up rather than loop forever
    }

    delete[] buf;
    return true;
}

std::string aggKey(u64 title, const std::string &user) {
    return util::toHexU64(title, 16) + "/" + user;
}

void appendSessionEvents(TitleAgg *agg, const std::vector<RawEvent> &events, size_t start,
                         size_t end) {
    bool last_was_out_focus = false;
    for (size_t i = start; i <= end && i < events.size(); ++i) {
        const RawEvent &e = events[i];
        if (e.type == "Lost Focus" && last_was_out_focus) continue;
        last_was_out_focus = (e.type == "Lost Focus");

        PlayLogEvent pe;
        pe.type = e.type;
        pe.clock = e.clock;
        pe.steady = e.steady;
        agg->events.push_back(pe);
    }
}

void groupSessions(const std::vector<RawEvent> &events, std::map<std::string, TitleAgg> *aggs,
                   TitleCache &titles) {
    size_t i = 0;
    while (i < events.size()) {
        const RawEvent &launch = events[i];
        if (launch.account || launch.type != "Launched") {
            ++i;
            continue;
        }

        size_t end = i;
        std::string session_user;
        for (size_t j = i + 1; j < events.size(); ++j) {
            if (!events[j].account && events[j].type == "Launched") {
                end = j - 1;
                break;
            }
            end = j;
            if (events[j].account && events[j].type == "Account Login" &&
                session_user.empty()) {
                session_user = events[j].user_id;
            }
            if (!events[j].account && events[j].type == "Closed") {
                break;
            }
        }

        if (!session_user.empty()) {
            std::string key = aggKey(launch.title_id, session_user);
            TitleAgg &agg = (*aggs)[key];
            if (agg.title_id == 0) {
                agg.title_id = launch.title_id;
                agg.user_id = session_user;
                agg.name = titles.nameFor(launch.title_id);
            }
            appendSessionEvents(&agg, events, i, end);
        }

        i = (end >= i ? end + 1 : i + 1);
    }
}

}  // namespace

bool playLogCollect(TitleCache &titles, std::vector<PlayLogUser> *out, std::string *err) {
    out->clear();

    if (!titles.ensureLoaded(err)) return false;

    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 user_count = 0;
    if (!ensureAccount()) {
        if (err) *err = "account 服务不可用";
        return false;
    }
    Result rc = accountListAllUsers(uids, ACC_USER_LIST_SIZE, &user_count);
    if (R_FAILED(rc) || user_count <= 0) {
        if (err) *err = R_FAILED(rc) ? util::format("accountListAllUsers failed (0x%x)", rc)
                                     : "no user profile found";
        return false;
    }

    rc = pdmqryInitialize();
    if (R_FAILED(rc)) {
        if (err) *err = util::format("pdmqryInitialize failed (0x%x)", rc);
        return false;
    }

    std::map<std::string, TitleAgg> aggs;
    const std::vector<u64> &apps = titles.ids();

    // 1) Summaries for every installed application x user.
    UI::progressPhase("读取游玩汇总", (int)apps.size());
    for (size_t a = 0; a < apps.size(); ++a) {
        if (!UI::pump()) {
            pdmqryExit();
            if (err) *err = "aborted";
            return false;
        }
        UI::progressStep((int)a + 1, titles.nameFor(apps[a]));

        for (s32 u = 0; u < user_count; ++u) {
            PdmPlayStatistics stats;
            memset(&stats, 0, sizeof(stats));
            rc = pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(apps[a], uids[u], true,
                                                                         &stats);
            if (R_FAILED(rc)) continue;
            if (stats.playtime == 0 && stats.total_launches == 0) continue;

            std::string user = util::accountUidHex(uids[u]);
            TitleAgg &agg = aggs[aggKey(apps[a], user)];
            agg.title_id = apps[a];
            agg.user_id = user;
            agg.name = titles.nameFor(apps[a]);
            agg.first_played = (long long)stats.first_timestamp_user;
            agg.last_played = (long long)stats.last_timestamp_user;
            agg.playtime_ns = stats.playtime;
            agg.launches = stats.total_launches;
            agg.has_summary = true;
        }
    }

    // 2) Detailed events from the system play log.
    UI::progressPhase("读取详细事件", 0);
    std::vector<RawEvent> events;
    std::string read_err;
    bool events_ok = readAllEvents(&events, &read_err);
    if (!events_ok && read_err != "aborted") {
        Log::printf("Warning: play event log: %s", read_err.c_str());
    }
    if (read_err == "aborted") {
        pdmqryExit();
        if (err) *err = "aborted";
        return false;
    }

    if (!events.empty()) {
        Log::printf("Play log: %u events read", (unsigned)events.size());
        groupSessions(events, &aggs, titles);
    }

    // 3) Fill summaries for titles that only exist in the event log (uninstalled games etc).
    for (std::map<std::string, TitleAgg>::iterator it = aggs.begin(); it != aggs.end(); ++it) {
        TitleAgg &agg = it->second;
        if (agg.has_summary) continue;
        for (s32 u = 0; u < user_count; ++u) {
            if (util::accountUidHex(uids[u]) != agg.user_id) continue;
            PdmPlayStatistics stats;
            memset(&stats, 0, sizeof(stats));
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(
                    agg.title_id, uids[u], true, &stats))) {
                agg.first_played = (long long)stats.first_timestamp_user;
                agg.last_played = (long long)stats.last_timestamp_user;
                agg.playtime_ns = stats.playtime;
                agg.launches = stats.total_launches;
                agg.has_summary = true;
            }
        }
    }

    pdmqryExit();

    // 4) Build the per-user structure.
    std::map<std::string, size_t> user_index;
    std::vector<PlayLogUser> users;
    for (s32 u = 0; u < user_count; ++u) {
        PlayLogUser user;
        user.id = util::accountUidHex(uids[u]);
        user.name = userName(uids[u]);
        user_index[user.id] = users.size();
        users.push_back(user);
    }

    for (std::map<std::string, TitleAgg>::iterator it = aggs.begin(); it != aggs.end(); ++it) {
        TitleAgg &agg = it->second;
        if (agg.playtime_ns == 0 && agg.events.empty()) continue;

        std::map<std::string, size_t>::iterator ui = user_index.find(agg.user_id);
        if (ui == user_index.end()) {
            PlayLogUser user;
            user.id = agg.user_id;
            user.name.clear();
            user_index[user.id] = users.size();
            users.push_back(user);
            ui = user_index.find(agg.user_id);
        }

        PlayLogTitle title;
        title.title_id = agg.title_id;
        title.name = agg.name;
        title.first_played = agg.first_played;
        title.last_played = agg.last_played;
        title.playtime_ns = agg.playtime_ns;
        title.launches = agg.launches;
        title.events = agg.events;
        users[ui->second].titles.push_back(title);
    }

    out->clear();
    for (size_t u = 0; u < users.size(); ++u) {
        if (users[u].titles.empty()) continue;
        std::sort(users[u].titles.begin(), users[u].titles.end(),
                  [](const PlayLogTitle &a, const PlayLogTitle &b) {
                      if (a.playtime_ns != b.playtime_ns) return a.playtime_ns > b.playtime_ns;
                      return a.title_id < b.title_id;
                  });
        out->push_back(users[u]);
    }
    return true;
}

std::string playLogToJson(const std::vector<PlayLogUser> &users, const std::string &device) {
    long long now = util::nowUnix();
    std::string j;
    j.reserve(64 * 1024);
    j += "{";
    j += "\"exportString\":\"" + util::jsonEscape(util::formatUnixTime(now)) + " UTC\",";
    j += "\"exportTimestamp\":" + util::format("%lld", now) + ",";
    j += "\"exportVersion\":\"" APP_VERSION "\",";
    j += "\"source\":\"SwitchCloudSync\",";
    j += "\"device\":\"" + util::jsonEscape(device) + "\",";
    j += "\"users\":[";

    for (size_t u = 0; u < users.size(); ++u) {
        if (u) j += ",";
        const PlayLogUser &user = users[u];
        j += "{\"id\":\"" + util::toUpper(user.id) + "\",";
        j += "\"name\":\"" + util::jsonEscape(user.name) + "\",";
        j += "\"titles\":[";
        for (size_t t = 0; t < user.titles.size(); ++t) {
            if (t) j += ",";
            const PlayLogTitle &title = user.titles[t];
            j += "{\"id\":\"" + util::toUpper(util::toHexU64(title.title_id, 16)) + "\",";
            j += "\"name\":\"" + util::jsonEscape(title.name) + "\",";
            j += "\"summary\":{";
            j += "\"firstPlayed\":" + util::format("%lld", title.first_played) + ",";
            j += "\"lastPlayed\":" + util::format("%lld", title.last_played) + ",";
            j += "\"launches\":" + util::format("%llu", (unsigned long long)title.launches) + ",";
            j += "\"playtime\":" +
                 util::format("%llu", (unsigned long long)(title.playtime_ns / 1000000000ULL));
            j += "},\"events\":[";
            for (size_t e = 0; e < title.events.size(); ++e) {
                if (e) j += ",";
                const PlayLogEvent &ev = title.events[e];
                j += "{\"clockTimestamp\":" + util::format("%lld", ev.clock);
                j += ",\"steadyTimestamp\":" + util::format("%lld", ev.steady);
                j += ",\"type\":\"" + util::jsonEscape(ev.type) + "\"}";
            }
            j += "]}";
        }
        j += "]}";
    }
    j += "]}";
    return j;
}

bool playtimeSyncUpload(const Config &cfg, HttpClient &http, TitleCache &titles, State &state,
                        int *rows_out, u64 *total_seconds_out, std::string *err) {
    if (rows_out) *rows_out = 0;
    if (total_seconds_out) *total_seconds_out = 0;
    if (!cfg.playtime_enabled) {
        if (err) *err = "[playtime] enabled = 0";
        return false;
    }

    std::vector<PlayLogUser> users;
    if (!playLogCollect(titles, &users, err)) return false;

    // Compact summary TSV (kept for compatibility with simple backends).
    u64 total_ns = 0;
    int row_count = 0;
    std::string body =
        "#switchcloud-playtime v1\n"
        "#title_id\tuser_id\tplaytime_ns\tlaunches\tlast_played\ttitle_name\n";
    std::vector<std::pair<std::string, u64> > top;

    for (size_t u = 0; u < users.size(); ++u) {
        for (size_t t = 0; t < users[u].titles.size(); ++t) {
            const PlayLogTitle &title = users[u].titles[t];
            total_ns += title.playtime_ns;
            ++row_count;
            std::string name = title.name;
            for (size_t i = 0; i < name.size(); ++i) {
                if (name[i] == '\t' || name[i] == '\n' || name[i] == '\r') name[i] = ' ';
            }
            body += util::toHexU64(title.title_id, 16) + "\t" + users[u].id + "\t" +
                    util::format("%llu", (unsigned long long)title.playtime_ns) + "\t" +
                    util::format("%llu", (unsigned long long)title.launches) + "\t" +
                    util::format("%lld", title.last_played) + "\t" + name + "\n";
            top.push_back(std::make_pair(name, title.playtime_ns));
        }
    }

    std::sort(top.begin(), top.end(),
              [](const std::pair<std::string, u64> &a, const std::pair<std::string, u64> &b) {
                  return a.second > b.second;
              });
    for (size_t i = 0; i < top.size() && i < 5; ++i) {
        Log::printf("  %s  %s", top[i].first.c_str(),
                    util::formatDuration(top[i].second / 1000000000ULL).c_str());
    }

    const std::string device = effectiveDeviceId(cfg, state);

    UI::progressPhase("上传游玩汇总", 1);
    {
        std::string url = cfg.api("/playtime") + "?device=" + util::urlEncode(device);
        std::vector<std::string> headers;
        headers.push_back("X-Device-Name: " + util::urlEncode(cfg.device_name));
        MemoryUploadSource src(body);
        HttpResponse resp;
        std::string http_err;
        if (!http.put(url, &src, "text/tab-separated-values; charset=utf-8", headers, &resp,
                      &http_err)) {
            if (err) *err = http_err;
            return false;
        }
        if (resp.status < 200 || resp.status >= 300) {
            if (err) *err = util::format("HTTP %ld", resp.status);
            return false;
        }
    }

    if (cfg.playtime_detailed) {
        UI::progressPhase("上传详细记录", 1);
        std::string json = playLogToJson(users, device);
        Log::printf("Detailed play log: %s, %u users",
                    util::formatBytes((long long)json.size()).c_str(), (unsigned)users.size());

        std::string url = cfg.api("/playlog") + "?device=" + util::urlEncode(device);
        std::vector<std::string> headers;
        headers.push_back("X-Device-Name: " + util::urlEncode(cfg.device_name));
        MemoryUploadSource src(json);
        HttpResponse resp;
        std::string http_err;
        if (!http.put(url, &src, "application/json; charset=utf-8", headers, &resp, &http_err)) {
            if (err) *err = http_err;
            return false;
        }
        if (resp.status < 200 || resp.status >= 300) {
            if (err) *err = util::format("HTTP %ld (detailed play log)", resp.status);
            return false;
        }
    }

    state.setLastSync(util::format("playtime %s", util::formatUnixTime(util::nowUnix()).c_str()));
    state.flush();

    if (rows_out) *rows_out = row_count;
    if (total_seconds_out) *total_seconds_out = total_ns / 1000000000ULL;
    return true;
}

#include "i18n.hpp"
#include "server_identity.hpp"

#include "log.hpp"
#include "state.hpp"

void syncServerIdentity(const Config &cfg, HttpClient &http, State &state) {
    HttpResponse resp;
    std::string err;
    if (!http.getText(cfg.api("/ping"), &resp, &err)) return;
    if (resp.status != 200) return;

    std::string body = util::trim(resp.body);
    if (body.empty()) return;

    size_t space = body.find_last_of(" \t\r\n");
    std::string id = space == std::string::npos ? body : body.substr(space + 1);
    id = util::trim(id);
    if (id.empty()) return;

    if (state.serverId() != id) {
        Log::printf(T("服务器实例已变化，重建本地去重索引（本次会完整上传一次）"));
        state.clearIndexes();
        state.setServerId(id);
        state.flush();
    }
}

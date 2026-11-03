#include "http.hpp"

#include "log.hpp"
#include "ui.hpp"

namespace {

const size_t kMaxBodyBytes = 8u * 1024u * 1024u;

struct WriteCtx {
    HttpResponse *resp = nullptr;
    DownloadSink *sink = nullptr;
    const char *label = "transfer";
    bool print_progress = false;
    int last_pct = -1;
};

size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    WriteCtx *ctx = (WriteCtx *)userdata;
    size_t len = size * nmemb;

    if (ctx->sink) {
        if (!ctx->sink->write(ptr, len)) return 0;
    } else if (ctx->resp && ctx->resp->body.size() < kMaxBodyBytes) {
        size_t room = kMaxBodyBytes - ctx->resp->body.size();
        ctx->resp->body.append(ptr, len < room ? len : room);
    }
    if (ctx->resp) ctx->resp->received += (long long)len;
    return len;
}

int xferinfoCb(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
               curl_off_t ulnow) {
    WriteCtx *ctx = (WriteCtx *)userdata;

    // Keeps the progress screen alive, handles B (cancel) and HOME (exit).
    if (!UI::pump()) return 1;

    if (!ctx->print_progress) return 0;

    curl_off_t total = ultotal > 0 ? ultotal : dltotal;
    curl_off_t now = ultotal > 0 ? ulnow : dlnow;
    if (total > 0) {
        int pct = (int)((now * 100) / total);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct != ctx->last_pct) {
            ctx->last_pct = pct;
            UI::progressPercent(pct);
        }
    }
    return 0;
}

size_t readCb(char *buffer, size_t size, size_t nitems, void *userdata) {
    UploadSource *src = (UploadSource *)userdata;
    if (g_abort) return CURL_READFUNC_ABORT;
    return src->read(buffer, size * nitems);
}

}  // namespace

FileUploadSource::FileUploadSource(const std::string &path) {
    file_ = fopen(path.c_str(), "rb");
    if (file_) {
        fseek(file_, 0, SEEK_END);
        long pos = ftell(file_);
        fseek(file_, 0, SEEK_SET);
        size_ = pos < 0 ? 0 : (long long)pos;
    }
}

FileUploadSource::~FileUploadSource() {
    if (file_) fclose(file_);
}

size_t FileUploadSource::read(char *buf, size_t cap) {
    if (!file_) return 0;
    return fread(buf, 1, cap, file_);
}

size_t MemoryUploadSource::read(char *buf, size_t cap) {
    if (offset_ >= data_.size()) return 0;
    size_t n = data_.size() - offset_;
    if (n > cap) n = cap;
    memcpy(buf, data_.data() + offset_, n);
    offset_ += n;
    return n;
}

FileDownloadSink::FileDownloadSink(const std::string &path) {
    util::ensureParentDirs(path);
    file_ = fopen(path.c_str(), "wb");
    ok_ = file_ != nullptr;
}

FileDownloadSink::~FileDownloadSink() { close(); }

bool FileDownloadSink::write(const char *data, size_t len) {
    if (!file_ || !ok_) return false;
    if (fwrite(data, 1, len, file_) != len) {
        ok_ = false;
        return false;
    }
    return true;
}

bool FileDownloadSink::close() {
    if (!file_) return ok_;
    if (fflush(file_) != 0) ok_ = false;
    if (fclose(file_) != 0) ok_ = false;
    file_ = nullptr;
    return ok_;
}

HttpClient::~HttpClient() { cleanup(); }

bool HttpClient::init(const Config &cfg, std::string *err) {
    static bool global_ready = false;
    if (!global_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        global_ready = true;
    }

    cfg_ = cfg;
    if (cfg_.ca_bundle.empty()) {
        // Common setup: drop a PEM at sdmc:/switch/cloud-sync/ca-bundle.pem and it is used.
        FILE *probe = fopen(SD_CA_BUNDLE, "rb");
        if (probe) {
            fclose(probe);
            cfg_.ca_bundle = SD_CA_BUNDLE;
        }
    }
    if (!curl_) {
        curl_ = curl_easy_init();
        if (!curl_) {
            if (err) *err = "curl_easy_init failed";
            return false;
        }
    }
    return true;
}

void HttpClient::cleanup() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

bool HttpClient::perform(const std::string &url, UploadSource *src, DownloadSink *sink,
                         const std::string &content_type,
                         const std::vector<std::string> &extra_headers, bool file_transfer,
                         HttpResponse *resp, std::string *err) {
    if (!curl_) {
        if (err) *err = "curl is not initialized";
        return false;
    }
    *resp = HttpResponse();

    if (g_abort || g_exit) {
        if (err) *err = "aborted";
        return false;
    }

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, APP_USER_AGENT);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_LIMIT, 512L);
    curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_TIME, (long)cfg_.timeout_sec);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, (long)(cfg_.insecure_tls ? 0 : 1));
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, (long)(cfg_.insecure_tls ? 0 : 2));
    if (!cfg_.ca_bundle.empty()) {
        curl_easy_setopt(curl_, CURLOPT_CAINFO, cfg_.ca_bundle.c_str());
    }

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Expect:");
    headers = curl_slist_append(headers, "Accept: text/plain, application/octet-stream");
    if (!cfg_.token.empty()) {
        std::string auth = "Authorization: Bearer " + cfg_.token;
        headers = curl_slist_append(headers, auth.c_str());
    }
    if (!content_type.empty()) {
        std::string ct = "Content-Type: " + content_type;
        headers = curl_slist_append(headers, ct.c_str());
    }
    for (size_t i = 0; i < extra_headers.size(); ++i) {
        headers = curl_slist_append(headers, extra_headers[i].c_str());
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    WriteCtx ctx;
    ctx.resp = resp;
    ctx.sink = sink;
    ctx.label = src ? "upload" : "download";
    ctx.print_progress = file_transfer;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, xferinfoCb);
    curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &ctx);

    if (src) {
        curl_easy_setopt(curl_, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl_, CURLOPT_READFUNCTION, readCb);
        curl_easy_setopt(curl_, CURLOPT_READDATA, src);
        curl_easy_setopt(curl_, CURLOPT_INFILESIZE_LARGE, (curl_off_t)src->size());
    }

    CURLcode res = curl_easy_perform(curl_);

    long status = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
    resp->status = status;

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        if (err) {
            if (res == CURLE_ABORTED_BY_CALLBACK) {
                *err = "aborted";
            } else {
                *err = util::format("%s (curl %d)", curl_easy_strerror(res), (int)res);
            }
        }
        return false;
    }
    return true;
}

bool HttpClient::getText(const std::string &url, HttpResponse *resp, std::string *err) {
    return perform(url, nullptr, nullptr, std::string(), std::vector<std::string>(), false, resp,
                   err);
}

bool HttpClient::put(const std::string &url, UploadSource *src, const std::string &content_type,
                     const std::vector<std::string> &extra_headers, HttpResponse *resp,
                     std::string *err) {
    return perform(url, src, nullptr, content_type, extra_headers, true, resp, err);
}

bool HttpClient::download(const std::string &url, DownloadSink *sink, HttpResponse *resp,
                          std::string *err) {
    return perform(url, nullptr, sink, std::string(), std::vector<std::string>(), true, resp, err);
}

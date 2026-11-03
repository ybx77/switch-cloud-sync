#pragma once

#include <curl/curl.h>

#include "common.hpp"
#include "config.hpp"

struct HttpResponse {
    long status = 0;
    long long received = 0;
    std::string body;
};

class UploadSource {
public:
    virtual ~UploadSource() {}
    virtual long long size() = 0;
    /// Returns 0 on end of file, or CURL_READFUNC_ABORT on error.
    virtual size_t read(char *buf, size_t cap) = 0;
};

class DownloadSink {
public:
    virtual ~DownloadSink() {}
    virtual bool write(const char *data, size_t len) = 0;
};

/// Uploads a file from the SD card / stdio filesystem.
class FileUploadSource : public UploadSource {
public:
    explicit FileUploadSource(const std::string &path);
    ~FileUploadSource() override;

    bool ok() const { return file_ != nullptr; }
    long long size() override { return size_; }
    size_t read(char *buf, size_t cap) override;

private:
    FILE *file_ = nullptr;
    long long size_ = 0;
};

/// Uploads an in-memory buffer.
class MemoryUploadSource : public UploadSource {
public:
    explicit MemoryUploadSource(const std::string &data) : data_(data) {}

    long long size() override { return (long long)data_.size(); }
    size_t read(char *buf, size_t cap) override;

private:
    std::string data_;
    size_t offset_ = 0;
};

/// Writes a download to a file.
class FileDownloadSink : public DownloadSink {
public:
    explicit FileDownloadSink(const std::string &path);
    ~FileDownloadSink() override;

    bool ok() const { return ok_; }
    bool close();
    bool write(const char *data, size_t len) override;

private:
    FILE *file_ = nullptr;
    bool ok_ = false;
};

class HttpClient {
public:
    HttpClient() {}
    ~HttpClient();

    bool init(const Config &cfg, std::string *err);
    void cleanup();

    const Config &config() const { return cfg_; }

    bool getText(const std::string &url, HttpResponse *resp, std::string *err);
    bool put(const std::string &url, UploadSource *src, const std::string &content_type,
             const std::vector<std::string> &extra_headers, HttpResponse *resp, std::string *err);
    bool download(const std::string &url, DownloadSink *sink, HttpResponse *resp, std::string *err);

private:
    bool perform(const std::string &url, UploadSource *src, DownloadSink *sink,
                 const std::string &content_type, const std::vector<std::string> &extra_headers,
                 bool upload_progress, HttpResponse *resp, std::string *err);

    CURL *curl_ = nullptr;
    Config cfg_;
};

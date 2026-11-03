#pragma once

#include "common.hpp"

/// Installed application list + NACP title names, cached for the session.
class TitleCache {
public:
    ~TitleCache() { shutdown(); }

    bool ensureLoaded(std::string *err);
    const std::vector<u64> &ids() const { return ids_; }

    /// Returns the localized title name, or an empty string when unavailable.
    std::string nameFor(u64 app_id);

    void shutdown();

private:
    bool loaded_ = false;
    bool ns_ready_ = false;
    std::vector<u64> ids_;
    std::map<u64, std::string> names_;
};

#include "titles.hpp"

#include "log.hpp"

namespace {

std::string cleanName(const char *raw) {
    std::string out;
    for (size_t i = 0; i < 0x200 && raw[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)raw[i];
        out.push_back(c < 0x20 ? ' ' : (char)c);
    }
    return util::trim(out);
}

}  // namespace

bool TitleCache::ensureLoaded(std::string *err) {
    if (loaded_) return true;

    if (!ns_ready_) {
        Result rc = nsInitialize();
        if (R_FAILED(rc)) {
            if (err) *err = util::format("nsInitialize failed (0x%x)", rc);
            return false;
        }
        ns_ready_ = true;
    }

    NsApplicationRecord records[64];
    s32 offset = 0;
    for (;;) {
        s32 count = 0;
        Result rc = nsListApplicationRecord(records, 64, offset, &count);
        if (R_FAILED(rc)) {
            if (err) *err = util::format("nsListApplicationRecord failed (0x%x)", rc);
            return false;
        }
        if (count <= 0) break;

        for (s32 i = 0; i < count; ++i) {
            ids_.push_back(records[i].application_id);
        }
        offset += count;
        if (count < 64) break;
    }

    loaded_ = true;
    Log::printf("Found %u installed applications", (unsigned)ids_.size());
    return true;
}

std::string TitleCache::nameFor(u64 app_id) {
    std::map<u64, std::string>::iterator it = names_.find(app_id);
    if (it != names_.end()) return it->second;

    std::string name;
    if (ns_ready_) {
        NsApplicationControlData *data =
            (NsApplicationControlData *)malloc(sizeof(NsApplicationControlData));
        if (data) {
            memset(data, 0, sizeof(NsApplicationControlData));
            u64 actual = 0;
            Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, data,
                                                   sizeof(NsApplicationControlData), &actual);
            if (R_FAILED(rc)) {
                rc = nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, app_id,
                                                 data, sizeof(NsApplicationControlData), &actual);
            }
            if (R_SUCCEEDED(rc)) {
                NacpLanguageEntry *lang = nullptr;
                if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&data->nacp, &lang)) && lang) {
                    name = cleanName(lang->name);
                }
                if (name.empty()) {
                    // Version-independent fallback (also handles the compressed table on newer
                    // firmware); do not poke at NacpStruct internals here.
                    lang = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &lang)) && lang) {
                        name = cleanName(lang->name);
                    }
                }
            }
            free(data);
        }
    }

    names_[app_id] = name;
    return name;
}

void TitleCache::shutdown() {
    if (ns_ready_) {
        nsExit();
        ns_ready_ = false;
    }
    loaded_ = false;
    ids_.clear();
    names_.clear();
}

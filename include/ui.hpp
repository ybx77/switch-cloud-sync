#pragma once

#include "common.hpp"
#include "config.hpp"

class State;

enum class HomeAction {
    UploadAll,
    UploadSaves,
    Restore,
    UploadAlbum,
    UploadPlaytime,
    Settings,
    Exit,
};

struct ServerStatus {
    bool configured = false;
    bool checked = false;
    bool ok = false;
    std::string text;  ///< short label for the header pill
};

namespace UI {

bool init(std::string *err);
void shutdown();

HomeAction home(const Config &cfg, const State &state, const ServerStatus &status);

/// Full settings editor. Returns true when the user saved changes.
bool settings(Config &cfg, const State &state, ServerStatus *status, std::string *err);

void message(const std::string &title, const std::vector<std::string> &lines, bool error = false);
bool confirm(const std::string &title, const std::vector<std::string> &lines);
int selectRemote(const std::vector<RemoteSave> &saves, const std::string &subtitle);
void result(const std::string &title, const std::vector<std::string> &lines, bool ok);

// Progress screen used by the sync modules. `pump()` must be called between units of work;
// it redraws (throttled), reads input and returns false when the user aborted / app must exit.
void progressBegin(const std::string &title, int total);
void progressPhase(const std::string &name, int total);
void progressStep(int done, const std::string &label);
void progressPercent(int percent);
void progressEnd();
bool pump();
bool isAborted();

void sleepFrame();

}  // namespace UI

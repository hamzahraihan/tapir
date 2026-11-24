#pragma once

#include "config.h"
#include "util/config_path.h"

#include <string>
#include <vector>

namespace tapir::config_file {

struct LoadResult {
    Settings settings;
    std::vector<config::Diagnostic> diagnostics;
    bool changed = false;
    bool valid = true;
};

bool ensureDefaults();

class Watcher {
public:
    Watcher() = default;
    ~Watcher() = default;
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    bool start(const Settings& initial);
    const Settings& settings() const noexcept { return lastGood_; }
    HANDLE handle() const noexcept { return directoryWatcher_.handle(); }
    void acknowledge() { directoryWatcher_.acknowledge(); }
    LoadResult refresh();

private:
    std::string lastText_;
    Settings lastGood_;
    util::config::DirectoryWatcher directoryWatcher_;
};

} // namespace tapir::config_file

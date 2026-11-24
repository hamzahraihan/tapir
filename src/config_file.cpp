#include "config_file.h"

#include "diagnostics.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace tapir::config_file {
namespace {

std::string readAll(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

bool ensureDefaults() {
    const std::wstring dir = util::config::directory();
    if (dir.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return false;
    const std::wstring path = util::config::configPath();
    if (std::filesystem::exists(path, error)) return true;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << config::defaultText();
    return output.good();
}

bool Watcher::start(const Settings& initial) {
    lastGood_ = initial;
    lastText_.clear();
    if (!ensureDefaults()) return false;
    if (!directoryWatcher_.start(util::config::directory())) {
        diagnostics::append(util::config::logPath(),
                            "warning: config change watcher unavailable\n");
    }
    refresh();
    return true;
}

LoadResult Watcher::refresh() {
    LoadResult result;
    const std::string text = readAll(util::config::configPath());
    result.changed = text != lastText_;
    if (!result.changed) {
        result.settings = lastGood_;
        return result;
    }
    lastText_ = text;

    const auto parsed = config::parse(text);
    result.valid = parsed.valid;
    result.diagnostics = parsed.diagnostics;
    result.changed = parsed.valid && !(parsed.settings == lastGood_);
    result.settings = parsed.valid ? parsed.settings : lastGood_;
    if (parsed.valid) lastGood_ = result.settings;

    for (const auto& diagnostic : parsed.diagnostics) {
        std::string line = diagnostic.severity == config::Severity::Error
                               ? "error: "
                               : "warning: ";
        line += diagnostic.message;
        line += '\n';
        diagnostics::append(util::config::logPath(), line);
    }
    return result;
}

} // namespace tapir::config_file

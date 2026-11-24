#include "config_path.h"

#include "constants.h"

#include <filesystem>

namespace tapir::util::config {
namespace {

std::wstring environmentValue(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size - 1);
    return value;
}

} // namespace

std::wstring directory() {
    const std::wstring override = environmentValue(L"TAPIR_CONFIG_DIR");
    if (!override.empty()) return override;
    const std::wstring profile = environmentValue(L"USERPROFILE");
    if (profile.empty()) return {};
    return (std::filesystem::path(profile) / L".config" /
            std::wstring(kAppName)).wstring();
}

std::wstring configPath() {
    return (std::filesystem::path(directory()) / L"config.toml").wstring();
}

std::wstring logPath() {
    return (std::filesystem::path(directory()) / L"tapir.log").wstring();
}

std::wstring historyPath() {
    return (std::filesystem::path(directory()) / L"history.dat").wstring();
}

std::wstring historyTempPath() {
    return (std::filesystem::path(directory()) / L"history.dat.tmp").wstring();
}

std::wstring historyBackupPath() {
    return (std::filesystem::path(directory()) / L"history.dat.bak").wstring();
}

DirectoryWatcher::~DirectoryWatcher() {
    if (handle_ != nullptr) FindClose(handle_);
}

bool DirectoryWatcher::start(const std::wstring& dir) {
    handle_ = FindFirstChangeNotificationW(
        dir.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_SIZE);
    return handle_ != nullptr;
}

void DirectoryWatcher::acknowledge() {
    if (handle_ != nullptr) FindNextChangeNotification(handle_);
}

} // namespace tapir::util::config

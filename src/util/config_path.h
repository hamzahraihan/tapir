#pragma once

#include <windows.h>

#include <string>

namespace tapir::util::config {

std::wstring directory();
std::wstring configPath();
std::wstring logPath();

class DirectoryWatcher {
public:
    DirectoryWatcher() = default;
    ~DirectoryWatcher();
    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    bool start(const std::wstring& dir);
    HANDLE handle() const noexcept { return handle_; }
    void acknowledge();

private:
    HANDLE handle_ = nullptr;
};

} // namespace tapir::util::config

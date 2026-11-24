#include "util/startup.h"

#include "constants.h"
#include "diagnostics.h"
#include "util/config_path.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace tapir::util::startup {
namespace {

// KEY_WOW64_64KEY below pins both functions to the native 64-bit view of
// HKCU\Software, so the Run value is the one the 64-bit shell reads regardless
// of registry redirection or the bitness of the process doing the writing.
constexpr wchar_t kRunPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const std::wstring kValueName(kAppName);

class Key {
  public:
    explicit Key(HKEY handle = nullptr) noexcept : handle_(handle) {}
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() {
        if (handle_ != nullptr) RegCloseKey(handle_);
    }

    HKEY get() const noexcept { return handle_; }

  private:
    HKEY handle_;
};

void setError(std::wstring* error, const wchar_t* reason) {
    if (error != nullptr) *error = reason;
}

bool modulePath(std::wstring& path) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return false;
        if (static_cast<std::size_t>(length) < buffer.size()) {
            buffer.resize(length);
            path = std::move(buffer);
            return true;
        }
        if (buffer.size() >= 32768) return false;
        buffer.resize(buffer.size() * 2);
    }
}

struct ValueInfo {
    bool present = false;
    bool isString = false;
    std::wstring text;
};

enum class ReadResult { Ok, Error };

ReadResult readValue(HKEY key, ValueInfo& info) {
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(key, kValueName.c_str(), nullptr, &type,
                                      nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return ReadResult::Ok;
    if (status != ERROR_SUCCESS) return ReadResult::Error;

    info.present = true;
    info.isString = type == REG_SZ;
    std::wstring data(
        (static_cast<std::size_t>(bytes) + sizeof(wchar_t) - 1) / sizeof(wchar_t),
        L'\0');
    if (bytes != 0) {
        status = RegQueryValueExW(key, kValueName.c_str(), nullptr, &type,
                                  reinterpret_cast<BYTE*>(data.data()), &bytes);
        if (status != ERROR_SUCCESS) return ReadResult::Error;
    }
    const auto end = std::find(data.begin(), data.end(), L'\0');
    info.text.assign(data.begin(), end);
    return ReadResult::Ok;
}

} // namespace

void applyFromSettings(bool enable) {
    if (!setEnabled(enable)) {
        diagnostics::append(config::logPath(),
                            "warning: could not update Windows startup setting\n");
    }
}

std::wstring commandLine(std::wstring_view exePath) {
    if (exePath.empty()) return {};
    std::wstring result;
    result.reserve(exePath.size() + 2);
    result.push_back(L'"');
    result.append(exePath.data(), exePath.size());
    result.push_back(L'"');
    return result;
}

bool setEnabled(bool enable, std::wstring* error) {
    HKEY handle = nullptr;
    LSTATUS status = ERROR_SUCCESS;
    if (enable) {
        status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunPath, 0, nullptr,
                                 REG_OPTION_NON_VOLATILE,
                                 KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY,
                                 nullptr, &handle, nullptr);
    } else {
        status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunPath, 0,
                               KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY,
                               &handle);
    }
    if (status != ERROR_SUCCESS) {
        if (!enable && status == ERROR_FILE_NOT_FOUND) return true;
        setError(error, L"could not open startup key");
        return false;
    }
    Key key(handle);

    ValueInfo info;
    if (readValue(key.get(), info) != ReadResult::Ok) {
        setError(error, L"could not read startup value");
        return false;
    }

    if (!enable) {
        if (!info.present) return true;
        status = RegDeleteValueW(key.get(), kValueName.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            setError(error, L"could not delete startup value");
            return false;
        }
        return true;
    }

    std::wstring exe;
    if (!modulePath(exe)) {
        setError(error, L"could not determine executable path");
        return false;
    }
    const std::wstring payload = commandLine(exe);
    if (payload.empty()) {
        setError(error, L"could not determine executable path");
        return false;
    }
    if (info.present && info.isString && info.text == payload) return true;

    const DWORD bytes = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
    status = RegSetValueExW(key.get(), kValueName.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(payload.c_str()), bytes);
    if (status != ERROR_SUCCESS) {
        setError(error, L"could not write startup value");
        return false;
    }
    return true;
}

} // namespace tapir::util::startup

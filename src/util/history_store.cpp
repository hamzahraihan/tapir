#include "history_store.h"

#include "diagnostics.h"
#include "util/config_path.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace tapir::history_store {
namespace {

class File {
 public:
  explicit File(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File() {
    if (handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
  }

  bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
  HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_;
};

class ProtectedBlob {
 public:
  ProtectedBlob() = default;
  ProtectedBlob(const ProtectedBlob&) = delete;
  ProtectedBlob& operator=(const ProtectedBlob&) = delete;
  ~ProtectedBlob() {
    if (blob_.pbData != nullptr) ::LocalFree(blob_.pbData);
  }

  DATA_BLOB* out() noexcept { return &blob_; }
  const char* data() const noexcept {
    return reinterpret_cast<const char*>(blob_.pbData);
  }
  DWORD size() const noexcept { return blob_.cbData; }

 private:
  DATA_BLOB blob_{};
};

enum class ReadResult { Ok, NotFound, Error };

ReadResult ReadAll(const std::wstring& path, std::string& bytes) {
  File file(::CreateFileW(path.c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.valid()) {
    return ::GetLastError() == ERROR_FILE_NOT_FOUND ? ReadResult::NotFound
                                                    : ReadResult::Error;
  }

  LARGE_INTEGER size{};
  if (::GetFileSizeEx(file.get(), &size) == FALSE || size.QuadPart < 0 ||
      size.QuadPart > MAXDWORD) {
    return ReadResult::Error;
  }
  bytes.clear();
  bytes.resize(static_cast<std::size_t>(size.QuadPart));

  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD wanted = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size() - offset, 1u << 20));
    DWORD read = 0;
    if (::ReadFile(file.get(), bytes.data() + offset, wanted, &read, nullptr) ==
            FALSE ||
        read == 0) {
      return ReadResult::Error;
    }
    offset += read;
  }
  return ReadResult::Ok;
}

bool WriteAll(const std::wstring& path, std::string_view bytes) {
  if (bytes.size() > MAXDWORD) return false;
  File file(::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file.valid()) return false;

  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (::WriteFile(file.get(), bytes.data() + offset,
                    static_cast<DWORD>(bytes.size() - offset), &written,
                    nullptr) == FALSE ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return ::FlushFileBuffers(file.get()) != FALSE;
}

void Log(std::string_view reason) {
  std::string line = "tapir: history ";
  line.append(reason);
  line.push_back('\n');
  diagnostics::append(util::config::logPath(), line);
}

void BackUpAndLog(const std::wstring& path, std::string_view reason) {
  ::MoveFileExW(path.c_str(), util::config::historyBackupPath().c_str(),
                MOVEFILE_REPLACE_EXISTING);
  Log(reason);
}

bool Protect(std::string& plain, std::string& result) {
  if (plain.size() > MAXDWORD) return false;
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE*>(plain.data())};
  ProtectedBlob output;
  if (::CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 0,
                         output.out()) == FALSE) {
    return false;
  }
  result.assign(output.data(), output.size());
  return true;
}

bool Unprotect(const std::string& protectedBytes, std::string& result) {
  if (protectedBytes.size() > MAXDWORD) return false;
  std::string mutableBytes = protectedBytes;
  DATA_BLOB input{static_cast<DWORD>(mutableBytes.size()),
                  reinterpret_cast<BYTE*>(mutableBytes.data())};
  ProtectedBlob output;
  if (::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0,
                           output.out()) == FALSE) {
    return false;
  }
  result.assign(output.data(), output.size());
  return true;
}

}  // namespace

history::Records Load() {
  const std::wstring path = util::config::historyPath();
  std::string bytes;
  const ReadResult read = ReadAll(path, bytes);
  if (read == ReadResult::NotFound) return {};
  if (read == ReadResult::Error) {
    BackUpAndLog(path, "read failed; backed up");
    return {};
  }
  if (bytes.size() < history::kPrefixSize) {
    BackUpAndLog(path, "truncated prefix; backed up");
    return {};
  }

  bool encrypted = false;
  if (!history::DecodePrefix(
          std::string_view(bytes).substr(0, history::kPrefixSize), encrypted)) {
    BackUpAndLog(path, "invalid prefix; backed up");
    return {};
  }

  std::string payload = bytes.substr(history::kPrefixSize);
  if (encrypted) {
    std::string plain;
    if (!Unprotect(payload, plain)) {
      BackUpAndLog(path, "decryption failed; backed up");
      return {};
    }
    payload = std::move(plain);
  }

  history::Records records;
  if (!history::DecodePayload(payload, records)) {
    BackUpAndLog(path, "corrupt payload; backed up");
    return {};
  }
  return records;
}

bool Save(bool encrypt, const history::Records& records) {
  std::string payload = history::EncodePayload(records);
  std::string body;
  if (encrypt) {
    if (!Protect(payload, body)) {
      Log("encryption failed; not saved");
      return false;
    }
  } else {
    body = std::move(payload);
  }

  const std::wstring directory = util::config::directory();
  if (!directory.empty()) ::CreateDirectoryW(directory.c_str(), nullptr);

  const std::wstring temp = util::config::historyTempPath();
  const std::string contents = history::EncodePrefix(encrypt) + body;
  if (!WriteAll(temp, contents)) {
    ::DeleteFileW(temp.c_str());
    Log("write failed; not saved");
    return false;
  }
  if (::MoveFileExW(temp.c_str(), util::config::historyPath().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
      FALSE) {
    ::DeleteFileW(temp.c_str());
    Log("replace failed; not saved");
    return false;
  }
  return true;
}

}  // namespace tapir::history_store

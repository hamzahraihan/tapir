#include "clipboard.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace tapir::clipboard {
namespace {

bool ownWritePending = false;

// Single-line display and search form of a copy. Every run of separators (CR,
// LF, TAB and other C0 controls) collapses to one space and leading/trailing
// whitespace is trimmed, so a multiline copy shows and matches as one row.
std::wstring MakePreview(std::wstring_view text) {
  std::wstring preview;
  preview.reserve(text.size());
  bool pendingSpace = false;
  for (const wchar_t ch : text) {
    const bool separator = ch == L'\r' || ch == L'\n' || ch == L'\t' || ch < 0x20;
    if (separator) {
      if (!preview.empty()) pendingSpace = true;
      continue;
    }
    if (pendingSpace) {
      preview.push_back(L' ');
      pendingSpace = false;
    }
    preview.push_back(ch);
  }
  return preview;
}

// Owns the clipboard for the duration of one capture. OpenClipboard can fail
// while another application holds it, so retry briefly and then skip.
class ClipboardScope {
 public:
  ClipboardScope() = default;
  ClipboardScope(const ClipboardScope&) = delete;
  ClipboardScope& operator=(const ClipboardScope&) = delete;
  ~ClipboardScope() {
    if (opened_) ::CloseClipboard();
  }

  bool TryOpen() {
    for (int attempt = 0; attempt < 5; ++attempt) {
      if (::OpenClipboard(nullptr)) {
        opened_ = true;
        return true;
      }
      ::Sleep(30);
    }
    return false;
  }

 private:
  bool opened_ = false;
};

UINT ExcludeFormat() {
  static const UINT id =
      ::RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
  return id;
}

UINT CanIncludeFormat() {
  static const UINT id =
      ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
  return id;
}

class GlobalMemory {
public:
  explicit GlobalMemory(HGLOBAL memory) noexcept : memory_(memory) {}
  GlobalMemory(const GlobalMemory&) = delete;
  GlobalMemory& operator=(const GlobalMemory&) = delete;
  ~GlobalMemory() {
    if (memory_ != nullptr) ::GlobalFree(memory_);
  }

  bool valid() const noexcept { return memory_ != nullptr; }
  void* lock() noexcept { return ::GlobalLock(memory_); }
  void unlock() noexcept { ::GlobalUnlock(memory_); }
  HGLOBAL get() const noexcept { return memory_; }
  void release() noexcept { memory_ = nullptr; }

private:
  HGLOBAL memory_;
};

bool FormatPresent(UINT target) {
  for (UINT format = ::EnumClipboardFormats(0); format != 0;
       format = ::EnumClipboardFormats(format)) {
    if (format == target) return true;
  }
  return false;
}

PrivacyFacts InspectPrivacy() {
  PrivacyFacts facts;
  facts.excludePresent = FormatPresent(ExcludeFormat());
  if (!FormatPresent(CanIncludeFormat())) return facts;

  facts.canIncludePresent = true;
  HANDLE data = ::GetClipboardData(CanIncludeFormat());
  if (data == nullptr || ::GlobalSize(data) != sizeof(DWORD)) {
    // Missing or unexpected payload size: fail closed.
    return facts;
  }
  const auto* value = static_cast<const DWORD*>(::GlobalLock(data));
  if (value == nullptr) return facts;
  facts.canIncludeValue = *value;
  facts.canIncludeValueValid = true;
  ::GlobalUnlock(data);
  return facts;
}

// Executable file name (basename with extension) of the app that owns the
// clipboard, falling back to the foreground window. Empty when unreadable.
std::wstring SourceExecutableName() {
  DWORD pid = 0;
  HWND owner = ::GetClipboardOwner();
  if (owner != nullptr) ::GetWindowThreadProcessId(owner, &pid);
  if (pid == 0) {
    HWND foreground = ::GetForegroundWindow();
    if (foreground != nullptr) ::GetWindowThreadProcessId(foreground, &pid);
  }
  if (pid == 0) return {};

  HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) return {};

  std::wstring path;
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
      path.assign(buffer.data(), size);
      break;
    }
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
    buffer.resize(buffer.size() * 2);
  }
  ::CloseHandle(process);

  const std::size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) path.erase(0, slash + 1);
  return path;
}

std::wstring ReadUnicodeText() {
  HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
  if (data == nullptr) return {};
  const auto* text = static_cast<const wchar_t*>(::GlobalLock(data));
  if (text == nullptr) return {};
  std::wstring result(text);
  ::GlobalUnlock(data);
  return result;
}

}  // namespace

History::History(const Settings::History& limits)
    : maxEntries_(limits.maxEntries > 0 ? static_cast<std::size_t>(limits.maxEntries)
                                        : 0),
      maxEntryBytes_(limits.maxEntryBytes > 0
                         ? static_cast<std::size_t>(limits.maxEntryBytes)
                         : 0) {}

Privacy EvaluatePrivacy(const PrivacyFacts& facts) noexcept {
  if (facts.excludePresent) return Privacy::Block;
  if (facts.canIncludePresent) {
    if (!facts.canIncludeValueValid) return Privacy::Block;
    if (facts.canIncludeValue != 1) return Privacy::Block;
  }
  return Privacy::Allow;
}

bool IsIgnored(std::wstring_view executableName,
               const std::vector<std::wstring>& ignoreList) {
  if (executableName.empty() || ignoreList.empty()) return false;
  std::wstring foldedName;
  fuzzy::FoldInto(executableName, foldedName);
  for (const std::wstring& candidate : ignoreList) {
    std::wstring foldedCandidate;
    fuzzy::FoldInto(candidate, foldedCandidate);
    if (foldedCandidate == foldedName) return true;
  }
  return false;
}

bool History::Push(std::wstring text) {
  if (text.empty()) return false;
  if (maxEntryBytes_ > 0 && text.size() * sizeof(wchar_t) > maxEntryBytes_) {
    return false;
  }

  // Collapse a consecutive duplicate of the newest entry (already on top).
  if (!entries_.empty() && entries_.front().text == text) return false;

  // A repeat that already exists lower down moves to the top.
  for (std::size_t i = 1; i < entries_.size(); ++i) {
    if (entries_[i].text == text) {
      Entry moved = std::move(entries_[i]);
      entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
      entries_.insert(entries_.begin(), std::move(moved));
      Rebuild();
      ++revision_;
      return true;
    }
  }

  Entry entry;
  entry.text = std::move(text);
  entry.preview = MakePreview(entry.text);
  fuzzy::FoldInto(entry.preview, entry.folded);
  entries_.insert(entries_.begin(), std::move(entry));
  if (maxEntries_ > 0 && entries_.size() > maxEntries_) entries_.resize(maxEntries_);
  Rebuild();
  ++revision_;
  return true;
}

const Entry* History::EntryAt(std::size_t index) const noexcept {
  if (index >= entries_.size()) return nullptr;
  return &entries_[index];
}

std::size_t History::Rank(std::wstring_view query,
                          std::span<fuzzy::Result> out) const noexcept {
  if (out.empty()) return 0;
  if (query.empty()) {
    // No query keeps recency order (entries_ is newest first); the fuzzy scorer
    // would otherwise reorder equal-scored rows by length, which is wrong here.
    const std::size_t count = std::min(entries_.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
      out[i] = fuzzy::Result{};
      out[i].index = i;
      out[i].nameLength = entries_[i].preview.size();
    }
    return count;
  }
  return fuzzy::Rank(fuzzy_, query, out);
}

void History::Rebuild() {
  fuzzy_.clear();
  fuzzy_.reserve(entries_.size());
  for (const Entry& entry : entries_) {
    fuzzy_.push_back(fuzzy::Entry{
        std::wstring_view(entry.preview), std::wstring_view(entry.folded), 0});
  }
}

bool WriteOwnText(const std::wstring& text) {
  const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  GlobalMemory memory(::GlobalAlloc(GMEM_MOVEABLE, bytes));
  if (!memory.valid()) return false;
  wchar_t* dest = static_cast<wchar_t*>(memory.lock());
  if (dest == nullptr) return false;
  std::memcpy(dest, text.c_str(), bytes);
  memory.unlock();

  ClipboardScope clipboard;
  ownWritePending = true;
  if (!clipboard.TryOpen() || ::EmptyClipboard() == FALSE) {
    ownWritePending = false;
    return false;
  }
  if (::SetClipboardData(CF_UNICODETEXT, memory.get()) == nullptr) {
    ownWritePending = false;
    return false;
  }
  memory.release();
  return true;
}

void clearOwnWriteMarker() noexcept {
  ownWritePending = false;
}

bool CaptureUpdate(const Settings::History& limits, History& out) {
  if (ownWritePending) {
    ownWritePending = false;
    return false;
  }

  ClipboardScope clipboard;
  if (!clipboard.TryOpen()) return false;

  if (EvaluatePrivacy(InspectPrivacy()) == Privacy::Block) return false;

  // The ignore list affects capture only and only when the source is readable.
  if (!limits.ignoreProcesses.empty()) {
    const std::wstring name = SourceExecutableName();
    if (!name.empty() && IsIgnored(name, limits.ignoreProcesses)) return false;
  }

  std::wstring text = ReadUnicodeText();
  if (text.empty()) return false;
  return out.Push(std::move(text));
}

}  // namespace tapir::clipboard
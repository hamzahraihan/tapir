#include "index.h"

#include "diagnostics.h"
#include "icons.h"
#include "index_cache.h"
#include "util/config_path.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>
#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl/client.h>

#define INITGUID
#include <propkeydef.h>
#undef INITGUID

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.h"

using Microsoft::WRL::ComPtr;

DEFINE_PROPERTYKEY(kTapirAppUserModelId, 0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0,
                   0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3, 5);

namespace tapir::index {
namespace {

class ComInitializer {
 public:
  ComInitializer() {
    const HRESULT hr = ::CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    owns_ = hr == S_OK;
  }
  ~ComInitializer() {
    if (owns_) ::CoUninitialize();
  }
  ComInitializer(const ComInitializer&) = delete;
  ComInitializer& operator=(const ComInitializer&) = delete;

 private:
  bool owns_ = false;
};

// Shell PIDL pointer typedefs carry __unaligned; preserve it in the owner.
using PidlItem = std::remove_pointer_t<PIDLIST_ABSOLUTE>;
struct PidlDeleter {
  void operator()(PidlItem* pidl) const noexcept {
    if (pidl != nullptr) ::ILFree(pidl);
  }
};
using Pidl = std::unique_ptr<PidlItem, PidlDeleter>;

struct CoTaskDeleter {
  void operator()(void* memory) const noexcept { ::CoTaskMemFree(memory); }
};
using CoTaskString = std::unique_ptr<wchar_t, CoTaskDeleter>;

struct FindCloser {
  void operator()(void* handle) const noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) ::FindClose(handle);
  }
};
using FindHandle = std::unique_ptr<void, FindCloser>;

std::wstring Lower(std::wstring_view text) {
  std::wstring result;
  fuzzy::FoldInto(text, result);
  return result;
}

std::wstring AumidDedupeKey(std::wstring_view aumid) {
  return L"aumid:" + Lower(aumid);
}

std::wstring PathDedupeKey(std::wstring_view path, std::wstring_view arguments) {
  return L"path:" + Lower(path) + L"|" + Lower(arguments);
}

bool SourceEnabled(const tapir::Settings& settings, std::wstring_view source) {
  const std::wstring wanted = Lower(source);
  for (const std::wstring& configured : settings.index.sources) {
    if (Lower(configured) == wanted) return true;
  }
  return false;
}

bool HasLnkExtension(std::wstring_view name) {
  if (name.size() < 4) return false;
  return Lower(name.substr(name.size() - 4)) == L".lnk";
}

std::wstring ShortcutName(std::wstring_view path) {
  const std::size_t separator = path.find_last_of(L"\\/");
  const std::size_t begin =
      separator == std::wstring_view::npos ? 0 : separator + 1;
  std::wstring name(path.substr(begin));
  const std::size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos) name.resize(dot);
  return name;
}

std::wstring DisplayName(IShellFolder* folder, PCUITEMID_CHILD child) {
  STRRET description{};
  if (FAILED(folder->GetDisplayNameOf(child, SHGDN_NORMAL, &description))) {
    return {};
  }
  wchar_t buffer[MAX_PATH]{};
  if (FAILED(::StrRetToBufW(&description, child, buffer,
                            static_cast<UINT>(MAX_PATH)))) {
    return {};
  }
  return std::wstring(buffer);
}

std::wstring ReadAumid(PCIDLIST_ABSOLUTE pidl) {
  ComPtr<IPropertyStore> store;
  if (FAILED(::SHGetPropertyStoreFromIDList(
          pidl, GPS_DEFAULT, IID_PPV_ARGS(store.GetAddressOf())))) {
    return {};
  }
  PROPVARIANT value{};
  std::wstring aumid;
  if (SUCCEEDED(store->GetValue(kTapirAppUserModelId, &value)) &&
      value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
    aumid = value.pwszVal;
  }
  ::PropVariantClear(&value);
  return aumid;
}

void CollectAppsFolder(std::vector<App>& apps,
                       std::unordered_set<std::wstring>& seen,
                       std::unordered_set<std::wstring>& seenNames) {
  PIDLIST_ABSOLUTE rawRoot = nullptr;
  if (FAILED(::SHGetKnownFolderIDList(FOLDERID_AppsFolder, KF_FLAG_DEFAULT,
                                      nullptr, &rawRoot))) {
    return;
  }
  Pidl root(rawRoot);

  ComPtr<IShellFolder> desktop;
  if (FAILED(::SHGetDesktopFolder(desktop.GetAddressOf()))) return;
  ComPtr<IShellFolder> folder;
  if (FAILED(desktop->BindToObject(root.get(), nullptr,
                                   IID_PPV_ARGS(folder.GetAddressOf())))) {
    return;
  }
  ComPtr<IEnumIDList> children;
  if (folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS,
                          children.GetAddressOf()) != S_OK) {
    return;
  }

  PITEMID_CHILD rawChild = nullptr;
  ULONG fetched = 0;
  while (children->Next(1, &rawChild, &fetched) == S_OK && fetched == 1 &&
         rawChild != nullptr) {
    Pidl child(rawChild);
    rawChild = nullptr;
    fetched = 0;

    Pidl absolute(::ILCombine(root.get(), child.get()));
    if (!absolute) continue;

    const std::wstring aumid = ReadAumid(absolute.get());
    if (aumid.empty()) continue;

    App app;
    app.aumid = aumid;
    app.target = L"shell:AppsFolder\\" + app.aumid;
    app.name = DisplayName(folder.Get(), child.get());
    if (app.name.empty()) app.name = app.aumid;
    const std::wstring dedupeKey = AumidDedupeKey(aumid);
    app.key = dedupeKey;
    if (!seen.insert(dedupeKey).second) continue;
    if (!seenNames.insert(Lower(app.name)).second) continue;
    apps.push_back(std::move(app));
  }
  if (rawChild != nullptr) ::ILFree(rawChild);
}

void FindLinks(const std::wstring& directory, std::vector<std::wstring>& links) {
  const std::wstring pattern = directory + L"\\*";
  WIN32_FIND_DATAW data{};
  HANDLE rawFind = ::FindFirstFileW(pattern.c_str(), &data);
  if (rawFind == INVALID_HANDLE_VALUE) return;
  FindHandle find(rawFind);

  do {
    const std::wstring_view entryName(data.cFileName);
    if (entryName == L"." || entryName == L"..") continue;
    const std::wstring full = directory + L"\\" + std::wstring(entryName);
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        FindLinks(full, links);
      }
    } else if (HasLnkExtension(entryName)) {
      links.push_back(full);
    }
  } while (::FindNextFileW(find.get(), &data) != FALSE);
}

void AddShortcut(const std::wstring& path, std::vector<App>& apps,
                  std::unordered_set<std::wstring>& seen,
                  std::unordered_set<std::wstring>& seenNames) {
  ComPtr<IShellLinkW> link;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(link.GetAddressOf())))) {
    return;
  }
  ComPtr<IPersistFile> file;
  if (FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ))) {
    return;
  }

  wchar_t rawTarget[MAX_PATH]{};
  if (FAILED(link->GetPath(rawTarget, MAX_PATH, nullptr, 0)) ||
      rawTarget[0] == L'\0') {
    return;
  }

  App app;
  app.name = ShortcutName(path);
  app.target = rawTarget;

  wchar_t buffer[MAX_PATH]{};
  if (SUCCEEDED(link->GetArguments(buffer, MAX_PATH))) app.arguments = buffer;
  if (SUCCEEDED(link->GetWorkingDirectory(buffer, MAX_PATH))) {
    app.workingDirectory = buffer;
  }

  PIDLIST_ABSOLUTE rawTargetPidl = nullptr;
  if (SUCCEEDED(link->GetIDList(&rawTargetPidl)) && rawTargetPidl != nullptr) {
    Pidl targetPidl(rawTargetPidl);
    app.aumid = ReadAumid(targetPidl.get());
  }

  const std::wstring key = app.aumid.empty()
                               ? PathDedupeKey(app.target, app.arguments)
                               : AumidDedupeKey(app.aumid);
  app.key = key;
  if (!seen.insert(key).second) return;
  if (app.name.empty()) app.name = app.target;
  // Many Start Menu links carry no AUMID, so their target key cannot match the
  // AppsFolder entry for the same app; the display name is the fallback key.
  if (!seenNames.insert(Lower(app.name)).second) return;
  apps.push_back(std::move(app));
}

void CollectStartMenu(std::vector<App>& apps,
                       std::unordered_set<std::wstring>& seen,
                       std::unordered_set<std::wstring>& seenNames) {
  const KNOWNFOLDERID* folders[] = {&FOLDERID_Programs,
                                    &FOLDERID_CommonPrograms};
  for (const KNOWNFOLDERID* folder : folders) {
    PWSTR rawPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(*folder, KF_FLAG_DEFAULT, nullptr,
                                      &rawPath))) {
      continue;
    }
    CoTaskString path(rawPath);
    std::vector<std::wstring> links;
    FindLinks(path.get(), links);
    for (const std::wstring& link : links) AddShortcut(link, apps, seen, seenNames);
  }
}

void FinalizeSnapshot(Snapshot& snapshot) {
  std::size_t total = 0;
  for (App& app : snapshot.apps) {
    fuzzy::FoldInto(app.name, app.folded);
    total += app.name.size() + app.folded.size();
  }

  snapshot.storage.reserve(total);
  std::vector<std::size_t> nameOffsets;
  std::vector<std::size_t> foldedOffsets;
  nameOffsets.reserve(snapshot.apps.size());
  foldedOffsets.reserve(snapshot.apps.size());
  for (const App& app : snapshot.apps) {
    nameOffsets.push_back(snapshot.storage.size());
    snapshot.storage.append(app.name);
    foldedOffsets.push_back(snapshot.storage.size());
    snapshot.storage.append(app.folded);
  }

  // Views are created only after every append is complete: a growing wstring
  // can reallocate (and SSO storage moves with it), invalidating earlier views.
  snapshot.entries.reserve(snapshot.apps.size());
  for (std::size_t i = 0; i < snapshot.apps.size(); ++i) {
    const App& app = snapshot.apps[i];
    snapshot.entries.push_back(fuzzy::Entry{
        std::wstring_view(snapshot.storage.data() + nameOffsets[i],
                          app.name.size()),
        std::wstring_view(snapshot.storage.data() + foldedOffsets[i],
                          app.folded.size()),
        app.launchCount});
  }
}

void ApplyCounts(Snapshot& snapshot, const tapir::launches::Table& table) {
  std::unordered_map<std::wstring, std::uint32_t> counts;
  counts.reserve(table.size());
  for (const auto& record : table) counts[record.key] = record.count;
  for (App& app : snapshot.apps) {
    const auto it = counts.find(app.key);
    app.launchCount = it == counts.end() ? 0 : it->second;
  }
}

std::shared_ptr<const Snapshot> BuildSnapshot(const tapir::Settings& settings,
                                             const tapir::launches::Table& table) {
  Snapshot snapshot;

  std::unordered_set<std::wstring> seen;
  std::unordered_set<std::wstring> seenNames;
  if (SourceEnabled(settings, L"appsfolder")) {
    CollectAppsFolder(snapshot.apps, seen, seenNames);
  }
  if (SourceEnabled(settings, L"startmenu")) {
    CollectStartMenu(snapshot.apps, seen, seenNames);
  }

  // Apply persisted launch counts by stable key.
  ApplyCounts(snapshot, table);

  FinalizeSnapshot(snapshot);
  return std::make_shared<Snapshot>(std::move(snapshot));
}

std::shared_ptr<const Snapshot> RebuildSnapshot(const Snapshot& base,
                                               const tapir::launches::Table& table) {
  Snapshot snapshot;
  snapshot.apps = base.apps;

  ApplyCounts(snapshot, table);

  FinalizeSnapshot(snapshot);
  return std::make_shared<Snapshot>(std::move(snapshot));
}

std::wstring CachePath() {
  return (std::filesystem::path(util::config::directory()) / L"index.cache").wstring();
}

void Persist(const Snapshot& snapshot) {
  std::vector<index_cache::Record> records;
  records.reserve(snapshot.apps.size());
  for (const App& app : snapshot.apps) {
    index_cache::Record record;
    record.name = app.name;
    record.key = app.key;
    record.target = app.target;
    record.arguments = app.arguments;
    record.workingDirectory = app.workingDirectory;
    record.aumid = app.aumid;
    record.launchCount = app.launchCount;
    records.push_back(std::move(record));
  }
  const std::string bytes = index_cache::Encode(records);
  const std::wstring path = CachePath();
  const std::wstring temp = path + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      diagnostics::append(util::config::logPath(), "index cache write failed");
      return;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      diagnostics::append(util::config::logPath(), "index cache write failed");
      return;
    }
  }
  if (MoveFileExW(temp.c_str(), path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
    diagnostics::append(util::config::logPath(), "index cache replace failed");
  }
}

}  // namespace

Index::Index() = default;

Index::~Index() {
  if (restartThread_.joinable()) restartThread_.join();
  if (worker_.joinable()) worker_.join();
}

void Index::LoadCached() {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(CachePath(), error);
  if (error || size > 16u * 1024u * 1024u) return;
  std::ifstream file(CachePath(), std::ios::binary);
  if (!file) return;
  const std::string bytes{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
  std::vector<index_cache::Record> records;
  if (!index_cache::Decode(bytes, records)) {
    diagnostics::append(util::config::logPath(), "index cache ignored");
    return;
  }
  Snapshot snapshot;
  snapshot.apps.reserve(records.size());
  for (const index_cache::Record& record : records) {
    App app;
    app.name = record.name;
    app.key = record.key;
    app.target = record.target;
    app.arguments = record.arguments;
    app.workingDirectory = record.workingDirectory;
    app.aumid = record.aumid;
    app.launchCount = record.launchCount;
    snapshot.apps.push_back(std::move(app));
  }
  FinalizeSnapshot(snapshot);
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = std::make_shared<Snapshot>(std::move(snapshot));
}

bool Index::Ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_ != nullptr;
}

void Index::Start(const tapir::Settings& settings, HWND notifyWindow,
                  unsigned int notifyMessage) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable()) return;
  sources_ = settings.index.sources;
  launchesPath_ = LaunchesPath();
  LoadLaunches();
  const tapir::launches::Table tableCopy = launchTable_;
  worker_ = std::thread([this, settings, tableCopy, notifyWindow, notifyMessage]() {
    ComInitializer com;
    std::shared_ptr<const Snapshot> built = BuildSnapshot(settings, tableCopy);
    Persist(*built);
    {
      std::lock_guard<std::mutex> guard(mutex_);
      snapshot_ = std::move(built);
    }
    if (notifyWindow != nullptr) {
      ::PostMessageW(notifyWindow, notifyMessage, 0, 0);
    }
  });
}

void Index::Restart(const tapir::Settings& settings, HWND notifyWindow,
                    unsigned int notifyMessage) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    changed = sources_ != settings.index.sources;
    if (changed) sources_ = settings.index.sources;
  }
  if (!changed || restartThread_.joinable()) return;
  restartThread_ = std::thread([this, settings, notifyWindow, notifyMessage]() {
    if (worker_.joinable()) worker_.join();
    Start(settings, notifyWindow, notifyMessage);
  });
}

std::shared_ptr<const Snapshot> Index::Current() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool Index::Launch(const App& app, HWND owner) {
  ComInitializer com;
  if (app.aumid.find(L'!') != std::wstring::npos) {
    ComPtr<IApplicationActivationManager> manager;
    if (FAILED(::CoCreateInstance(
            CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(manager.GetAddressOf())))) {
      return false;
    }
    DWORD processId = 0;
    return SUCCEEDED(manager->ActivateApplication(
        app.aumid.c_str(), app.arguments.c_str(), AO_NONE, &processId));
  }

  SHELLEXECUTEINFOW info{};
  info.cbSize = static_cast<DWORD>(sizeof(info));
  info.fMask = SEE_MASK_FLAG_LOG_USAGE;
  info.hwnd = owner;
  info.lpFile = app.target.c_str();
  info.lpParameters = app.arguments.empty() ? nullptr : app.arguments.c_str();
  info.lpDirectory =
      app.workingDirectory.empty() ? nullptr : app.workingDirectory.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ::ShellExecuteExW(&info) != FALSE;
}

std::wstring Index::ConfigDir() const {
  const std::wstring dir = util::config::directory();
  if (!dir.empty()) ::CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring Index::LaunchesPath() const { return ConfigDir() + L"\\launches.dat"; }

void Index::LoadLaunches() {
  launchTable_.clear();
  std::ifstream file(launchesPath_, std::ios::binary);
  if (!file) return;
  std::string bytes((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
  tapir::launches::Table parsed;
  if (tapir::launches::Parse(bytes, parsed)) launchTable_ = std::move(parsed);
}

void Index::PersistLaunches() {
  if (launchesPath_.empty()) return;
  const std::string bytes = tapir::launches::Serialize(launchTable_);
  const std::wstring tmp = launchesPath_ + L".tmp";
  {
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (!file) {
      ::OutputDebugStringA("tapir: failed to open launches temp file\n");
      return;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
      ::OutputDebugStringA("tapir: failed to write launches temp file\n");
      return;
    }
  }
  if (!::MoveFileExW(tmp.c_str(), launchesPath_.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
    ::OutputDebugStringA("tapir: failed to replace launches.dat\n");
  }
}

void Index::RecordLaunch(const App& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool found = false;
  for (auto& record : launchTable_) {
    if (record.key == app.key) {
      record.count = tapir::launches::Increment(record.count);
      found = true;
      break;
    }
  }
  if (!found) launchTable_.push_back({app.key, 1});
  PersistLaunches();
  if (snapshot_ != nullptr) {
    snapshot_ = RebuildSnapshot(*snapshot_, launchTable_);
  }
}

}  // namespace tapir::index

namespace tapir::icons {
namespace {

using Microsoft::WRL::ComPtr;

struct HBitmapGuard {
  HBITMAP bitmap = nullptr;

  ~HBitmapGuard() {
    if (bitmap != nullptr) DeleteObject(bitmap);
  }
};

struct HdcGuard {
  HDC dc = nullptr;

  ~HdcGuard() {
    if (dc != nullptr) DeleteDC(dc);
  }
};

ComPtr<ID2D1Bitmap> createBitmapFromHBitmap(ID2D1RenderTarget* target,
                                            HBITMAP hbitmap) {
  if (target == nullptr || hbitmap == nullptr) return {};

  BITMAP info{};
  if (GetObjectW(hbitmap, sizeof(info), &info) == 0) return {};
  if (info.bmWidth <= 0 || info.bmHeight <= 0) return {};

  BITMAPINFO dib{};
  dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  dib.bmiHeader.biWidth = info.bmWidth;
  dib.bmiHeader.biHeight = -info.bmHeight;  // top-down
  dib.bmiHeader.biPlanes = 1;
  dib.bmiHeader.biBitCount = 32;
  dib.bmiHeader.biCompression = BI_RGB;

  const std::size_t stride = static_cast<std::size_t>(info.bmWidth) * 4u;
  std::vector<unsigned char> buffer(stride * static_cast<std::size_t>(info.bmHeight));

  HdcGuard screen{CreateCompatibleDC(nullptr)};
  if (screen.dc == nullptr) return {};
  const int copied = GetDIBits(screen.dc, hbitmap, 0, info.bmHeight, buffer.data(),
                               &dib, DIB_RGB_COLORS);
  if (copied == 0) return {};

  // GetDIBits returns straight alpha; D2D render-target bitmaps require
  // premultiplied alpha. Without this, translucent icon edges render too dark.
  for (std::size_t i = 0; i < buffer.size(); i += 4) {
    const unsigned int alpha = buffer[i + 3];
    buffer[i] = static_cast<unsigned char>(buffer[i] * alpha / 255u);
    buffer[i + 1] = static_cast<unsigned char>(buffer[i + 1] * alpha / 255u);
    buffer[i + 2] = static_cast<unsigned char>(buffer[i + 2] * alpha / 255u);
  }

  const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1Bitmap> bitmap;
  if (FAILED(target->CreateBitmap(D2D1::SizeU(info.bmWidth, info.bmHeight),
                                  buffer.data(), static_cast<UINT32>(stride),
                                  &props, &bitmap))) {
    return {};
  }
  return bitmap;
}

}  // namespace

Loader::Loader(ComPtr<ID2D1RenderTarget> target) noexcept
    : target_(std::move(target)), cache_(kIconCacheEntries) {}

ComPtr<ID2D1Bitmap> Loader::Get(std::wstring_view parsingName, unsigned dpi) {
  const std::wstring cacheKey(parsingName);
  if (ComPtr<ID2D1Bitmap>* cached = cache_.Find(cacheKey)) return *cached;
  if (!target_) return {};

  const auto miss = [this, &cacheKey]() {
    cache_.Put(cacheKey, ComPtr<ID2D1Bitmap>{});
    return ComPtr<ID2D1Bitmap>{};
  };

  const std::wstring path(parsingName);
  ComPtr<IShellItem> item;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                         IID_PPV_ARGS(&item)))) {
    return miss();
  }
  ComPtr<IShellItemImageFactory> factory;
  if (FAILED(item.As(&factory))) return miss();

  const int px = std::max(1, 32 * static_cast<int>(dpi) / 96);
  HBITMAP raw = nullptr;
  if (FAILED(factory->GetImage(SIZE{px, px}, SIIGBF_ICONONLY, &raw))) {
    return miss();
  }
  HBitmapGuard guard{raw};

  ComPtr<ID2D1Bitmap> bitmap = createBitmapFromHBitmap(target_.Get(), raw);
  if (!bitmap) return miss();
  cache_.Put(cacheKey, bitmap);
  return bitmap;
}

}  // namespace tapir::icons

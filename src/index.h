#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "fuzzy.h"

#ifndef _WINDEF_
struct HWND__;
using HWND = HWND__*;
#endif

namespace tapir::index {

struct App {
  std::wstring name;
  std::wstring folded;
  std::wstring target;
  std::wstring arguments;
  std::wstring workingDirectory;
  std::wstring aumid;
  std::uint32_t launchCount = 0;
};

struct Snapshot {
  std::wstring storage;
  std::vector<fuzzy::Entry> entries;
  std::vector<App> apps;
};

class Index {
 public:
  Index();
  ~Index();
  Index(const Index&) = delete;
  Index& operator=(const Index&) = delete;
  void Start(const tapir::Settings& settings);
  std::shared_ptr<const Snapshot> Current() const;
  static bool Launch(const App& app, HWND owner);

 private:
  std::thread worker_;
  mutable std::mutex mutex_;
  std::shared_ptr<const Snapshot> snapshot_;
};

}  // namespace tapir::index

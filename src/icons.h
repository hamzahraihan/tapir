#pragma once

#include "icon_cache.h"

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

namespace tapir::icons {

// Single configured bound for the shell icon cache. Config parsing is a later
// milestone; this is the one place the limit lives for now.
inline constexpr std::size_t kIconCacheEntries = 128;

// The window message posted by the icon worker when a shell-extracted HBITMAP
// is ready for UI-thread conversion. WM_APP+6; +1..+5 are already used.
inline constexpr unsigned int kIconReadyMessage = 0x8000 + 6;

// Loads shell icons off the UI thread: Get is a lock-free cache lookup plus a
// background extraction request; a worker performs the shell I/O, and the UI
// thread converts ready HBITMAPs into D2D bitmaps in DrainReady and redraws.
class Loader {
 public:
  Loader(Microsoft::WRL::ComPtr<ID2D1RenderTarget> target, HWND notify) noexcept;
  ~Loader();
  Loader(const Loader&) = delete;
  Loader& operator=(const Loader&) = delete;

  // UI thread only. Cache hit returns the bitmap; a miss records a pending
  // empty bitmap, queues background extraction, and returns an empty ComPtr.
  Microsoft::WRL::ComPtr<ID2D1Bitmap> Get(std::wstring_view parsingName,
                                          unsigned dpi);

  // UI thread only. Converts and caches every HBITMAP the worker finished.
  void DrainReady();

 private:
  struct Request {
    std::wstring key;
    unsigned dpi;
  };
  struct Ready {
    std::wstring key;
    HBITMAP bitmap;
  };

  // Worker thread only. Performs the shell image extraction for one request.
  HBITMAP Extract(std::wstring_view parsingName, unsigned dpi);
  // Worker thread entry. Blocks on a condition variable; no polling.
  void Worker();

  Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
  Cache<Microsoft::WRL::ComPtr<ID2D1Bitmap>> cache_;

  // Guarded by mutex_. The worker owns queue_ and writes ready_; the UI thread
  // swaps ready_ out in DrainReady.
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Request> queue_;
  std::vector<Ready> ready_;
  bool stop_ = false;
  HWND notify_ = nullptr;
  std::thread worker_;
};

}  // namespace tapir::icons

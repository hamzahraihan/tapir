#pragma once

#include "icon_cache.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <d2d1.h>
#include <wrl/client.h>

namespace tapir::icons {

// Single configured bound for the shell icon cache. Config parsing is a later
// milestone; this is the one place the limit lives for now.
inline constexpr std::size_t kIconCacheEntries = 128;

// Loads shell icons only when asked, and never extracts a new icon on the UI
// thread: GetImage is restricted to INCACHEONLY | MEMORYONLY.
class Loader {
 public:
  explicit Loader(Microsoft::WRL::ComPtr<ID2D1RenderTarget> target) noexcept;
  Loader(const Loader&) = delete;
  Loader& operator=(const Loader&) = delete;

  Microsoft::WRL::ComPtr<ID2D1Bitmap> Get(std::wstring_view parsingName,
                                          unsigned dpi);

 private:
  Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
  Cache<Microsoft::WRL::ComPtr<ID2D1Bitmap>> cache_;
};

}  // namespace tapir::icons

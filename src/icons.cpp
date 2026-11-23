#include "icons.h"

#include <shobjidl_core.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

  const std::size_t stride =
      static_cast<std::size_t>(info.bmWidth) * 4u;  // 32-bit BGRA
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
  if (FAILED(factory->GetImage(SIZE{px, px},
                               SIIGBF_ICONONLY | SIIGBF_INCACHEONLY |
                                   SIIGBF_MEMORYONLY,
                               &raw))) {
    return miss();
  }
  HBitmapGuard guard{raw};

  ComPtr<ID2D1Bitmap> bitmap = createBitmapFromHBitmap(target_.Get(), raw);
  if (!bitmap) return miss();
  cache_.Put(cacheKey, bitmap);
  return bitmap;
}

}  // namespace tapir::icons
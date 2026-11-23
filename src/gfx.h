#pragma once

#include "config.h"
#include "render.h"

#include <d2d1.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_3.h>
#include <memory>
#include <optional>
#include <windows.h>
#include <wrl/client.h>

namespace tapir::gfx {

struct Context {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> compVisual;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> compEffect;
    std::optional<render::Painter> painter;
    bool acrylic = false;
    unsigned dpi = 96;
    int widthPx = 0;
    int heightPx = 0;
};

std::unique_ptr<Context> create(HWND hwnd, unsigned dpi, int widthPx, int heightPx,
                                const Settings& settings);
bool render(Context& context, const render::PaletteView& view);
void resize(Context& context, unsigned dpi, int widthPx, int heightPx);
void setVisualState(Context& context, float opacity, float offsetPx);
void trim(Context& context);

} // namespace tapir::gfx

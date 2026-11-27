#include "gfx.h"

#include <d2d1.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_3.h>

#include <optional>

namespace tapir::gfx {
namespace {

using Microsoft::WRL::ComPtr;

constexpr int kCornerPreferenceAttribute = 33;
constexpr int kCornerRound = 2;
constexpr int kBackdropTypeAttribute = 38;
constexpr int kBackdropMica = 2;
constexpr int kBackdropTransient = 3;
constexpr UINT kGetHighContrast = 0x0042;
constexpr DWORD kHighContrastOn = 0x00000001;

bool highContrastActive() noexcept {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    return SystemParametersInfoW(kGetHighContrast, sizeof(hc), &hc, 0) != 0 &&
           (hc.dwFlags & kHighContrastOn) != 0;
}

ComPtr<ID3D11Device> makeDevice() {
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                    D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        return device;
    }
    ComPtr<ID3D11Device> warp;
    if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                    D3D11_SDK_VERSION, &warp, nullptr, nullptr))) {
        return warp;
    }
    return {};
}

void applyBackdrop(HWND hwnd, const Settings& settings, bool& acrylic) {
    acrylic = false;
    if (settings.theme.backdrop == Backdrop::None || highContrastActive()) return;
    const int backdrop = settings.theme.backdrop == Backdrop::Mica ? kBackdropMica : 1;
    acrylic = SUCCEEDED(DwmSetWindowAttribute(hwnd, kBackdropTypeAttribute,
                                              &backdrop, sizeof(backdrop)));
    const int corner = kCornerRound;
    DwmSetWindowAttribute(hwnd, kCornerPreferenceAttribute, &corner, sizeof(corner));
}

bool makeTargetBitmap(ID2D1DeviceContext* context, IDXGISwapChain* swapChain,
                      ComPtr<ID2D1Bitmap1>& out) {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    return SUCCEEDED(context->CreateBitmapFromDxgiSurface(surface.Get(), &props, &out));
}

} // namespace

std::unique_ptr<Context> create(HWND hwnd, unsigned dpi, int widthPx, int heightPx,
                                const Settings& settings) {
    if (hwnd == nullptr || widthPx <= 0 || heightPx <= 0) return {};

    auto ctx = std::make_unique<Context>();
    ctx->dpi = dpi;
    ctx->widthPx = widthPx;
    ctx->heightPx = heightPx;

    ctx->device = makeDevice();
    if (!ctx->device ||
        FAILED(ctx->device.As(&ctx->dxgiDevice))) return {};

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(ctx->dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&ctx->factory)))) return {};

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(widthPx);
    desc.Height = static_cast<UINT>(heightPx);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;
    if (FAILED(ctx->factory->CreateSwapChainForComposition(
            ctx->device.Get(), &desc, nullptr, &ctx->swapChain))) return {};

    if (FAILED(D2D1CreateDevice(ctx->dxgiDevice.Get(), nullptr, &ctx->d2dDevice)) ||
        FAILED(ctx->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                   &ctx->context)) ||
        !makeTargetBitmap(ctx->context.Get(), ctx->swapChain.Get(), ctx->targetBitmap)) {
        return {};
    }
    ctx->context->SetTarget(ctx->targetBitmap.Get());

    ComPtr<ID2D1RenderTarget> renderTarget;
    if (FAILED(ctx->context.As(&renderTarget))) return {};
    ctx->painter = render::Painter::create(renderTarget, settings);
    if (!ctx->painter) return {};
    ctx->iconLoader.emplace(renderTarget, hwnd);

    if (FAILED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&ctx->compDevice))) ||
        FAILED(ctx->compDevice->CreateTargetForHwnd(hwnd, TRUE, &ctx->compTarget)) ||
        FAILED(ctx->compDevice->CreateVisual(&ctx->compVisual)) ||
        FAILED(ctx->compDevice->CreateEffectGroup(&ctx->compEffect)) ||
        FAILED(ctx->compEffect->SetOpacity(1.0f)) ||
        FAILED(ctx->compVisual->SetEffect(ctx->compEffect.Get())) ||
        FAILED(ctx->compVisual->SetContent(ctx->swapChain.Get())) ||
        FAILED(ctx->compTarget->SetRoot(ctx->compVisual.Get()))) {
        return {};
    }
    ctx->compDevice->Commit();

    applyBackdrop(hwnd, settings, ctx->acrylic);
    return ctx;
}

bool render(Context& context, const render::PaletteView& view) {
    if (!context.painter) return false;
    render::PaletteView v = view;
    v.acrylic = context.acrylic;
    context.painter->draw(v);
    return SUCCEEDED(context.swapChain->Present(1, 0));
}

void resize(Context& context, unsigned dpi, int widthPx, int heightPx) {
    if (widthPx <= 0 || heightPx <= 0) return;
    const unsigned previousDpi = context.dpi;
    if (previousDpi != dpi && context.painter) {
        context.painter->clearTextCache();
    }
    context.dpi = dpi;
    context.widthPx = widthPx;
    context.heightPx = heightPx;
    // The bitmap and the device context both keep the old swap-chain buffer
    // alive; release them before ResizeBuffers or the call fails with
    // DXGI_ERROR_INVALID_CALL and new rows are never presented.
    if (context.context != nullptr) context.context->SetTarget(nullptr);
    context.targetBitmap.Reset();
    if (FAILED(context.swapChain->ResizeBuffers(0, static_cast<UINT>(widthPx),
                                                 static_cast<UINT>(heightPx),
                                                 DXGI_FORMAT_UNKNOWN, 0))) {
        return;
    }
    if (!makeTargetBitmap(context.context.Get(), context.swapChain.Get(),
                          context.targetBitmap)) {
        return;
    }
    context.context->SetTarget(context.targetBitmap.Get());
    ComPtr<ID2D1RenderTarget> renderTarget;
    if (SUCCEEDED(context.context.As(&renderTarget))) {
        context.painter->setTarget(renderTarget);
    }
}

void applySettings(Context& context, HWND hwnd, const Settings& settings) {
    applyBackdrop(hwnd, settings, context.acrylic);
    if (context.painter) context.painter->applySettings(settings);
}

void setVisualState(Context& context, float opacity, float offsetPx) {
    if (!context.compVisual || !context.compEffect) return;
    context.compEffect->SetOpacity(opacity);
    context.compVisual->SetOffsetY(offsetPx);
    context.compDevice->Commit();
}

void trim(Context& context) {
    ComPtr<IDXGIDevice3> dxgi3;
    if (SUCCEEDED(context.device.As(&dxgi3))) {
        dxgi3->Trim();
    }
}

} // namespace tapir::gfx

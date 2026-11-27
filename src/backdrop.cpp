#include "backdrop.h"

#include "blur_effect.h"

#include <dispatcherqueue.h>
#include <Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <windows.ui.composition.interop.h>

namespace tapir::backdrop {
namespace {

constexpr const wchar_t* kClassName = L"tapir-backdrop";

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

Backdrop::~Backdrop() {
    if (hwnd != nullptr) DestroyWindow(hwnd);
}

std::unique_ptr<Backdrop> create(HWND popup) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hbrBackground = nullptr;
        if (RegisterClassW(&wc) == 0) return {};
        registered = true;
    }

    auto result = std::make_unique<Backdrop>();
    result->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, kClassName, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (result->hwnd == nullptr) return {};

    DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_ASTA};
    winrt::com_ptr<ABI::Windows::System::IDispatcherQueueController> rawDispatcher;
    if (FAILED(CreateDispatcherQueueController(options, rawDispatcher.put()))) return {};
    result->dispatcher = rawDispatcher.as<winrt::Windows::System::DispatcherQueueController>();

    namespace comp = winrt::Windows::UI::Composition;
    result->compositor = comp::Compositor{};
    auto desktop = result->compositor
                       .as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
    winrt::com_ptr<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget> rawTarget;
    if (FAILED(desktop->CreateDesktopWindowTarget(result->hwnd, TRUE, rawTarget.put()))) {
        return {};
    }
    result->target = rawTarget.as<comp::Desktop::DesktopWindowTarget>();
    result->root = result->compositor.CreateContainerVisual();
    result->root.Size({1.0f, 1.0f});
    result->target.Root(result->root);
    result->visual = result->compositor.CreateSpriteVisual();
    result->visual.Size({1.0f, 1.0f});
    try {
        auto blurSource = tapir::createGaussianBlurEffect(24.0f);
        Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Effects::IGraphicsEffect> abiEffect;
        winrt::check_hresult(blurSource.As(&abiEffect));
        auto rawCompositor = result->compositor.as<ABI::Windows::UI::Composition::ICompositor>();
        winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionEffectFactory> rawFactory;
        winrt::check_hresult(
            rawCompositor->CreateEffectFactory(abiEffect.Get(), rawFactory.put()));
        auto factory = rawFactory.as<comp::CompositionEffectFactory>();
        auto effectBrush = factory.CreateBrush();
        effectBrush.SetSourceParameter(L"backdrop", result->compositor.CreateBackdropBrush());
        result->visual.Brush(effectBrush);
    } catch (...) {
        result->visual.Brush(result->compositor.CreateBackdropBrush());
    }
    result->root.Children().InsertAtTop(result->visual);
    ShowWindow(result->hwnd, SW_HIDE);

    SetWindowPos(result->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(popup, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return result;
}

void position(Backdrop& backdrop, const RECT& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    SetWindowPos(backdrop.hwnd, nullptr, rect.left, rect.top, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    const winrt::Windows::Foundation::Numerics::float2 size{
        static_cast<float>(width), static_cast<float>(height)};
    backdrop.root.Size(size);
    backdrop.visual.Size(size);
}

void setVisible(Backdrop& backdrop, bool visible) {
    ShowWindow(backdrop.hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

} // namespace tapir::backdrop

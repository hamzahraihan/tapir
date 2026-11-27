#pragma once

#include <windows.h>
#include <dispatcherqueue.h>
#include <Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>

#include <memory>

namespace tapir::backdrop {

struct Backdrop {
    HWND hwnd = nullptr;
    winrt::Windows::System::DispatcherQueueController dispatcher{nullptr};
    winrt::Windows::UI::Composition::Compositor compositor{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual root{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual visual{nullptr};

    ~Backdrop();
};

std::unique_ptr<Backdrop> create(HWND popup);
void position(Backdrop& backdrop, const RECT& rect);
void setVisible(Backdrop& backdrop, bool visible);

} // namespace tapir::backdrop

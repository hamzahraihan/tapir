# Real acrylic through the OS Visual layer

Date: 2026-11-26
Status: design approved by the user; implementation not started

## Problem

tapir asks Windows for `DWMSBT_TRANSIENTWINDOW` through `DWMWA_SYSTEMBACKDROP_TYPE`, but the popup renders a flat light surface, not live acrylic. A controlled test placed a solid magenta window behind the popup: the rounded corners showed magenta, the interior stayed `#686868`. The wallpaper at that screen position is dark navy `#1c1b2b`, so the gray is a DWM flat fill, not sampled content. Setting `DWMWA_USE_HOSTBACKDROPBRUSH`, extending the frame into the client area, removing the tint floor and removing each window style were all tested and changed nothing.

## Goal and success criteria

- With `backdrop = "acrylic"`, a solid magenta window behind the popup produces a visibly magenta-tinted blur in the popup interior, proving the backdrop samples live content.
- The UI layer, app icons, text, hit testing and compact resize keep working exactly as today.
- Any failure in the new composition path falls back to the existing solid theme background; the popup is never blank, opaque white or broken.
- Windows 10 and high-contrast mode keep the solid fallback.
- Budgets from `docs/budget-report.md` remain met: executable under 2 MB, idle CPU about 0%, hotkey-to-visible under 50 ms, working set in the low tens of MB. Measure before and after; report any regression.
- No new third-party dependency and no Windows App SDK runtime. C++/WinRT headers from the Windows SDK and `windowsapp.lib` are allowed as OS APIs.

## Non-goals

Windows App SDK `DesktopAcrylicController`, Win2D, Mica on Windows 10, changing the color palette or Settings fields beyond what this design names, changing fuzzy search, clipboard, icons, animations or window placement.

## Design

### Composition model

Create one `Windows::UI::Composition::Compositor` on the UI thread after `winrt::init_apartment()` and a `DispatcherQueueController` created with `CreateDispatcherQueueController` (STA, current thread). Keep both alive for the process.

Replace the raw DirectComposition device, target, visual and effect group in `gfx::Context` with a WinRT `DesktopWindowTarget` obtained through `ICompositorDesktopInterop::CreateDesktopWindowTarget(hwnd, TRUE)` and a `ContainerVisual` root sized in DIPs. The root holds three `SpriteVisual` layers in z-order:

1. `backdropVisual` — brush is a `CompositionEffectBrush` whose source is `ICompositor2::CreateBackdropBrush()` through a Gaussian blur effect graph described with `IGraphicsEffectD2D1Interop`. The Win32 sample at `notes.yvt.jp/Desktop-Apps/Enabling-Backdrop-Blur` reports `CreateBackdropBrush()` works for Win32 while `CreateHostBackdropBrush()` did not; if the magenta capture fails with `CreateBackdropBrush()`, try `CreateHostBackdropBrush()` before changing architecture. Blur radius starts at 24 DIP in one named constant and is tuned only if the magenta capture shows too little or too much diffusion; it is never animated.
2. `tintVisual` — `CompositionColorBrush` from `theme.background` with alpha from `backdropOpacity`. This replaces the D2D background tint fill.
3. `uiVisual` — `CompositionSurfaceBrush` created by `ICompositorInterop::CreateCompositionSurfaceForSwapChain` on the existing DXGI swap chain. The D3D11 device, swap chain, D2D device context, painter and icon loader stay unchanged.

`gfx::render` stops overwriting `view.acrylic`; `Painter::draw` clears transparent and skips its background fill when an OS backdrop is active, so the swap chain contributes only UI pixels. When the fallback is active, `Painter::draw` keeps today's solid clear.

### Backend selection and lifecycle

Add an internal enum to `gfx::Context`: `Acrylic`, `Solid`. `applyBackdrop` maps config to this enum:

- `Backdrop::Acrylic` and OS build supports `CreateHostBackdropBrush`, high contrast off, and graph creation succeeds → `Acrylic`.
- `Backdrop::Mica` → no blur graph; keep the DWM Mica attribute if it works, otherwise `Solid`.
- `Backdrop::None`, high contrast on, Windows 10, or any failure → `Solid`.

When `Acrylic` is selected, set `DWMWA_SYSTEMBACKDROP_TYPE` to `DWMSBT_NONE` so DWM stops drawing its flat fill. Rounded corners remain via `DWMWA_WINDOW_CORNER_PREFERENCE`.

`gfx::create` builds the WinRT composition tree once. `gfx::resize` updates root and layer sizes plus the swap-chain surface brush; the existing `ResizeBuffers` and D2D target recreation stay. `gfx::applySettings` rebuilds only the tint color and backend selection on config reload, not the swap chain. `gfx::setVisualState` sets root `Opacity` and `Offset` instead of the DComp effect group. `setVisualState(1.0f, 0.0f)` on hide and `trim()` stay. `show()` and `hide()` gain no new window.

### Config and settings

No new config key. `theme.backdrop` keeps `acrylic | mica | none` and `theme.backdrop_opacity` keeps `0.0..1.0`. The 0.6 tint floor in `config::glassTintAlpha` applies only to the solid fallback; under `Acrylic` the tint uses the configured value directly so low opacity reveals more of the blur. Update the config comment and README sentence accordingly. Settings field indices stay unchanged.

### Error handling

Every WinRT call is wrapped; on a failed HRESULT, missing interface, unsupported OS or device loss, dispose the partial graph, set `Solid`, and let the existing D2D solid clear draw the popup. No exception escapes `gfx` into the message loop. High-contrast changes arriving through `WM_SETTINGCHANGE` re-run backend selection.

### Verification

- Fresh magenta-behind-popup capture: interior shows magenta blur; solid fallback still solid.
- Before/after screenshots at the same query and selection for `acrylic`, `mica`, `none`, high-contrast and Windows 10 (the last two may be TRACED if the hardware is unavailable, and must be labelled as such).
- Tests: 211 existing cases must still pass; add no tests for Win32 rendering.
- Release build with `/W4 /WX`, then run `tools/measure.ps1` and report executable size, idle CPU, GPU memory, working set, startup and hotkey latency against the budget report.

## Risks and decisions

- The blur graph is described by hand because Win2D would add a dependency. This is the least-documented part; the magenta test is the gate before any further work.
- Acrylic is documented by Microsoft as GPU-intensive and live. If measurements show idle cost or a size regression, stop and report rather than optimising blindly.
- `CreateHostBackdropBrush` samples windows behind the target. With two topmost windows, z-order decides what is sampled; that is the intended acrylic behavior.
- The existing `DWMWA_SYSTEMBACKDROP_TYPE` code is removed rather than kept as a second path, so there is one backend per mode.

## Handoff

Implementation must not begin until the user reviews this file. The next workflow step after approval is writing-plans, not code.

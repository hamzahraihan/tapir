# OS Composition Acrylic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat DWM backdrop with a real, live-blurred acrylic layer built from OS Windows.UI.Composition, behind the existing D3D11/D2D UI.

**Architecture:** `gfx::Context` owns a WinRT `Compositor`, a `DispatcherQueueController`, a `DesktopWindowTarget` on the popup HWND and a `ContainerVisual` root with three `SpriteVisual` layers: a blurred backdrop brush, a tint color, and the existing swap-chain surface brush. Raw DirectComposition objects are removed. Any failure selects the current solid fallback.

**Tech Stack:** C++20, MSVC, C++/WinRT headers from the Windows SDK, `windowsapp.lib`, WRL `ComPtr`, Direct3D 11, DXGI, Direct2D/DirectWrite, static CRT, CMake/Ninja.

## Global Constraints

- No Windows App SDK runtime, no Win2D, no new third-party dependency.
- OS WinRT headers and `windowsapp.lib` only; static CRT `/MT` stays.
- One HWND; the popup keeps `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP` and the existing swap chain, D2D painter and icon loader.
- No new config key; `theme.backdrop` keeps `acrylic | mica | none`, `theme.backdrop_opacity` keeps `0.0..1.0`.
- Any failure falls back to the existing solid theme background; the popup is never blank or opaque white.
- `/W4 /WX` clean, 211 existing tests pass, budgets in `docs/budget-report.md` remain met.
- Do not commit unless explicitly requested.

---

### Task 1: WinRT foundation

**Files:**
- Modify: `CMakeLists.txt:93`
- Modify: `src/main.cpp:23-33`
- Modify: `src/gfx.h:1-37`
- Modify: `src/gfx.cpp:1-20`

**Interfaces:**
- Consumes: existing `gfx::Context`, `wWinMain`.
- Produces: initialised `winrt::init_apartment()`, a `Windows::System::DispatcherQueueController` on the UI thread, and `windowsapp.lib` linked.

- [ ] **Step 1: Link the OS WinRT library**

In `CMakeLists.txt`, change the `target_link_libraries(tapir ...)` line at 93 to append `windowsapp` at the end of the existing library list.

- [ ] **Step 2: Initialise WinRT on the UI thread**

At the top of `wWinMain`, before any window creation, add:

```cpp
winrt::init_apartment(winrt::apartment_type::single_threaded);
```

Create the dispatcher queue in `gfx::create` (Task 3) with `CreateDispatcherQueueController`; it must be created on the same thread as the `Compositor`.

- [ ] **Step 3: Add includes and forward fields**

In `src/gfx.cpp`, add:

```cpp
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>
```

In `src/gfx.h`, add the same headers except the interop header, and add to `Context`:

```cpp
    winrt::Windows::System::DispatcherQueueController dispatcher{ nullptr };
    winrt::Windows::UI::Composition::Compositor compositor{ nullptr };
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual root{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual backdropVisual{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual tintVisual{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual uiVisual{ nullptr };
    winrt::Windows::UI::Composition::CompositionEffectBrush backdropBrush{ nullptr };
    enum class Backend { Acrylic, Solid } backend = Backend::Solid;
```

Keep `bool acrylic = false;` in `Context` for now; Task 3 removes it when it replaces every call site. Task 1 must compile on its own.

- [ ] **Step 4: Verify compilation**

Run `cmake --build build --config Release --target tapir -- /m:1`. Expected: exit 0, no `/W4 /WX` warnings. If an interop header signature differs from this sketch, correct the call against the installed SDK header at `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\windows.ui.composition.interop.h` and report the change.

### Task 2: Hand-rolled Gaussian blur effect

**Files:**
- Create: `src/blur_effect.h`

**Interfaces:**
- Consumes: WRL and `windows.graphics.effects.interop.h` from the Windows SDK.
- Produces: `tapir::createGaussianBlurEffect(float radiusDip)` returning a `winrt::com_ptr<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>` usable as the root of `Compositor.CreateEffectFactory`.

- [ ] **Step 1: Write the effect class**

Create `src/blur_effect.h` with a WRL `RuntimeClass` implementing `IGraphicsEffect`, `IGraphicsEffectSource` and `IGraphicsEffectD2D1Interop`. The methods must return: effect name (null string accepted by `CreateEffectFactory`), `GetEffectId` = `CLSID_D2D1GaussianBlur`, `GetPropertyCount` = 2, `GetProperty` returning `D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION` as `float radiusDip` for index 0 and `D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION` = `D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED` for index 1, `GetSourceCount` = 1, `GetSource` = the `CompositionEffectSourceParameter` named `L"backdrop"`. Use the method signatures from the installed `windows.graphics.effects.interop.h` rather than guessing them; the yvt article "Creating A Custom Effect Class" documents the required interface set.

```cpp
inline winrt::com_ptr<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>
createGaussianBlurEffect(float radiusDip) {
    auto source = winrt::make<GaussianBlurEffect>(radiusDip);
    return source.as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>();
}
```

- [ ] **Step 2: Add a compile-and-instantiate check**

In `src/gfx.cpp`, temporarily include `blur_effect.h` and add inside `gfx::create`:

```cpp
auto blurSource = tapir::createGaussianBlurEffect(24.0f);
```

Build Release and confirm `CreateEffectFactory` is reachable in Task 3. Remove the unused variable once Task 3 wires the graph. This step has no doctest: the gate is a successful `/W4 /WX` build.

### Task 3: Composition tree and swap-chain layer

**Files:**
- Modify: `src/gfx.cpp:71-138`, `:140-168`, `:175-180`
- Modify: `src/gfx.h:19-45`

**Interfaces:**
- Consumes: Task 1 fields, Task 2 `createGaussianBlurEffect`, existing `swapChain`, `widthPx`, `heightPx`, `dpi`.
- Produces: a live three-layer visual tree and `Backend::Acrylic` when creation succeeds.

- [ ] **Step 1: Create the target and root**

In `gfx::create`, after the existing DirectComposition block is removed, add:

```cpp
DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_ASTA };
winrt::check_hresult(CreateDispatcherQueueController(options, ctx->dispatcher.put()));
ctx->compositor = winrt::Windows::UI::Composition::Compositor{};
auto desktopInterop = ctx->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
winrt::com_ptr<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget> rawTarget;
winrt::check_hresult(desktopInterop->CreateDesktopWindowTarget(
    hwnd, TRUE, reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(rawTarget.put())));
ctx->target = rawTarget.as<winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget>();
ctx->root = ctx->compositor.CreateContainerVisual();
ctx->root.RelativeSizeAdjustment({1.0f, 1.0f});
ctx->target.Root(ctx->root);
```

- [ ] **Step 2: Build the blur and tint layers**

```cpp
auto sourceParam = ctx->compositor.CreateEffectSourceParameter(L"backdrop");
auto blur = tapir::createGaussianBlurEffect(24.0f);
auto factory = ctx->compositor.CreateEffectFactory(blur);
ctx->backdropBrush = factory.CreateBrush();
auto backdrop = ctx->compositor.CreateBackdropBrush();
ctx->backdropBrush.SetSourceParameter(L"backdrop", backdrop);
ctx->backdropVisual = ctx->compositor.CreateSpriteVisual();
ctx->backdropVisual.RelativeSizeAdjustment({1.0f, 1.0f});
ctx->backdropVisual.Brush(ctx->backdropBrush);
ctx->tintVisual = ctx->compositor.CreateSpriteVisual();
ctx->tintVisual.RelativeSizeAdjustment({1.0f, 1.0f});
ctx->root.Children().InsertAtTop(ctx->backdropVisual);
ctx->root.Children().InsertAtTop(ctx->tintVisual);
```

If `CreateBackdropBrush()` is unavailable or the magenta test later fails, retry with `CreateHostBackdropBrush()` before falling back.

- [ ] **Step 3: Bind the swap chain as the UI layer**

```cpp
auto interop = ctx->compositor.as<ABI::Windows::UI::Composition::Interop::ICompositorInterop>();
winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionSurface> surface;
winrt::check_hresult(interop->CreateCompositionSurfaceForSwapChain(ctx->swapChain.get(), surface.put()));
auto brush = ctx->compositor.CreateSurfaceBrush(surface.as<winrt::Windows::UI::Composition::ICompositionSurface>());
ctx->uiVisual = ctx->compositor.CreateSpriteVisual();
ctx->uiVisual.RelativeSizeAdjustment({1.0f, 1.0f});
ctx->uiVisual.Brush(brush);
ctx->root.Children().InsertAtTop(ctx->uiVisual);
```

Match the exact namespace from the installed `windows.ui.composition.interop.h`; the ABI namespace may be `::ICompositorInterop` rather than a WinRT namespace.

- [ ] **Step 4: Replace DComp lifetime calls**

In `gfx::setVisualState`, replace `compEffect->SetOpacity` / `compVisual->SetOffsetY` / `Commit()` with `root.Opacity(opacity)` and `root.Offset({0.0f, offsetPx, 0.0f})`. In `gfx::resize`, after `ResizeBuffers`, update `root`, `backdropVisual`, `tintVisual` and `uiVisual` sizes to `{static_cast<float>(widthPx), static_cast<float>(heightPx)}` (physical pixels, as composition uses pixels). In `gfx::create`, remove `compDevice`, `compTarget`, `compVisual`, `compEffect` and their `Create*` block from `gfx.h`/`gfx.cpp`. Remove `bool acrylic = false;` from `Context` and replace all remaining reads of `context.acrylic` with the new backend check.

- [ ] **Step 5: Verify**

Run `cmake --build build --config Debug --target tapir_tests` and `build\Debug\tapir_tests.exe`: all 211 tests pass. Then build Release. Expected: no warnings.

### Task 4: Backend selection, DWM and tint

**Files:**
- Modify: `src/gfx.cpp:47-56`, `:132-138`, `:170-173`
- Modify: `src/render.cpp:156-158`, `:569-576`
- Modify: `src/config.cpp:22-25`, default config comment
- Modify: `README.md:41,69`

**Interfaces:**
- Consumes: `Context::backend`, `Settings::Theme`, `PaletteView`.
- Produces: `Acrylic` only when safe; solid fallback otherwise; tint alpha from config under `Acrylic`.

- [ ] **Step 1: Replace applyBackdrop**

Rewrite `applyBackdrop(HWND, const Settings&, Context&)` to: set corner preference round when `backdrop != None` and high contrast is off; set `DWMWA_SYSTEMBACKDROP_TYPE` to `DWMSBT_NONE`; choose `ctx.backend = Backend::Acrylic` only when `settings.theme.backdrop == Backdrop::Acrylic`, high contrast is off, the WinRT tree exists and neither `CreateBackdropBrush` nor `CreateHostBackdropBrush` failed. Otherwise `Backend::Solid`. For `Backdrop::Mica`, keep `ctx.backend = Backend::Solid` and set `DWMSBT_MAINWINDOW` instead of none.

- [ ] **Step 2: Stop the painter filling the background under Acrylic**

In `Painter::draw`, change the branch at 569 to:

```cpp
if (view.acrylic) {
    p.target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
} else {
    p.target->Clear(toColor(p.theme.background));
}
```

In `gfx::render`, set `v.acrylic = context.backend == Context::Backend::Acrylic;`.

In `Painter::recreateBrushes`, keep `glassTintAlpha` only for the solid fallback; under `Acrylic`, `gfx::applySettings` instead sets `tintVisual.Brush(compositor.CreateColorBrush(toColor(theme.background, theme.backdropOpacity)))` using a D2D-to-WinRT color conversion with alpha as `float` in `[0,1]`.

- [ ] **Step 3: Handle high-contrast changes**

In `window.cpp:1237-1239` (`WM_SETTINGCHANGE`), call `gfx::applySettings(*app->gfx, app->popup, app->settings)` when the new backend differs; `gfx::applySettings` itself is idempotent.

- [ ] **Step 4: Update documentation**

Change the README sentence and generated config comment so they no longer describe a 0.6 tint floor as the acrylic behavior; state that the floor applies to the solid fallback only.

- [ ] **Step 5: Verify**

Run the Debug test build and tests; expected 211/211. Build Release with `/W4 /WX`.

### Task 5: Visual proof and budget measurement

**Files:**
- No source changes unless a defect is found.

- [ ] **Step 1: Magenta proof**

Run `tools/measure.ps1`-style configuration with `theme.backdrop = "acrylic"` and `backdrop_opacity = 0.2`, show a solid magenta window behind the popup, and capture with the existing `G:\Temp\tmp\opencode\probe-backdrop.ps1`. Pass condition: popup interior is tinted magenta, not flat gray. If it fails, try `CreateHostBackdropBrush` (Task 3 step 2) once; if that fails, revert to the solid fallback and report NEEDS HUMAN REVIEW.

- [ ] **Step 2: Fallback checks**

Capture `backdrop = "none"` and high-contrast mode; both must show the solid theme background with fully readable text. Confirm no blank or white popup.

- [ ] **Step 3: Measurements**

Run `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/measure.ps1` from a Release build. Report executable size, idle CPU, GPU memory, working set, hotkey-to-visible and startup against `docs/budget-report.md`. If a budget regresses, stop and report the number rather than hiding it.

- [ ] **Step 4: Report**

Summarise changed files, tests, build, magenta proof and numbers. Do not commit.

## Self-review

- Spec coverage: composition model → Task 3; backend selection and fallback → Task 4; config/settings unchanged → Task 4 step 4; verification and budgets → Task 5; no new dependency → Global Constraints and Task 1.
- Placeholder scan: every code step contains code; no "similar to" or TODO.
- Type consistency: `Context::backend`, `Backdrop::Acrylic`, `createGaussianBlurEffect`, `backdropBrush` and `tintVisual` keep the same names across tasks.

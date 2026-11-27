# System backdrops vs DWM attributes: cost, compatibility, packaging

Evidence gathered 2026-11-26 from Microsoft Learn primary sources. Every claim below links to the page that owns it; quotes are verbatim. Adjacent guidance is labelled as such. Inferences are labelled and must be measured before they are treated as facts.

## Bottom line for the risk section

- `DWMWA_SYSTEMBACKDROP_TYPE` with `DWMSBT_TRANSIENTWINDOW` already produces **real Desktop Acrylic** on Windows 11 build 22621+, is **drawn by DWM** ("system-drawn backdrop material"), and needs **no Windows App SDK, no WinRT, no extra DLLs**. This is the lowest-risk path for tapir's single-exe constraint.
- `DesktopAcrylicController` / `MicaController` require the **Windows App SDK runtime**, C++/WinRT, a `DispatcherQueue` and a `Compositor`. Framework-dependent unpackaged apps need an installed runtime plus a bootstrapper DLL; self-contained apps carry the framework binaries next to the exe. Both break "one executable, nothing to install".
- Mica is Windows 11 only and samples the wallpaper **once**. Acrylic is a **live** blur and is documented as GPU-intensive. Neither path has a published numeric GPU/CPU budget — measurement is mandatory.
- A Windows 10 solid fallback is feasible and is the **documented** behavior for Mica. For acrylic, the Windows App SDK path supports Windows 10 1809+, but that is exactly the path that breaks packaging. The DWM attribute path is Windows 11 only, so Windows 10 needs a solid fallback there.

## 1. Documented GPU/CPU cost and continuous animation

**Mica samples once.** [Mica material](https://learn.microsoft.com/en-us/windows/apps/design/style/mica):

> "Mica is specifically designed for app performance as it only samples the desktop wallpaper once to create its visualization."

> "They are designed for performance as they capture the background wallpaper only once to create their visualizations."

[System backdrops](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops):

> "Mica is designed for performance as it only captures the background wallpaper once to create its visualization, so we recommend it for the foundation layer of your app, especially in the title bar area."

**Acrylic is live.** [Materials overview](https://learn.microsoft.com/en-us/windows/apps/develop/ui/materials):

> "Desktop Acrylic shows a live, blurred view of the desktop and content behind the app window, creating a frosted-glass appearance."

**Acrylic is GPU-intensive.** [Acrylic material](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic):

> "Rendering acrylic surfaces is GPU-intensive, which can increase device power consumption and shorten battery life. Acrylic effects are automatically disabled when a device enters Battery Saver mode. Users can disable acrylic effects for all apps by turning off *Transparency effects* in Settings > Personalization > Colors."

> "The acrylic recipe: background, blur, exclusion blend, color/tint overlay, noise"

[Materials overview](https://learn.microsoft.com/en-us/windows/apps/develop/ui/materials):

> "Materials rely on compositor effects that require hardware support and user preferences."

**Adjacent (not backdrop-specific) guidance on continuous work.** [Optimize animations, media, and images for WinUI apps](https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-animations-and-media):

> "We recommend minimizing the use of infinite animations because they continually consume CPU resources and can prevent the CPU from going into a low power or idle state, causing it to run out of power more quickly."

> "Adding a handler for CompositionTarget.Rendering is similar to running an infinite animation. Normally the UI thread is active only when there is work to do, but adding a handler for this event forces it to run every frame. Remove the handler when there is no work to be done and reregister it when it's needed again."

This is about XAML animations, not backdrops. It is included because it is the only first-party statement of the idle-CPU principle tapir's budget depends on.

**What is NOT documented:** no Microsoft page found gives a numeric GPU utilization, frame rate, or per-frame cost for either backdrop, and none says in so many words that acrylic "animates continuously". The documented distinction is one-time wallpaper sample (Mica) vs live desktop blur (Acrylic). Treat "acrylic re-samples as content behind it changes" as a reasonable reading of "live, blurred view", not as a quoted spec.

**Where the cost lands.** [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute) describes `DWMWA_SYSTEMBACKDROP_TYPE` as retrieving or specifying "the **system-drawn** backdrop material of a window". Inference: the blur runs in DWM/the compositor, not in tapir's render loop, so tapir's own idle CPU can stay near zero either way. This must be verified with measurement; it is not stated as a performance guarantee.

## 2. Windows version requirements and Windows 10 behavior

**DWM attribute path.** [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute):

> "`DWMWA_SYSTEMBACKDROP_TYPE` ... This value is supported starting with Windows 11 Build 22621."

> "`DWMWA_WINDOW_CORNER_PREFERENCE` ... This value is supported starting with Windows 11 Build 22000."

> "`DWMWA_USE_HOSTBACKDROPBRUSH` ... This value is supported starting with Windows 11 Build 22000."

[DWM_SYSTEMBACKDROP_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type) — "Minimum supported client: Windows 11 Build 22621":

> "`DWMSBT_MAINWINDOW` Draw the backdrop material effect corresponding to a long-lived window behind the entire window bounds. For Windows 11, this corresponds to Mica in its default variant."

> "`DWMSBT_TRANSIENTWINDOW` Draw the backdrop material effect corresponding to a transient window behind the entire window bounds. For Windows 11, this corresponds to Desktop Acrylic, also known as Background Acrylic, in its brightest variant."

> "`DWMSBT_TABBEDWINDOW` ... For Windows 11, this corresponds to Mica in its alternate variant (Mica Alt)."

> "`DWMSBT_NONE` Don't draw any system backdrop."

Note the docs say "The material effect might change with future Windows releases" for each value — the visual result is not frozen to today's acrylic.

**Windows App SDK controller path.** [Windows developer FAQ](https://learn.microsoft.com/en-us/windows/apps/get-started/windows-developer-faq):

> "Desktop Acrylic is supported on Windows 10, version 1809 and later. Mica requires Windows 11 and falls back to a solid theme color on Windows 10. Call `MicaController.IsSupported` or `DesktopAcrylicController.IsSupported` at run time before applying a backdrop."

[Mica material](https://learn.microsoft.com/en-us/windows/apps/design/style/mica) — solid fallback when:

> "The Windows version is below 22000."

[Materials overview](https://learn.microsoft.com/en-us/windows/apps/develop/ui/materials):

> "Mica ... Windows version: Windows 11 only. Falls back to a solid theme color on Windows 10."

> "Desktop Acrylic ... Windows version: Windows 10 (build 17763) and later."

[Windows App SDK 1.2 release notes](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/release-notes/windows-app-sdk-1-2):

> "Acrylic backdrop material with DesktopAcrylicController is now supported in Windows 10 apps."

[Windows versions and SDK overview](https://learn.microsoft.com/en-us/windows/apps/get-started/versioning-overview): Windows App SDK minimum supported OS is Windows 10 1809 (build 17763) across versions 1.7–2.2.

**Legacy blur is a dead end.** [DwmEnableBlurBehindWindow](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow):

> "Beginning with Windows 8, calling this function doesn't result in the blur effect, due to a style change in the way windows are rendered."

So it cannot produce blur on Windows 10 or 11 regardless of the DWM attribute choice.

## 3. Windows App SDK runtime dependency vs a single-exe no-install app

[Use Mica material in Win32 apps](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-mica-win32) — prerequisites:

> "To apply Mica to a Win32 app for Windows 11, you need to use the Windows App SDK."

> "For unpackaged apps, reference the Windows App SDK, install WinRT, and have a matching Windows App Runtime Redistributable (Microsoft.WindowsAppRuntime.Redist) installed."

The sample requires `winrt::init_apartment()`, a `WindowsAppSDKBootstrapperContext`, a `DispatcherQueueController`, a `Compositor`, and `MicaController.SetTarget(WindowId, CompositionTarget)`.

[Deployment guide for framework-dependent unpackaged apps](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-unpackaged-apps):

> "You're responsible for deploying the required Windows App SDK runtime packages with your unpackaged app, either by running the runtime installer or by installing the MSIX packages directly."

> "Your app must also use the Bootstrapper API, either automatically or explicitly, to add the Framework package to its process package graph before using Windows App SDK APIs."

> "The Visual C++ Redistributable is a requirement."

[Use the Windows App SDK runtime](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/use-windows-app-sdk-run-time):

> "The bootstrapper API consists of three C/C++ functions ... provided by the bootstrapper library in the Windows App SDK. That library is a small DLL that you must distribute with your app; it's not part of the framework package itself."

[Deployment guide for self-contained apps](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps):

> "Having set the `WindowsAppSDKSelfContained` property to `true` ... the contents of the Windows App SDK Framework package will be extracted to your build output, and deployed as part of your application."

> "If your app is packaged with external location or unpackaged, then the Windows App SDK dependencies are copied next to the `.exe` in your build output. You can xcopy-deploy the resulting files, or include them in a custom installer."

> "Note that `dotnet publish` bundles managed assemblies but cannot produce a single-file EXE for WinUI 3 apps — the native Windows App SDK runtime dependencies must remain as separate files."

[Distribute an unpackaged WinUI 3 app](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/unpackage-winui-app) — single-file EXE:

> "Starting with Windows App SDK 1.5, unpackaged, self-contained WinUI 3 apps support the .NET `PublishSingleFile` deployment model. This produces a single distributable EXE file — all dependencies are bundled into the EXE and extracted to a temp directory at first launch."

> "`PublishSingleFile` is not supported for packaged apps (MSIX or packaged with external location) or for framework-dependent apps."

> "`IncludeAllContentForSelfExtract=true` means dependencies are extracted to a temp directory on the user's machine at first launch — the app is not a zero-extraction binary."

> "If you need a zero-extraction single binary, or if extraction behavior is not acceptable in your deployment environment, consider: Use MSIX packaging ... Use a traditional installer (WiX, Inno Setup) ... Use a different framework."

**Conclusion:** the controller path is incompatible with tapir's "one executable, statically linked CRT, nothing to install" constraint. The DWM attribute path has no such dependency.

## 4. C++/WinRT / WinRT headers, static CRT, binary size

C++/WinRT itself is not the blocker for `/MT`. [cppwinrt issue #1053](https://github.com/microsoft/cppwinrt/issues/1053), maintainer kennykerr:

> "For C++/WinRT you can use whatever CRT is supported by Visual C++."

[Windows App SDK Hybrid CRT guidelines](https://github.com/microsoft/WindowsAppSDK/blob/main/docs/Coding-Guidelines/HybridCRT.md):

> "For their own CRT usage, C/C++ Windows App SDK app developers may continue to link statically, with no redistribution requirements, or dynamically, requiring either a VCLibs* dependency (for packaged apps) or a vc_redist.*.exe dependency (for unpackaged apps)."

> "For unpackaged self-contained C/C++ Windows App SDK apps, developers may find Hybrid CRT linkage useful for enabling xcopy deployment."

> "Static CRT links needed symbols into PE files making avoiding the need for an external dependency at runtime at the cost of increased size of every PE file in Windows App SDK. The cost varies depending on the symbols needed but 1M+ is common." (This describes the SDK's own DLLs, not tapir's exe.)

> "The 'Hybrid CRT' is a technique using the Universal CRT AND the static CRT to get functional coverage without the overhead of the static CRT or the external dependency of the dynamic CRT."

> "This poses a theoretical maximum worst-case static linkage size of ~200KB, but that assumes every symbol in the Standard C/C++ Library is needed. In practice it's a smaller slice."

> "Microsoft.ProjectReunion.dll in 0.8-preview grew from 516,096 bytes using the dynamic CRT to 554,496 using the Hybrid CRT (+37KB or ~+7%)."

[PR #888 "Use the Universal C Runtime"](https://github.com/microsoft/WindowsAppSDK/pull/888) measured six SDK binaries: total 723,968 -> 852,480 bytes (+128,512) after switching to Hybrid CRT.

C++/CX is a separate, incompatible case: [HybridCRT.md](https://github.com/microsoft/WindowsAppSDK/blob/main/docs/Coding-Guidelines/HybridCRT.md) notes `1>cl : command line error D8016: '/MTd' and '/ZW' command-line options are incompatible`. tapir does not use C++/CX, so this does not apply.

**Conclusion:** `/MT` remains technically possible with C++/WinRT headers. The size and packaging problem is the Windows App SDK runtime binaries and framework package, not the CRT choice. No first-party figure was found for the size of a single C++/WinRT exe that uses `Microsoft.UI.Composition.SystemBackdrops`; that number must be measured, not assumed.

## 5. Guidance to disable blur when hidden or in high-contrast mode

**High contrast — all materials suppressed.** [Materials overview](https://learn.microsoft.com/en-us/windows/apps/develop/ui/materials):

> "High contrast mode — all materials are suppressed; the system applies high-contrast theme colors instead."

[Acrylic material](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic):

> "In High Contrast mode, users continue to see the familiar background color of their choosing in place of acrylic. In addition, both background acrylic and in-app acrylic appear as a solid color"

[Mica material](https://learn.microsoft.com/en-us/windows/apps/design/style/mica):

> "In High Contrast mode, users continue to see the familiar background color of their choosing in place of Mica or Mica Alt."

The controller path exposes explicit policy knobs. [SystemBackdropConfiguration](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.composition.systembackdrops.systembackdropconfiguration?view=windows-app-sdk-2.0) properties: `HighContrastBackgroundColor`, `IsHighContrast`, `IsInputActive`, `Theme`. `IsInputActive` is documented as:

> "Gets or sets a value that indicates whether the system backdrop should consider the current window as having input focus."

**Deactivation — directly relevant to tapir's hide-on-focus-loss.** [Acrylic material](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic):

> "In addition, only background acrylic will replace its translucency and texture with a solid color: When an app window on desktop deactivates."

[Mica material](https://learn.microsoft.com/en-us/windows/apps/design/style/mica):

> "the material helps users focus on the current task by falling back to a neutral color when the app is inactive."

> "An app window on desktop deactivates."

**Other automatic fallbacks.** [Materials overview](https://learn.microsoft.com/en-us/windows/apps/develop/ui/materials):

> "Remote Desktop or virtual machines — the compositor cannot blend with desktop content over RDP, so `SystemBackdrop` materials fall back automatically."

> "Insufficient graphics hardware — Mica and Desktop Acrylic require DirectX 11 and adequate GPU memory. Devices that don't meet the threshold fall back to a solid theme color."

> "Transparency effects disabled — if the user turns off Transparency effects in Settings > Personalization > Colors, all `SystemBackdrop` materials and `AcrylicBrush` fall back to their solid-color alternatives."

> "Battery Saver active — Windows disables acrylic (`DesktopAcrylicBackdrop` and `AcrylicBrush`) when Battery Saver is on. Mica is not affected."

> "Your app doesn't need to detect these conditions — the APIs handle fallback automatically. Make sure any `FallbackColor` or theme background you configure reads well as a plain solid color."

**No explicit "disable the backdrop when the window is hidden" guidance was found** in the Microsoft docs reviewed. Inference to verify: a DWM-drawn backdrop is not composited while the window is hidden, so hiding the popup should remove its GPU cost without an explicit API call; the controller path has `IsInputActive` for the inactive case. Do not treat this as documented until measured.

## 6. Pure-Win32 alternatives assessed

| Approach | Real blur? | Version | Runtime dependency | Verdict for tapir |
|---|---|---|---|---|
| `DWMWA_SYSTEMBACKDROP_TYPE` = `DWMSBT_TRANSIENTWINDOW` | Yes, Desktop Acrylic on Win11 | Win11 22621+ | None (DWM) | Best fit. Win10 needs solid fallback. |
| `DWMWA_SYSTEMBACKDROP_TYPE` = `DWMSBT_MAINWINDOW` | Yes, Mica on Win11 | Win11 22621+ | None (DWM) | Same, but one-time sample, cheaper. |
| `DwmEnableBlurBehindWindow` | No on Win10/11 | Vista+ but broken since Win8 | None | Reject — documented no-op. |
| `DWMWA_USE_HOSTBACKDROPBRUSH` + `Compositor.CreateHostBackdropBrush` | Yes, host backdrop acrylic | Win11 22000+ | OS WinRT (`Windows.UI.Composition`), no Windows App SDK runtime | Possible but needs WinRT activation, a `DispatcherQueue` and a composition target. Higher complexity; not measured. |
| `DesktopAcrylicController` / `MicaController` | Yes, incl. Win10 1809+ acrylic | Win10 1809+ | Windows App SDK runtime + bootstrapper | Reject — breaks single-exe no-install. |
| Custom D2D `CLSID_D2D1GaussianBlur` | Only if you capture the desktop behind the window yourself | Win8+ | None | Reject for live blur: needs a per-frame redraw, conflicting with render-on-demand and idle CPU ~0%. |

Supporting quote for the last row. [Gaussian blur effect](https://learn.microsoft.com/en-us/windows/win32/direct2d/gaussian-blur):

> "Use the Gaussian blur effect to create a blur based on the Gaussian function over the entire input image."

[Effect shader linking](https://learn.microsoft.com/en-us/windows/win32/direct2d/effect-shader-linking):

> "Some pixel shaders, such as a Gaussian blur, compute their output from multiple input samples rather than just a single sample. Such a pixel shader is said to perform complex sampling."

> "Only shader functions with simple inputs can have their input provided by another shader function. Shader functions with complex inputs must be provided with an input texture to sample. This means that Direct2D will not link a shader with complex inputs to its predecessor."

So a Gaussian blur cannot be shader-linked to preceding transforms; it costs its own pass and intermediate surface. There is also no documented, supported Win32 API for continuously sampling the desktop behind a window for this purpose.

## Budget measurement plan (inputs to the risk section)

Measure each candidate with the existing `tools/measure.ps1` harness after a Release build:

1. Hotkey-to-first-frame with `DWMSBT_TRANSIENTWINDOW` vs solid fallback, cold and warm.
2. DWM process CPU and system GPU utilization with the popup visible for 10 s vs hidden for 60 s. This is the only way to settle the "continuous animation" cost question — the docs give no number.
3. Idle working set with the popup hidden, to confirm no render loop was introduced.
4. Release `tapir.exe` size with and without any WinRT/Windows App SDK linkage, against the <2 MB budget.
5. Windows 10 behavior: confirm what `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, ...)` actually returns on an unsupported build (the docs do not say whether it fails or is silently ignored) and verify the solid fallback path.
6. High-contrast and Battery Saver: verify the solid fallback is legible with the configured theme colors.

## Ambiguities the docs do not resolve

- No numeric GPU/CPU cost or frame rate for either backdrop material.
- No explicit instruction to disable a backdrop when the window is hidden.
- No first-party binary-size figure for a C++/WinRT exe using the system backdrop controllers.
- The exact return behavior of `DwmSetWindowAttribute` for `DWMWA_SYSTEMBACKDROP_TYPE` on Windows 10 is not documented.
- The DWM enum docs warn the material "might change with future Windows releases", so a visual match to today's acrylic is not guaranteed long-term.

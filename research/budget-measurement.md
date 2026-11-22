# Measuring the section 9 performance and size budgets on this machine

Research ticket: [hamzahraihan/tapir#6](https://github.com/hamzahraihan/tapir/issues/6) — "How can the section 9 performance and size budgets be measured on this machine?"

**This document specifies a measurement METHOD, not measured numbers.** The budgets themselves are copied from `AGENTS.md` section 9 of the tapir repository; no measurement has been run as part of this research, and no build or installation was performed.

All authoritative claims in this document come from first-party Microsoft documentation (Microsoft Learn, Windows SDK/Win32/DirectX reference, MSVC reference, Windows Performance Toolkit and PIX documentation). Sources are numbered `[n]` and listed with full URLs in [Sources](#sources).

---

## 1. Scope and environment

- The budgets to measure are: hotkey-to-drawn < 50 ms (ideally one frame); keystroke-to-results < 16 ms; idle CPU ≈ 0%; each animation < ~150 ms at display refresh rate with a frame timer that runs only while animating; hidden state with GPU/cache memory trimmed via `IDXGIDevice3::Trim`; idle working set in the low tens of MB; release executable < ~2 MB; startup-to-hotkey-ready < 500 ms.
- The target application is a native C++20 Win32/D3D11/DXGI-flip-model/Direct2D/DirectWrite/DirectComposition executable (`tapir.exe`) with render-on-demand, a single pre-created window, and a background app-index thread.
- On this machine MSVC is reachable only from **Developer PowerShell for VS**. Microsoft documents the two developer shells and notes that they set the environment variables and `PATH` needed by `CL`, `DUMPBIN`, `LIB`, and `MSBuild`; `Launch-VsDevShell.ps1` (which loads the `Microsoft.VisualStudio.DevShell.dll` module and invokes `Enter-VsDevShell`) is the recommended way to initialize Developer PowerShell interactively or from automation `[31]`. The same environment can be established in an existing shell with `vcvarsall.bat`, whose syntax is `vcvarsall.bat [architecture] [platform_type] [winsdk_version] [-vcvars_ver:vcversion] [spectre_mode]` `[32]`.
- Two complementary layers are used throughout: **in-app timestamps** (precise boundary definitions, negligible overhead) and **system-level ETW/PIX/perf-counter captures** (independent confirmation and breakdown). This split matters because Windows Performance Counter data is explicitly "not appropriate for high-frequency data collection or for application profiling" and is not designed to be collected more than once per second `[12]`.

### 1.1 In-app instrumentation primitives (used by several budgets)

- Use `QueryPerformanceCounter` (QPC) for timestamps and `QueryPerformanceFrequency` once at startup for conversion `[1][2]`. Microsoft describes QPC as a high-resolution (< 1 µs) time stamp suitable for time-interval measurements `[1]`, states that the frequency is fixed at system boot and need only be queried at initialization `[2]`, and recommends QPC over `RDTSC` because processor frequency varies with power management `[4]`. The "Acquiring high-resolution time stamps" guidance documents tick-uncertainty and cross-processor consistency caveats `[3]`.
- Emit the stamp pairs as **TraceLogging** ETW events from a tapir provider, with fields for event name, QPC tick, and any relevant identifiers (frame id, query length, PID). Microsoft documents that TraceLogging events are captured with WPR (`wpr.exe -start ...` / `wpr.exe -stop ...` from an elevated prompt), decoded with WPA, and appear in the **Generic Events** graph/table `[5]`. WPR is the recommended ETW controller and WPA the recommended consumer `[6]`; together they form the Windows Performance Toolkit shipped with the Windows ADK `[7]`.
- Record results as median and p95 over a fixed number of repetitions (e.g. 100 hotkey toggles, one representative query typed key-by-key, 30 s idle windows, 10 launches). Always note the build type (release), graphics backend (hardware vs WARP), refresh rate, and git commit alongside the numbers.

### 1.2 Cross-check tooling and tool selection

- **First-party tool-selection guidance:** start with the Visual Studio Performance Profiler for a single process with source; move to Windows Performance Toolkit for composition, ETW scenarios and very large traces `[49]`. WPR/WPA ship in the Windows ADK `[7]`; WPA opens ETL files and provides the step-by-step workflow for selecting intervals and filtering events `[48]`.
- **PIX** is a separate Microsoft tool; its CPU profiling works for any Windows application and its GPU capabilities work on any application using Direct3D `[60]`. For this D3D11 app, use **Timing Captures with GPU timings** rather than a GPU Capture, which records D3D12 API calls `[28][30a]`.
- **Sysinternals** utilities are hosted by Microsoft and can be run from `\\live.sysinternals.com\tools\` without installation `[57]`: Process Explorer for live CPU cross-checks `[54]`, VMMap for per-type committed/working-set breakdown `[55]`, and RAMMap for system-wide physical-memory context `[56]`.
- **PowerShell `Get-Counter`** is a first-party counter reader for quick CPU/working-set checks `[50]`; PDH queries are built with `PdhOpenQuery` plus `PdhAddCounter`, with locale-neutral paths via `PdhAddEnglishCounter` `[51][53][52]`.
- **DwmFlush** blocks until outstanding DirectX surface updates are drawn and is useful as a test-only synchronization point before reading present statistics `[42]`. Prefer the direct `DXGI_FRAME_STATISTICS` structure and `IDXGISwapChain::GetFrameStatistics` for present counting `[43][24b]`. Note that Microsoft discourages `DwmGetCompositionTimingInfo` in favour of the DXGI flip-model present statistics and that the queued-present model is being deprecated `[44]`; use DWM timing only to read the refresh rate.

---

## 2. Hotkey to fully drawn popup (< 50 ms, ideally one frame)

**Boundary.** Start at receipt of the hotkey message (`WM_HOTKEY`) in the popup window's message loop; end when the frame containing the *full* search-row content is presented and confirmed displayed. This matches the spec's requirement that the first frame after the hotkey already contains the full content `[AGENTS.md §4.4]`.

**Method.**
1. Stamp `t0` with QPC at the top of the `WM_HOTKEY` handler, before any show/focus work `[1]`. `WM_HOTKEY` is posted to the message queue of the thread that called `RegisterHotKey` and is picked up in the message loop, so this is the earliest in-app boundary available `[58][59]`.
2. Stamp `t1` immediately after the `IDXGISwapChain::Present` (or `Present1`) call that draws the complete first frame. `Present` is the API that "presents a rendered image to the user" and its `SyncInterval` parameter controls vertical-blank synchronization `[23]`.
3. For flip-model swap chains, call `IDXGISwapChain::GetFrameStatistics` and log `DXGI_FRAME_STATISTICS` (`PresentRefreshCount`, `SyncRefreshCount`, `SyncQPCTime`) with the event. Flip-model swap chains provide present statistics in both windowed and full-screen modes `[24]`.
4. Optionally call `DwmGetCompositionTimingInfo` before and after the show to capture composition/display counters. On Windows 8.1 and later the `hwnd` parameter must be `NULL` or the call returns `E_INVALIDARG` `[25]`. `DWM_TIMING_INFO` exposes `rateRefresh`, `qpcRefreshPeriod`, `qpcFrameDisplayed`, `cRefreshFrameDisplayed`, and dropped/missed frame counts `[26]`. For variable-refresh displays, the compositor clock API's `DCompositionGetStatistics` / `DCompositionGetTargetStatistics` provide per-target frame statistics and frame IDs (created/confirmed/completed) `[27]`.
5. Emit `t0`, `t1`, and the frame statistics as one TraceLogging event pair; in WPA, the Generic Events table gives the elapsed QPC ticks, converted with the cached frequency `[2][5][6]`.

**Repeatable procedure.** Rebuild in release, launch tapir, start an elevated `wpr.exe -start GeneralProfile` (plus a custom `.wprp` that enables the tapir TraceLogging provider) `[5][10]`, toggle the hotkey 100 times with the popup fully hidden between toggles, then `wpr.exe -stop hotkey.etl` and open the ETL in WPA with symbols loaded `[9]`. Compute the delta from the event stamps and report median/p95; use the CPU-sampling graph to locate any unexpected work in the handler `[8]`.

**Caveats.** The first `GetFrameStatistics` call returns `DXGI_ERROR_FRAME_STATISTICS_DISJOINT` to mark the beginning of a sequence, and present statistics are "not reliable in many multiple monitor scenarios" (tapir is multi-monitor) `[24]`; `DwmFlush` may be required before frame statistics update on Hardware Flip Queue systems `[24]`. Some `DWM_TIMING_INFO` counters (e.g. `cFramesDisplayed`, `cFramesDropped`) are valid only after a second call `[26]`. Vsync quantizes display time to the refresh period read from `DWM_TIMING_INFO.rateRefresh` `[26]`, so report CPU-to-Present latency and Present-to-display latency separately. Do not measure from OS input arrival unless you explicitly label that as a second, larger number; do not use a frame timer to delay the first frame.

---

## 3. Keystroke to updated results (< 16 ms)

**Boundary.** Start when the character message reaches the window procedure; end when the frame showing the newly filtered and laid-out result list has been presented.

**Method.**
1. Stamp QPC on receipt of `WM_CHAR` (or the equivalent input message) in `WndProc` and emit a TraceLogging `InputReceived` event with the query length `[1][5]`.
2. Stamp after the filter/score/sort/layout phase completes and emit `ResultsUpdated`; this isolates the pure fuzzy work from presentation, which is useful because the spec requires no debounce and an update on every keystroke `[AGENTS.md §1, §6]`.
3. Stamp after the corresponding `Present` call and emit `ResultsPresented` `[23]`.
4. Cross-check with `GetFrameStatistics` / `DWM_TIMING_INFO` for the present-to-display component as in section 2 `[24][25][26]`.
5. For a deeper breakdown of scoring and layout CPU time, take a **PIX Timing Capture** with CPU sampling (and GPU timings if desired); PIX Timing Capture options include CPU Samples, Context Switch Callstacks, GPU timings and video frames, and the Metrics layout graphs event duration, execution and stalled time `[28]`.

**Repeatable procedure.** Under the same WPR/TraceLogging session as section 2, type a representative query (e.g. `vsc`) one character at a time with a fixed inter-key gap, emit one event triple per keystroke, and compute per-keystroke deltas in WPA `[5][6][9]`. Report the worst-case and p95 input→Present time and the separate present→display time.

**Caveats.** 16 ms is roughly one frame at 60 Hz, so a CPU result that completes in 8 ms can still be displayed one refresh later — never conflate the two boundaries `[26]`. Measure in a release build with a realistic index size (a few thousand entries) because the hot path is allocation-sensitive `[AGENTS.md §6]`. WPR/PIX collection adds overhead; use in-app QPC stamps for the headline number and the trace for attribution `[8][28]`. Launch under a warm index, not while the background index is still swapping in.

---

## 4. Idle CPU (≈ 0%)

**Definition of idle.** Popup hidden, no animation active, no input for a sustained window (e.g. 30 s). The budget specifically requires that no frame loop runs when nothing changes `[AGENTS.md §9]`.

**Method (two independent readings).**
1. **In-app:** call `GetProcessTimes` at the start and end of the idle window. It returns summed kernel-mode and user-mode time across all threads of the process as `FILETIME` values in 100-ns units `[11]`. CPU fraction = `(Δkernel + Δuser) / (Δwall × processor count)`. Note that the user-mode value can exceed elapsed real time when the process runs across multiple cores `[11]`.
2. **OS counters:** sample `\Process(tapir)\% Processor Time` with `typeperf -si 1 -sc 30 -o idle-cpu.csv -f CSV`, or watch PerfMon/Task Manager. `typeperf` writes performance data to the console or a log file and supports `-si` (sample interval), `-sc` (sample count) and `-o` (output) `[14]`. Microsoft lists PerfMon, TypePerf, Relog and PDH as the counter-consuming tools `[15]`. On Windows 11, prefer the `Process V2` counterset: the legacy `Process` counterset uses non-unique instance names based on the EXE filename, which can produce data glitches, whereas `Process V2` includes the process ID in the instance name `[13]`.
3. **Confirmation:** capture a short WPR CPU-sampling trace during the same idle window and verify in WPA that there are no tapir stacks and no periodic timer wake-ups `[8][9]`. This is the direct test of "no frame loop at idle".

**Caveats.** Performance counters are limited to roughly 1 Hz collection and are unsuitable for profiling `[12]`, so use `GetProcessTimes` for the primary figure. `GetProcessTimes` excludes system-wide interrupt/DPC time, which belongs to the system rather than to tapir `[11]`. Short WPR captures themselves add CPU cost `[8]`. Verify the result with animations both enabled and disabled (`SPI_GETCLIENTAREAANIMATION` reports the user's client-area animation setting) `[30]`; a leftover frame timer will show up in the WPA CPU graph even if the CPU percentage looks near zero.

---

## 5. Idle working set (low tens of MB)

**Method.**
1. In-app, call `GetProcessMemoryInfo` with `PROCESS_MEMORY_COUNTERS_EX`, which contains `WorkingSetSize` (current working set), `PeakWorkingSetSize`, `PrivateUsage` (commit charge) and page-pool fields `[16][17]`. Microsoft's "Memory Performance Information" table maps the `\Process(...)\Working Set` counter to `WorkingSetSize`, `Private Bytes` to `PrivateUsage`, and notes that `Working Set - Private` (Task Manager's "Private Working Set") has no direct PSAPI structure and must be read from the performance counter `[18]`.
2. Sample for 30 s after hiding the popup: log `GetProcessMemoryInfo` once per second in-app, and in parallel collect `\Process(tapir)\Working Set`, `\Process(tapir)\Working Set - Private` and `\Process(tapir)\Private Bytes` with `typeperf` `[14][18]`. Report the steady-state working set and private bytes; Task Manager/Resource Monitor/PerfMon are also first-party consumers of the same data `[12][18]`.

**Caveats.** The working set is the set of pages currently resident in physical memory and includes shared code pages (e.g. system DLLs), whereas `PrivateUsage`/`Private Bytes` is the process's commit charge — they are different metrics and both should be reported `[17][19]`. Do not call `EmptyWorkingSet` as part of the measurement, because it would perturb the number being measured; Microsoft documents it as a way to remove pages from a working set `[19]`. Measure after at least one show/hide cycle (cold startup pages are not the steady state) and record whether the device is hardware or WARP, since fallback changes memory use. Use the `Process V2` counterset on Windows 11 to avoid instance-name mixups `[13]`.

---

## 6. Hidden state: GPU and cache memory trimmed via `IDXGIDevice3::Trim`

**What the API guarantees.** `IDXGIDevice3::Trim` "trims the graphics memory allocated by the IDXGIDevice3 DXGI device on the app's behalf". Graphics drivers periodically allocate internal buffers to speed up later rendering; those allocations count against the app's memory usage, and `Trim` makes the runtime and driver discard them, reducing memory footprint. Microsoft says an app should call `Trim` as one of the last D3D operations before going idle, should call `ID3D11DeviceContext::ClearState` first (to clear pipeline bindings and release references), and expects a brief performance hit when buffers are reallocated after the call — so it should only be called when going idle for a period of time `[20]`. Obtain `IDXGIDevice3` by `QueryInterface` on the D3D device `[21]`.

**Method.**
1. On hide, call `ID3D11DeviceContext::ClearState()`, release middleware references, then `IDXGIDevice3::Trim()`, and emit a TraceLogging `TrimInvoked` event `[20][5]`.
2. Measure GPU memory with `IDXGIAdapter3::QueryVideoMemoryInfo(NodeIndex, MemorySegmentGroup, &info)` for both local and non-local segment groups; it "informs the process of the current budget and process usage" `[22]`. The returned `DXGI_QUERY_VIDEO_MEMORY_INFO` contains `Budget`, `CurrentUsage`, `AvailableForReservation`, and `CurrentReservation`; `CurrentUsage` is the application's current video memory usage in bytes `[23a]`. Use `NodeIndex = 0` for single-GPU operation `[22]`.
3. Sample `CurrentUsage` immediately before hide, immediately after `Trim`, and at 1 s and 5 s after hide; log all samples via TraceLogging and report the delta `[22][5]`. In parallel, sample `WorkingSetSize` and `PrivateUsage` with `GetProcessMemoryInfo` to capture the CPU-side cache drop `[16][17]`.
4. Optional deeper view: a PIX Timing Capture with the **GPU resources** option tracks D3D object allocation, GPU residency, demoted allocations and allocation migrations; PIX GPU Captures can additionally replay a frame and collect timing data `[29][30a]`.

**Caveats.** Use `ClearState` before `Trim` or the D3D pipeline may keep references alive and prevent the expected release `[20]`. Driver cleanup can be asynchronous, so a short series of samples is more reliable than a single before/after pair `[20]`. The spec's hide path is "instant" with no animation `[AGENTS.md §4.4]`, so do not wait on a frame timer in the hide path; the brief reallocation cost on the next show is expected and should be noted rather than treated as a regression `[20]`. WARP fallback and multi-adapter systems behave differently; record the adapter in use `[22]`.

---

## 7. Animation smoothness (< ~150 ms, frame timer only while active, refresh-rate smooth)

**Method.**
1. Wrap each animation (show ~120 ms, selection pill ~100 ms, compact expand/collapse ~140 ms) with TraceLogging `AnimStart` / `AnimEnd` events carrying QPC ticks and an animation id `[1][5]`. The duration is read directly from WPA; assert it is under ~150 ms `[AGENTS.md §4.4]`.
2. Verify the frame timer runs only while active: count timer callbacks in-app (emit a `TimerTick` event) and confirm in WPA that `TimerTick` events exist only between `AnimStart` and `AnimEnd`, and that CPU samples show no tapir activity at idle `[5][8][9]`.
3. Measure smoothness by sampling `DwmGetCompositionTimingInfo` before and after each animation and diffing `cFramesDropped`, `cFramesMissed`, `cRefreshesDisplayed`, and the QPC display timestamps; pass `NULL` as `hwnd` on Windows 8.1+ `[25][26]`. For variable-refresh displays, use the compositor clock's `DCompositionGetStatistics` / `DCompositionGetTargetStatistics` frame IDs and per-target statistics `[27]`.
4. Optional visual correlate: a PIX Timing Capture with **video capture** of the tapir window puts a Video Frames lane alongside the timing data, so dropped frames can be seen against the animation; the Metrics layout graphs frame-time outliers `[28][30b]`.

**Caveats.** Several `DWM_TIMING_INFO` counters are only valid after a second call (`cFramesDisplayed`, `cFramesAvailable`, `cFramesDropped`) `[26]`; take a baseline call before starting the animation. Animations must be disabled and produce no timeline when `animations = false` in config or when `SPI_GETCLIENTAREAANIMATION` reports client-area animations off, so run that configuration as a control `[30]`. Animations must never delay input — repeat the section 3 keystroke test while an animation is in progress `[AGENTS.md §4.4]`. Vsync/dynamic-refresh can quantize or boost the frame cadence, so compare against `rateRefresh` from `DWM_TIMING_INFO` `[26][27]`.

---

## 8. Release executable size (< ~2 MB)

**Build configuration (in Developer PowerShell for VS).** Use `/O2` (the default optimization for release builds, "creates the fastest code") `[33]`; use `/GL` (whole-program optimization) together with the linker's `/LTCG` — Microsoft notes that `/GL` objects require `/LTCG` to link and that `/ZI` cannot be used with `/GL` `[34]`; use `/MT` for the multithreaded, static CRT (`LIBCMT.lib`) as required by the spec `[35][AGENTS.md §2]`; and put symbols in a separate PDB with compiler `/Zi` and linker `/DEBUG` plus `/PDB:<file>` `[36][37][38]`. Note that `/DEBUG` changes linker `/OPT` defaults to `NOREF`/`NOICF`, so explicitly specify `/OPT:REF` and `/OPT:ICF` after `/DEBUG` if you want the release defaults `[37]`.

**Method.**
1. Produce a clean release build from the pinned commit with the flags above, all in Developer PowerShell for VS (`cl`/`link`/`dumpbin` are among the tools whose environment the developer shell sets) `[31][32]`.
2. Report `tapir.exe` on-disk length in bytes as the budget figure (the PDB is a separate artifact and is not counted). Microsoft states that an EXE or DLL cannot contain debug information — it is always placed in an OBJ or PDB — which is what makes the EXE size meaningful `[37]`.
3. Confirm the binary characteristics with `dumpbin /HEADERS tapir.exe`, which displays the file header and each section header; note that `DUMPBIN` must be run from a developer prompt with the environment configured `[39][40]`. `/SUMMARY` is also available for a quick section summary `[39]`.
4. If the build is near or over budget, open the EXE in **SizeBench**, a first-party Microsoft utility for investigating and reducing the size of compiled native PE binaries, to find the largest contributors `[61]`.
5. Record the figure with the exact compiler/linker flags and commit SHA; report before/after for any change that could affect size `[AGENTS.md §9]`.

**Caveats.** Static CRT (`/MT`) links the runtime into the executable (unlike `/MD`) and increases size, but it is a hard constraint here `[35][62]`. `/O2` favours speed and can produce larger code than `/O1`; do not silently switch to `/O1` to meet the budget `[33]`. `/DEBUG` implies `/INCREMENTAL`, which can affect layout; use a clean link and explicitly add `/OPT:REF,ICF`, which Microsoft recommends for 64-bit builds `[37][62]`. Generating debug symbols does not change the generated code, and the PDB is a separate artifact `[62]`. Only `DUMPBIN /HEADERS` is available on files compiled with `/GL` `[34][40]`, so do not expect `/SYMBOLS` or `/DISASM` to work on whole-program-optimized objects. Measure the linked EXE, not intermediate `.obj` files.

---

## 9. Startup to hotkey ready (< 500 ms)

**Boundary.** Start at process creation (`wWinMain` entry is the earliest instrumentable point in the app) and end when the hotkey can actually be handled: window created, `RegisterHotKey` succeeded, Direct2D/DirectWrite resources pre-created, and the message loop entered. The background index must not gate this point `[AGENTS.md §5, §6, §9]`.

**Method.**
1. Stamp QPC at `wWinMain` entry and again immediately after `RegisterHotKey` returns success (the documented success signal that the hotkey is now registered) and before entering the message loop; emit a TraceLogging `HotkeyReady` event with both ticks and the PID `[1][2][5][58]`.
2. Cross-check with `GetProcessTimes`, whose `lpCreationTime` gives process creation time in `FILETIME` units `[11]`, and with WPR/ETW process/loader events in WPA for cold and warm launches `[6][8][9]`.
3. From a script launched in Developer PowerShell for VS, start tapir, wait for the `HotkeyReady` event in a TraceLogging session, and compute the interval; repeat 10 times warm (and once cold after reboot) and report median/p95 `[5][31]`.

**Caveats.** First-run config creation and a cold file cache add time — label cold and warm separately `[AGENTS.md §8]`. Record the graphics backend: a WARP fallback device changes startup time. A failure of `RegisterHotKey` must be reported, not crash the app, and the readiness event should reflect that path `[AGENTS.md §5]`. "Hotkey ready" is not the same as "first frame drawn" (section 2) nor "index ready" (background thread).

---

## 10. Repeatable measurement session (runbook)

1. **Open Developer PowerShell for VS** (or initialize with `Launch-VsDevShell.ps1` / `Enter-VsDevShell`; alternatively `vcvarsall.bat amd64`) and verify `cl`, `link`, and `dumpbin` resolve `[31][32]`.
2. **Build one clean release** with `/O2 /GL /MT /Zi`, link `/LTCG /DEBUG /PDB:tapir.pdb /OPT:REF /OPT:ICF`, and record the commit SHA and exact flags `[33][34][35][36][37][38]`.
3. **Add the instrumentation described above**: cached `QueryPerformanceFrequency`, QPC stamps at the boundaries in sections 2–9, and TraceLogging events (one provider; fields: event, QPC tick, animation/query id, PID) `[1][2][5]`.
4. **Start capture elevated**: `wpr.exe -start GeneralProfile -start tapir.wprp` (a custom `.wprp` enabling the tapir provider) for the interactive scenarios; WPR can log to file or circular memory buffers, and file mode is recommended for predictable finite events such as startup `[5][10]`. Avoid lost events by using adequate buffers and a simple scenario `[10]`.
5. **Exercise the scenarios**, one at a time, keeping the machine otherwise quiet (AC power, no other GPU-heavy apps, same display/refresh rate, no debugger attached): 100 hotkey toggles (§2); one representative typed query, key by key (§3); 30 s hidden idle with `typeperf -si 1 -sc 30` for CPU and working-set counters running alongside (§4–§5); 5 hide cycles for the GPU-memory series (§6); 3 animations with a control run where animations are off (§7); 10 warm launches plus one cold launch (§9) `[5][14][22][30]`.
6. **Stop and analyze**: `wpr.exe -stop session.etl`, open in WPA, load symbols, then use Generic Events for the TraceLogging stamps, CPU Sampling for idle/attribution, and the graphs for composition data `[5][6][9]`.
7. **Measure size separately**: `dumpbin /HEADERS tapir.exe` plus the file's byte length from the filesystem `[39][40]`.
8. **Report** a table with metric, target, median, p95/worst, sample count, build/commit, graphics backend, and refresh rate. State explicitly that these are measurements produced by the implementer, not by this research document. Re-run before/after any change that risks a budget `[AGENTS.md §9]`.

---

## Sources

| # | Source | What it supports |
|---|---|---|
| [1] | https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter | `QueryPerformanceCounter`: high-resolution (< 1 µs) time stamp for interval measurement |
| [2] | https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency | `QueryPerformanceFrequency`: frequency fixed at boot, query once |
| [3] | https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps | QPC guidance, tick uncertainty, cross-processor consistency |
| [4] | https://learn.microsoft.com/en-us/windows/win32/dxtecharts/game-timing-and-multicore-processors | Prefer QPC over `RDTSC`; variable CPU frequency caveat |
| [5] | https://learn.microsoft.com/en-us/windows/win32/tracelogging/tracelogging-record-and-display-tracelogging-events | TraceLogging capture with WPR, decode with WPA, Generic Events |
| [6] | https://learn.microsoft.com/en-us/windows-hardware/test/weg/instrumenting-your-code-with-etw | ETW controller/consumer model; WPR recommended controller, WPA consumer; instrumentation planning |
| [7] | https://learn.microsoft.com/en-us/windows-hardware/test/wpt | Windows Performance Toolkit components: WPR and WPA; system requirements; Xperf supported, Xperfview not |
| [8] | https://learn.microsoft.com/en-us/windows-hardware/test/wpt/introduction-to-wpr | WPR = ETW recorder; built-in profiles; file vs memory logging |
| [9] | https://learn.microsoft.com/en-us/windows-hardware/test/wpt/windows-performance-analyzer | WPA graphs/tables from ETL; symbols; Issues window |
| [10] | https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options | WPR CLI options including profiles, `-pmcsources`, `-setprofint` |
| [11] | https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes | `GetProcessTimes`: summed kernel/user time, 100-ns units, multicore caveat |
| [12] | https://learn.microsoft.com/en-us/windows/win32/perfctrs/about-performance-counters | Performance counters limited to ~1 Hz; not for profiling; Task Manager/PerfMon/typeperf/PDH |
| [13] | https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data | `Process` instance-name glitches; Windows 11 `Process V2` includes PID |
| [14] | https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/typeperf | `typeperf` syntax and `-si`, `-sc`, `-o`, `-f` options |
| [15] | https://learn.microsoft.com/en-us/windows/win32/perfctrs/performance-counters-tools | PerfMon, TypePerf, Relog, PDH tool list |
| [16] | https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo | `GetProcessMemoryInfo` and `PROCESS_MEMORY_COUNTERS_EX` input |
| [17] | https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex | `WorkingSetSize`, `PeakWorkingSetSize`, `PrivateUsage` field definitions |
| [18] | https://learn.microsoft.com/en-us/windows/win32/memory/memory-performance-information | Map of process counters to PSAPI fields and Task Manager columns; `Working Set - Private` |
| [19] | https://learn.microsoft.com/en-us/windows/win32/procthread/process-working-set | Working set definition; `GetProcessMemoryInfo`; `EmptyWorkingSet` |
| [20] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim | `IDXGIDevice3::Trim`: driver buffers, `ClearState` first, call only when idle, reallocation hit |
| [21] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nn-dxgi1_3-idxgidevice3 | Obtain `IDXGIDevice3` via `QueryInterface` on the D3D device |
| [22] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo | `QueryVideoMemoryInfo`: node index, segment group, budget/usage |
| [23a] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info | `DXGI_QUERY_VIDEO_MEMORY_INFO` fields including `CurrentUsage` |
| [23] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present | `IDXGISwapChain::Present` and sync interval semantics |
| [24] | https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model | Flip-model present statistics, disjoint first call, multi-monitor caveat, `DwmFlush` |
| [24b] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics | `GetFrameStatistics` requirements and flip-model availability |
| [25] | https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmgetcompositiontiminginfo | `DwmGetCompositionTimingInfo`; `hwnd` must be NULL on Windows 8.1+ |
| [26] | https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ns-dwmapi-dwm_timing_info | `DWM_TIMING_INFO`: refresh rate, frame display times, dropped/missed frames, second-call caveat |
| [27] | https://learn.microsoft.com/en-us/windows/win32/directcomp/compositor-clock/compositor-clock | Compositor clock, dynamic refresh rate, `DCompositionGetStatistics`, per-target frame stats |
| [28] | https://learn.microsoft.com/en-us/windows/pix/timing-captures/timing-captures-overview | PIX Timing Capture options: CPU samples, context-switch callstacks, GPU timings, video |
| [29] | https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/timing-captures/pix-timing-captures | PIX Timing Captures, GPU resources/residency, Metrics layout |
| [30] | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow | `SPI_GETCLIENTAREAANIMATION` / `SPI_SETCLIENTAREAANIMATION` |
| [30a] | https://learn.microsoft.com/en-us/windows/pix/gpu-captures/gpu-captures-overview | PIX GPU Captures: single-frame replay, timing and resource analysis |
| [30b] | https://learn.microsoft.com/en-us/windows/pix/timing-captures/capture-video-frames | PIX video-frame capture and Video Frames lane |
| [31] | https://learn.microsoft.com/en-us/visualstudio/ide/reference/command-prompt-powershell | Developer Command Prompt / Developer PowerShell, `Launch-VsDevShell.ps1`, tools including `DUMPBIN` |
| [32] | https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170 | MSVC toolset from the command line; `vcvarsall.bat` syntax |
| [33] | https://learn.microsoft.com/en-us/cpp/build/reference/o1-o2-minimize-size-maximize-speed?view=msvc-170 | `/O1`, `/O2`; `/O2` is the release default |
| [34] | https://learn.microsoft.com/en-us/cpp/build/reference/gl-whole-program-optimization?view=msvc-170 | `/GL` requires `/LTCG`; `/ZI` incompatible; limited DUMPBIN support |
| [35] | https://learn.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170 | `/MT` = static multithreaded CRT (`LIBCMT.lib`) |
| [36] | https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170 | `/Zi` creates a separate PDB; smaller executable |
| [37] | https://learn.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info?view=msvc-170 | `/DEBUG`, `/OPT` default changes, `/INCREMENTAL`, debug info always in OBJ/PDB |
| [38] | https://learn.microsoft.com/en-us/cpp/build/reference/pdb-use-program-database?view=msvc-170 | `/PDB:<filename>` names the linker's PDB |
| [39] | https://learn.microsoft.com/en-us/cpp/build/reference/dumpbin-reference?view=msvc-170 | DUMPBIN purpose; run from a Visual Studio developer prompt |
| [40] | https://learn.microsoft.com/en-us/cpp/build/reference/headers?view=msvc-170 | `DUMPBIN /HEADERS`; only `/HEADERS` works on `/GL` objects |
| [41] | https://learn.microsoft.com/en-us/cpp/build/reference/linker-options?view=msvc-170 | MSVC linker options list including `/OPT`, `/PDB`, `/PDBSTRIPPED` |
| [42] | https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush | `DwmFlush` blocks until outstanding DirectX updates are drawn |
| [43] | https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics | `DXGI_FRAME_STATISTICS`: `PresentRefreshCount`, `SyncQPCTime` |
| [44] | https://learn.microsoft.com/en-us/windows/compatibility/queued-present-model-is-being-deprecated | Microsoft discourages `DwmGetCompositionTimingInfo`; flip model preferred |
| [48] | https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpa-step-by-step-guide | WPA workflow: open ETL, select intervals, filter events |
| [49] | https://learn.microsoft.com/en-us/windows/apps/develop/performance/choose-between-tools | First-party choice between Visual Studio profiler and WPT |
| [50] | https://learn.microsoft.com/en-us/previous-versions/dd367892(v=technet.10)?redirectedfrom=MSDN | PowerShell `Get-Counter` counter reader |
| [51] | https://learn.microsoft.com/en-us/windows/win32/perfctrs/creating-a-query | PDH `PdhOpenQuery` / `PdhAddCounter` / `PdhAddEnglishCounter` |
| [52] | https://learn.microsoft.com/en-us/windows/win32/perfctrs/specifying-a-counter-path | Counter path syntax and wildcards |
| [53] | https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddcounterw | `PdhAddCounterW` adds a counter to a query |
| [54] | https://learn.microsoft.com/en-us/sysinternals/downloads/process-explorer | Process Explorer: live CPU cross-check |
| [55] | https://learn.microsoft.com/en-us/sysinternals/downloads/vmmap | VMMap: per-type committed and working-set breakdown |
| [56] | https://learn.microsoft.com/en-us/sysinternals/downloads/rammap | RAMMap: system-wide physical memory context |
| [57] | https://learn.microsoft.com/en-us/sysinternals/ | Sysinternals hosted by Microsoft; live.sysinternals.com share |
| [58] | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey | `RegisterHotKey` and its success signal |
| [59] | https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey | `WM_HOTKEY` posted to the registering thread's queue |
| [60] | https://learn.microsoft.com/en-us/windows/pix/overview | PIX CPU profiling for any Windows app, GPU for Direct3D apps |
| [61] | https://learn.microsoft.com/en-us/windows/apps/develop/performance/ | SizeBench: first-party native PE binary size investigation tool |
| [62] | https://learn.microsoft.com/en-us/cpp/build/optimization-best-practices?view=msvc-170 | `/OPT:REF,ICF` recommendation; symbols do not affect generated code |

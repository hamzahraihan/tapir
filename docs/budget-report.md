# Budget measurements

Measured on 2026-11-24 21:06:48 with the external harness at `tools/measure.ps1`. No instrumentation was added to tapir, and the harness makes no network calls.

Machine: Intel(R) Core(TM) i7-7500U CPU @ 2.70GHz; 4 logical processors; Microsoft Windows 11 Pro build 26200; processor load at start 77.09%.

## Release build evidence

`CMakeLists.txt` sets `/MT`, `/O2`, `/GL`, `/LTCG`, `/OPT:REF`, `/OPT:ICF`, `/DEBUG`, and `/Zi` for Release. The linker writes the PDB beside the executable. Adding `/Zi` changed `tapir.pdb` from 6,762,496 bytes to 10,579,968 bytes; `tapir.exe` stayed 528,896 bytes.

The harness writes a fresh config and history fixture under `%TEMP%`, saves and restores the HKCU `Run` value named `tapir`, and deletes the fixture on exit.

Verdict: hotkey visibility, idle CPU, show animation, startup, and binary size meet their targets. Keystroke latency is inconclusive: the resize probe is above 16 ms and its limiter is not isolated, while the screen-pixel probe is too slow to bound the app-only cost. Working set is about 51 MiB, slightly outside "low tens of MB". Compact animation is at the 150 ms boundary and exceeds it on some runs. GPU counters do not expose the driver caches that Trim releases, so the hidden-state trim budget is not verified by this harness.

## Results

| Budget | Result | Target | Notes |
|---|---|---|---|
| Hotkey to visible popup | median 21.95 ms, range 1.46-75.59 ms, n=5 | < 50 ms | WM_HOTKEY posted to the popup; measured to `IsWindowVisible`, not to photon. The first full frame is drawn before `ShowWindow`. |
| Keystroke to result-list resize | median 44.18 ms, range 11.73-48.64 ms, n=5 | <= 16 ms | Inconclusive. Compact mode, animations off. Measured to the synchronous `SetWindowPos` after ranking and drawing. Limiter not isolated; the external polling loop and DWM scheduling are in the path. |
| Keystroke to screen-pixel change | median 33.30 ms, range 31.96-48.70 ms, n=5 | < 16 ms | Upper bound only. Each `CopyFromScreen` plus DWM present costs several milliseconds in the external probe. |
| Idle CPU, hidden | median 0.000% of machine | about 0% | 0, 0, 0 |
| Idle CPU, visible | median 0.000% of machine | about 0% | 0, 0, 0 |
| Working set, visible / hidden | 53.65 / 52.64 MiB | low tens of MB (missed) | Hidden sample 500 ms after the trim path. |
| GPU memory, visible / hidden | local 9.75 / 8.74 MiB, dedicated 0.00 / 0.00 MiB | trimmed on hide (unverified) | `GPU Process Memory` counters filtered by PID. No drop was observed; `IDXGIDevice3::Trim` releases driver caches, which these counters do not expose. |
| Show animation | median 94.07 ms, range 66.37-111.64 ms, n=5 | < 150 ms | Window-top polling; first movement to stable. |
| Compact expand animation | median 151.37 ms, range 135.50-165.89 ms, n=5 | < 150 ms | Window-height polling; first resize to stable. The configured duration is 140 ms plus one 16 ms frame, so a run slightly over 150 ms is expected. |
| Startup to hotkey ready | median 396.07 ms, range 293.00-483.70 ms, n=5 | < 500 ms | Process start to first popup visible after posting WM_HOTKEY. Includes process creation and window setup. |
| Executable size | 0.50 MiB | < 2 MiB | `build\Release\tapir.exe`. |

## Not measured

- Icon-cache memory dropped on hide: the cache is retained by design within its size limit; no external counter exposes it.
- Windows 10 solid fallback, high-contrast mode, and mixed-DPI behavior across monitors: require hardware or OS not available here. See the manual checklist in `README.md`.
- Selection-highlight animation: no selection animation timeline exists in the code; the 100 ms row transition in the spec is not implemented.

## Raw runs

- HotkeyToVisibleMs: 75.59, 1.90, 1.46, 21.95, 29.81
- KeystrokeToResizeMs: 11.73, 30.93, 48.64, 44.18, 47.55
- KeystrokeToPixelChangeMs: 48.70, 32.83, 33.37, 31.96, 33.30
- ShowAnimationMs: 94.07, 89.29, 111.64, 66.37, 108.48
- CompactAnimationMs: 146.16, 165.89, 160.54, 135.50, 151.37
- StartupToHotkeyReadyMs: 293.00, 404.69, 483.70, 396.07, 381.12
- IdleCpuPercentOfMachine.Hidden: 0.000, 0.000, 0.000
- IdleCpuPercentOfMachine.VisibleIdle: 0.000, 0.000, 0.000

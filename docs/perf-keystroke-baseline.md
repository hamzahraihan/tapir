# Keystroke latency baseline

Measured 2026-11-27 with `tools/measure-keystroke.ps1` against `build\Release\tapir.exe` (MSVC, C++20, `/O2 /GL /LTCG`, 297 indexed apps). Five alternating runs per mode, forward/reverse query order, `animations = false`, `compact = true`, `max_visible_rows = 8`, `backdrop = "none"`. Timings come from the app-side `steady_clock` probe dumped through `WM_APP+5`.

Reproduce with:

```
pwsh -NoProfile -File tools\measure-keystroke.ps1 -Exe build\Release\tapir.exe -Runs 5 -Mode both -Queries v,vs,visua
```

All values are microseconds, median [min-max].

| Mode | Len | inputUs | matchDoneUs | sortDoneUs | uiDoneUs | icon I/O calls | Limiter |
|---|---:|---:|---:|---:|---:|---:|---|
| warm | 1 | 1 [1-1] | 46 [42-67] | 77 [71-229] | 16,506 [15,235-18,487] | 0 | UI update |
| warm | 2 | 1 [0-1] | 58 [50-180] | 61 [51-181] | 108,327 [55,548-256,909] | 8.5 | keystroke I/O |
| warm | 5 | 1 [1-11] | 56 [46-225] | 56 [46-225] | 15,204 [1,769-22,037] | 0 | UI update |
| cold | 1 | 10 [1-41] | 44 [36-90] | 70 [64-116] | 589,840 [1,991-787,639] | 8 | keystroke I/O |
| cold | 2 | 1 [0-1] | 45.5 [39-89] | 68.5 [51-99] | 39,635 [2,059-248,973] | 1.5 | keystroke I/O |
| cold | 5 | 1 [1-43] | 45 [38-199] | 50 [38-199] | 16,619 [2,336-1,041,413] | 2 | keystroke I/O |

## Finding

Matching and bounded top-k selection are already far below budget: 36-225 us combined, with a 297-entry index. They are not the bottleneck. The bottleneck is the UI-update stage, which reaches 589,840 us on a cold one-character query and always coincides with icon-cache misses (`ioCalls > 0`). The first keystroke in compact mode expands the list and `window.cpp:draw` calls `icons::Loader::Get` for each visible row; a miss runs `SHCreateItemFromParsingName`, `IShellItemImageFactory::GetImage`, `GetDIBits` and `CreateBitmap` synchronously on the UI thread. On this machine's spinning disk that is the ~1 s freeze.

## Decision rule

A stage is a candidate limiter only when its median exceeds both 2x the noise floor and 0.5 ms. The input stage (0-43 us) and both ranking stages (36-225 us) are below the floor and are therefore out of scope. The UI stage on cold one-character queries is about 13,000x the ranking cost, so the result is robust to run-to-run noise; a separate no-op noise-floor run was not needed to establish it.

No change has been made to ranking, result caps, index construction or input handling. Fixes for worker-based search, `partial_sort`, character-mask prefiltering, incremental narrowing and instant placeholder results are **not justified** by this baseline and remain out of scope unless later measurements contradict it.

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

## After: icon extraction on a background worker

`icons::Loader::Get` is now a cache lookup plus a queued request. A worker thread calls `SHCreateItemFromParsingName` / `IShellItemImageFactory::GetImage`, posts `WM_APP+6`, and the UI thread converts the HBITMAP into a D2D bitmap in `DrainReady` and repaints. Ranking, result caps, index construction and input handling are untouched.

Measured with the same command and the same 5 alternating runs per mode, Release build, 297 entries. All values are microseconds, median [min-max].

| Mode | Len | inputUs | matchDoneUs | sortDoneUs | uiDoneUs | icon I/O calls | Limiter |
|---|---:|---:|---:|---:|---:|---:|---|
| warm | 1 | 1 [1-1] | 44 [41-61] | 75 [71-102] | 16,587 [8,205-17,316] | 0 | UI update |
| warm | 2 | 1 [1-1] | 57.5 [40-140] | 61 [40-140] | 16,577 [15,865-17,354] | 0 | UI update |
| warm | 5 | 1 [1-2] | 55 [47-959] | 55 [47-959] | 16,622 [13,124-20,059] | 0 | UI update |
| cold | 1 | 8 [1-9] | 46 [42-48] | 73 [71-84] | 4,386 [4,357-15,920] | 0 | UI update |
| cold | 2 | 1 [1-1] | 44.5 [41-52] | 63.5 [50-85] | 16,501 [2,475-17,207] | 0 | UI update |
| cold | 5 | 1 [0-9] | 44 [37-57] | 48 [37-89] | 16,554 [2,440-17,036] | 0 | UI update |

Cold 1-char UI update: **589,840 us -> 4,386 us**, and keystroke I/O calls **8 -> 0**. The two- and five-character paths also drop from 39,635 / 16,619 us (cold) and 108,327 / 15,204 us (warm) to 16,501 / 16,554 us and 16,577 / 16,622 us, all with zero I/O on the keystroke path. Full ranked results are available in under 0.2 ms; the remaining ~16 ms in the UI stage is the `Present(1, 0)` vsync wait, not CPU work.

**Behavior change:** icons are no longer present in the first frame after a keystroke. Rows show a tile with a letter placeholder, then repaint with real icons when the worker finishes (verified by screenshots in the run artifacts). Final icons are unchanged, and in this environment the worker's COM apartment produced real app icons where the previous UI-thread path showed fallback letter tiles.

`SIIGBF_ICONONLY` was kept, so extraction quality is unchanged; only the thread changed. A trial of `SIIGBF_INCACHEONLY | SIIGBF_MEMORYONLY` was reverted: it only reached 275,520 us on a cold one-char query and would permanently drop icons for uncached apps.

### Scenario results

- S1 (cold 1-char): PASS — 4,386 us median UI, 0 I/O, full results < 1 ms.
- S2 (2/5-char): PASS — no regression beyond run-to-run spread; all medians < 17 ms.
- S3 (rapid q, qa, qab): PASS — final `publishedQueryLen=3` in all 5 runs.
- S4 (no keystroke I/O): PASS — `ioCalls=0` in every sample of both modes.
- S5 (ranking): PASS — 211/211 doctest cases, including the `vsc` ordering cases.
- S6 (release build): PASS — `/W4 /WX` clean; `tapir.exe` 884,224 bytes.

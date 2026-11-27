# Cached DirectWrite text layouts for lower keystroke latency

Date: 2026-11-25
Status: design approved by the user; no implementation yet
Repository state: spec is intentionally uncommitted at the user's request

## Problem

`docs/budget-report.md` records a median keystroke-to-result-resize of 44.18 ms (range 11.73–48.64 ms, n=5) against a 16 ms target, and a screen-pixel-change median of 33.30 ms. The report marks the result inconclusive because the external polling loop and DWM scheduling are in the measured path.

`src/render.cpp` creates a new `IDWriteTextLayout` for every text segment on every frame:

- `makeLayout` at line 44 calls `CreateTextLayout`, `SetTrimming` and `SetMaxWidth`.
- `drawSegment` at line 158 creates a layout and calls `GetMetrics` on each call.
- `textWidth` at line 171 does the same for the right-aligned type label.
- `measurePrefix` at line 63 creates an additional layout per measured prefix, used four times in `drawSearchRow` (lines 209, 211, 220, 227).
- `drawHighlightedTitle` (lines 235–257) and `drawResultRow` (lines 259–296) call `drawSegment` several times per visible row.

With eight visible rows this is roughly 60–80 layout creations per keystroke. `src/window.cpp:239` also calls `refreshResults` and builds the whole view on each draw before `gfx::render`.

## Goal and success criteria

- Primary gate: app-side work attributed to a keystroke drops by at least 2x against a freshly measured baseline on this machine.
- Target: app-side keystroke work approaches 16 ms. The external median is reported for continuity but is not the sole gate because it includes the harness polling loop and DWM present scheduling.
- Run-to-run spread must be smaller than the measured improvement; otherwise the result is noise, not a gain.
- Visual output is unchanged: matched-character highlighting, ellipsis trimming, ghost text, caret position, selection and hover fills, and the command bar must look identical before and after.

## Scope

In scope:

- Layout acquisition and measurement in `src/render.cpp`.
- A pure, testable cache key and bounded-cache policy header with unit tests.
- Cache invalidation in `Painter::applySettings` and in `gfx::resize` when DPI changes.
- Temporary timing instrumentation used to isolate the limiter; it is removed before completion unless the user explicitly asks to keep it.

Out of scope: fuzzy ranking, the app index, history, icons, window placement, the message loop, render loops, new dependencies, binary-size and working-set work, and any commit of this spec.

## Design

### Cache

Add a bounded cache to `Painter::Impl` in `src/render.cpp`.

Key fields:

- a `FormatId` enum identifying the existing text format (`title`, `titleStrong`, `subtitle`, `label`, `key`, `text`, `placeholder`, `glyph`, `details`);
- `std::wstring text`;
- `float maxWidthPx`;
- `float heightPx`;
- `bool trim`.

Value fields:

- `ComPtr<IDWriteTextLayout> layout`;
- `float measuredWidth`, already clamped to `maxWidthPx`, so callers do not call `GetMetrics` on a cache hit.

All layout creation and measurement routes through one `Impl` helper. `drawSegment`, `textWidth` and `measurePrefix` call that helper instead of `makeLayout` and `GetMetrics`. Rendering happens only on the UI thread, so the cache needs no lock.

The cache holds at most 512 entries. On overflow it clears all entries rather than maintaining LRU bookkeeping; a full rebuild is rare and bounded.

Invalidation:

- `Painter::applySettings` clears the cache because font settings rebuild the text formats.
- `gfx::resize` clears the cache when the DPI value changes, because pixel widths and heights are part of the key and stale entries would otherwise accumulate.
- Theme-only color changes do not clear the cache: colors come from brushes, not layouts.

### Pure testable policy

Extract only the key type and the clear-on-overflow container policy into the pure header `src/util/text_layout_cache.h`. It stores values by type parameter and has no DirectWrite dependency. Unit tests cover:

- key equality and hashing across different format ids, text, width, height and trim values;
- identical keys hitting the cache after insertion;
- clear-on-overflow at the 512-entry cap;
- explicit clear removing all entries.

The COM layout object and the DirectWrite factory stay in `src/render.cpp`; the generic container does not own or call them. Add the test source to the `tapir_tests` target in `CMakeLists.txt`.

### Error handling

If `CreateTextLayout` fails, insert nothing and return an empty layout, matching current behavior. If `GetMetrics` fails, do not cache the width and return zero, matching current behavior. No new error path is visible to the user.

## Measurement plan

1. Before any source change, capture a baseline with at least 7 runs using the same config and fixture approach as `tools/measure.ps1`. Record median and full range.
2. Add temporary `QueryPerformanceCounter` timing around four stages of a keystroke: `refreshResults` (ranking), view construction, `gfx::render` (draw plus Present), and the synchronous `SetWindowPos` performed by the compact resize path. Use these stage times to name the limiter. Remove the instrumentation before completion unless the user asks to keep it.
3. Rule out noise: compare the run-to-run spread against the expected effect; if the spread is larger, take more runs or change the fixture rather than reporting a gain.
4. After the change, repeat the same procedure and report median, range and the new limiter. State which stage improved and which stage now bounds the result.
5. State what else the number could measure — harness polling, DWM present scheduling, cached work that never ran — and rule each out with the stage timings.

## Verification

- Build Release with `/W4 /WX` and no warnings.
- Run `build\Release\tapir_tests.exe`; all tests must pass.
- Manually compare rendering before and after with the same query and selection: highlighted characters, truncated titles, ghost text, caret position, hover and selection fills, and the command bar.
- Repeat one visual check after a live font change and a DPI change to confirm cache invalidation.
- Record measured numbers and the named limiter in the final report.

## Risks and decisions

- A stale layout after a font reload would render with the old font. The explicit clear in `Painter::applySettings` prevents this.
- A stale or unbounded cache after a DPI change is prevented by the DPI comparison in `gfx::resize` plus the 512-entry cap.
- Copying the key text on each lookup adds small allocations; this is accepted because it replaces far more expensive `CreateTextLayout` calls. LRU bookkeeping was explicitly rejected as unnecessary complexity.
- A single layout per row with colored runs was considered and rejected for this iteration: it has a higher ceiling but requires a larger rewrite of `drawHighlightedTitle` and caret measurement.
- Offscreen row bitmaps were rejected because every keystroke changes ranking, highlighting and ghost text, so nearly all rows would be dirty.

## Handoff

Implementation must not begin until the user has reviewed this file and approved it. The next workflow step after approval is the writing-plans skill, not code.

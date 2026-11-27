# Cached Text Layouts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut app-side keystroke work in tapir by caching DirectWrite text layouts and measurements, preserving visuals exactly.

**Architecture:** All text layout creation and width measurement moves behind one `Painter::Impl` helper backed by a bounded key/value cache. A pure header owns the key type and clear-on-overflow policy so they can be unit tested without DirectWrite. DPI and settings changes clear the cache; theme-only color changes do not. Temporary QPC instrumentation isolates the limiter before and after the change.

**Tech Stack:** C++20, MSVC, CMake/Ninja-or-MSBuild, Direct2D, DirectWrite, WRL `ComPtr`, doctest, PowerShell 7 measurement scripts.

## Global Constraints

- Windows 10 and 11, x64 only; raw Win32, D3D11/DXGI/D2D/DWrite/DirectComposition.
- C++20, MSVC, `/W4 /WX /permissive- /utf-8 /EHsc`, static CRT (`/MT`, `/MTd` debug).
- Only third-party libraries already allowed: `toml++` and `doctest`. No new dependencies.
- No network access, no telemetry, no background render loop, no image assets for UI glyphs.
- All colors come from config; the renderer never hardcodes a color.
- `render` draws only from plain data; no searching, file I/O, or clipboard access.
- Cache cap is exactly 512 entries; overflow clears the whole cache.
- Cache invalidation is explicit in `Painter::applySettings` and on DPI change in `gfx::resize`.
- No git commit unless the user explicitly requests one. Every task's commit step is conditional on that request.
- Spec: `docs/superpowers/specs/2026-11-25-keystroke-latency-design.md`.

---

## File Structure

- `src/util/text_layout_cache.h` — new pure header: `TextFormatId`, `TextLayoutKey`, `TextLayoutKeyHash`, `BoundedTextLayoutCache<Value>`. No DirectWrite dependency.
- `src/util/stopwatch.h` — new temporary pure header: `Stopwatch` using `QueryPerformanceCounter`. Deleted in Task 5.
- `src/render.h` — adds `Painter::clearTextCache()`.
- `src/render.cpp` — owns the cache in `Painter::Impl`; all layout creation/measurement goes through it.
- `src/gfx.cpp` — clears the painter cache when `resize` sees a DPI change.
- `src/window.cpp` — temporary profiling instrumentation only; reverted in Task 5.
- `tests/text_layout_cache.test.cpp` — new doctest tests for the pure cache policy.
- `CMakeLists.txt` — registers the new test file with `tapir_tests`.
- `tools/profile-keystroke.ps1` — new temporary probe; deleted in Task 5.
- `docs/budget-report.md` — final measured numbers replace the inconclusive keystroke rows.

---

### Task 1: Baseline with isolated app-side timing

**Files:**
- Create: `src/util/stopwatch.h`
- Modify: `src/window.cpp:1-30` (includes and anonymous-namespace helpers), `src/window.cpp:239-331` (`draw`), `src/window.cpp:394-409` (`tickCompact`)
- Create: `tools/profile-keystroke.ps1`
- Temporary artifacts to delete in Task 5: all of the above except the final numbers.

**Interfaces:**
- Consumes: existing `app::refreshResults`, `gfx::render`, `reposition`, `diagnostics::append`, `util::config::logPath`.
- Produces: log lines `profile rank <ms>`, `profile view <ms>`, `profile render <ms>`, `profile resize <ms>` in `tapir.log` when `TAPIR_PROFILE` is set; baseline numbers in `%TEMP%\tapir-profile-baseline.txt`.

- [ ] **Step 1: Add the stopwatch header**

```cpp
#pragma once

#include <windows.h>

namespace tapir::util {

class Stopwatch {
public:
    Stopwatch() noexcept { QueryPerformanceCounter(&start_); }

    double elapsedMs() const noexcept {
        static const double frequency = [] {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return static_cast<double>(value.QuadPart);
        }();
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start_.QuadPart) * 1000.0 / frequency;
    }

private:
    LARGE_INTEGER start_;
};

} // namespace tapir::util
```

- [ ] **Step 2: Add the profiling helpers to `src/window.cpp`**

Add `#include "util/stopwatch.h"` after `#include "util/startup.h"`.

Add to the anonymous namespace near the top of `src/window.cpp`:

```cpp
bool profileEnabled() {
    return GetEnvironmentVariableW(L"TAPIR_PROFILE", nullptr, 0) != 0;
}

void logProfile(std::string_view stage, double ms) {
    std::string line = "profile ";
    line.append(stage);
    line += ' ';
    line += std::to_string(ms);
    line += '\n';
    diagnostics::append(util::config::logPath(), line);
}
```

- [ ] **Step 3: Instrument `draw` in `src/window.cpp`**

At the start of `void draw(AppState& app)` (line 239), before `app::refreshResults(app)`:

```cpp
    const bool profile = profileEnabled();
    util::Stopwatch profileClock;
    double lastStage = 0.0;
    const auto stage = [&](std::string_view name) {
        if (!profile) return;
        const double now = profileClock.elapsedMs();
        logProfile(name, now - lastStage);
        lastStage = now;
    };
```

Immediately after `app::refreshResults(app);` add `stage("rank");`.
Immediately before `gfx::render(*app.gfx, view);` add `stage("view");`.
Immediately after `gfx::render(*app.gfx, view);` add `stage("render");`.

- [ ] **Step 4: Instrument the compact resize in `src/window.cpp`**

In `tickCompact` (lines 394-409), wrap the `reposition(app, ...)` call:

```cpp
    util::Stopwatch resizeClock;
    reposition(app, targetHeightDip);
    if (profileEnabled()) logProfile("resize", resizeClock.elapsedMs());
```

Use the exact expression currently passed to `reposition`; do not change the argument.

- [ ] **Step 5: Create `tools/profile-keystroke.ps1`**

```powershell
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\Release\tapir.exe'),
    [int]$Keys = 8
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class ProfileProbe
{
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    const uint WM_HOTKEY = 0x0312;
    const uint WM_CHAR = 0x0102;

    public static IntPtr FindPopup(int pid)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) =>
        {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner != (uint)pid) return true;
            var name = new StringBuilder(64);
            GetClassName(hwnd, name, name.Capacity);
            if (name.ToString() != "tapir") return true;
            found = hwnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static void Toggle(IntPtr hwnd) { PostMessage(hwnd, WM_HOTKEY, new IntPtr(1), IntPtr.Zero); }
    public static void Char(IntPtr hwnd, char value) { PostMessage(hwnd, WM_CHAR, new IntPtr(value), IntPtr.Zero); }
}
'@

$fixture = Join-Path $env:TEMP ('tapir-profile-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
@'
[general]
version = 1
hotkey = "Ctrl+Space"
animations = false
start_with_windows = false

[window]
monitor = "primary"
position = "upper-center"
width = 680
compact = false
max_visible_rows = 8
details_pane = false

[theme]
backdrop = "none"

[privacy]
encrypt_history = false
'@ | Set-Content -LiteralPath (Join-Path $fixture 'config.toml') -Encoding utf8

$process = $null
try {
    $previousDir = $env:TAPIR_CONFIG_DIR
    $previousProfile = $env:TAPIR_PROFILE
    $env:TAPIR_CONFIG_DIR = $fixture
    $env:TAPIR_PROFILE = '1'
    try { $process = Start-Process -FilePath $Exe -PassThru }
    finally {
        if ($null -eq $previousDir) { Remove-Item Env:TAPIR_CONFIG_DIR -ErrorAction SilentlyContinue }
        else { $env:TAPIR_CONFIG_DIR = $previousDir }
        if ($null -eq $previousProfile) { Remove-Item Env:TAPIR_PROFILE -ErrorAction SilentlyContinue }
        else { $env:TAPIR_PROFILE = $previousProfile }
    }

    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 100; $i++) {
        $hwnd = [ProfileProbe]::FindPopup($process.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'popup not created' }

    [void][ProfileProbe]::Toggle($hwnd)
    Start-Sleep -Milliseconds 500
    for ($i = 0; $i -lt $Keys; $i++) {
        [void][ProfileProbe]::Char($hwnd, 'z')
        Start-Sleep -Milliseconds 50
    }
    Start-Sleep -Milliseconds 500

    $log = Join-Path $fixture 'tapir.log'
    $lines = @(Get-Content -LiteralPath $log | Where-Object { $_ -like 'profile *' })
    Write-Output ("captured {0} profile lines" -f $lines.Count)
    $lines | Select-Object -Last 40
    $lines | Set-Content -LiteralPath (Join-Path $env:TEMP 'tapir-profile-baseline.txt') -Encoding utf8
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit(2000) | Out-Null
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue }
}
```

- [ ] **Step 6: Build and capture the baseline**

Run: `cmake --build build --config Release -- /m`
Expected: build succeeds with no warnings.

Run: `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/profile-keystroke.ps1`
Expected: at least 24 lines (rank, view, render, resize per key). Read `%TEMP%\tapir-profile-baseline.txt` and compute median and range for each stage. The largest median stage is the limiter; record it in the working notes for Task 5.

- [ ] **Step 7: Commit (only if the user explicitly asks)**

```bash
git add src/util/stopwatch.h src/window.cpp tools/profile-keystroke.ps1
git commit -m "chore: add temporary keystroke profiling instrumentation"
```

---

### Task 2: Pure cache key and bounded policy with tests

**Files:**
- Create: `src/util/text_layout_cache.h`
- Create: `tests/text_layout_cache.test.cpp`
- Modify: `CMakeLists.txt:100-144` (`tapir_tests` source list)

**Interfaces:**
- Consumes: C++20 standard library only.
- Produces: `tapir::util::TextFormatId`, `TextLayoutKey`, `TextLayoutKeyHash`, `BoundedTextLayoutCache<Value>` with `find`, `insert`, `clear`, `size`, and `kMaxEntries = 512`. Task 3 consumes these exact names.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <doctest.h>
#include "util/text_layout_cache.h"

using tapir::util::BoundedTextLayoutCache;
using tapir::util::TextFormatId;
using tapir::util::TextLayoutKey;

namespace {

TextLayoutKey key(TextFormatId format, std::wstring text, float width,
                  float height, bool trim = true) {
    return TextLayoutKey{format, std::move(text), width, height, trim};
}

} // namespace

TEST_CASE("layout key distinguishes format, text, width, height and trim") {
    const auto base = key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f);
    CHECK(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::titleStrong, L"Visual Studio Code", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio", 400.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 401.0f, 24.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 25.0f));
    CHECK_FALSE(base == key(TextFormatId::title, L"Visual Studio Code", 400.0f, 24.0f, false));
}

TEST_CASE("cache returns the inserted value for an identical key") {
    BoundedTextLayoutCache<int> cache;
    cache.insert(key(TextFormatId::subtitle, L"App", 100.0f, 20.0f), 42);
    const int* found = cache.find(key(TextFormatId::subtitle, L"App", 100.0f, 20.0f));
    REQUIRE(found != nullptr);
    CHECK(*found == 42);
    CHECK(cache.find(key(TextFormatId::label, L"App", 100.0f, 20.0f)) == nullptr);
}

TEST_CASE("cache clears every entry when the 512 entry cap is reached") {
    BoundedTextLayoutCache<int> cache;
    for (std::size_t i = 0; i < BoundedTextLayoutCache<int>::kMaxEntries; ++i) {
        cache.insert(key(TextFormatId::text, std::to_wstring(i), 100.0f, 20.0f),
                     static_cast<int>(i));
    }
    CHECK(cache.size() == 512);
    cache.insert(key(TextFormatId::text, L"overflow", 100.0f, 20.0f), 999);
    CHECK(cache.size() == 1);
    CHECK(cache.find(key(TextFormatId::text, L"0", 100.0f, 20.0f)) == nullptr);
    const int* found = cache.find(key(TextFormatId::text, L"overflow", 100.0f, 20.0f));
    REQUIRE(found != nullptr);
    CHECK(*found == 999);
}

TEST_CASE("explicit clear removes all entries") {
    BoundedTextLayoutCache<int> cache;
    cache.insert(key(TextFormatId::text, L"a", 10.0f, 10.0f), 1);
    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(cache.find(key(TextFormatId::text, L"a", 10.0f, 10.0f)) == nullptr);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: compile failure because `util/text_layout_cache.h` does not exist yet.

- [ ] **Step 3: Write the pure header**

```cpp
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace tapir::util {

enum class TextFormatId : unsigned char {
    text,
    placeholder,
    glyph,
    title,
    titleStrong,
    subtitle,
    label,
    key,
    details,
};

struct TextLayoutKey {
    TextFormatId format;
    std::wstring text;
    float maxWidthPx;
    float heightPx;
    bool trim;

    bool operator==(const TextLayoutKey& other) const noexcept {
        return format == other.format && text == other.text &&
               maxWidthPx == other.maxWidthPx && heightPx == other.heightPx &&
               trim == other.trim;
    }
};

struct TextLayoutKeyHash {
    std::size_t operator()(const TextLayoutKey& key) const noexcept {
        std::size_t hash = std::hash<unsigned char>{}(static_cast<unsigned char>(key.format));
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        };
        for (wchar_t ch : key.text) mix(std::hash<wchar_t>{}(ch));
        mix(std::hash<float>{}(key.maxWidthPx));
        mix(std::hash<float>{}(key.heightPx));
        mix(std::hash<bool>{}(key.trim));
        return hash;
    }
};

template <typename Value>
class BoundedTextLayoutCache {
public:
    static constexpr std::size_t kMaxEntries = 512;

    Value* find(const TextLayoutKey& key) noexcept {
        const auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : &it->second;
    }

    void insert(const TextLayoutKey& key, Value value) {
        if (entries_.size() >= kMaxEntries) entries_.clear();
        entries_.insert_or_assign(key, std::move(value));
    }

    void clear() noexcept { entries_.clear(); }
    std::size_t size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<TextLayoutKey, Value, TextLayoutKeyHash> entries_;
};

} // namespace tapir::util
```

- [ ] **Step 4: Register the test file**

Add `tests/text_layout_cache.test.cpp` to the `add_executable(tapir_tests ...)` list in `CMakeLists.txt` immediately after `tests/anim.test.cpp`. Add `src/util/text_layout_cache.h` to the same list after the test file.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: all tests pass, including the four new cases.

- [ ] **Step 6: Commit (only if the user explicitly asks)**

```bash
git add src/util/text_layout_cache.h tests/text_layout_cache.test.cpp CMakeLists.txt
git commit -m "test: add pure text layout cache key and bounded policy"
```

---

### Task 3: Route all layout creation and measurement through the cache

**Files:**
- Modify: `src/render.h:94-114` (`Painter`)
- Modify: `src/render.cpp:44-61`, `76-99`, `158-178`, `184-233`, `235-296`, `298-430`, `456-463`, `464-520`
- Test: `tests/text_layout_cache.test.cpp` from Task 2

**Interfaces:**
- Consumes: `TextFormatId`, `TextLayoutKey`, `BoundedTextLayoutCache<Value>` from `src/util/text_layout_cache.h`.
- Produces: `Painter::clearTextCache()`; `Impl::layout(TextFormatId, std::wstring_view, float, float, bool)` returning `const CachedLayout*` with fields `IDWriteTextLayout* layout` and `float width`.

- [ ] **Step 1: Add the public invalidation method**

In `src/render.h`, inside `class Painter` public section after `applySettings`:

```cpp
    void clearTextCache() noexcept;
```

- [ ] **Step 2: Add the cache to `Painter::Impl` and define the cached value**

In `src/render.cpp`, add `#include "util/text_layout_cache.h"` after `#include "render.h"`.

Before `struct Painter::Impl` (line 76) add:

```cpp
struct CachedLayout {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    float width = 0.0f;
};
```

Inside `struct Painter::Impl`, after the format members (line 98), add:

```cpp
    tapir::util::BoundedTextLayoutCache<CachedLayout> layoutCache;
```

- [ ] **Step 3: Replace the free `makeLayout` with an `Impl` helper**

Delete the free `makeLayout` function at lines 44-61. In the anonymous namespace after `using Impl = Painter::Impl;` add:

```cpp
IDWriteTextFormat* formatFor(Impl& p, tapir::util::TextFormatId id) noexcept {
    switch (id) {
        case tapir::util::TextFormatId::text: return p.textFormat.Get();
        case tapir::util::TextFormatId::placeholder: return p.placeholderFormat.Get();
        case tapir::util::TextFormatId::glyph: return p.glyphFormat.Get();
        case tapir::util::TextFormatId::title: return p.titleFormat.Get();
        case tapir::util::TextFormatId::titleStrong: return p.titleStrongFormat.Get();
        case tapir::util::TextFormatId::subtitle: return p.subtitleFormat.Get();
        case tapir::util::TextFormatId::label: return p.labelFormat.Get();
        case tapir::util::TextFormatId::key: return p.keyFormat.Get();
        case tapir::util::TextFormatId::details: return p.detailsFormat.Get();
    }
    return nullptr;
}

const CachedLayout* layoutFor(Impl& p, tapir::util::TextFormatId id,
                              std::wstring_view text, float maxWidth, float height,
                              bool trim) {
    if (text.empty() || maxWidth <= 0.0f || height <= 0.0f) return nullptr;
    const tapir::util::TextLayoutKey key{id, std::wstring(text), maxWidth, height, trim};
    if (const CachedLayout* cached = p.layoutCache.find(key)) return cached;

    IDWriteTextFormat* format = formatFor(p, id);
    if (format == nullptr) return nullptr;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(p.write->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                         format, maxWidth, height, &layout))) {
        return nullptr;
    }
    if (trim) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        if (FAILED(layout->SetTrimming(&trimming, nullptr))) return nullptr;
    }
    if (FAILED(layout->SetMaxWidth(maxWidth))) return nullptr;

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return nullptr;

    CachedLayout entry;
    entry.layout = std::move(layout);
    entry.width = std::min(metrics.widthIncludingTrailingWhitespace, maxWidth);
    p.layoutCache.insert(key, std::move(entry));
    return p.layoutCache.find(key);
}
```

`layoutFor` performs exactly one `CreateTextLayout`, one `GetMetrics`, and one insertion on a miss; all later calls are map lookups.

- [ ] **Step 4: Convert the measurement helpers**

Replace `measurePrefix` (lines 63-72) with:

```cpp
float measurePrefix(Impl& p, tapir::util::TextFormatId id,
                    std::wstring_view text, float maxWidth, float height,
                    std::size_t upto) {
    const std::size_t n = upto < text.size() ? upto : text.size();
    const CachedLayout* layout = layoutFor(p, id, text.substr(0, n), maxWidth, height, true);
    return layout == nullptr ? 0.0f : layout->width;
}
```

Replace `drawSegment` (lines 158-169) with:

```cpp
float drawSegment(Impl& p, tapir::util::TextFormatId id, ID2D1Brush* brush,
                  std::wstring_view text, float x, float y, float maxW, float h,
                  bool trim = true) {
    const CachedLayout* cached = layoutFor(p, id, text, maxW, h, trim);
    if (cached == nullptr || cached->layout == nullptr) return 0.0f;
    p.target->DrawTextLayout(D2D1::Point2F(x, y), cached->layout.Get(), brush,
                             D2D1_DRAW_TEXT_OPTIONS_NONE);
    return cached->width;
}
```

Replace `textWidth` (lines 171-178) with:

```cpp
float textWidth(Impl& p, tapir::util::TextFormatId id, std::wstring_view text,
                float maxW, float h) {
    const CachedLayout* cached = layoutFor(p, id, text, maxW, h, true);
    return cached == nullptr ? 0.0f : cached->width;
}
```

- [ ] **Step 5: Update every call site to pass a format id**

Replace each `p.<name>Format.Get()` format argument of `drawSegment`, `textWidth` and `measurePrefix` with the matching enum. The helper's first argument remains `p`:

| Existing format argument | Replacement |
|---|---|
| `p.glyphFormat.Get()` | `tapir::util::TextFormatId::glyph` |
| `p.placeholderFormat.Get()` | `tapir::util::TextFormatId::placeholder` |
| `p.textFormat.Get()` | `tapir::util::TextFormatId::text` |
| `p.titleFormat.Get()` | `tapir::util::TextFormatId::title` |
| `p.titleStrongFormat.Get()` | `tapir::util::TextFormatId::titleStrong` |
| `p.subtitleFormat.Get()` | `tapir::util::TextFormatId::subtitle` |
| `p.labelFormat.Get()` | `tapir::util::TextFormatId::label` |
| `p.keyFormat.Get()` | `tapir::util::TextFormatId::key` |
| `p.detailsFormat.Get()` | `tapir::util::TextFormatId::details` |

These occur in `drawSearchRow` (194, 199, 202, 209, 211, 220, 222, 227), `drawHighlightedTitle` (241, 248, 251, 255), `drawResultRow` (279, 286 via `drawHighlightedTitle`, 287, 290, 292), `keyCap` (303), `drawDetailsPane` (338, 363, 377, 390), `drawCommandBar` (402, 410, 418), and `Painter::draw` (500, 505). Pass `p` as the first argument where the old call passed `p.write.Get()` or `p.write.Get(), p.<name>Format.Get()`. Do not change any text, coordinate, width or height argument.

- [ ] **Step 6: Add invalidation**

In `Painter::applySettings` (line 456), after `impl_->theme = settings.theme;`, add:

```cpp
    impl_->layoutCache.clear();
```

After `Painter::draw`'s closing brace, add:

```cpp
void Painter::clearTextCache() noexcept {
    if (impl_) impl_->layoutCache.clear();
}
```

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build build --config Release -- /m && build\Release\tapir_tests.exe`
Expected: no compiler warnings or errors; all tests pass.

- [ ] **Step 8: Commit (only if the user explicitly asks)**

```bash
git add src/render.h src/render.cpp
git commit -m "perf: cache DirectWrite text layouts between frames"
```

---

### Task 4: Clear the cache on DPI change

**Files:**
- Modify: `src/gfx.cpp:140-164`

**Interfaces:**
- Consumes: `Painter::clearTextCache()` from Task 3.
- Produces: cache invalidation when `resize` observes a different DPI.

- [ ] **Step 1: Capture the old DPI before assignment**

In `gfx::resize`, insert as the first line after the width/height guard:

```cpp
    const unsigned previousDpi = context.dpi;
```

- [ ] **Step 2: Clear the cache when DPI changed**

Immediately after `context.context->SetTarget(context.targetBitmap.Get());` add:

```cpp
    if (previousDpi != dpi && context.painter != nullptr) {
        context.painter->clearTextCache();
    }
```

- [ ] **Step 3: Build and run the tests**

Run: `cmake --build build --config Release -- /m && build\Release\tapir_tests.exe`
Expected: no warnings; all tests pass.

- [ ] **Step 4: Commit (only if the user explicitly asks)**

```bash
git add src/gfx.cpp
git commit -m "perf: invalidate cached text layouts on DPI change"
```

---

### Task 5: Measure, remove instrumentation, and report

**Files:**
- Modify: `src/window.cpp` (revert Task 1 edits), `docs/budget-report.md`
- Delete: `src/util/stopwatch.h`, `tools/profile-keystroke.ps1`, `%TEMP%\tapir-profile-baseline.txt`
- Test: `tools/measure.ps1`

**Interfaces:**
- Consumes: baseline lines in `%TEMP%\tapir-profile-baseline.txt` and the instrumented build from Task 4.
- Produces: post-change stage lines, final numbers in `docs/budget-report.md`, and a clean source tree with no profiling code.

- [ ] **Step 1: Capture the post-change profile**

Run: `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/profile-keystroke.ps1`
Expected: at least 24 profile lines. Compute median and range for rank, view, render and resize. Compare each against the baseline stage medians and name the new limiter.

- [ ] **Step 2: Check the decision criteria**

For each stage, verify the improvement exceeds the run-to-run range; otherwise repeat with `$Keys = 16` before concluding. State in the working notes which stage improved and which stage now bounds the result.

- [ ] **Step 3: Remove the temporary instrumentation**

Delete `src/util/stopwatch.h` and `tools/profile-keystroke.ps1`. Remove from `src/window.cpp`: the `#include "util/stopwatch.h"` line, the `profileEnabled` and `logProfile` helpers, the `profile`/`profileClock`/`lastStage`/`stage` block and the three `stage(...)` calls in `draw`, and the `resizeClock`/`logProfile` lines in `tickCompact`. Restore the original single `reposition(app, ...)` call.

- [ ] **Step 4: Rebuild and run the full harness**

Run: `cmake --build build --config Release -- /m && build\Release\tapir_tests.exe && pwsh -NoProfile -ExecutionPolicy Bypass -File tools/measure.ps1 -Runs 7`
Expected: clean build, all tests pass, harness completes. The harness rewrites `docs/budget-report.md`; edit its KeystrokeToResizeMs and KeystrokeToPixelChangeMs rows and the Verdict paragraph to include the before/after app-stage medians, ranges, run count and the named limiter. Mention that external numbers include polling and DWM scheduling and are not the app-side gate.

- [ ] **Step 5: Manual visual check**

At a real desktop, confirm with a query such as `vsc`: highlighted characters use the accent color and semibold, long titles end with an ellipsis, ghost text appears after the caret, the selection pill and hover fill are unchanged, and the command bar key caps look identical. Repeat after a live font change and a DPI change to confirm the cache clears.

- [ ] **Step 6: Delete temporary evidence**

Remove `%TEMP%\tapir-profile-baseline.txt`. Confirm with `git status` that the tree contains only the intended source, test, CMake and budget-report changes plus the pre-existing user changes.

- [ ] **Step 7: Commit (only if the user explicitly asks)**

```bash
git add src/render.h src/render.cpp src/gfx.cpp src/util/text_layout_cache.h tests/text_layout_cache.test.cpp CMakeLists.txt docs/budget-report.md
git commit -m "perf: cache text layouts to cut keystroke latency"
```

---

## Self-review

- Spec coverage: goal and success criteria are in Task 1 Step 6 and Task 5 Steps 1-2; scope items are in Tasks 2-4; the pure policy and tests are in Task 2; error behavior is in `layoutFor`; invalidation is in Task 3 Step 6 and Task 4 Step 2; measurement, reporting and visual verification are in Task 5; rejected alternatives are recorded in the spec.
- No placeholders: every code step contains concrete C++ or PowerShell. Commit steps are conditional on an explicit user request, as required by the project rules.
- Type consistency: `TextFormatId`, `TextLayoutKey`, `BoundedTextLayoutCache`, `layoutFor`, `formatFor`, `CachedLayout`, and `Painter::clearTextCache()` use the same names in every task that references them.

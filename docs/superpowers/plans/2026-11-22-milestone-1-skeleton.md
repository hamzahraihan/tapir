# Milestone 1 Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the tapir skeleton — CMake project, manifest, `wWinMain`, single instance, and a hidden DPI-aware popup window toggled by `Alt+Space`, hidden by `Esc` or focus loss.

**Architecture:** A single Win32 GUI executable. `wWinMain` owns a named mutex for single instance, a named event for second-launch signalling, and a `MsgWaitForMultipleObjects` message loop. One hidden popup window is created at startup and only shown/hidden. Pure logic (hotkey string grammar, popup placement math) lives in testable modules; Win32 calls stay in `hotkey`, `window`, `main`, and `util`.

**Tech Stack:** C++20, MSVC 19.51 (VS 18 Community), CMake 4.4.4, Ninja 1.13.2, doctest v2.5.3 vendored as a single header, Windows SDK 10.0.26100.0.

## Global Constraints

- Executable name is `tapir.exe`; the name lives only in the CMake `APP_NAME` variable and `src/constants.h`. No other file hardcodes it.
- Windows 10 and 11 only. No cross-platform abstractions, no network access, no telemetry, no services.
- C++20, `/W4 /WX /permissive- /utf-8 /EHsc`. Warnings are fixed, never suppressed.
- Static CRT: `MultiThreaded` in Release, `MultiThreadedDebug` in Debug.
- Release: `/O2 /GL`, linker `/LTCG /DEBUG /OPT:REF /OPT:ICF`; debug info in a separate PDB.
- Only third-party libraries allowed are toml++ v3.4.0 and doctest v2.5.3, vendored unmodified in `third_party/`. This milestone vendors doctest only; toml++ arrives with config work in milestone 6.
- Builds **must** run from Developer PowerShell for VS: `Import-Module 'G:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell 9d836cc6 -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'`. `vswhere` does not discover this install.
- Default hotkey is `Alt+Space`; grammar: `Ctrl|Alt|Shift|Win` modifiers joined by `+` with exactly one key, case-insensitive, no repeated modifiers.
- Popup: `WS_POPUP`, `WS_EX_TOOLWINDOW | WS_EX_TOPMOST`, width 680 DIP, placement `upper-center` on the monitor under the cursor, PerMonitorV2 DPI awareness.
- Store `GetForegroundWindow()` as the previous window **before** showing the popup on a hotkey.
- Hide on `Esc` and on `WM_ACTIVATE` deactivation. Hide is instant; no animation in this milestone.
- Diagnostics use reason-only messages; no clipboard content exists yet, and none may ever be logged.

---

### Task 1: Build scaffold, manifest, and doctest harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/constants.h`
- Create: `resources/app.manifest`
- Create: `resources/app.rc`
- Create: `third_party/README.md`
- Create: `third_party/doctest/doctest.h` (vendored copy)
- Create: `tests/doctest_main.test.cpp`
- Create: `tests/smoke.test.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: `tapir_tests` target, `kAppName` constant, CMake options and compiler settings used by every later task.

- [ ] **Step 1: Write the failing smoke test**

`tests/smoke.test.cpp`:
```cpp
#include <doctest.h>

TEST_CASE("doctest harness runs") {
    CHECK(1 + 1 == 2);
}
```

`tests/doctest_main.test.cpp` (the single implementation unit):
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
```

- [ ] **Step 2: Run it to verify it fails**

From Developer PowerShell for VS:
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DTAPIR_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: configure fails because `CMakeLists.txt` does not exist yet.

- [ ] **Step 3: Create the scaffold**

`src/constants.h`:
```cpp
#pragma once

#include <string_view>

namespace tapir {
inline constexpr std::wstring_view kAppName = L"tapir";
}
```

`resources/app.manifest`:
```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1">
  <assemblyIdentity version="1.0.0.0" name="tapir" type="win32" />
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
```

`resources/app.rc` (the `@` token is substituted by CMake so `rc.exe` gets an absolute path):
```
#include "winres.h"

CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST "@CMAKE_CURRENT_SOURCE_DIR@/resources/app.manifest"
```

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.24)
project(tapir LANGUAGES CXX RC)

set(APP_NAME tapir)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
  message(FATAL_ERROR "tapir requires MSVC from Developer PowerShell for VS")
endif()

add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL>)
add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

configure_file(resources/app.rc ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

option(TAPIR_BUILD_TESTS "Build tapir unit tests" ON)

if(TAPIR_BUILD_TESTS)
  enable_testing()
  add_executable(tapir_tests
    tests/doctest_main.test.cpp
    tests/smoke.test.cpp
  )
  target_include_directories(tapir_tests PRIVATE third_party/doctest)
  add_test(NAME tapir_tests COMMAND tapir_tests)
endif()
```

`third_party/README.md`:
```markdown
# Vendored third-party headers

These files are byte-for-byte copies of tagged upstream releases. Do not edit them locally.
Updating either library requires a decision ticket because `AGENTS.md` limits third-party
libraries to these two.

| Library | Version | License | Upstream | File |
| --- | --- | --- | --- | --- |
| doctest | v2.5.3 | MIT | https://github.com/doctest/doctest | `doctest/doctest.h` |

Fetch command used (run once, never at build time):

    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h" -OutFile "third_party/doctest/doctest.h"

toml++ v3.4.0 (MIT, https://github.com/marzer/tomlplusplus) will be vendored as
`toml++/toml.hpp` when config work begins in milestone 6.
```

Append to `.gitignore`:
```
build/
```

- [ ] **Step 4: Vendor doctest**

From the repo root, run the fetch command in the README. Verify the header is present and unmodified:
```powershell
(Get-FileHash -LiteralPath 'third_party\doctest\doctest.h' -Algorithm SHA256).Hash
(Get-Content -LiteralPath 'third_party\doctest\doctest.h' -TotalCount 5)
```
Expected: a hash is printed; the first lines contain the doctest copyright and MIT notice.

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DTAPIR_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: 1 test passes.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/constants.h resources/app.manifest resources/app.rc third_party .gitignore tests
git commit -m "build: scaffold CMake project with vendored doctest harness"
```

---

### Task 2: Hotkey grammar and registration

**Files:**
- Create: `src/hotkey.h`
- Create: `src/hotkey.cpp`
- Create: `tests/hotkey.test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `tapir::hotkey::kDefaultHotkey` — `std::wstring_view`, value `L"Alt+Space"`.
  - `tapir::hotkey::Combination` — `{ unsigned modifiers; std::wstring key; }`, modifier bits `Ctrl=1, Alt=2, Shift=4, Win=8`, `key` is uppercase (`SPACE`, `K`, `F1`, `A`, `0`).
  - `std::optional<Combination> tapir::hotkey::parse(std::wstring_view text)` — pure.
  - `bool tapir::hotkey::registerHotkey(HWND hwnd, int id, const Combination& combo)` and `void unregisterHotkey(HWND hwnd, int id)`.

- [ ] **Step 1: Write the failing tests**

`tests/hotkey.test.cpp`:
```cpp
#include <doctest.h>
#include "hotkey.h"

using tapir::hotkey::parse;
using tapir::hotkey::Alt;
using tapir::hotkey::Ctrl;
using tapir::hotkey::Shift;

TEST_CASE("hotkey parses the default Alt+Space") {
    const auto c = parse(L"Alt+Space");
    REQUIRE(c.has_value());
    CHECK(c->modifiers == Alt);
    CHECK(c->key == L"SPACE");
}

TEST_CASE("hotkey names are case-insensitive") {
    const auto c = parse(L"ctrl+shift+K");
    REQUIRE(c.has_value());
    CHECK(c->modifiers == (Ctrl | Shift));
    CHECK(c->key == L"K");
}

TEST_CASE("hotkey accepts digits and function keys") {
    const auto f1 = parse(L"Win+F1");
    REQUIRE(f1.has_value());
    CHECK(f1->modifiers == tapir::hotkey::Win);
    CHECK(f1->key == L"F1");
    const auto digit = parse(L"Alt+2");
    REQUIRE(digit.has_value());
    CHECK(digit->key == L"2");
}

TEST_CASE("hotkey rejects malformed combinations") {
    CHECK_FALSE(parse(L"Alt").has_value());
    CHECK_FALSE(parse(L"Alt+").has_value());
    CHECK_FALSE(parse(L"Alt+Alt+Space").has_value());
    CHECK_FALSE(parse(L"Alt+K+Space").has_value());
    CHECK_FALSE(parse(L"Alt+Space++K").has_value());
    CHECK_FALSE(parse(L"Foo+Space").has_value());
    CHECK_FALSE(parse(L"Alt+Spacebar").has_value());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Add to `CMakeLists.txt` inside the `TAPIR_BUILD_TESTS` block:
```cmake
  target_sources(tapir_tests PRIVATE
    src/hotkey.cpp
    tests/hotkey.test.cpp
  )
  target_include_directories(tapir_tests PRIVATE src)
  target_link_libraries(tapir_tests PRIVATE user32)
```

Then:
```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: build fails because `src/hotkey.h` and `src/hotkey.cpp` do not exist.

- [ ] **Step 3: Implement the grammar and native registration**

`src/hotkey.h`:
```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

namespace tapir::hotkey {

enum Modifier : unsigned {
    None  = 0,
    Ctrl  = 1,
    Alt   = 2,
    Shift = 4,
    Win   = 8,
};

struct Combination {
    unsigned modifiers = None;
    std::wstring key;
};

inline constexpr std::wstring_view kDefaultHotkey = L"Alt+Space";

std::optional<Combination> parse(std::wstring_view text);
bool registerHotkey(HWND hwnd, int id, const Combination& combo);
void unregisterHotkey(HWND hwnd, int id);

} // namespace tapir::hotkey
```

`src/hotkey.cpp`:
```cpp
#include "hotkey.h"

#include <algorithm>
#include <cwctype>

namespace tapir::hotkey {
namespace {

bool asciiEquals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

std::optional<unsigned> modifierBit(std::wstring_view token) {
    if (asciiEquals(token, L"ctrl")) return Ctrl;
    if (asciiEquals(token, L"alt")) return Alt;
    if (asciiEquals(token, L"shift")) return Shift;
    if (asciiEquals(token, L"win")) return Win;
    return std::nullopt;
}

bool validKey(std::wstring_view key) {
    if (key.size() == 1) {
        const wchar_t c = static_cast<wchar_t>(std::towupper(key.front()));
        return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
    }
    if (key.size() == 2 || key.size() == 3) {
        if (!asciiEquals(key.substr(0, 1), L"f")) return false;
        const int n = (key[1] - L'0') * 10 + (key.size() == 3 ? key[2] - L'0' : 0);
        return n >= 1 && n <= 12;
    }
    return asciiEquals(key, L"space");
}

std::wstring normalizeKey(std::wstring_view key) {
    std::wstring out(key);
    for (wchar_t& c : out) c = static_cast<wchar_t>(std::towupper(c));
    return out;
}

UINT nativeKey(const std::wstring& key) {
    if (key == L"SPACE") return VK_SPACE;
    if (key[0] == L'F') {
        int n = 0;
        for (std::size_t i = 1; i < key.size(); ++i) n = n * 10 + (key[i] - L'0');
        return static_cast<UINT>(VK_F1 + n - 1);
    }
    return static_cast<UINT>(key.front());
}

} // namespace

std::optional<Combination> parse(std::wstring_view text) {
    Combination result;
    bool haveKey = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(L'+', start);
        const std::wstring_view token =
            text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        if (token.empty()) return std::nullopt;

        if (const auto bit = modifierBit(token)) {
            if ((result.modifiers & *bit) != 0) return std::nullopt;
            result.modifiers |= *bit;
        } else if (!haveKey && validKey(token)) {
            result.key = normalizeKey(token);
            haveKey = true;
        } else {
            return std::nullopt;
        }

        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    if (!haveKey) return std::nullopt;
    return result;
}

bool registerHotkey(HWND hwnd, int id, const Combination& combo) {
    UINT nativeModifiers = 0;
    if (combo.modifiers & Ctrl)  nativeModifiers |= MOD_CONTROL;
    if (combo.modifiers & Alt)   nativeModifiers |= MOD_ALT;
    if (combo.modifiers & Shift) nativeModifiers |= MOD_SHIFT;
    if (combo.modifiers & Win)   nativeModifiers |= MOD_WIN;
    return RegisterHotKey(hwnd, id, nativeModifiers, nativeKey(combo.key)) != 0;
}

void unregisterHotkey(HWND hwnd, int id) {
    UnregisterHotKey(hwnd, id);
}

} // namespace tapir::hotkey
```

- [ ] **Step 4: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: 5 tests pass (1 smoke + 4 hotkey).

- [ ] **Step 5: Commit**

```bash
git add src/hotkey.h src/hotkey.cpp tests/hotkey.test.cpp CMakeLists.txt
git commit -m "feat: add hotkey grammar and Win32 registration"
```

---

### Task 3: Pure popup placement math

**Files:**
- Create: `src/util/placement.h`
- Create: `src/util/placement.cpp`
- Create: `tests/placement.test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `tapir::placement::Rect { int left, top, right, bottom; }`
  - `enum class tapir::placement::Position { UpperCenter, Center };`
  - `constexpr int tapir::placement::kDefaultWidthDip = 680;`
  - `constexpr int tapir::placement::kCollapsedHeightDip = 56;`
  - `Rect tapir::placement::compute(const Rect& monitor, int widthDip, int heightDip, unsigned dpi, Position position);`

- [ ] **Step 1: Write the failing tests**

`tests/placement.test.cpp`:
```cpp
#include <doctest.h>
#include "util/placement.h"

using tapir::placement::compute;
using tapir::placement::Position;
using tapir::placement::Rect;

TEST_CASE("upper-center placement at 96 dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, 680, 56, 96, Position::UpperCenter);
    CHECK(r.left == 620);
    CHECK(r.top == 341);
    CHECK(r.right == 1300);
    CHECK(r.bottom == 397);
}

TEST_CASE("center placement at 96 dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, 680, 56, 96, Position::Center);
    CHECK(r.left == 620);
    CHECK(r.top == 512);
}

TEST_CASE("placement scales with dpi") {
    const Rect monitor{0, 0, 1920, 1080};
    const Rect r = compute(monitor, 680, 56, 144, Position::UpperCenter);
    CHECK(r.left == 450);
    CHECK(r.top == 332);
    CHECK(r.right == 1470);
    CHECK(r.bottom == 416);
}

TEST_CASE("placement clamps to a smaller monitor") {
    const Rect monitor{0, 0, 400, 300};
    const Rect r = compute(monitor, 680, 56, 96, Position::UpperCenter);
    CHECK(r.left == 0);
    CHECK(r.right == 400);
    CHECK(r.top == 81);
    CHECK(r.bottom == 137);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Add to the `TAPIR_BUILD_TESTS` block in `CMakeLists.txt`:
```cmake
  target_sources(tapir_tests PRIVATE
    src/util/placement.cpp
    tests/placement.test.cpp
  )
```
Then run `cmake --build build --config Debug` and `ctest --test-dir build -C Debug --output-on-failure`. Expected: build fails because placement files do not exist.

- [ ] **Step 3: Implement the placement function**

`src/util/placement.h`:
```cpp
#pragma once

namespace tapir::placement {

struct Rect {
    int left;
    int top;
    int right;
    int bottom;
};

enum class Position { UpperCenter, Center };

inline constexpr int kDefaultWidthDip = 680;
inline constexpr int kCollapsedHeightDip = 56;

Rect compute(const Rect& monitor, int widthDip, int heightDip, unsigned dpi, Position position);

} // namespace tapir::placement
```

`src/util/placement.cpp`:
```cpp
#include "util/placement.h"

#include <algorithm>

namespace tapir::placement {
namespace {

int toPixels(int dip, unsigned dpi) {
    return (dip * static_cast<int>(dpi) + 48) / 96;
}

} // namespace

Rect compute(const Rect& monitor, int widthDip, int heightDip, unsigned dpi, Position position) {
    const int monitorWidth = monitor.right - monitor.left;
    const int monitorHeight = monitor.bottom - monitor.top;

    const int width = std::min(toPixels(widthDip, dpi), monitorWidth);
    const int height = std::min(toPixels(heightDip, dpi), monitorHeight);

    const int x = monitor.left + (monitorWidth - width) / 2;
    const int freeY = monitorHeight - height;
    const int y = monitor.top + (position == Position::UpperCenter ? freeY / 3 : freeY / 2);

    return Rect{x, y, x + width, y + height};
}

} // namespace tapir::placement
```

- [ ] **Step 4: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: 9 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/util/placement.h src/util/placement.cpp tests/placement.test.cpp CMakeLists.txt
git commit -m "feat: add pure popup placement math"
```

---

### Task 4: Popup window lifecycle and app state

**Files:**
- Create: `src/app.h`
- Create: `src/app.cpp`
- Create: `src/window.h`
- Create: `src/window.cpp`

**Interfaces:**
- Consumes: `tapir::placement::compute`, `kDefaultWidthDip`, `kCollapsedHeightDip`, `Position::UpperCenter`, `tapir::hotkey::Combination`.
- Produces:
  - `struct tapir::AppState { HWND popup; HWND previous; int hotkeyId; bool visible; };` in `src/app.h`.
  - `bool tapir::window::create(AppState& app, unsigned dpi);`
  - `void tapir::window::destroy(AppState& app);`
  - `void tapir::window::show(AppState& app);`
  - `void tapir::window::hide(AppState& app);`
  - `void tapir::window::toggle(AppState& app);`

- [ ] **Step 1: Write the state and window headers**

`src/app.h`:
```cpp
#pragma once

#include <windows.h>

namespace tapir {

struct AppState {
    HWND popup = nullptr;
    HWND previous = nullptr;
    int hotkeyId = 1;
    bool visible = false;
};

} // namespace tapir
```

`src/app.cpp`:
```cpp
#include "app.h"

namespace tapir {
// AppState is plain data; this translation unit anchors it for future wiring.
} // namespace tapir
```

`src/window.h`:
```cpp
#pragma once

#include "app.h"

namespace tapir::window {

bool create(AppState& app, unsigned dpi);
void destroy(AppState& app);
void show(AppState& app);
void hide(AppState& app);
void toggle(AppState& app);

} // namespace tapir::window
```

- [ ] **Step 2: Implement the window lifecycle**

`src/window.cpp`:
```cpp
#include "window.h"

#include "constants.h"
#include "util/placement.h"

#include <shellscalingapi.h>
#include <windowsx.h>

namespace tapir::window {
namespace {

AppState* stateFromHwnd(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void reposition(HWND hwnd, unsigned dpi) {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    const placement::Rect work{
        info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom
    };
    const placement::Rect r = placement::compute(
        work, placement::kDefaultWidthDip, placement::kCollapsedHeightDip, dpi,
        placement::Position::UpperCenter);

    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* app = stateFromHwnd(hwnd);

    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        app = stateFromHwnd(hwnd);
        if (app) app->popup = hwnd;
        return 0;
    }
    case WM_HOTKEY:
        if (app) toggle(*app);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (app) hide(*app);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (app) hide(*app);
        }
        return 0;
    case WM_DPICHANGED: {
        const unsigned dpi = static_cast<unsigned>(LOWORD(wParam));
        reposition(hwnd, dpi);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

bool create(AppState& app, unsigned dpi) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kAppName.data();
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassW(&wc) == 0) return false;
        registered = true;
    }

    app.popup = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kAppName.data(), kAppName.data(),
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandleW(nullptr), &app);
    if (app.popup == nullptr) return false;

    reposition(app.popup, dpi);
    ShowWindow(app.popup, SW_HIDE);
    return true;
}

void destroy(AppState& app) {
    if (app.popup != nullptr) {
        DestroyWindow(app.popup);
        app.popup = nullptr;
    }
    app.visible = false;
}

void show(AppState& app) {
    if (app.popup == nullptr || app.visible) return;
    app.previous = GetForegroundWindow();
    ShowWindow(app.popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(app.popup);
    app.visible = true;
}

void hide(AppState& app) {
    if (app.popup == nullptr || !app.visible) return;
    ShowWindow(app.popup, SW_HIDE);
    app.visible = false;
}

void toggle(AppState& app) {
    if (app.visible) hide(app);
    else show(app);
}

} // namespace tapir::window
```

- [ ] **Step 3: Compile the non-GUI targets to check the new code**

CMake does not yet have an executable target that uses these files, so compile them directly with the MSVC environment:
```powershell
cl /nologo /W4 /WX /permissive- /utf-8 /EHsc /c /Isrc /Fo:build\window.obj src\window.cpp
cl /nologo /W4 /WX /permissive- /utf-8 /EHsc /c /Isrc /Fo:build\app.obj src\app.cpp
```
Expected: no errors; `build\window.obj` and `build\app.obj` exist. Delete both `.obj` files afterwards.

- [ ] **Step 4: Commit**

```bash
git add src/app.h src/app.cpp src/window.h src/window.cpp
git commit -m "feat: add popup window lifecycle and app state"
```

---

### Task 5: Single-instance `wWinMain` and end-to-end skeleton

**Files:**
- Create: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `tapir::AppState`, `tapir::window::*`, `tapir::hotkey::*`, `tapir::kAppName`.
- Produces: `tapir.exe`, the runnable milestone-1 deliverable.

- [ ] **Step 1: Write `wWinMain`**

`src/main.cpp`:
```cpp
#include "app.h"
#include "constants.h"
#include "hotkey.h"
#include "window.h"

#include <string>
#include <windows.h>

namespace {

constexpr int kShowRequestId = 1;

std::wstring instanceName(std::wstring_view suffix) {
    return std::wstring(kAppName) + L"-" + std::wstring(suffix);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, instanceName(L"show").c_str());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, instanceName(L"single-instance").c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (showEvent != nullptr) SetEvent(showEvent);
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 0;
    }

    tapir::AppState app;
    const DWORD initialDpi = GetDpiForSystem();
    if (!tapir::window::create(app, initialDpi)) {
        if (mutex != nullptr) CloseHandle(mutex);
        if (showEvent != nullptr) CloseHandle(showEvent);
        return 1;
    }

    const auto combo = tapir::hotkey::parse(tapir::hotkey::kDefaultHotkey);
    if (!combo.has_value() ||
        !tapir::hotkey::registerHotkey(app.popup, app.hotkeyId, *combo)) {
        const std::wstring message =
            std::wstring(kAppName) +
            L": the default hotkey Alt+Space is already in use by another application. " +
            L"Close that application or change the hotkey in config.toml.";
        MessageBoxW(nullptr, message.c_str(), kAppName.data(), MB_OK | MB_ICONWARNING);
    }

    bool running = true;
    while (running) {
        const bool waitOnEvent = showEvent != nullptr;
        HANDLE waits[1] = {showEvent};
        const DWORD result = MsgWaitForMultipleObjects(
            waitOnEvent ? 1 : 0, waits, FALSE, INFINITE, QS_ALLINPUT);

        if (waitOnEvent && result == WAIT_OBJECT_0) {
            app.previous = GetForegroundWindow();
            tapir::window::show(app);
            continue;
        }

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    tapir::hotkey::unregisterHotkey(app.popup, app.hotkeyId);
    tapir::window::destroy(app);
    if (mutex != nullptr) CloseHandle(mutex);
    if (showEvent != nullptr) CloseHandle(showEvent);
    return 0;
}
```

- [ ] **Step 2: Add the executable target to CMake**

Insert the executable target before the existing `option(TAPIR_BUILD_TESTS ...)` line in `CMakeLists.txt`. Keep the existing `configure_file` line and do not add a second one:
```cmake
add_executable(${APP_NAME} WIN32
  src/main.cpp
  src/app.cpp
  src/app.h
  src/hotkey.cpp
  src/hotkey.h
  src/window.cpp
  src/window.h
  src/util/placement.cpp
  src/util/placement.h
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_include_directories(${APP_NAME} PRIVATE src)
target_link_libraries(${APP_NAME} PRIVATE user32 shcore)

option(TAPIR_BUILD_TESTS "Build tapir unit tests" ON)

if(TAPIR_BUILD_TESTS)
  enable_testing()
  add_executable(tapir_tests
    tests/doctest_main.test.cpp
    tests/smoke.test.cpp
    tests/hotkey.test.cpp
    tests/placement.test.cpp
    src/hotkey.cpp
    src/util/placement.cpp
  )
  target_include_directories(tapir_tests PRIVATE src third_party/doctest)
  target_link_libraries(tapir_tests PRIVATE user32)
  add_test(NAME tapir_tests COMMAND tapir_tests)
endif()
```

- [ ] **Step 3: Build and run all automated tests**

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DTAPIR_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `tapir.exe` and `tapir_tests.exe` build with no warnings; 9 tests pass.

- [ ] **Step 4: Verify the Win32 surface manually**

From Developer PowerShell for VS:
```powershell
Start-Process -FilePath '.\build\Debug\tapir.exe'
```
Then check, in order:
1. Press `Alt+Space`: a blank borderless window appears immediately, about 680 DIP wide, horizontally centered in the upper third of the monitor containing the cursor.
2. Press `Alt+Space` again: it hides instantly.
3. Press `Alt+Space`, then `Esc`: it hides.
4. Press `Alt+Space`, then click another window: it hides.
5. Run `Start-Process` a second time: no second process exists; the first popup appears.
6. On a mixed-DPI setup, repeat step 1 on a monitor with different scaling: size and position follow that monitor.
7. Confirm keyboard focus reaches the popup by pressing `Esc` without clicking it.
Expected: all seven checks pass. If step 7 fails, add the documented `AttachThreadInput` fallback to `show()` before proceeding; do not ship a popup that cannot receive keys.

- [ ] **Step 5: Verify the release build and binary size**

```powershell
cmake -S . -B build-release -G "Visual Studio 18 2026" -A x64 -DTAPIR_BUILD_TESTS=OFF
cmake --build build-release --config Release
(Get-Item -LiteralPath '.\build-release\Release\tapir.exe').Length
```
Expected: build succeeds; size is reported. The under-2 MB target is a release budget, recorded here but formally measured in milestone 7.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp CMakeLists.txt
git commit -m "feat: add single-instance wWinMain and popup message loop"
```

---

## Self-review notes

- **Spec coverage:** milestone 1 requirements map to Task 1 (CMake, manifest, single `APP_NAME`, doctest), Task 2 (`RegisterHotKey`, Alt+Space grammar, failure message), Task 3 (680 DIP width, DIP scaling, upper-center placement), Task 4 (one hidden window, show/hide, `Esc`, focus loss, DPI change, previous-window capture), Task 5 (single instance, second-launch show, message loop, manual verification).
- **Deferred by milestone order:** DWM backdrop and rendering (2), app index and fuzzy results (3–4), clipboard capture and paste (5), TOML config and `history.dat` (6), polish and budget measurement (7). The corresponding decisions are already recorded in [the wayfinder map](https://github.com/hamzahraihan/tapir/issues/1) and are not re-decided here.
- **Type consistency:** `AppState` fields are used identically in Tasks 4 and 5; `hotkey::Combination` produced by `parse` is consumed unchanged by `registerHotkey`; `placement::Rect` is the only rectangle type.

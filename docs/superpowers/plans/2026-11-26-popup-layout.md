# Popup Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change the tapir popup to the approved Command Palette-style layout without adding any feature, favourites list, or behaviour change.

**Architecture:** Extend the plain-data view structs in `render.h`, fill the new fields from `window.cpp::draw`, and reposition drawing in `render.cpp`. Drawing keeps consuming `PaletteView` only. DIP constants remain 56/20/48/40 so window-height math and hit-testing are untouched.

**Tech Stack:** C++20, MSVC, Direct2D/DirectWrite, raw Win32, CMake/Ninja, doctest.

## Global Constraints

- Windows only; no new dependencies.
- `render` draws from plain data; no Win32, file, clipboard or index access in it.
- Modify only `src/render.h`, `src/render.cpp`, `src/window.cpp`.
- No favourites, no "Quick AI", no new command, no new setting, no new keybinding.
- No changes to `AGENTS.md`, README, config defaults, ranking, clipboard, icons, colours or animations.
- `/W4 /WX` must stay clean. No `as any`, no warning suppression.
- Do not commit: this repo's rule is no commits without an explicit user request.

---

### Task 1: Extend the view structs and wire the data

**Files:**
- Modify: `src/render.h:26-37`, `src/render.h:60-67`
- Modify: `src/window.cpp:243-244`, `src/window.cpp:324-325`

**Interfaces:**
- Consumes: `AppState::appsMode`, `AppState::moreMenuOpen` (existing).
- Produces: `SearchRowView::hint`, `SearchRowView::hintKey`, `CommandBarView::filterGlyph` for Tasks 2 and 3.

- [ ] **Step 1: Add the fields**

In `src/render.h`, inside `SearchRowView` after `std::wstring_view ghost;` add:

```cpp
    std::wstring_view hint;
    std::wstring_view hintKey;
```

Inside `CommandBarView` after `bool showMore = true;` add:

```cpp
    wchar_t filterGlyph = 0;
```

- [ ] **Step 2: Populate them from AppState**

In `src/window.cpp::draw`, replace:

```cpp
    view.commands.modeLabel = app.appsMode ? L"Apps  ·  Tab" : L"Clipboard  ·  Tab";
```

with:

```cpp
    view.search.hint = app.moreMenuOpen ? L"Actions"
                       : app.appsMode ? L"Apps" : L"Clipboard";
    view.search.hintKey = L"Tab";
    view.commands.modeLabel = L"";
    view.commands.filterGlyph = static_cast<wchar_t>(0xE71C);
```

`0xE71C` is `Filter` in the Segoe Fluent Icons list.

- [ ] **Step 3: Verify the build**

Run: `cmake --build build --config Debug --target tapir_tests`
Expected: compiles with no warnings; tests still pass when run with `build\Debug\tapir_tests.exe`.

### Task 2: Draw the search-row hint and the footer glyph

**Files:**
- Modify: `src/render.cpp:226-275` (`drawSearchRow`)
- Modify: `src/render.cpp:445-468` (`drawCommandBar`)

**Interfaces:**
- Consumes: `SearchRowView::hint`, `SearchRowView::hintKey` from Task 1; existing `textWidth`, `drawSegment`, `keyCap` helpers.
- Produces: right-hand search hint and left footer glyph.

- [ ] **Step 1: Forward-declare keyCap**

`keyCap` is defined below `drawSearchRow`. Insert this declaration immediately above `drawSearchRow`:

```cpp
float keyCap(Impl& p, std::wstring_view label, float x, float y, float scale);
```

- [ ] **Step 2: Reserve the right-hand slot**

At the start of `drawSearchRow`, after `if (textW <= 0.0f || width <= 0.0f || height <= 0.0f) return;`, insert:

```cpp
    const float hintW = view.hint.empty()
                            ? 0.0f
                            : textWidth(p, tapir::util::TextFormatId::label, view.hint,
                                        160.0f * scale, height);
    const float keyW = view.hintKey.empty()
                           ? 0.0f
                           : textWidth(p, tapir::util::TextFormatId::key, view.hintKey,
                                       160.0f * scale, 22.0f * scale) + 12.0f * scale;
    const bool hasHint = hintW > 0.0f || keyW > 0.0f;
    const float slotW = hasHint ? hintW + 8.0f * scale + keyW + 16.0f * scale : 0.0f;
```

Change the existing `textW` declaration to:

```cpp
    float textW = width - textX - slotW - 12.0f * scale;
```

- [ ] **Step 3: Draw the hint and keycap**

Immediately before the closing brace of `drawSearchRow` (after the `view.focused` block), insert:

```cpp
    if (hasHint) {
        const float capY = (height - 22.0f * scale) * 0.5f;
        const float capX = width - 16.0f * scale - keyW;
        if (!view.hintKey.empty()) keyCap(p, view.hintKey, capX, capY, scale);
        if (!view.hint.empty()) {
            drawSegment(p, tapir::util::TextFormatId::label, p.muted.Get(), view.hint,
                        capX - 8.0f * scale - hintW, 0.0f, hintW + scale, height);
        }
    }
```

- [ ] **Step 4: Draw the footer glyph**

In `drawCommandBar`, replace:

```cpp
    drawSegment(p, tapir::util::TextFormatId::label, p.muted.Get(), bar.modeLabel,
                16.0f * scale, y, 160.0f * scale, height);
```

with:

```cpp
    if (bar.filterGlyph != 0) {
        drawSegment(p, tapir::util::TextFormatId::glyph, p.muted.Get(),
                    std::wstring_view(&bar.filterGlyph, 1), 16.0f * scale, y,
                    20.0f * scale, height);
    }
```

- [ ] **Step 5: Verify**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: build clean, 202 tests pass.

### Task 3: Draw one-line result rows and shift the header

**Files:**
- Modify: `src/render.cpp:277-339` (`drawHighlightedTitle`, `drawResultRow`)
- Modify: `src/render.cpp:547-551` (`Painter::draw` section header)

**Interfaces:**
- Consumes: existing `ResultRowView` fields; Tasks 1-2 do not affect this task.
- Produces: inline subtitle rendering; no new view fields.

- [ ] **Step 1: Make drawHighlightedTitle return the consumed width**

Change its signature from `void drawHighlightedTitle(...)` to `float drawHighlightedTitle(...)`, and add `return used;` after the final `if (pos < row.title.size() && used < maxW)` block. All existing call sites ignore the return value, which is valid C++.

- [ ] **Step 2: Replace the stacked title/subtitle block**

In `drawResultRow`, replace the title and subtitle section:

```cpp
    const float textX = 68.0f * scale;
    const float textW = std::max(1.0f, width - textX - 120.0f * scale);
    drawHighlightedTitle(p, row, textX, y + 1.0f * scale, textW, 24.0f * scale);
    drawSegment(p, tapir::util::TextFormatId::subtitle, p.muted.Get(), row.subtitle,
                textX, y + 24.0f * scale, textW, 20.0f * scale);
```

with:

```cpp
    const float textX = 68.0f * scale;
    const float labelW = row.typeLabel.empty()
                             ? 0.0f
                             : textWidth(p, tapir::util::TextFormatId::label,
                                         row.typeLabel, 80.0f * scale, height);
    const float textW = std::max(1.0f, width - textX - labelW - 32.0f * scale);
    const float textY = y + 12.0f * scale;
    const float titleW = drawHighlightedTitle(p, row, textX, textY, textW, 24.0f * scale);
    if (!row.subtitle.empty() && titleW + 8.0f * scale < textW) {
        drawSegment(p, tapir::util::TextFormatId::subtitle, p.muted.Get(), row.subtitle,
                    textX + titleW + 8.0f * scale, textY,
                    textW - titleW - 8.0f * scale, 24.0f * scale);
    }
```

Change the type-label draw call's `y + 8.0f * scale` to `textY`.

- [ ] **Step 3: Shift the section header**

In `Painter::draw`, change the section-header call from x `52.0f * scale` and width `listWidth - 64.0f * scale` to x `28.0f * scale` and width `listWidth - 40.0f * scale`.

- [ ] **Step 4: Verify**

Run: `cmake --build build --config Debug --target tapir_tests && build\Debug\tapir_tests.exe`
Expected: build clean with `/W4 /WX`, 202 tests pass.

### Task 4: Visual QA and release build

**Files:**
- No source changes; verification only.

- [ ] **Step 1: Release build**

Run: `cmake --build build --config Release --target tapir`
Expected: exit 0, no warnings.

- [ ] **Step 2: Before/after visual check**

Capture the popup at the same query and selection before and after the change. Confirm: right-hand mode hint and Tab keycap in the search row; header aligned with the icon tile; one-line rows with inline, ellipsised subtitle; filter glyph at the footer left; unchanged `Enter Open` and `Ctrl+K More` on the footer right.

- [ ] **Step 3: No-favourites check**

Open the popup with an empty query in both Apps and Clipboard modes. Confirm no favourites list, no extra section, and no new UI appears.

- [ ] **Step 4: Report**

Report what changed, test and build results, and any pre-existing issues noticed. Do not commit.

## Self-review

- Every spec section maps to a task: search row → Task 2, rows → Task 3, footer → Task 2, data flow → Task 1, non-goals → Task 4 step 3, verification → Task 4.
- No placeholders: all code is written out; no "similar to" steps.
- Type consistency: `hint`, `hintKey`, `filterGlyph` are named identically in Tasks 1-2; `drawHighlightedTitle` keeps its parameter list and only changes return type.

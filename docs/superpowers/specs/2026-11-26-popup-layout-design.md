# Popup layout matching the Command Palette reference

Date: 2026-11-26
Status: design approved by the user

## Problem

The current popup renders the result title and subtitle on two lines, shows the mode label in the footer, and has no right-hand hint in the search row. The reference image shows a single-line result row with an inline subtitle, a right-hand hint in the search row, and a filter glyph in the footer. The user wants that visual structure and nothing else.

## Goal and success criteria

- The popup layout matches the reference in these four places: search-row right-hand hint, section-header alignment, single-line result rows with inline subtitle, and footer arrangement.
- No favourites list, no "Quick AI" label, no new command, and no new behaviour of any kind.
- Existing data and labels are unchanged: app/clipboard mode names, `Open` and `More`, result type labels, section-header strings.
- Rendering still draws only from `PaletteView` plain data, per `AGENTS.md:93`.
- DIP constants stay at 56 search / 20 header / 48 row / 40 footer, so window-height math in `window.cpp` and hit-testing need no changes.
- Release build is warning-free, unit tests pass, and a before/after screenshot at the same query and selection shows only the intended layout change.

## Non-goals

Favourites or pinned-first ordering, AI affordances, new keybindings, new settings, icon changes, colour changes, animation changes, and any change to ranking or clipboard behaviour.

## Design

### Search row (`render.h`, `render.cpp`, `window.cpp`)

`SearchRowView` gains two fields: `hint` (`std::wstring_view`) and `hintKey` (`std::wstring_view`). `window.cpp` fills them from the current mode and sets the mode label for the footer to empty. The hint key is `Tab`.

`drawSearchRow` reserves the right-hand slot using `textWidth(TextFormatId::label, hint)`, the keycap width computed as `textWidth(TextFormatId::key, hintKey) + 12 * scale` (matching `keyCap`), and an 8 DIP gap, then reduces the text width by that slot. The hint and keycap are drawn right-aligned at `width - 16 * scale`. Placeholder, typed text, selection, caret and ghost text all use the reduced text width, so nothing overlaps the hint. The existing glyph stays at 16 DIP and text starts at 52 DIP.

### Result rows (`render.cpp`)

`drawResultRow` keeps the selection fill, pill, icon tile and right-aligned type label. It draws the highlighted title at `textX` with the current 24 DIP height, then measures the consumed width using the existing `measurePrefix`/`drawSegment` return value and draws the subtitle inline after an 8 DIP gap in the remaining width up to the type label. DirectWrite trimming already applied by `makeLayout` gives the ellipsis. The type label and the title/subtitle block are vertically centred in the 48 DIP row.

`Painter::draw` moves the section header from x=52 to x=28 DIP so it aligns with the icon tile. Header text, the empty state and the list visibility rules are unchanged.

### Footer (`render.h`, `render.cpp`, `window.cpp`)

`CommandBarView` gains a `filterGlyph` (`wchar_t`). `window.cpp` sets it to `U+E71C` (Filter, confirmed in the Segoe Fluent Icons list) and clears `modeLabel`. `drawCommandBar` draws that glyph with the muted brush at x=16 DIP instead of the mode text. The right-hand `Enter Open` and `Ctrl+K More` keycaps and labels stay exactly as they are.

### Data flow

`window.cpp::draw` builds `PaletteView` from `AppState` as today. It splits the existing mode label into `search.hint` plus `search.hintKey`, and supplies `commands.filterGlyph`. `Painter::draw` consumes those fields only; no file, clipboard or index access is added.

## Risks and decisions

- Long app subtitles will truncate inline. The user chose this explicitly over stacking or conditional layout.
- Moving the mode hint out of the footer deviates from `AGENTS.md:4.1`, which places the mode and Tab hint in the command bar. The user's reference image takes precedence.
- A fixed right-hand slot reduces typing width; at 680 DIP there is ample room. No responsive special case is added.
- `U+E723`, currently used as the clipboard mode glyph, is actually `Attach` in Segoe Fluent Icons. That is a pre-existing issue and is deliberately out of scope.

## Verification

- `cmake --build build --config Release --target tapir` with `/W4 /WX` and no warnings.
- `build\Debug\tapir_tests.exe`: all tests pass.
- Capture the popup before and after at the same query and selection; compare search row, header alignment, one-line rows, and footer glyph placement.
- Confirm no favourites or other new UI appears when the popup opens with an empty query.

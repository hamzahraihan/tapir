# AGENTS.md: tapir

Instructions for AI coding agents working on **tapir**. Read this whole file before writing code.

The app name is **tapir** (executable `tapir.exe`). Keep the name in one CMake variable (`APP_NAME`, default `tapir`) and in one constants header, so nothing else hardcodes it.

## 1. What we are building

A **Spotlight-style launcher for Windows**, written in **C++**, with exactly two features:

1. **Fuzzy search** for apps (and nothing else at first).
2. **Clipboard history** with search and paste.

Hard constraints:

- **Windows only** (Windows 10 and 11), for now. No cross-platform abstractions.
- **No bloat.** No telemetry, no accounts, no auto-updater service, no web runtime, no plugin store, no AI features, no network access of any kind.
- **Native.** Raw Win32 + Direct3D/Direct2D/DirectWrite + DirectComposition. No Electron, WebView, Qt, or WinUI.
- **Instant.** The popup appears the moment the hotkey is pressed. Search results update on every keystroke with no debounce.
- **Small.** One executable, statically linked CRT, nothing to install.

Non-goals (do not build unless asked): file search, calculator, snippets, window switcher, web search, extensions/plugins, contacts/calendar/mail integration, cloud sync, mobile or other OS support.

## 2. Tech stack

| Concern | Choice |
|---|---|
| Language | C++20, MSVC (Visual Studio Build Tools) |
| Build | CMake (Ninja or MSBuild generator) |
| Windowing | Raw Win32 (`wWinMain`, message loop) |
| Rendering | Direct3D 11 (hardware, WARP fallback) + DXGI flip-model swap chain + Direct2D + DirectWrite, presented through DirectComposition. Render on demand only. |
| Backdrop | DWM system backdrop (acrylic) and rounded corners via `DwmSetWindowAttribute`, with a solid fallback |
| UI glyphs | Segoe Fluent Icons font (Segoe MDL2 Assets fallback), no image assets |
| Hotkey | `RegisterHotKey` on the popup window or a message-only window |
| Clipboard | `AddClipboardFormatListener` (event-driven, never poll) |
| App discovery | Enumerate the shell `AppsFolder` (covers Win32 and UWP apps) plus Start Menu `.lnk` files via `IShellLink` as needed |
| Icons | `IShellItemImageFactory`, loaded lazily for visible rows and cached |
| Config | TOML via `toml++` (header-only) |
| Storage | A single history file in the config dir (see section 8). No database unless measurements prove it is needed. |
| At-rest protection | DPAPI (`CryptProtectData`), no extra dependency |
| Tests | `doctest` (header-only, dev only) |
| CRT | Static (`/MT`, `/MTd` in debug) |

Dependency policy: the only third-party libraries allowed are `toml++` and `doctest`. **Ask before adding anything else.** Prefer the standard library and Windows APIs. Use `wil` or `Microsoft::WRL::ComPtr` for COM and handle RAII only if you decide it is worth it, and ask first.

## 3. Project layout

```
tapir/
├─ CMakeLists.txt
├─ AGENTS.md
├─ resources/
│  ├─ app.manifest          # PerMonitorV2 DPI, UTF-8 code page, longPathAware
│  └─ app.rc
├─ config/
│  └─ config.toml           # bundled default config
├─ src/
│  ├─ main.cpp              # wWinMain, single instance, message loop
│  ├─ app.{h,cpp}           # wiring, modes, state
│  ├─ hotkey.{h,cpp}
│  ├─ window.{h,cpp}        # popup window, show/hide, focus handling, DWM backdrop and corners
│  ├─ gfx.{h,cpp}           # D3D11 device, swap chain, DirectComposition, Trim on hide
│  ├─ render.{h,cpp}        # Direct2D/DirectWrite drawing helpers and screens (from plain data)
│  ├─ anim.{h,cpp}          # easing and animation timeline (pure logic where possible)
│  ├─ input.{h,cpp}         # single-line text field logic (caret, selection, editing)
│  ├─ index.{h,cpp}         # app discovery, refresh, icon cache
│  ├─ fuzzy.{h,cpp}         # scoring, pure functions
│  ├─ clipboard.{h,cpp}     # listener, history store, paste
│  ├─ config.{h,cpp}
│  └─ util/                 # string conversion, RAII wrappers, logging
└─ tests/                   # fuzzy, input, history, config tests
```

Rules:

- `fuzzy`, `input` (editing logic), history logic, and `config` parsing must be **pure and testable**: no Win32 calls inside them.
- `render` only draws from plain data. It never searches, reads files, or touches the clipboard.
- `anim` easing and timeline math is pure and unit-tested; only the frame timer lives in `window`.
- All Win32/COM calls are confined to `window`, `gfx`, `render`, `hotkey`, `index`, `clipboard`, and `util`.

## 4. UI specification

**Reference: PowerToys Command Palette.** Match its Fluent-style look and keyboard-first feel: a frosted, rounded popup with a large search box, a list of icon rows, and a command bar at the bottom. The goal is the same polish at a fraction of the weight, with only two modes (Apps and Clipboard).

"Fancy" must come from the Windows compositor and short animations, not from heavy code or assets. If an effect costs idle CPU, GPU, or memory, do not use it.

### 4.1 Layout

```
┌──────────────────────────────────────────────┐
│ [icon]  Search apps...                       │  search row
├──────────────────────────────────────────────┤
│ Top result                                   │  optional section header
│ ▌[ic] Visual Studio Code            App      │  selected row
│   [ic] VS Code Insiders             App      │
│   [ic] Visual Studio 2022           App      │
├──────────────────────────────────────────────┤
│ Apps · Clipboard (Tab)    [Enter] Open  [Ctrl+K] More │  command bar
└──────────────────────────────────────────────┘
```

- **Window:** borderless, topmost, `WS_EX_TOOLWINDOW`, about 680 DIP wide (configurable), horizontally centered on the monitor chosen by config (default: monitor with the cursor), placed in the upper third.
- **Search row:** about 56 DIP tall. Large input text (18 to 20 DIP), muted placeholder (`Search apps...` or `Search clipboard...`), a mode glyph on the left.
- **Rows:** about 48 DIP tall. Icon 32 DIP, title 14 DIP, subtitle 12 DIP in the muted color, a small right-aligned type label.
- **Selected row:** rounded rectangle (radius about 6 DIP) filled with the accent color at low alpha, plus a thin accent pill (about 3 DIP wide) on the left edge, like Fluent list selection. **Hover:** a subtler fill of the same shape.
- **Section header (optional):** small muted label such as `Top result` or `Recent` (shown when the query is empty).
- **Empty state:** centered muted `No results`.
- **Command bar (footer):** about 40 DIP tall. Left: current mode and the `Tab` hint. Right: the primary action (`Enter  Open` or `Enter  Paste`) and `Ctrl+K  More`. Keys are drawn as small rounded key-cap rectangles.
- **Details pane (optional, later):** a right-hand pane for clipboard mode that shows the full text of the selected entry. Off by default, milestone 7.
- **Compact mode:** with `compact = true` (default), an empty query shows only the search row, like Spotlight. The window animates open to show results on the first keystroke and collapses when the query is cleared. In clipboard mode the list shows immediately.
- **Icons:** app icons come from the shell (`IShellItemImageFactory`). UI glyphs (search, clipboard, pin) come from the **Segoe Fluent Icons** font, falling back to Segoe MDL2 Assets on Windows 10. **Ship no image assets for UI glyphs.**
- **Typography:** Segoe UI Variable, falling back to Segoe UI. Use DirectWrite text layouts cached per row, and clip long text with an ellipsis. Matched characters in titles are drawn in the accent color and semibold. Show dimmed **ghost text** for the likely completion in the search box.

### 4.2 Backdrop and theme

- On **Windows 11**, enable the DWM backdrop: `DwmSetWindowAttribute` with `DWMWA_SYSTEMBACKDROP_TYPE` set to acrylic for the transient popup, and `DWMWA_WINDOW_CORNER_PREFERENCE` set to round. The compositor draws blur, corners, and shadow, so this costs almost nothing in our process.
- Draw the theme background as a **tint** over the backdrop, with configurable opacity (`backdrop_opacity`).
- On **Windows 10**, if backdrop setup fails, or in **high-contrast mode**, fall back to a solid theme background with the same layout and system colors. Never leave a broken or fully transparent window.
- Config: `backdrop = "acrylic" | "mica" | "none"`.
- Theme colors come only from config: `background`, `foreground`, `accent`, `muted`, `surface`, `selection`. Default theme: `#2b2b2b` base with Catppuccin Mocha text, accent, muted and surface (`#cdd6f4`, `#cba6f7`, `#6c7086`, `#313244`). Never hardcode a color in the renderer.

### 4.3 Rendering architecture

- Direct3D 11 device (hardware, falling back to WARP if unavailable) -> DXGI flip-model swap chain for composition with premultiplied alpha -> Direct2D device context drawing into the swap chain surface -> DirectComposition visual attached to the window (`WS_EX_NOREDIRECTIONBITMAP`).
- Create the device, swap chain, brushes, and text formats **once at startup**. Show and hide the window only.
- **Render on demand only.** No render loop at idle. A frame is drawn when input, data, or an animation changes something.
- When the window is hidden, call `IDXGIDevice3::Trim()` and drop non-essential caches (keep the app icon cache within a size limit) so idle GPU and system memory stay low. Report the measured numbers.
- Use Direct2D effects (blur, glow) only if measured cost is negligible. Prefer the DWM backdrop for any blur.
- Keep drawing helpers small and reusable: rounded card, selection pill, key cap, icon tile, text with ellipsis, animation helper. Build these **before** the screens.

### 4.4 Motion

| Motion | Duration | Notes |
|---|---|---|
| Show | about 120 ms | Fade in plus a slight rise (about 6 DIP), ease-out |
| Selection highlight | about 100 ms | The pill slides between rows, ease-out |
| Compact expand / collapse | about 140 ms | Animate window height |
| Hide | none | Dismiss instantly |

- Animations run on a frame timer **only while active**, then the timer stops.
- Disable all motion when `animations = false` in config, or when Windows has client-area animations off (`SPI_GETCLIENTAREAANIMATION`).
- Nothing animates continuously (no pulsing, no looping gradients).
- The first frame after the hotkey must already contain the full content. Animations must never delay input.

### 4.5 Input and interaction

- Mouse: hover highlight, click to activate, wheel to scroll.
- The popup must be fully usable by keyboard alone.
- Accessibility (UI Automation) is not required for v1, but keep the UI structured as a list of items so it can be added later. Respect high-contrast mode.

**Keyboard map** (all configurable later, these are the defaults):

| Key | Action |
|---|---|
| Hotkey (default `Ctrl+Space`) | Toggle the popup |
| Type | Filter results |
| Up / Down, Page Up / Page Down | Move selection (Home / End stay with the text field caret) |
| Enter | Launch app, or paste the selected clipboard item |
| Shift+Enter | Clipboard mode: copy only, do not paste |
| Ctrl+K | Open the **More actions** menu for the selected item |
| Tab | Switch between Apps and Clipboard |
| Ctrl+P | Pin or unpin the selected clipboard item |
| Delete | Remove the selected clipboard item |
| Esc | Close the More menu if open, else clear the query if not empty, else hide the popup |
| Focus loss | Hide the popup |

**More actions menu (`Ctrl+K`)**: a small filterable list, fixed and minimal:

- Apps: Open, Run as administrator, Open file location, Copy path
- Clipboard: Paste, Copy only, Pin / Unpin, Delete

Do not add other actions without asking.

## 5. Window and focus behavior (get this right)

- Create the popup window **once** at startup and only show and hide it. Never destroy and recreate it.
- Pre-create all Direct2D and DirectWrite resources at startup.
- On hotkey press, **before showing the popup**, store `GetForegroundWindow()` as the previous window. This is used for pasting.
- `SetForegroundWindow` is restricted by Windows. Verify the popup actually receives keyboard focus in testing, and use a documented workaround if needed (for example, attaching input threads) with a comment explaining why.
- Hide on `WM_ACTIVATE` deactivation and on `Esc`.
- Single instance via a named mutex. A second launch signals the first to show itself, then exits.
- Declare per-monitor v2 DPI awareness in the manifest. Do layout in DIPs and handle `WM_DPICHANGED`.
- Multi-monitor: show on the monitor containing the cursor (or the active window; make it configurable).
- If `RegisterHotKey` fails (key already taken), show a clear message once and let the user pick another key in config. Do not crash.

## 6. Fuzzy search

- Index entries store the display name, a **precomputed lowercase copy**, the launch target, and a launch count.
- Matching is subsequence-based with scoring. Prefer:
  - strong bonus for a match at the start of the string and at word starts (after space, `-`, `_`, `.`, or a camelCase boundary)
  - bonus for consecutive matched characters
  - penalty for gaps between matches and for late first matches
  - exact and prefix matches rank first
  - a modest boost from launch count, so frequently used apps float up
- Case-insensitive by default, smart-case optional. Support Unicode via UTF-16 case folding at the boundary; keep an ASCII fast path.
- **Performance target:** under 1 ms for a few thousand entries on a typical laptop. No heap allocation per keystroke in the hot path: reuse buffers and keep the entries in contiguous arrays.
- Keep scoring as a pure function with unit tests: ranking examples (`"vsc"` should rank Visual Studio Code above Visual Studio 2022), edge cases (empty query, single character, no match, Unicode names).

**App index**

- Build the index in a background thread at startup, then swap it in atomically. The UI must never block on indexing.
- Sources: shell `AppsFolder` and Start Menu `.lnk` files (user and all-users). Dedupe by target.
- Refresh with `ReadDirectoryChangesW` on the Start Menu folders, with debouncing. A manual rescan option is acceptable.
- Launch with `ShellExecuteEx` (or `IApplicationActivationManager` for UWP). Update and save the launch count.
- Load icons lazily for visible rows only, and cache them. Never load every icon at startup.

## 7. Clipboard history

- Capture with `AddClipboardFormatListener` (`WM_CLIPBOARDUPDATE`). Never poll.
- Start with **text only** (`CF_UNICODETEXT`). Images and files are out of scope for v1.
- Opening the clipboard can fail when another app holds it: retry a few times with a short delay and a timeout, then skip.
- Dedupe consecutive identical entries and move repeated copies to the top. Cap history (default 500 entries) and cap per-entry size (default 1 MB for text, truncated or skipped beyond that).
- Support **pinned** entries that are exempt from the cap and from clearing.
- Searching clipboard history uses the same fuzzy scorer.
- **Paste flow:** hide the popup, restore focus to the stored previous window, set the clipboard content (marking it so our own listener ignores the update), wait for focus to settle, then send `Ctrl+V` with `SendInput`. Handle failure to restore focus by leaving the item on the clipboard and doing nothing else.
- **Do not capture sensitive copies.** Skip any clipboard update that carries `ExcludeClipboardContentFromMonitorProcessing` or `CanIncludeInClipboardHistory` set to disallow (password managers set these registered formats). Support an ignore-list of process names in config.
- History stays **local**. No network code anywhere in this project.

## 8. Configuration and storage

- Config directory: `%USERPROFILE%\.config\tapir\` (the same convention as the user's other tools; keep the path logic in one function so it is easy to change). Allow a `TAPIR_CONFIG_DIR` environment variable override for testing.
- Files: `config.toml` (settings) and `history.dat` (clipboard history). On first run create the folder and a commented default `config.toml`. **Never overwrite an existing config.**
- Config keys: hotkey, popup monitor, position and width, `compact` (default true), `backdrop` (`acrylic` | `mica` | `none`), `backdrop_opacity`, `animations` (default true), font, theme colors, max visible rows, history size, per-entry size limit, ignore-list, app index sources, `encrypt_history`.
- Hot reload config on change (debounced). Invalid TOML keeps the last good config and logs an error. Missing keys use defaults. Unknown keys are warnings.
- History file: protect with DPAPI by default (`encrypt_history = true`). Write atomically (write to a temp file, then replace) and debounce writes so rapid copying does not thrash the disk. A corrupt or unreadable history file must never crash the app: back it up and start fresh.
- Never store secrets in config. Never log clipboard contents.

## 9. Performance and size budgets

| Metric | Target |
|---|---|
| Hotkey to fully drawn popup | under 50 ms, ideally one frame |
| Keystroke to updated results | under 16 ms |
| Idle CPU | about 0% (no frame loop when nothing changes) |
| Animation | each under about 150 ms, frame timer runs only while animating, smooth at the display refresh rate |
| Hidden state | GPU and cache memory trimmed (`IDXGIDevice3::Trim`), measure and report |
| Idle working set | under 15 MiB (measure and report) |
| Executable size | under about 2 MB |
| Startup to hotkey ready | under 500 ms (index builds in the background) |

If a change risks a budget, measure before and after and report the numbers.

## 10. Milestones (one at a time, in this order)

1. **Skeleton.** CMake project, manifest, `wWinMain`, single instance, hidden popup window toggled by the hotkey.
   *Done when:* the hotkey shows and hides a blank, correctly sized, DPI-aware window instantly, and `Esc` or clicking away hides it.
2. **Rendering foundation and search row.** D3D11 + DXGI swap chain + DirectComposition + Direct2D/DirectWrite, DWM acrylic backdrop and rounded corners with solid fallback (Windows 10 and high-contrast), drawing helpers (rounded card, selection pill, key cap, text with ellipsis), `anim` easing, show animation, theme from defaults, single-line text field (caret, selection, copy/paste, Home/End, Ctrl+arrows, backspace repeat), ghost text.
   *Done when:* the popup appears as a frosted rounded window with the search box fully drawn on the first frame, typing and editing work, idle CPU is about 0%, and the fallback look works when the backdrop is unavailable.
3. **App index, fuzzy search, and the results list.** Background indexing, scorer with tests, result rows (icon, title, subtitle, matched-letter highlight), selection pill with slide animation, hover state, command bar footer, compact mode (search row only until the first keystroke, animated expand and collapse), launching apps.
   *Done when:* typing a few letters finds and launches the right app, the list and command bar match section 4, and scoring tests pass.
4. **Ranking and icons.** Launch counts persisted, boosted ranking, lazy icon loading and cache.
   *Done when:* frequently launched apps rise in results and scrolling does not stutter.
5. **Clipboard history.** Listener, history store, Apps/Clipboard mode switch, search, paste flow, privacy format checks, ignore-list.
   *Done when:* copying in other apps fills history, selecting an item pastes into the previously focused window, and password-manager copies are not stored.
6. **Config, theme, hot reload.** TOML config in the config dir, Catppuccin Mocha default, pin/delete/clear, encrypted history file.
   *Done when:* editing `config.toml` changes behavior without restart and a broken file does not break the app.
7. **Polish.** `Ctrl+K` More actions menu, optional clipboard details pane, multi-monitor placement, startup-with-Windows option, high-DPI checks, budget measurements, release build size check, README.

## 11. How to work

- One milestone at a time. Do not start the next until the current one meets its "done when".
- Small, reviewable commits with clear messages.
- Modern C++: RAII for every handle, COM pointer, and GDI/D2D resource. No raw `new`/`delete`, no manual `CloseHandle` scattered in code. Prefer `std::string_view`, `std::span`, `std::optional`, and `std::expected`-style error returns (or a small `Result` type) over exceptions in hot paths.
- Use wide (UTF-16) APIs at the Win32 boundary and UTF-8 internally if you prefer, with conversion in one place (`util`).
- Compile with `/W4 /WX` and a `/permissive-` conformance mode. Fix warnings, do not suppress them without a comment.
- Run AddressSanitizer and UBSan builds (where MSVC supports them) and fix what they find. Run `clang-format` and `clang-tidy` if configured.
- Add unit tests for fuzzy scoring, text input logic, history dedupe/caps, and config parsing. Win32 behavior (hotkey, focus, paste) gets a short manual test checklist in the PR description.
- When something is ambiguous or needs a decision (new dependency, behavior that changes the spec, anything touching privacy), **ask instead of guessing**.
- Do not refactor unrelated code or add features outside this document.
- Release build: `/O2`, `/GL` with `/LTCG`, static CRT, strip debug info into a separate PDB. Report binary size at the end of each milestone.
- At the end of each task report: what changed, what you tested, measured numbers where relevant, what is left, and any assumptions.

## 12. Definition of done (v0.1)

- Runs on Windows 10 and 11 with correct DPI scaling on multiple monitors.
- Hotkey popup with instant response, fuzzy app search, and launching.
- Clipboard history with search, pinning, deleting, and reliable paste into the previous app.
- Sensitive clipboard content from password managers is not stored, and history is encrypted at rest by default.
- No network access, no telemetry, no background services.
- Looks and feels like PowerToys Command Palette (frosted rounded popup, command bar, short smooth animations) with a plain solid fallback on Windows 10 and in high-contrast mode.
- Meets the budgets in section 9.

## Agent skills

### Issue tracker

Issues and specs live in GitHub Issues for `hamzahraihan/tapir` (gh CLI). See `docs/agents/issue-tracker.md`.

### Triage labels

The five triage roles use `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

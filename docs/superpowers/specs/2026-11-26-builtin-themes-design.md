# Built-in color themes with dark and light modes

Date: 2026-11-26
Status: design approved by the user

## Problem

tapir ships one hardcoded Catppuccin-derived palette. There is no way to switch the whole look without hand-editing six color keys. The user wants five built-in families — Nord, Catppuccin, Kanagawa, Material and Gruvbox — each with a dark and a light palette.

## Goal and success criteria

- Ten named palettes are available: five families x dark/light.
- A user selects one through `theme.preset` and `theme.mode` in config.toml or through two controls in the Settings Appearance tab.
- Explicit color keys in config.toml still override the chosen preset, so existing configurations keep their exact appearance.
- Palette lookup and resolution are pure and unit-tested; no Win32 dependency in the palette module.
- No new dependency, no new file IO, no change to rendering internals, high-contrast handling, or any non-theme behaviour.
- Debug tests pass, Release builds with `/W4 /WX`, and switching a preset visibly changes the popup without a restart.

## Non-goals

Per-theme backdrop, font or opacity; automatic Windows light/dark following; user-defined themes loaded from files; editing or deleting preset colors; changing the default palette's rendered values; any change to ranking, clipboard, icons, layout or animation.

## Palette data

Roles are `background`, `foreground`, `accent`, `muted`, `surface` and `selection`. Values are drawn from each family's published role mapping, with the accent set to the family's signature hue and selection to its elevated surface.

| Family | Mode | background | foreground | accent | muted | surface | selection |
|---|---|---|---|---|---|---|---|
| nord | dark | #2e3440 | #d8dee9 | #88c0d0 | #a0a8bd | #3b4252 | #434c5e |
| nord | light | #eceff4 | #2e3440 | #5e81ac | #4c566a | #e5e9f0 | #d8dee9 |
| catppuccin | dark | #2b2b2b | #cdd6f4 | #cba6f7 | #a6adc8 | #313244 | #45475a |
| catppuccin | light | #eff1f5 | #4c4f69 | #8839ef | #5c5f85 | #e6e9ef | #ccd0da |
| kanagawa | dark | #1f1f28 | #dcd7ba | #7e9cd8 | #a3a08f | #2a2a37 | #223249 |
| kanagawa | light | #f2ecbc | #545464 | #624c83 | #666258 | #dcd5ac | #c7d7e0 |
| material | dark | #212121 | #eeffff | #82aaff | #78909c | #292929 | #3c4452 |
| material | light | #fafafa | #37474f | #7c4dff | #546e7a | #f0f0f0 | #d7dce0 |
| gruvbox | dark | #282828 | #ebdbb2 | #d3869b | #a89984 | #3c3836 | #504945 |
| gruvbox | light | #fbf1c7 | #3c3836 | #af3a03 | #665c54 | #ebdbb2 | #d5c4a1 |

The muted values were raised from the raw upstream comment shades after visual QA measured several below WCAG AA (Gruvbox light 2.45:1, Catppuccin light 3.49:1, Nord dark 1.69:1). Each replacement is a lighter or darker shade from the same family and clears 4.5:1 against its background; a unit test enforces this.

`catppuccin` dark deliberately keeps #2b2b2b as its background, matching the current default described in `AGENTS.md:4.2`, so switching to that preset does not change today's look. Kanagawa light uses the upstream Lotus variant.

## Design

### Palette module

Add the pure header `src/themes.h`. It includes `config.h` for `Rgb` and defines `ThemeMode { Dark, Light }`, `ThemePalette` (six `Rgb` roles), `builtinPresetNames()` returning the five names in display order, and `builtinPalette(std::wstring_view preset, ThemeMode mode)` returning `std::optional<ThemePalette>`. All data is `constexpr`; there are no Win32 calls, no allocation in lookup, and comparison is case-insensitive to match existing enum parsing.

### Config model and parsing

`Settings::Theme` gains `std::wstring preset = L"catppuccin"` and `ThemeMode mode = ThemeMode::Dark`; `operator==` continues to compare all fields by default. `config::parseTheme` reads `theme.preset` and `theme.mode` first, applies `builtinPalette`, then reads the six color keys into the same struct so they override. An invalid preset or mode warns and leaves the default value, as other invalid enum values already do. `checkUnknownKeys` gains `preset` and `mode`.

`config::defaultText()` adds `preset = "catppuccin"` and `mode = "dark"` under `[theme]`, keeps the existing six color lines unchanged, and documents the two new keys in the same commented style. This preserves the rendered default for both existing and new config files.

### Settings UI

Append two fields to `kFields` in `src/settings_ui.cpp` with tab 1 (Appearance): `theme.preset` and `theme.mode`, both `Kind::Cycle`. Appending avoids renumbering the existing hardcoded `values[]` indices. `setValues` fills index 24 with the preset name and index 25 with `dark`/`light`. `cycleOptions` returns the five preset names or `dark`/`light`. Cycling either field refreshes `values[7..12]` (the six color strings) from `builtinPalette`, so pressing OK writes the selected palette into config.toml via the existing `config_writer` path.

### Data flow

Config watcher already detects config.toml changes and posts a reload message; `window::applySettings` already calls `gfx::applySettings`, which rebuilds brushes. No new message, timer or render path is added.

## Testing

- `tests/themes.test.cpp` is added to the `tapir_tests` target in `CMakeLists.txt`: all ten presets resolve; names list has five entries; invalid preset returns `nullopt`; dark and light differ for every family; each palette has six distinct role values where the upstream roles differ.
- `tests/config.test.cpp`: parse `preset`/`mode`, confirm resolved colors, confirm an explicit color key overrides the preset, and confirm an unknown preset warns without invalidating the file.
- Build and tests: Debug test target with all tests passing; Release target with no `/W4 /WX` warnings.
- Visual check: switch preset and mode in a temporary config, confirm the popup recolors, and confirm an existing config with explicit colors looks unchanged.

## Risks and decisions

- Some users may have hand-edited colors plus a preset key; explicit colors win by design, so their edits survive.
- Kanagawa Lotus accent uses `lotusViolet-4`; no other family uses that hue, so presets remain distinguishable.
- The Settings cycle refresh writes preset colors on OK; a user who wants a partial override can still edit an individual color afterward.
- No automatic system theme following: `theme.mode` is explicit, matching the config-only theme rule in `AGENTS.md:4.2`.

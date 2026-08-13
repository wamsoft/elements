# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Elements is a lightweight C++ GUI library originally by Joel de Guzman (cycfi). This fork has been **ported from Cairo to ThorVG** for rendering, with pluggable text/glyph backends.

**Current state:** Cairo 依存を完全除去。描画は ThorVG、テキスト描画は ThorVG (`tvg::Text`)、グリフレイアウトは FreeType + HarfBuzz 直結。ホスト層は Win32 と SDL3 の 2 系統。

## Build Commands

```bash
# Configure (uses CMake presets)
make prebuild                    # or: cmake --preset x64-windows

# Build (default: Release)
make build                       # or: make build BUILD_TYPE=Debug

# SDL host build
cmake --preset x64-windows -DELEMENTS_HOST_UI_LIBRARY=sdl

# Clean
make clean
```

The build uses Ninja Multi-Config generator. Build output goes to the path defined in CMakePresets.json.

## Architecture

### Rendering Pipeline (ThorVG port)

The key architectural change from the original:

- **Original:** `cairo_t*` flows from host → `view::draw(cairo_t*)` → `canvas{cairo_t&}` → element tree
- **Ported:** Host creates pixel buffer (DIB section on Windows) → `canvas{uint32_t* buf, w, h, scale}` → `view::draw(canvas&)` → element tree → `canvas::flush()` → BitBlt to screen

ThorVG uses **deferred rendering**: shapes are accumulated via `_tvg_canvas->add()`, then rendered in batch by `flush()` which calls `update() → draw() → sync()`. The canvas destructor auto-flushes.

### Key Layers

- **Host layer** (`lib/host/windows/` or `lib/host/sdl/`): Platform window management, event handling, paint loop. Win32 uses DIB section, SDL uses `SDL_Texture`. Selected via `ELEMENTS_HOST_UI_LIBRARY` CMake option.
- **View** (`lib/src/view.cpp`, `lib/include/elements/view.hpp`): Bridges host and element tree. Manages layout, event dispatch, undo/redo, async tasks via ASIO.
- **Canvas** (`lib/src/support/canvas.cpp`): ThorVG-based 2D drawing API wrapping `tvg::SwCanvas`. Creates shapes per fill/stroke operation.
- **Text backend** (`lib/src/support/text_backend_tvg.cpp`): Text rendering via the `text_backend` interface. The only implementation uses `tvg::Text`.
- **Element tree** (`lib/src/element/`, `lib/include/elements/element/`): ~57 UI element implementations. Each element has `limits()`, `layout()`, `draw()`, `click()` etc.
- **Glyph layout backend** (`lib/src/support/glyph_layout_ft.cpp`): Text shaping / metrics via the `glyph_layout_backend` interface. The only implementation uses HarfBuzz + FreeType directly.
- **Support** (`lib/src/support/`): Font management (requires explicit `register_font()` — no auto-discovery), glyphs, pixmap, theme, text utilities.

### Scratch Context

Off-screen measurement uses `detail::scratch_context` (small 4×4 ThorVG canvas) instead of Cairo recording surfaces. Used by `view::set_limits()` and `with_context_do()`.

### Dependencies

- **ThorVG** (git submodule at `external/thorvg`, branch: `cmake` of `wtnbgo/thorvg`): Vector graphics engine. Built via `add_subdirectory()` so local edits to the FT loader can flow back to the fork. Built with `TVG_LOADER_FT=ON` (FreeType + HarfBuzz multilingual loader); `TVG_LOADER_TTF=OFF`. Spec: `external/thorvg/README_ft_text.md`. Initialize with `git submodule update --init --recursive external/thorvg`.
- **FreeType** (vcpkg): Font loading and metrics.
- **HarfBuzz** (vcpkg): Used by ThorVG's FT loader. Plus HarfBuzz 13.1.1 via FetchContent for `glyph_layout_ft.cpp` (ICU disabled).
- **SDL3** (FetchContent, release-3.4.0): Used for SDL host layer (`ELEMENTS_HOST_UI_LIBRARY=sdl`).
- **cycfi/infra**: Utility library, fetched via FetchContent.
- **In-tree task queue** (`support/task_queue.{hpp,cpp}`): Tiny `post` / `post_after` / `poll` queue driven from the UI thread (`view::poll`). Replaces the old ASIO dependency so platforms without `socketpair`/`eventfd` (e.g. Nintendo Switch) can build.

### Text Backend Interface

Text rendering goes through the `text_backend` interface (`text_backend.hpp`). The only implementation lives in `text_backend_tvg.cpp` and uses `tvg::Text`. The interface is kept (rather than calling `tvg::Text` directly from `canvas`) so a specialized backend can be reintroduced later for narrow use cases (e.g. a text editor) without touching call sites. Backends access canvas state via `canvas::get_state()`, `canvas::flush_shapes()`, `canvas::make_clip_shape()`, `canvas::tvg_canvas()`.

### Glyph Layout Interface

Text shaping and font metrics go through the `glyph_layout_backend` and `font_backend` interfaces (`glyph_utils.hpp`). The only implementation uses HarfBuzz `hb_shape()` + `FT_Face` metrics directly. `glyphs.cpp` and `font.cpp` access fonts only through these interfaces.

### Block Text Interface (rectangle flow)

`master_glyphs::break_lines` is a width-greedy word wrap with no script awareness. A host that owns a full text engine usually has a better one *and* needs the two to agree — the same caption drawn by the host and by an Elements widget should break at the same places. So block layout goes through an injectable `block_text_backend` (`support/block_text.hpp`): the host calls `set_block_text_backend()`, and without one a built-in fallback uses Elements' own wrapping. The backend answers "where do the lines break, where does a `count` reveal cut, how tall is the block"; the glyph rendering stays on the normal `tvg::Text` path.

`block_text_box` (`element/block_text.hpp`, JSON `text_area` in elements_modal) is the widget on top of it: alignment, extra line spacing, base direction, and a cluster `count` limit for a typewriter reveal that never reflows (wrapping is resolved for the whole text before `count` applies). 吉里吉里Z injects a glyphware-backed implementation, so `text_area` and `Layer.drawShapedTextArea` wrap identically, 禁則 included. Spec: `docs/block-text.md`.

### ThorVG-Specific Quirks

- **Font name key**: ThorVG registers loaded fonts by filename stem (e.g., `"OpenSans-Regular"`), not by the font's internal family name. `canvas::font()` converts via `stem_from_path()`.
- **DPI compensation**: ThorVG internally applies 96/72 DPI factor to font metrics. `tvg_font_scale = 72.0f/96.0f` is applied to all `text->size()` calls to match Cairo's user-space convention.
- **pixmap scale convention**: Cairo uses `device_scale = 1/scale`, so `pixmap::size() = pixels * scale`, not `pixels / scale`.
- **Dirty region**: `EngineOption::None` is required to prevent ThorVG from clearing glyph regions with black.

### Keyboard Navigation

All interactive widgets are keyboard-operable (not just text boxes). Tab/Shift+Tab cycles focus (wraps at ends); Space/Enter activates buttons; arrows adjust the focused slider/dial/thumbwheel on their primary axis. `view::arrow_focus_navigation(true)` enables an opt-in 2D directional focus mode where unconsumed arrows move focus to the nearest widget in that direction (value adjustment wins over navigation). Initial focus can be set with `view::focus(element_ptr)` or the declarative `initial_focus(...)` wrapper from `<elements/element/focus.hpp>`. Spec: `docs/keyboard-navigation.md`. Reference example: `examples/key_driven/`.

### Gamepad Support (SDL3 host only)

Gamepad input piggybacks on the keyboard plumbing. Discrete buttons map to synthesized key events via a per-view binding table (`view::bind_pad_button`); defaults are A=Enter, B=Esc, X=Shift+Tab, Y=Tab. D-Pad and analog sticks feed an axis-mode machinery (`view::dpad_mode` / `left_stick_mode` / `right_stick_mode` / `trigger_mode`) where each axis group can be set to `focus` (threshold-triggered arrow synth with auto-repeat), `value` (continuous dispatch to focused widget via `element::pad_axis(info)`), `both` (value first, focus as fallback), or `disabled`. Shortcut registry (`view::bind_shortcut`) works uniformly for keys and pad buttons and is suppressed while a text-editing widget holds focus (override the `consumes_text()` virtual on element). The SDL3 host initializes `SDL_INIT_GAMEPAD`, auto-opens gamepads on `SDL_EVENT_GAMEPAD_ADDED`, and routes button/axis events to whichever view currently holds SDL input focus. Spec: `docs/gamepad-support.md`. Same example as keyboard: `examples/key_driven/`.

### Multilingual Text (FT loader)

The FT loader gives per-codepoint fallback (load order = priority), HarfBuzz shaping, mixed-UPM normalization, and BCP47 locale tags. Plumbing:
- `canvas_state::text_locale` carries a BCP47 tag (e.g. `"ja-JP"`, `"zh-TW"`). Saved/restored by `canvas::save()/restore()`.
- `canvas::text_locale(string)` setter; `text_backend_tvg.cpp` calls `tvg::Text::locale()` in fill/stroke/measure paths when non-empty.
- `default_label_styler::get_text_locale()` virtual; `label_styler_with_locale` + `label::locale("ja-JP")` chainable builder.
- Color emoji (sbix/CBDT/COLRv1) is intentionally out of scope. Monochrome emoji (e.g. Noto Emoji) works via outline extraction.
- Reference example: `examples/multilingual_text/`.

### Known Remaining Issues

- `glyphs::for_each()` has O(n²) byte offset recalculation
- No system font discovery (e.g., "Segoe UI Symbol"); `load_fonts_from_directory()` scans `resources/` at startup

## Code Conventions

- C++20 standard required
- Namespace: `cycfi::elements`
- MSVC builds use `/utf-8` and `/Zc:__cplusplus`
- Windows defines: `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_UNICODE`, `ELEMENTS_HOST_UI_LIBRARY_WIN32` (or `ELEMENTS_HOST_UI_LIBRARY_SDL`)
- Example apps use `ElementsConfigApp.cmake` — set `ELEMENTS_APP_PROJECT`, `ELEMENTS_APP_SOURCES`, then `include(ElementsConfigApp)`
- Host UI library: `ELEMENTS_HOST_UI_LIBRARY` CMake option (`win32` default, or `sdl`)

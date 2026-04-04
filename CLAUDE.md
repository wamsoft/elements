# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Elements is a lightweight C++ GUI library originally by Joel de Guzman (cycfi). This fork is being **ported from Cairo to ThorVG + richtext** for rendering. The original (unmodified) source is at `../elemets_orig/` and the richtext library is at `../richtext/`.

**Current state:** Windows host layer, canvas, font, glyphs, and pixmap have been rewritten for ThorVG. Other platforms (GTK, macOS) are removed; eventual plan is SDL unification.

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

# Download test fonts
make fontdata
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
- **Text backends** (`lib/src/support/text_backend_tvg.cpp`, `text_backend_richtext.cpp`): Pluggable text rendering via `text_backend` interface. Default is ThorVG. richtext backend available via `create_richtext_text_backend()`.
- **Element tree** (`lib/src/element/`, `lib/include/elements/element/`): ~57 UI element implementations. Each element has `limits()`, `layout()`, `draw()`, `click()` etc.
- **Glyph layout backends** (`lib/src/support/glyph_layout_ft.cpp`, `glyph_layout_richtext.cpp`): Pluggable text shaping/metrics via `glyph_layout_backend` interface. Default is FreeType+HarfBuzz. richtext backend available via `create_richtext_glyph_layout_backend()`.
- **Support** (`lib/src/support/`): Font management (requires explicit `register_font()` — no auto-discovery), glyphs, pixmap, theme, text utilities.

### Scratch Context

Off-screen measurement uses `detail::scratch_context` (small 4×4 ThorVG canvas) instead of Cairo recording surfaces. Used by `view::set_limits()` and `with_context_do()`.

### Dependencies

When `ELEMENTS_USE_RICHTEXT=OFF` (default):
- **ThorVG** (FetchContent from `wtnbgo/thorvg` cmake branch): Vector graphics engine.
- **FreeType** (vcpkg): Font loading and metrics.
- **HarfBuzz 13.1.1** (FetchContent, ICU disabled): Text shaping.

When `ELEMENTS_USE_RICHTEXT=ON`:
- **richtext** (FetchContent from `wamsoft/richtext`): Includes ThorVG, minikin, HarfBuzz, FreeType.

Common:
- **SDL3** (FetchContent, release-3.4.0): Used for SDL host layer (`ELEMENTS_HOST_UI_LIBRARY=sdl`).
- **cycfi/infra**: Utility library, fetched via FetchContent.
- **ASIO**: Async I/O for timers/callbacks, fetched via FetchContent.

### Text Backend Plugin System

Text rendering is abstracted via `text_backend` interface (`text_backend.hpp`):
- **ThorVG backend** (default): Uses `tvg::Text` for rendering. Always available.
- **richtext backend**: Uses richtext `GlyphRenderer` for glyph-level vector rendering. Available via `create_richtext_text_backend()` from `text_backend_richtext.hpp`.
- Switch at runtime: `canvas::set_text_backend(create_richtext_text_backend())`
- Backends access canvas state via `canvas::get_state()`, `canvas::flush_shapes()`, `canvas::make_clip_shape()`, `canvas::tvg_canvas()`.

### Glyph Layout Plugin System

Text shaping and font metrics are abstracted via `glyph_layout_backend` and `font_backend` interfaces (`glyph_utils.hpp`):
- **FreeType+HarfBuzz backend** (default): Direct `hb_shape()` + `FT_Face` metrics. No minikin/richtext dependency.
- **richtext backend**: Uses richtext `TextLayout`/minikin for shaping. Available via `create_richtext_glyph_layout_backend()` from `glyph_utils_richtext.hpp`.
- Switch at runtime: `set_glyph_layout_backend(create_richtext_glyph_layout_backend())` and `set_font_backend(create_richtext_font_backend())`
- `glyphs.cpp` and `font.cpp` have no direct richtext dependency — all access goes through the backend interfaces.

### ThorVG-Specific Quirks

- **Font name key**: ThorVG registers loaded fonts by filename stem (e.g., `"OpenSans-Regular"`), not by the font's internal family name. `canvas::font()` converts via `stem_from_path()`.
- **DPI compensation**: ThorVG internally applies 96/72 DPI factor to font metrics. `tvg_font_scale = 72.0f/96.0f` is applied to all `text->size()` calls to match Cairo's user-space convention.
- **pixmap scale convention**: Cairo uses `device_scale = 1/scale`, so `pixmap::size() = pixels * scale`, not `pixels / scale`.
- **Dirty region**: `EngineOption::None` is required to prevent ThorVG from clearing glyph regions with black.

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

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Elements is a lightweight C++ GUI library originally by Joel de Guzman (cycfi). This fork is being **ported from Cairo to ThorVG + richtext** for rendering. The original (unmodified) source is at `../elemets_orig/` and the richtext library is at `../richtext/`.

**Current state:** Windows host layer, canvas, font, glyphs, and pixmap have been rewritten for ThorVG. Other platforms (GTK, macOS) are removed; eventual plan is SDL unification.

## Build Commands

```bash
# Configure (uses CMake presets, default: x64-windows)
make prebuild                    # or: cmake --preset x64-windows

# Build (default: Release)
make build                       # or: make build BUILD_TYPE=Debug

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

- **Host layer** (`lib/host/windows/`): Win32 window management, event handling, paint loop. `base_view.cpp` owns the rendering surface (DIB section + pixel buffer).
- **View** (`lib/src/view.cpp`, `lib/include/elements/view.hpp`): Bridges host and element tree. Manages layout, event dispatch, undo/redo, async tasks via ASIO.
- **Canvas** (`lib/src/support/canvas.cpp`): ThorVG-based 2D drawing API wrapping `tvg::SwCanvas`. Creates shapes per fill/stroke operation.
- **Element tree** (`lib/src/element/`, `lib/include/elements/element/`): ~57 UI element implementations. Each element has `limits()`, `layout()`, `draw()`, `click()` etc.
- **Support** (`lib/src/support/`): Font management (requires explicit `register_font()` — no auto-discovery), glyphs, pixmap, theme, text utilities.

### Scratch Context

Off-screen measurement uses `detail::scratch_context` (small 4×4 ThorVG canvas) instead of Cairo recording surfaces. Used by `view::set_limits()` and `with_context_do()`.

### Dependencies

- **richtext** (at `../../richtext` relative to `lib/`): Provides ThorVG, minikin (text layout), FreeType. Built as subdirectory via CMake.
- **cycfi/infra**: Utility library, fetched via FetchContent.
- **ASIO**: Async I/O for timers/callbacks, fetched via FetchContent.

### ThorVG-Specific Quirks

- **Font name key**: ThorVG registers loaded fonts by filename stem (e.g., `"OpenSans-Regular"`), not by the font's internal family name. `canvas::font()` converts via `stem_from_path()`.
- **DPI compensation**: ThorVG internally applies 96/72 DPI factor to font metrics. `tvg_font_scale = 72.0f/96.0f` is applied to all `text->size()` calls to match Cairo's user-space convention.
- **pixmap scale convention**: Cairo uses `device_scale = 1/scale`, so `pixmap::size() = pixels * scale`, not `pixels / scale`.
- **Dirty region**: `EngineOption::None` is required to prevent ThorVG from clearing glyph regions with black.

### Known Porting Issues

- `canvas::draw(pixmap, src, dest)` ignores the `src` rect parameter (no source cropping)
- Clip operations don't compose — only the last `clip()` is active (Cairo maintained a clip stack)
- `glyphs::for_each()` has O(n²) byte offset recalculation
- No system font discovery (e.g., "Segoe UI Symbol"); `load_fonts_from_directory()` scans `resources/` at startup
- Sprite button images render as blue rectangles (pixmap drawing may have issues)

## Code Conventions

- C++20 standard required
- Namespace: `cycfi::elements`
- MSVC builds use `/utf-8` and `/Zc:__cplusplus`
- Windows defines: `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_UNICODE`, `ELEMENTS_HOST_UI_LIBRARY_WIN32`
- Example apps use `ElementsConfigApp.cmake` — set `ELEMENTS_APP_PROJECT`, `ELEMENTS_APP_SOURCES`, then `include(ElementsConfigApp)`

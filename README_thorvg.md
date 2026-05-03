# Elements ThorVG Port

This fork ports the [Elements](https://github.com/cycfi/elements) C++ GUI library from Cairo to ThorVG-based rendering, with a pluggable architecture for text rendering and glyph layout.

## Changes from Original

### Rendering Engine: Cairo → ThorVG

- **Canvas** (`lib/src/support/canvas.cpp`): Complete rewrite from `cairo_t*` to `tvg::SwCanvas`. Shapes are accumulated via deferred rendering and flushed in batch.
- **Pixmap** (`lib/src/support/pixmap.cpp`): Uses `tvg::Picture` instead of `cairo_surface_t`. Source rectangle cropping implemented via transform + clip.
- **Clip compositing**: Clips are intersected (bounding box) to support nested elements, matching Cairo's `cairo_clip()` behavior.
- **Gradient rendering**: Removed double-transform bug. Single-pass pre-blended gradient option for buttons.

### Host Layer

Two host layers are available, selected at build time via `ELEMENTS_HOST_UI_LIBRARY`:

| Host | Platform | Notes |
|------|----------|-------|
| **Win32** (default) | Windows | DIB section rendering, `WndProc` event dispatch |
| **SDL3** | Cross-platform | `SDL_Texture` rendering, SDL3 event loop, FetchContent (release-3.4.0) |

```bash
# Win32 (default)
cmake --preset x64-windows

# SDL3
cmake --preset x64-windows -DELEMENTS_HOST_UI_LIBRARY=sdl
```

### Text Rendering Backend

Text rendering goes through the `text_backend` interface (`text_backend.hpp`). The shipped implementation uses `tvg::Text` (`text_backend_tvg.cpp`). The interface is kept so a specialized backend can be reintroduced later for narrow use cases (e.g. a text editor) without touching call sites.

### Glyph Layout Backend

Text shaping and font metrics go through the `glyph_layout_backend` and `font_backend` interfaces (`glyph_utils.hpp`). The shipped implementation uses HarfBuzz `hb_shape()` + `FT_Face` metrics directly (`glyph_layout_ft.cpp`).

### Multilingual Text (ThorVG FT Loader)

ThorVG is built with the fork's **FreeType + HarfBuzz text loader** (`TVG_LOADER_FT=ON`) instead of the built-in minimal TTF reader (`TVG_LOADER_TTF=OFF`). This adds:

- **HarfBuzz shaping** — ligatures, kerning, complex scripts (Arabic, Indic), correct CJK glyph selection
- **Per-codepoint font fallback** — codepoints missing from the primary font are resolved against other registered fonts in load order; mixed-UPM fonts (e.g. NotoEmoji 2048 vs OpenSans 1000) are normalized
- **BCP47 locale tags** — `Text::locale("ja-JP")` triggers HarfBuzz `locl` GSUB feature for language-sensitive glyph variants
- **Wrap modes** — `Character`, `Word`, `Smart`, `Ellipsis` (full parity with the TTF loader)

See `build/x64-windows/_deps/thorvg-src/README_ft_text.md` for ThorVG-side spec. **Color emoji** (sbix/CBDT/COLRv1) is intentionally out of scope; monochrome emoji fonts (e.g. Noto Emoji) work via normal outline extraction.

#### Font registration and fallback

`load_fonts_from_directory()` is invoked automatically at app startup by both host layers. It scans `resources/fonts/` then `resources/`, and calls `tvg::Text::load()` on every `.ttf`/`.otf` it finds. **Load order = fallback priority** — primary fonts should sort before fallback fonts.

Drop additional fonts into `resources/fonts/` (or list them in the example's `ELEMENTS_APP_RESOURCES`) and they participate in fallback automatically. No per-string font switching is needed:

```cpp
label("Hi 👋  今日もコードを書こう 💻 🌸")
   .font(font_descr{"Open Sans"})   // primary
   .font_size(20);
// CJK → Noto Sans JP, emoji → Noto Emoji, all chosen per codepoint
```

#### Setting a locale

`canvas::text_locale()` and `label::locale()` plumb a BCP47 tag through to `tvg::Text::locale()`. The tag is part of the canvas state and participates in `save()`/`restore()`, so it does not leak across `new_state()` boundaries.

```cpp
// On a label (chains like other style methods)
label("直 骨 次 海")
   .font(font_descr{"Noto Sans JP"})
   .locale("ja-JP")              // Japanese glyph variants
   .font_size(20);

// Inside a custom draw
ctx.canvas.text_locale("zh-TW");
ctx.canvas.fill_text("直 骨 次 海", point{x, y});
```

The locale tag affects within-face glyph variant selection. Cross-face selection is governed by load order — to render the same codepoints with Simplified-Chinese vs Traditional-Chinese shapes, set both the primary font and the locale appropriately for each label.

#### Example

`examples/multilingual_text/` demonstrates per-codepoint fallback (English / Japanese / monochrome emoji) and a side-by-side JP / SC / TC comparison using `font_descr` + `.locale()`:

```bash
cmake --preset x64-windows
cmake --build build/x64-windows --config Debug --target MultilingualText
```

It bundles `NotoSansJP-Regular.otf`, `NotoSansSC-Regular.otf`, `NotoSansTC-Regular.otf`, and `NotoEmoji-Regular.ttf` via `ELEMENTS_APP_RESOURCES`.

### Build Configuration

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ELEMENTS_HOST_UI_LIBRARY` | `win32` | Host UI: `win32` or `sdl` |

#### Dependencies

- ThorVG — git submodule at `external/thorvg` (cmake branch of `wtnbgo/thorvg`). Built with `TVG_LOADER_FT=ON` (FreeType + HarfBuzz multilingual text loader); the built-in `TVG_LOADER_TTF` is off.
- FreeType — vcpkg
- HarfBuzz — vcpkg (used by ThorVG's FT loader). Plus HarfBuzz 13.1.1 via FetchContent for `glyph_layout_ft.cpp` (ICU disabled).

#### Build Commands

```bash
# Default (Win32 + ThorVG text + FT/HB glyphs)
cmake --preset x64-windows
cmake --build build/x64-windows/ --config Debug

# SDL3 host
cmake --preset x64-windows -DELEMENTS_HOST_UI_LIBRARY=sdl
cmake --build build/x64-windows/ --config Debug
```

### ThorVG-Specific Notes

- **Font name key**: ThorVG's `tvg::Text` registers fonts by filename stem (e.g., `"OpenSans-Regular"`). The ThorVG text backend converts via `stem_from_path()`.
- **DPI compensation**: ThorVG internally applies 96/72 DPI factor. `tvg_font_scale = 72.0f/96.0f` is applied in the ThorVG text backend.
- **Dirty region**: `EngineOption::None` is required to prevent ThorVG from clearing glyph regions with black.
- **pixmap scale**: Cairo convention `device_scale = 1/scale`, so `pixmap::size() = pixels * scale`.

### File Structure

```
lib/
├── host/
│   ├── windows/          # Win32 host (app, base_view, window, key, drag_and_drop)
│   └── sdl/              # SDL3 host (app, base_view, window)
├── include/elements/support/
│   ├── canvas.hpp         # ThorVG canvas wrapper
│   ├── text_backend.hpp   # Text rendering backend interface
│   └── glyph_utils.hpp    # Glyph layout backend interface
└── src/support/
    ├── canvas.cpp           # Canvas implementation
    ├── text_backend_tvg.cpp # ThorVG text backend
    ├── glyph_layout_ft.cpp  # FreeType+HarfBuzz glyph backend
    ├── font.cpp             # Font registration (backend-agnostic)
    └── glyphs.cpp           # Text layout/shaping (backend-agnostic)
```

/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_PAD_ICON_JUNE_12_2026)
#define ELEMENTS_PAD_ICON_JUNE_12_2026

#include <elements/element/element.hpp>
#include <elements/support/pixmap.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // pad_theme — which controller flavor to draw icons in.
   //   none     — no theme set; icons resolve to placeholder.
   //   xbox     — Xbox Series (also covers 360/One stylistically)
   //   ps       — PlayStation (PS3/4/5 common buttons)
   //   switch_  — Nintendo Switch
   //   keyboard — Keyboard & Mouse glyphs
   ////////////////////////////////////////////////////////////////////////////
   enum class pad_theme
   {
      none, xbox, ps, switch_, keyboard
   };

   pad_theme   parse_pad_theme(std::string_view name);
   const char* pad_theme_name(pad_theme t);

   pad_theme   get_pad_theme();
   void        set_pad_theme(pad_theme t);

   ////////////////////////////////////////////////////////////////////////////
   // Asset base directory — typically `<repo>/resources/kenny_input_prompts`
   // (or wherever the host has placed the Kenney pack). Set once at startup
   // before resolving any icons.
   ////////////////////////////////////////////////////////////////////////////
   void               set_pad_icon_base_dir(std::string path);
   const std::string& get_pad_icon_base_dir();

   ////////////////////////////////////////////////////////////////////////////
   // Register all 4 theme TTFs with the elements font system. Looks for files
   // under base_dir/<theme>/<font>.ttf using the Kenney pack convention.
   // No-op if base_dir is empty or files are missing for a theme; returns true
   // if at least one font was registered.
   ////////////////////////////////////////////////////////////////////////////
   bool load_pad_icon_fonts();

   ////////////////////////////////////////////////////////////////////////////
   // Lookups. Theme-aware — pass an explicit theme or use the current global.
   //
   // Accepted names (per theme):
   //   - Steam Input style logical names: face_south / face_east / face_west /
   //     face_north / dpad_up / dpad_down / dpad_left / dpad_right /
   //     lb / rb / lt / rt / lstick / rstick / lstick_press / rstick_press /
   //     start / back (or select) / home / share
   //   - Theme-native names:
   //       xbox:    a, b, x, y, view, menu, share, guide
   //       ps:      cross, circle, square, triangle, options, touchpad,
   //                playstation, l1, r1, l2, r2, l3, r3
   //       switch:  a, b, x, y, l, r, zl, zr, plus, minus, capture, home, sl, sr
   //       keyboard: keyboard_enter, keyboard_space, keyboard_escape,
   //                 keyboard_arrow_up/down/left/right, keyboard_a..keyboard_z,
   //                 keyboard_0..keyboard_9, ...
   //
   // resolve_pad_icon_svg_path: absolute path to the SVG, "" if not resolved.
   // resolve_pad_icon_codepoint: U+E0xx codepoint, 0 if not resolved.
   ////////////////////////////////////////////////////////////////////////////
   std::string   resolve_pad_icon_svg_path(std::string_view logical_name, pad_theme t);
   std::uint32_t resolve_pad_icon_codepoint(std::string_view logical_name, pad_theme t);

   inline std::string resolve_pad_icon_svg_path(std::string_view n)
   {
      return resolve_pad_icon_svg_path(n, get_pad_theme());
   }
   inline std::uint32_t resolve_pad_icon_codepoint(std::string_view n)
   {
      return resolve_pad_icon_codepoint(n, get_pad_theme());
   }

   // Font family name (as registered by load_pad_icon_fonts). Empty if not
   // available for this theme.
   std::string_view pad_icon_font_family(pad_theme t);
   inline std::string_view pad_icon_font_family()
   {
      return pad_icon_font_family(get_pad_theme());
   }

   ////////////////////////////////////////////////////////////////////////////
   // pad_icon — SVG-backed element.
   //   Lazily loads the SVG via tvg::Picture on first draw, caches the pixmap
   //   globally (keyed by absolute path) so repeated instances of the same
   //   icon share a single decoded picture. On resolve failure (missing
   //   theme, unknown name, or load error) draws a small gray rounded
   //   placeholder so the dialog stays usable.
   //
   //   `target_height` is the desired draw height in logical pixels. Width is
   //   derived from the SVG's intrinsic aspect ratio (square for most Kenney
   //   buttons).
   ////////////////////////////////////////////////////////////////////////////
   class pad_icon : public element
   {
   public:

      explicit                pad_icon(std::string logical_name,
                                       float target_height = 64.0f,
                                       bool  colored = false);

      view_limits             limits(basic_context const&) const override;
      void                    draw(context const&) override;

      std::string const&      logical_name() const { return _name; }
      float                   target_height() const { return _target_height; }
      void                    target_height(float h) { _target_height = h; }
      bool                    colored() const { return _colored; }

   private:

      void                    ensure_loaded() const;

      std::string             _name;
      float                   _target_height;
      bool                    _colored;
      pad_theme               _theme_at_construct;
      mutable pixmap_ptr      _pixmap;
      mutable bool            _tried = false;
   };
}

#endif

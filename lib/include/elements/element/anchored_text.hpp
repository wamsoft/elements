/*=============================================================================
   Copyright (c) 2026 wamsoft

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ANCHORED_TEXT_JULY_28_2026)
#define ELEMENTS_ANCHORED_TEXT_JULY_28_2026

#include <elements/element/element.hpp>
#include <elements/element/text.hpp>
#include <elements/support/font.hpp>
#include <elements/support/point.hpp>
#include <elements/support/color.hpp>
#include <infra/string_view.hpp>
#include <string>

namespace cycfi::elements
{
   /**
    * \class anchored_text
    *
    * \brief
    *    Draws text at an absolute baseline anchor.
    *
    *    Unlike `label`, which aligns text within its bounds, `anchored_text`
    *    places the text baseline origin at `bounds.top_left + anchor`. This
    *    reproduces the "put the baseline at (x,y)" model used by Photoshop,
    *    Canvas 2D (`textBaseline = "alphabetic"`), PDF/PostScript, etc.
    *
    *    - Vertical placement is always the baseline (anchor.y is the baseline).
    *    - `halign` (canvas::left/center/right) selects whether anchor.x is the
    *      left edge, horizontal center, or right edge of the run — matching the
    *      source justification.
    *    - The bounds size is supplied by the placing container (e.g. a canvas
    *      that assigns an `at` rect); only the origin is used here.
    *    - `family` empty falls back to the theme's label font.
    */
   class anchored_text : public element, public text_writer, public text_reader
   {
   public:

                              anchored_text(
                                 std::string text, std::string family, float size,
                                 color col, int halign, point anchor,
                                 int tracking = 0, std::string locale = {});

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      std::string const&      get_text() const override { return _text; }
      void                    set_text(string_view text) override { _text = std::string(text); }

      void                    set_anchor(point a)        { _anchor = a; }
      point                   get_anchor() const         { return _anchor; }
      void                    set_font_family(std::string f) { _family = std::move(f); }
      void                    set_halign(int a)          { _halign = a; }

   private:

      font_descr              make_descr() const;

      std::string             _text;
      std::string             _family;        // parsed human family ("" = theme)
      unsigned char           _weight;        // font_constants::weight_enum
      unsigned char           _slant;         // font_constants::slant_enum
      bool                    _resolved;      // family registered (usable)?
      float                   _size;
      color                   _color;
      int                     _halign;        // canvas::left / center / right
      point                   _anchor;        // baseline origin, relative to bounds
      int                     _tracking;      // letter spacing, 1/1000 em (0 = none)
      std::string             _locale;
   };

   inline element_ptr make_anchored_text(
      std::string text, std::string family, float size, color col,
      int halign, point anchor, int tracking = 0, std::string locale = {})
   {
      return share(anchored_text(
         std::move(text), std::move(family), size, col, halign, anchor,
         tracking, std::move(locale)));
   }
}

#endif

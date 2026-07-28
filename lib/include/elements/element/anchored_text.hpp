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
#include <vector>

namespace cycfi::elements
{
   // フォント名解決結果 (parse_font_name + 登録確認)。 ok=false は theme 既定へ
   // フォールバック。
   struct resolved_font
   {
      std::string    family;   // 解決した human family ("" = theme 既定)
      unsigned char  weight = 0;
      unsigned char  slant  = 0;
      bool           ok     = false;
   };

   // PSD 由来のフォント名 ("NotoSansJP-Medium" 等) を human family + weight/slant に
   // 分解し、 登録済みかを確認する。 未登録なら一度だけ警告して ok=false。
   resolved_font resolve_font_name(std::string const& name);

   // rich text の 1 span (フォント/サイズ/色が一定の区間)。 family/weight/slant は
   // 解決済み (resolve_font_name の結果)。 text は \n を含みうる。
   struct text_run
   {
      std::string    text;
      std::string    family;   // 解決済み human family ("" = theme 既定)
      unsigned char  weight = 0;
      unsigned char  slant  = 0;
      float          size   = 0;
      color          col;
   };

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
                                 int tracking = 0, float leading = 0.0f,
                                 bool wrap = false, std::string locale = {});

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      std::string const&      get_text() const override { return _text; }
      void                    set_text(string_view text) override { _text = std::string(text); }

      void                    set_anchor(point a)        { _anchor = a; }
      point                   get_anchor() const         { return _anchor; }
      void                    set_font_family(std::string f) { _family = std::move(f); }
      void                    set_halign(int a)          { _halign = a; }
      // rich text (run 別書式)。 空でなければ draw は run 別描画に切替わる。
      void                    set_runs(std::vector<text_run> r) { _runs = std::move(r); }
      // 段落別アライン (canvas::left/center/right)。 段落 = \n 区切り。
      void                    set_para_aligns(std::vector<int> a) { _para_aligns = std::move(a); }

   private:

      font_descr              make_descr() const;
      void                    draw_rich(context const& ctx);   // run 別描画

      font_descr              run_descr(text_run const& r) const;

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
      float                   _leading;       // line advance px (0 = ~1.2em auto)
      bool                    _wrap;          // box text: word-wrap within bounds
      std::string             _locale;
      std::vector<text_run>   _runs;          // rich text (空=単一書式)
      std::vector<int>        _para_aligns;   // 段落別 halign (空=_halign 一律)
   };

   inline element_ptr make_anchored_text(
      std::string text, std::string family, float size, color col,
      int halign, point anchor, int tracking = 0, float leading = 0.0f,
      bool wrap = false, std::string locale = {})
   {
      return share(anchored_text(
         std::move(text), std::move(family), size, col, halign, anchor,
         tracking, leading, wrap, std::move(locale)));
   }
}

#endif

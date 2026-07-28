/*=============================================================================
   Copyright (c) 2026 wamsoft

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/anchored_text.hpp>
#include <elements/support/text_utils.hpp>
#include <elements/support/theme.hpp>
#include <elements/support/context.hpp>
#include <elements/support/canvas.hpp>
#include <cstdio>
#include <set>

namespace cycfi::elements
{
   anchored_text::anchored_text(
      std::string text, std::string family, float size, color col,
      int halign, point anchor, int tracking, std::string locale)
    : _text(std::move(text))
    , _weight(font_constants::weight_normal)
    , _slant(font_constants::slant_normal)
    , _resolved(true)
    , _size(size)
    , _color(col)
    , _halign(halign)
    , _anchor(anchor)
    , _tracking(tracking)
    , _locale(std::move(locale))
   {
      // PSD 由来のフォント名 ("NotoSansJP-Medium" 等) を human family + weight/slant
      // に分解し、 登録済みか確認する。 未登録なら一度だけ警告して (フォント追加を
      // 促す) theme 既定へフォールバックする。 空指定は最初から theme 既定。
      if (!family.empty())
      {
         auto pf = parse_font_name(family);
         _family = pf.family;
         _weight = static_cast<unsigned char>(pf.weight);
         _slant  = static_cast<unsigned char>(pf.slant);
         _resolved = font_family_available(_family);
         if (!_resolved)
         {
            static std::set<std::string> warned;
            if (warned.insert(family).second)
               std::fprintf(stderr,
                  "[elements] font \"%s\" (family \"%s\") not available; "
                  "add a matching .ttf/.otf to the fonts directory to use it "
                  "(falling back to the default font).\n",
                  family.c_str(), _family.c_str());
         }
      }
   }

   font_descr anchored_text::make_descr() const
   {
      // family 未指定 or 未登録なら theme 既定フォントを土台に。 いずれも _size 適用。
      // _families は string_view なので描画中だけ生きていればよい (_family はメンバ)。
      if (_family.empty() || !_resolved)
      {
         font_descr fd = get_theme().label_font;
         fd._size = _size;
         return fd;
      }
      font_descr fd{};
      fd._families = _family;
      fd._weight = _weight;
      fd._slant = _slant;
      fd._size = _size;
      return fd;
   }

   view_limits anchored_text::limits(basic_context const& ctx) const
   {
      auto size = measure_text(ctx.canvas, _text, make_descr());
      return {{size.x, size.y}, {size.x, size.y}};
   }

   void anchored_text::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  state = cnv.new_state();

      cnv.fill_style(_color);
      cnv.font(make_descr(), _size);
      if (!_locale.empty())
         cnv.text_locale(_locale);
      // tracking (1/1000 em, 加算) → advance 倍率 (全角で近似的に一致)。
      if (_tracking != 0)
         cnv.letter_spacing(1.0f + _tracking / 1000.0f);

      // 水平 align のみ渡す (垂直ビット無し) → tvg backend の既定 = baseline。
      cnv.text_align(_halign & 0x3);
      point p{ctx.bounds.left + _anchor.x, ctx.bounds.top + _anchor.y};
      cnv.fill_text(_text, p);
   }
}

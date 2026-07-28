/*=============================================================================
   Copyright (c) 2026 wamsoft

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/anchored_text.hpp>
#include <elements/support/text_utils.hpp>
#include <elements/support/theme.hpp>
#include <elements/support/context.hpp>
#include <elements/support/canvas.hpp>

namespace cycfi::elements
{
   anchored_text::anchored_text(
      std::string text, std::string family, float size, color col,
      int halign, point anchor, std::string locale)
    : _text(std::move(text))
    , _family(std::move(family))
    , _size(size)
    , _color(col)
    , _halign(halign)
    , _anchor(anchor)
    , _locale(std::move(locale))
   {}

   font_descr anchored_text::make_descr() const
   {
      // family 未指定なら theme 既定フォントを土台に。 いずれも _size を適用。
      // _families は string_view なので、 描画中だけ生きていればよい (_family は
      // メンバなので draw/limits の間は有効)。
      font_descr fd = _family.empty() ? get_theme().label_font : font_descr{};
      if (!_family.empty())
         fd._families = _family;
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

      // 水平 align のみ渡す (垂直ビット無し) → tvg backend の既定 = baseline。
      cnv.text_align(_halign & 0x3);
      point p{ctx.bounds.left + _anchor.x, ctx.bounds.top + _anchor.y};
      cnv.fill_text(_text, p);
   }
}

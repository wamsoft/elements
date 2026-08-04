/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_STYLE_TOGGLE_SELECTOR_JUNE_5_2016)
#define ELEMENTS_STYLE_TOGGLE_SELECTOR_JUNE_5_2016

#include <elements/support/theme.hpp>
#include <elements/element/element.hpp>
#include <elements/support/text_utils.hpp>
#include <string>
#include <utility>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // toggle_selector (e.g. check_box and radio_button where the small button
   // at the left followed by some text at the right of the button.)
   ////////////////////////////////////////////////////////////////////////////
   struct toggle_selector : element
   {
                              // `scale` は 1.0 = テーマ既定サイズ。 >1.0 で
                              // ラベルフォント・インジケータ・余白を一括拡大する
                              // (button の get_size() と同じ意味論)。 既定 1.0 は
                              // 従来と完全一致。
                              toggle_selector(std::string text, float scale = 1.0f)
                               : _text(std::move(text)), _scale(scale)
                              {}

      view_limits             limits(basic_context const& ctx) const override;
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;
      bool                    wants_control() const override;

      std::string             _text;
      float                   _scale = 1.0f;
   };

   inline view_limits toggle_selector::limits(basic_context const& ctx) const
   {
      auto& thm = get_theme();
      auto  font = thm.label_font;
      font = font.size(font._size * _scale);
      auto  size = measure_text(ctx.canvas, _text.c_str(), font);
      // 左マージン 15 + インジケータ幅 (= 文字高 size.y) + 文字との間隔 10 +
      // 右マージン 15。 インジケータは size.y 由来なので既に scale 済み、
      // 固定ギャップだけ scale を掛ける。
      size.x += (15 + 10 + 15) * _scale + size.y;
      return {{size.x, size.y}, {size.x, size.y}};
   }

   inline bool toggle_selector::cursor(context const& ctx, point /* p */, cursor_tracking /* status */)
   {
      ctx.view.refresh(ctx);
      return true;
   }

   inline bool toggle_selector::wants_control() const
   {
      return true;
   }
}

#endif

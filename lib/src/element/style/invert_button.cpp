/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/style/invert_button.hpp>
#include <elements/element/traversal.hpp>
#include <elements/support/theme.hpp>

namespace cycfi::elements
{
   namespace
   {
      // Derive focus / press / hilite from the enclosing basic_button parent.
      bool focused_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->focused();
      }
      bool pressed_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->value();
      }
      bool hilite_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->hilite();
      }
   }

   void invert_button_styler::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      auto enabled = ctx.enabled;
      bool focus   = focused_of(ctx);
      bool pressed = pressed_of(ctx);
      bool hilite  = hilite_of(ctx);

      if (pressed)
         bounds = bounds.move(1, 1);

      color bg, fg;
      if (focus)
      {
         bg = colors::white;
         fg = colors::black;
      }
      else if (hilite)
      {
         bg = rgba(60, 60, 60, 255);
         fg = colors::white;
      }
      else
      {
         bg = colors::black;
         fg = colors::white;
      }

      if (!enabled)
      {
         auto a = get_theme().disabled_opacity;
         bg = bg.opacity(a);
         fg = fg.opacity(a);
      }

      cnv.fill_style(bg);
      cnv.fill_rect(bounds);

      if (!focus)
      {
         cnv.line_width(2.0f);
         cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
         cnv.stroke_rect(bounds.inset(1, 1));
      }

      auto const& th = get_theme();
      auto font = th.label_font;
      font = font.size(font._size * get_size());

      cnv.font(font);
      cnv.fill_style(fg);
      cnv.text_align(cnv.center | cnv.middle);
      cnv.fill_text(
         get_text(),
         {bounds.left + bounds.width() / 2.0f, bounds.top + bounds.height() / 2.0f}
      );
   }

   view_limits ring_button_styler::limits(basic_context const& ctx) const
   {
      auto lim = default_button_styler::limits(ctx);
      lim.min.x += 2 * reserve;
      lim.min.y += 2 * reserve;
      if (lim.max.x < full_extent - 2 * reserve)
         lim.max.x += 2 * reserve;
      if (lim.max.y < full_extent - 2 * reserve)
         lim.max.y += 2 * reserve;
      return lim;
   }

   void ring_button_styler::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto outer   = ctx.bounds;
      auto inner   = outer.inset(reserve, reserve);
      auto enabled = ctx.enabled;
      bool focus   = focused_of(ctx);
      bool pressed = pressed_of(ctx);
      bool hilite  = hilite_of(ctx);

      if (pressed)
         inner = inner.move(1, 1);

      color bg, fg;
      if (hilite && !focus)
      {
         bg = rgba(60, 60, 60, 255);
         fg = colors::white;
      }
      else
      {
         bg = colors::black;
         fg = colors::white;
      }

      if (!enabled)
      {
         auto a = get_theme().disabled_opacity;
         bg = bg.opacity(a);
         fg = fg.opacity(a);
      }

      cnv.fill_style(bg);
      cnv.fill_rect(inner);

      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.stroke_rect(inner.inset(1, 1));

      if (focus)
      {
         auto ring = outer.inset(outline_width / 2.0f, outline_width / 2.0f);
         cnv.line_width(outline_width);
         cnv.stroke_style(_outline.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
         cnv.stroke_rect(ring);
      }

      auto const& th = get_theme();
      auto font = th.label_font;
      font = font.size(font._size * get_size());

      cnv.font(font);
      cnv.fill_style(fg);
      cnv.text_align(cnv.center | cnv.middle);
      cnv.fill_text(
         get_text(),
         {inner.left + inner.width() / 2.0f, inner.top + inner.height() / 2.0f}
      );
   }
}

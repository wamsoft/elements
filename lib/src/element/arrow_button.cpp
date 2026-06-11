/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/arrow_button.hpp>
#include <elements/support/theme.hpp>
#include <elements/view.hpp>

namespace cycfi::elements
{
   namespace
   {
      void draw_left_arrow(canvas& cnv, rect r, color c)
      {
         float mid_y = r.top + r.height() / 2.0f;
         float pad_x = r.width() * 0.30f;
         float h     = r.height() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({r.left + pad_x,  mid_y});
         cnv.line_to({r.right - pad_x, mid_y - h});
         cnv.line_to({r.right - pad_x, mid_y + h});
         cnv.close_path();
         cnv.fill();
      }

      void draw_right_arrow(canvas& cnv, rect r, color c)
      {
         float mid_y = r.top + r.height() / 2.0f;
         float pad_x = r.width() * 0.30f;
         float h     = r.height() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({r.right - pad_x, mid_y});
         cnv.line_to({r.left + pad_x,  mid_y - h});
         cnv.line_to({r.left + pad_x,  mid_y + h});
         cnv.close_path();
         cnv.fill();
      }
   }

   arrow_button::arrow_button(direction dir, on_step_function cb)
    : on_step(std::move(cb))
    , _dir(dir)
   {}

   view_limits arrow_button::limits(basic_context const& /*ctx*/) const
   {
      return {{32.0f, 28.0f}, {64.0f, full_extent}};
   }

   void arrow_button::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      bool enabled = ctx.enabled;

      color bg, fg;
      if (_pressed)
      {
         bg = colors::white;
         fg = colors::black;
      }
      else if (_hovered)
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

      auto draw_r = bounds;
      if (_pressed)
         draw_r = draw_r.move(1, 1);

      cnv.fill_style(bg);
      cnv.fill_rect(draw_r);

      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.stroke_rect(draw_r.inset(1, 1));

      if (_dir == arrow_left)
         draw_left_arrow(cnv, draw_r, fg);
      else
         draw_right_arrow(cnv, draw_r, fg);
   }

   bool arrow_button::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left)
         return false;

      bool was_pressed = _pressed;
      _pressed = btn.down;

      if (btn.down)
      {
         if (on_step)
            on_step();
         ctx.view.refresh();
      }
      else if (was_pressed)
      {
         ctx.view.refresh(ctx);
      }
      return true;
   }

   bool arrow_button::cursor(context const& ctx, point p, cursor_tracking status)
   {
      bool was_hovered = _hovered;
      _hovered = (status != cursor_tracking::leaving) && ctx.bounds.includes(p);
      if (_hovered != was_hovered)
         ctx.view.refresh(ctx);
      return true;
   }
}

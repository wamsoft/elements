/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/arrow_button.hpp>
#include <elements/support/theme.hpp>
#include <elements/view.hpp>
#include <chrono>

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

      void draw_up_arrow(canvas& cnv, rect r, color c)
      {
         float mid_x = r.left + r.width() / 2.0f;
         float pad_y = r.height() * 0.30f;
         float w     = r.width() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({mid_x,     r.top + pad_y});
         cnv.line_to({mid_x - w, r.bottom - pad_y});
         cnv.line_to({mid_x + w, r.bottom - pad_y});
         cnv.close_path();
         cnv.fill();
      }

      void draw_down_arrow(canvas& cnv, rect r, color c)
      {
         float mid_x = r.left + r.width() / 2.0f;
         float pad_y = r.height() * 0.30f;
         float w     = r.width() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({mid_x,     r.bottom - pad_y});
         cnv.line_to({mid_x - w, r.top + pad_y});
         cnv.line_to({mid_x + w, r.top + pad_y});
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

      switch (_dir)
      {
         case arrow_left:  draw_left_arrow(cnv, draw_r, fg);  break;
         case arrow_right: draw_right_arrow(cnv, draw_r, fg); break;
         case arrow_up:    draw_up_arrow(cnv, draw_r, fg);    break;
         case arrow_down:  draw_down_arrow(cnv, draw_r, fg);  break;
      }
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
         _held_inside = true;
         if (on_step)
            on_step();
         ctx.view.refresh();
         start_repeat(ctx);
      }
      else
      {
         stop_repeat();
         if (was_pressed)
            ctx.view.refresh(ctx);
      }
      return true;
   }

   void arrow_button::drag(context const& ctx, mouse_button btn)
   {
      if (!_pressed)
         return;
      // Held down but dragged off the button: pause the repeat, resume on the
      // way back in. Matches how a scrollbar stepper behaves.
      bool inside = ctx.bounds.includes(btn.pos);
      if (inside != _held_inside)
      {
         _held_inside = inside;
         ctx.view.refresh(ctx);
      }
   }

   void arrow_button::start_repeat(context const& ctx)
   {
      stop_repeat();
      if (!_repeat)
         return;

      auto& view_ = ctx.view;
      std::weak_ptr<element> self = shared_from_this();
      const int rate = _repeat_rate_ms > 0 ? _repeat_rate_ms : 60;

      _repeat_tick = std::make_shared<std::function<void()>>();
      std::weak_ptr<std::function<void()>> tick = _repeat_tick;
      *_repeat_tick =
         [this, self, tick, &view_, rate]()
         {
            auto keep = self.lock();
            if (!keep || !_pressed || !is_enabled())
               return;
            if (_held_inside && on_step)
            {
               on_step();
               view_.refresh();
            }
            if (auto t = tick.lock())
               _repeat_timer = view_.post(std::chrono::milliseconds(rate), *t);
         };

      const int delay = _repeat_delay_ms > 0 ? _repeat_delay_ms : 400;
      _repeat_timer = view_.post(std::chrono::milliseconds(delay), *_repeat_tick);
   }

   void arrow_button::stop_repeat()
   {
      _repeat_timer.reset();
      _repeat_tick.reset();
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

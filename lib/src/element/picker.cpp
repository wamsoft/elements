/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/picker.hpp>
#include <elements/support/theme.hpp>
#include <elements/support/text_utils.hpp>
#include <elements/view.hpp>
#include <algorithm>

namespace cycfi::elements
{
   namespace
   {
      // Pad-axis edge detection:
      //   - Tick once on |value| > engage, then suppress further ticks
      //     until a gap of at least `pad_quiet` ms passes without a call.
      //   - The view filters out v=0 from the value-mode path, so we infer
      //     "release" purely from call cadence — continuous holds keep
      //     firing at the poll rate (~16 ms), so a 35 ms quiet window
      //     distinguishes "still held" from "released and pressed again".
      constexpr float pad_engage  = 0.55f;
      constexpr float pad_release = 0.20f;
      constexpr auto  pad_quiet   = std::chrono::milliseconds(35);

      bool is_horizontal_axis(pad_axis axis)
      {
         return axis == pad_axis::dpad_x
             || axis == pad_axis::left_x
             || axis == pad_axis::right_x;
      }

      font_descr label_font_descr()
      {
         return get_theme().label_font;
      }

      struct draw_state
      {
         color bg;
         color fg;
         bool  draw_frame;
      };

      draw_state pick_draw_state(bool focused, bool enabled)
      {
         draw_state s;
         if (focused)
         {
            s.bg = colors::white;
            s.fg = colors::black;
            s.draw_frame = false;
         }
         else
         {
            s.bg = colors::black;
            s.fg = colors::white;
            s.draw_frame = true;
         }
         if (!enabled)
         {
            auto a = get_theme().disabled_opacity;
            s.bg = s.bg.opacity(a);
            s.fg = s.fg.opacity(a);
         }
         return s;
      }

      void draw_body(canvas& cnv, rect r, draw_state const& ds, bool enabled)
      {
         cnv.fill_style(ds.bg);
         cnv.fill_rect(r);
         if (ds.draw_frame)
         {
            cnv.line_width(2.0f);
            cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
            cnv.stroke_rect(r.inset(1, 1));
         }
      }
   }

   // ==================================================================
   // cycle_picker
   // ==================================================================
   cycle_picker::cycle_picker(std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void cycle_picker::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool cycle_picker::step(int delta)
   {
      if (_options.size() < 2)
         return false;
      auto n = int(_options.size());
      auto next = (int(_index) + delta % n + n) % n;
      if (next != int(_index))
      {
         _index = std::size_t(next);
         if (on_change)
            on_change(_index);
         return true;
      }
      return false;
   }

   view_limits cycle_picker::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      float pad_x = 56.0f;
      float pad_y = 12.0f;
      auto min_x = widest + pad_x;
      auto min_y = h + pad_y;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   void cycle_picker::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds = ctx.bounds;
      bool enabled = ctx.enabled;
      auto ds = pick_draw_state(_has_focus, enabled);
      draw_body(cnv, bounds, ds, enabled);

      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      cnv.font(font);
      cnv.fill_style(ds.fg);

      float mid_y = bounds.top + bounds.height() / 2.0f;

      cnv.text_align(cnv.left | cnv.middle);
      cnv.fill_text("<", {bounds.left + 12.0f, mid_y});
      cnv.text_align(cnv.right | cnv.middle);
      cnv.fill_text(">", {bounds.right - 12.0f, mid_y});

      if (!_options.empty())
      {
         cnv.text_align(cnv.center | cnv.middle);
         cnv.fill_text(_options[_index], {bounds.left + bounds.width() / 2.0f, mid_y});
      }
   }

   bool cycle_picker::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left || !btn.down)
         return false;
      ctx.view.focus(shared_from_this());
      float mid_x = ctx.bounds.left + ctx.bounds.width() / 2.0f;
      if (step(btn.pos.x < mid_x ? -1 : +1))
         ctx.view.refresh(ctx);
      return true;
   }

   bool cycle_picker::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled)
         return false;
      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;

      int delta = 0;
      switch (k.key)
      {
         case key_code::left:  delta = -1; break;
         case key_code::right: delta = +1; break;
         default:              return false;
      }
      if (step(delta))
         ctx.view.refresh(ctx);
      return true;
   }

   bool cycle_picker::pad_axis(context const& ctx, pad_axis_info info)
   {
      if (!ctx.enabled)
         return false;
      if (!is_horizontal_axis(info.axis))
         return false;

      float mag = std::abs(info.value);
      if (mag < pad_release)
         return false;

      if (mag > pad_engage)
      {
         auto now = std::chrono::steady_clock::now();
         if (now - _last_pad_step >= pad_quiet)
         {
            _last_pad_step = now;
            if (step(info.value < 0.0f ? -1 : +1))
               ctx.view.refresh(ctx);
         }
         else
         {
            _last_pad_step = now;
         }
      }
      return true;
   }

   // ==================================================================
   // framed_cycle_picker
   // ==================================================================
   framed_cycle_picker::framed_cycle_picker(
      std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void framed_cycle_picker::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool framed_cycle_picker::step(int delta)
   {
      if (_options.size() < 2)
         return false;
      auto n = int(_options.size());
      auto next = (int(_index) + delta % n + n) % n;
      if (next != int(_index))
      {
         _index = std::size_t(next);
         if (on_change)
            on_change(_index);
         return true;
      }
      return false;
   }

   view_limits framed_cycle_picker::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      float min_x = widest + 24.0f + 2.0f * (arrow_box_w + gap);
      float min_y = h + 12.0f;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   namespace
   {
      void draw_left_triangle(canvas& cnv, rect r, color c)
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

      void draw_right_triangle(canvas& cnv, rect r, color c)
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

      rect center_panel_of(rect bounds)
      {
         return rect{
            bounds.left  + framed_cycle_picker::arrow_box_w + framed_cycle_picker::gap,
            bounds.top,
            bounds.right - framed_cycle_picker::arrow_box_w - framed_cycle_picker::gap,
            bounds.bottom
         };
      }

      rect left_arrow_of(rect bounds)
      {
         return rect{
            bounds.left,
            bounds.top,
            bounds.left + framed_cycle_picker::arrow_box_w,
            bounds.bottom
         };
      }

      rect right_arrow_of(rect bounds)
      {
         return rect{
            bounds.right - framed_cycle_picker::arrow_box_w,
            bounds.top,
            bounds.right,
            bounds.bottom
         };
      }
   }

   void framed_cycle_picker::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      bool enabled = ctx.enabled;
      auto ds      = pick_draw_state(_has_focus, enabled);

      auto left_r   = left_arrow_of(bounds);
      auto right_r  = right_arrow_of(bounds);
      auto center_r = center_panel_of(bounds);

      draw_body(cnv, left_r,   ds, enabled);
      draw_body(cnv, center_r, ds, enabled);
      draw_body(cnv, right_r,  ds, enabled);

      draw_left_triangle(cnv,  left_r,  ds.fg);
      draw_right_triangle(cnv, right_r, ds.fg);

      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      cnv.font(font);
      cnv.fill_style(ds.fg);
      cnv.text_align(cnv.center | cnv.middle);
      if (!_options.empty())
      {
         cnv.fill_text(
            _options[_index],
            {center_r.left + center_r.width() / 2.0f, center_r.top + center_r.height() / 2.0f}
         );
      }
   }

   bool framed_cycle_picker::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left || !btn.down)
         return false;
      ctx.view.focus(shared_from_this());

      auto left_r  = left_arrow_of(ctx.bounds);
      auto right_r = right_arrow_of(ctx.bounds);
      int delta = 0;
      if (left_r.includes(btn.pos))
         delta = -1;
      else if (right_r.includes(btn.pos))
         delta = +1;
      if (delta != 0 && step(delta))
         ctx.view.refresh(ctx);
      return true;
   }

   bool framed_cycle_picker::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled)
         return false;
      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;
      int delta = 0;
      switch (k.key)
      {
         case key_code::left:  delta = -1; break;
         case key_code::right: delta = +1; break;
         default:              return false;
      }
      if (step(delta))
         ctx.view.refresh(ctx);
      return true;
   }

   bool framed_cycle_picker::pad_axis(context const& ctx, pad_axis_info info)
   {
      if (!ctx.enabled)
         return false;
      if (!is_horizontal_axis(info.axis))
         return false;

      float mag = std::abs(info.value);
      if (mag < pad_release)
         return false;

      if (mag > pad_engage)
      {
         auto now = std::chrono::steady_clock::now();
         if (now - _last_pad_step >= pad_quiet)
         {
            _last_pad_step = now;
            if (step(info.value < 0.0f ? -1 : +1))
               ctx.view.refresh(ctx);
         }
         else
         {
            _last_pad_step = now;
         }
      }
      return true;
   }

   // ==================================================================
   // segmented_picker
   // ==================================================================
   segmented_picker::segmented_picker(std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void segmented_picker::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool segmented_picker::step(int delta)
   {
      if (_options.empty())
         return false;
      int next = int(_index) + delta;
      if (next < 0 || next >= int(_options.size()))
         return false;
      if (next != int(_index))
      {
         _index = std::size_t(next);
         if (on_change)
            on_change(_index);
         return true;
      }
      return false;
   }

   view_limits segmented_picker::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      float seg_w = widest + 24.0f;
      float min_x = seg_w * std::max<std::size_t>(_options.size(), 1u);
      float min_y = h + 12.0f;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   void segmented_picker::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds = ctx.bounds;
      bool enabled = ctx.enabled;
      auto outer_ds = pick_draw_state(_has_focus, enabled);

      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.fill_style(colors::black.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.fill_rect(bounds);
      cnv.stroke_rect(bounds.inset(1, 1));

      if (_options.empty())
         return;

      auto font = label_font_descr();
      font = font.size(font._size * _font_size);
      cnv.font(font);

      float seg_w = bounds.width() / float(_options.size());
      float mid_y = bounds.top + bounds.height() / 2.0f;

      for (std::size_t i = 0; i < _options.size(); ++i)
      {
         rect seg{
            bounds.left + seg_w * float(i),
            bounds.top,
            bounds.left + seg_w * float(i + 1),
            bounds.bottom
         };
         bool is_selected = (i == _index);

         if (is_selected)
         {
            auto a = enabled ? 1.0f : get_theme().disabled_opacity;
            cnv.fill_style(colors::white.opacity(a));
            cnv.fill_rect(seg.inset(2, 2));
            cnv.fill_style(colors::black.opacity(a));
         }
         else
         {
            cnv.fill_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
         }

         cnv.text_align(cnv.center | cnv.middle);
         cnv.fill_text(_options[i], {seg.left + seg.width() / 2.0f, mid_y});

         if (i + 1 < _options.size())
         {
            cnv.line_width(1.0f);
            cnv.stroke_style(colors::white.opacity((enabled ? 1.0f : get_theme().disabled_opacity) * 0.6f));
            cnv.begin_path();
            cnv.move_to({seg.right, bounds.top + 2.0f});
            cnv.line_to({seg.right, bounds.bottom - 2.0f});
            cnv.stroke();
         }
      }
   }

   bool segmented_picker::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left || !btn.down)
         return false;
      ctx.view.focus(shared_from_this());
      if (_options.empty())
         return true;
      float seg_w = ctx.bounds.width() / float(_options.size());
      int hit = int((btn.pos.x - ctx.bounds.left) / seg_w);
      hit = std::clamp(hit, 0, int(_options.size()) - 1);
      if (std::size_t(hit) != _index)
      {
         _index = std::size_t(hit);
         if (on_change)
            on_change(_index);
         ctx.view.refresh(ctx);
      }
      return true;
   }

   bool segmented_picker::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled)
         return false;
      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;

      int delta = 0;
      switch (k.key)
      {
         case key_code::left:  delta = -1; break;
         case key_code::right: delta = +1; break;
         default:              return false;
      }
      if (step(delta))
      {
         ctx.view.refresh(ctx);
         return true;
      }
      return false;
   }

   bool segmented_picker::pad_axis(context const& ctx, pad_axis_info info)
   {
      if (!ctx.enabled)
         return false;
      if (!is_horizontal_axis(info.axis))
         return false;

      float mag = std::abs(info.value);
      if (mag < pad_release)
         return false;

      if (mag > pad_engage)
      {
         auto now = std::chrono::steady_clock::now();
         if (now - _last_pad_step >= pad_quiet)
         {
            if (step(info.value < 0.0f ? -1 : +1))
            {
               _last_pad_step = now;
               ctx.view.refresh(ctx);
               return true;
            }
            return false;
         }
         _last_pad_step = now;
      }
      return true;
   }
}

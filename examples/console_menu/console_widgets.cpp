#include "console_widgets.hpp"

#include <algorithm>

namespace console_menu
{
   namespace
   {
      // Pad-axis edge detection:
      //   - Tick once on |value| > engage, then suppress further ticks
      //     until a gap of at least `pad_quiet` ms passes without a call.
      //   - We can't rely on a v=0 event reaching us — the view filters
      //     out zero values from the value-mode path — so we infer the
      //     "release" purely from the call cadence instead of latching
      //     on state. Continuous holds (D-Pad or analog) keep firing the
      //     widget at the poll cadence (~16ms), so a 35ms quiet window
      //     comfortably distinguishes "still held" from "released and
      //     pressed again".
      constexpr float pad_engage  = 0.55f;
      constexpr float pad_release = 0.20f;
      constexpr auto  pad_quiet   = std::chrono::milliseconds(35);

      bool is_horizontal_axis(pad_axis axis)
      {
         return axis == pad_axis::dpad_x
             || axis == pad_axis::left_x
             || axis == pad_axis::right_x;
      }

      // Text drawing helpers reused by both selectors.
      font_descr label_font_descr()
      {
         return get_theme().label_font;
      }

      point measure_label(canvas& cnv, std::string const& s)
      {
         return measure_text(cnv, s, label_font_descr());
      }

      // Body+frame mimic of console_button_styler (focused = white body,
      // black text; otherwise = black body, white frame, white text).
      struct draw_state
      {
         color bg;
         color fg;
         bool  draw_frame;  // 2px white outer frame when not focused
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
   // trigger_selector
   // ==================================================================
   trigger_selector::trigger_selector(std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void trigger_selector::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool trigger_selector::step(int delta)
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

   view_limits trigger_selector::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      // Padding for triangles ("◀ " + text + " ▶") + body inset.
      float pad_x = 56.0f;
      float pad_y = 12.0f;
      auto min_x = widest + pad_x;
      auto min_y = h + pad_y;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   void trigger_selector::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds = ctx.bounds;
      bool enabled = ctx.enabled;
      auto ds = pick_draw_state(_has_focus, enabled);
      draw_body(cnv, bounds, ds, enabled);

      auto font = label_font_descr();
      cnv.font(font);
      cnv.fill_style(ds.fg);

      float mid_y = bounds.top + bounds.height() / 2.0f;

      // Left / right triangles
      cnv.text_align(cnv.left | cnv.middle);
      cnv.fill_text("<", {bounds.left + 12.0f, mid_y});
      cnv.text_align(cnv.right | cnv.middle);
      cnv.fill_text(">", {bounds.right - 12.0f, mid_y});

      // Current value
      if (!_options.empty())
      {
         cnv.text_align(cnv.center | cnv.middle);
         cnv.fill_text(_options[_index], {bounds.left + bounds.width() / 2.0f, mid_y});
      }
   }

   bool trigger_selector::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left || !btn.down)
         return false;
      // Acquire focus on click. Cycle direction by which half was hit:
      // left half = previous, right half = next.
      ctx.view.focus(shared_from_this());
      float mid_x = ctx.bounds.left + ctx.bounds.width() / 2.0f;
      if (step(btn.pos.x < mid_x ? -1 : +1))
         ctx.view.refresh(ctx);
      return true;
   }

   bool trigger_selector::key(context const& ctx, key_info k)
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

   bool trigger_selector::pad_axis(context const& ctx, pad_axis_info info)
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
            // Still held — bump the timestamp so the quiet window is
            // measured from the latest tick the widget actually saw.
            _last_pad_step = now;
         }
      }
      return true; // consume while above the release threshold
   }

   // ==================================================================
   // arrow_button  (standalone click-only stepper part)
   // ==================================================================
   namespace
   {
      void draw_left_arrow_at(canvas& cnv, rect r, color c)
      {
         float mid_y = r.top + r.height() / 2.0f;
         float pad_x = r.width() * 0.30f;
         float h     = r.height() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({r.left + pad_x,        mid_y});
         cnv.line_to({r.right - pad_x,       mid_y - h});
         cnv.line_to({r.right - pad_x,       mid_y + h});
         cnv.close_path();
         cnv.fill();
      }

      void draw_right_arrow_at(canvas& cnv, rect r, color c)
      {
         float mid_y = r.top + r.height() / 2.0f;
         float pad_x = r.width() * 0.30f;
         float h     = r.height() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({r.right - pad_x,       mid_y});
         cnv.line_to({r.left + pad_x,        mid_y - h});
         cnv.line_to({r.left + pad_x,        mid_y + h});
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
      // Square-ish box; matches a typical selector row height.
      return {{32.0f, 28.0f}, {64.0f, full_extent}};
   }

   void arrow_button::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      bool enabled = ctx.enabled;

      // Body: black; hover lightens slightly; pressed inverts.
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

      // Frame (always present — these aren't focusable, the frame is
      // just the part's outline).
      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.stroke_rect(draw_r.inset(1, 1));

      if (_dir == arrow_left)
         draw_left_arrow_at(cnv, draw_r, fg);
      else
         draw_right_arrow_at(cnv, draw_r, fg);
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
         // Fire on press for snappy keyboard-less menus.
         if (on_step)
            on_step();
         // The callback usually mutates another widget; refresh the
         // entire view so its repaint propagates.
         ctx.view.refresh();
      }
      else if (was_pressed)
      {
         // Just visual release.
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

   // ==================================================================
   // trigger_selector_arrows  (variant with outer triangle buttons)
   // ==================================================================
   trigger_selector_arrows::trigger_selector_arrows(
      std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void trigger_selector_arrows::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool trigger_selector_arrows::step(int delta)
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

   view_limits trigger_selector_arrows::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      float min_x = widest + 24.0f
                  + 2.0f * (arrow_box_w + gap);
      float min_y = h + 12.0f;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   namespace
   {
      void draw_left_arrow(canvas& cnv, rect r, color c)
      {
         float mid_y = r.top + r.height() / 2.0f;
         float pad_x = r.width() * 0.30f;
         float h     = r.height() * 0.30f;
         cnv.fill_style(c);
         cnv.begin_path();
         cnv.move_to({r.left + pad_x,        mid_y});
         cnv.line_to({r.right - pad_x,       mid_y - h});
         cnv.line_to({r.right - pad_x,       mid_y + h});
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
         cnv.move_to({r.right - pad_x,       mid_y});
         cnv.line_to({r.left + pad_x,        mid_y - h});
         cnv.line_to({r.left + pad_x,        mid_y + h});
         cnv.close_path();
         cnv.fill();
      }

      // Centered value panel rect, inset between the two arrow boxes.
      rect center_panel_of(rect bounds)
      {
         using T = trigger_selector_arrows;
         return rect{
            bounds.left  + T::arrow_box_w + T::gap,
            bounds.top,
            bounds.right - T::arrow_box_w - T::gap,
            bounds.bottom
         };
      }

      rect left_arrow_of(rect bounds)
      {
         return rect{
            bounds.left,
            bounds.top,
            bounds.left + trigger_selector_arrows::arrow_box_w,
            bounds.bottom
         };
      }

      rect right_arrow_of(rect bounds)
      {
         return rect{
            bounds.right - trigger_selector_arrows::arrow_box_w,
            bounds.top,
            bounds.right,
            bounds.bottom
         };
      }
   }

   void trigger_selector_arrows::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      bool enabled = ctx.enabled;
      auto ds      = pick_draw_state(_has_focus, enabled);

      auto left_r   = left_arrow_of(bounds);
      auto right_r  = right_arrow_of(bounds);
      auto center_r = center_panel_of(bounds);

      // Three independent framed boxes.
      draw_body(cnv, left_r,   ds, enabled);
      draw_body(cnv, center_r, ds, enabled);
      draw_body(cnv, right_r,  ds, enabled);

      // Filled triangles.
      draw_left_arrow(cnv,  left_r,  ds.fg);
      draw_right_arrow(cnv, right_r, ds.fg);

      // Current value at center.
      auto font = label_font_descr();
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

   bool trigger_selector_arrows::click(context const& ctx, mouse_button btn)
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
      // Click on center: just acquire focus (no cycle).
      if (delta != 0 && step(delta))
         ctx.view.refresh(ctx);
      return true;
   }

   bool trigger_selector_arrows::key(context const& ctx, key_info k)
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

   bool trigger_selector_arrows::pad_axis(context const& ctx, pad_axis_info info)
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
   // segmented_selector
   // ==================================================================
   segmented_selector::segmented_selector(std::vector<std::string> options, std::size_t initial)
    : _options(std::move(options))
    , _index(_options.empty() ? 0 : std::min(initial, _options.size() - 1))
   {}

   void segmented_selector::select(std::size_t i)
   {
      if (!_options.empty() && i < _options.size() && i != _index)
      {
         _index = i;
         if (on_change)
            on_change(_index);
      }
   }

   bool segmented_selector::step(int delta)
   {
      if (_options.empty())
         return false;
      int next = int(_index) + delta;
      if (next < 0 || next >= int(_options.size()))
         return false; // end-stop; let arrow_focus_navigation take over
      if (next != int(_index))
      {
         _index = std::size_t(next);
         if (on_change)
            on_change(_index);
         return true;
      }
      return false;
   }

   view_limits segmented_selector::limits(basic_context const& ctx) const
   {
      auto& cnv = ctx.canvas;
      float widest = 0.0f;
      float h = 0.0f;
      auto font = label_font_descr();
      for (auto const& s : _options)
      {
         auto sz = measure_text(cnv, s, font);
         widest = std::max(widest, sz.x);
         h = std::max(h, sz.y);
      }
      // Each segment: widest text + 24px horizontal padding.
      float seg_w = widest + 24.0f;
      float min_x = seg_w * std::max<std::size_t>(_options.size(), 1u);
      float min_y = h + 12.0f;
      return {{min_x, min_y}, {full_extent, min_y}};
   }

   void segmented_selector::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds = ctx.bounds;
      bool enabled = ctx.enabled;
      auto outer_ds = pick_draw_state(_has_focus, enabled);

      // Outer frame (around the whole selector).
      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      // Fill the whole strip with black first to avoid bleed.
      cnv.fill_style(colors::black.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.fill_rect(bounds);
      cnv.stroke_rect(bounds.inset(1, 1));

      if (_options.empty())
         return;

      auto font = label_font_descr();
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
            // Highlighted segment uses the focus-style invert.
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

         // Separator between segments (skip rightmost border — outer frame handles it).
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

   bool segmented_selector::click(context const& ctx, mouse_button btn)
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

   bool segmented_selector::key(context const& ctx, key_info k)
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
      // End-stop: let view-level navigation try to move focus.
      return false;
   }

   bool segmented_selector::pad_axis(context const& ctx, pad_axis_info info)
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
            // End-stop: let the axis fall through to view focus nav.
            return false;
         }
         _last_pad_step = now;
      }
      return true;
   }

   // ==================================================================
   // focus_row_element
   // ==================================================================
   void focus_row_element::draw(context const& ctx)
   {
      // Ask the subtree if any descendant currently holds focus.
      // composite_base::focus() returns the focused child or nullptr;
      // proxy_base::focus() delegates to its subject. So any non-null
      // means a descendant is focused.
      bool has_focus_inside = subject().focus() != nullptr;

      if (has_focus_inside)
      {
         auto& cnv = ctx.canvas;
         auto  st = cnv.new_state();
         cnv.fill_style(colors::white.opacity(0.10f));
         cnv.fill_rect(ctx.bounds);
      }

      proxy_base::draw(ctx);
   }

   element* focus_row_element::hit_test(context const& ctx, point p, bool leaf, bool control)
   {
      if (!ctx.bounds.includes(p))
         return nullptr;
      // Give the inner subject first pick: clicking directly on the
      // control should let the control handle the click as usual
      // (slider drag start, trigger click-to-cycle, etc.).
      if (auto* sub = proxy_base::hit_test(ctx, p, leaf, control))
         return sub;
      // Otherwise (typically the decorative label area) accept the hit
      // here so click() can move focus to the target.
      return this;
   }

   bool focus_row_element::click(context const& ctx, mouse_button btn)
   {
      // First let the subject handle the click. If the inner control
      // (selector / slider / arrow_button etc.) consumes it, that's the
      // normal path — we MUST forward, otherwise focus_row would swallow
      // every click that vtile/composite dispatched to us as the
      // immediate-child-with-wants_control.
      if (proxy_base::click(ctx, btn))
         return true;

      // Subject didn't consume — typically a click on the decorative
      // label area (or any other dead space inside the row). Treat it
      // as "focus the target".
      if (btn.state == mouse_button::left && btn.down)
      {
         if (auto t = _target.lock())
            ctx.view.focus(t);
         return true;
      }
      return false;
   }
}

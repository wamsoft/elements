/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/slider.hpp>
#include <elements/support/theme.hpp>
#include <elements/view.hpp>
#include <algorithm>
#include <cmath>

namespace cycfi::elements
{
   view_limits slider_base::limits(basic_context const& ctx) const
   {
      auto  limits_ = track().limits(ctx);
      auto  tmb_limits = thumb().limits(ctx);

      // We multiply thumb min limits by 2 so that there is always some space to move it.
      if (_is_horiz = limits_.max.x > limits_.max.y; _is_horiz)
      {
         limits_.min.y = std::max<float>(limits_.min.y, tmb_limits.min.y);
         limits_.max.y = std::max<float>(limits_.max.y, tmb_limits.max.y);
         limits_.min.x = std::max<float>(limits_.min.x, tmb_limits.min.x * 2);
      }
      else
      {
         limits_.min.x = std::max<float>(limits_.min.x, tmb_limits.min.x);
         limits_.max.x = std::max<float>(limits_.max.x, tmb_limits.max.x);
         limits_.min.y = std::max<float>(limits_.min.y, tmb_limits.min.y * 2);
      }

      return limits_;
   }

   void slider_base::layout(context const& ctx)
   {
      {
         context sctx {ctx, &track(), ctx.bounds};
         sctx.bounds = track_bounds(sctx);
         track().layout(sctx);
      }
      {
         context sctx {ctx, &thumb(), ctx.bounds};
         sctx.bounds = thumb_bounds(sctx);
         thumb().layout(sctx);
      }
   }

   void slider_base::draw(context const& ctx)
   {
      if (intersects(ctx.bounds, ctx.view_bounds()))
      {
         {
            context sctx {ctx, &track(), ctx.bounds};
            sctx.bounds = track_bounds(sctx);
            track().draw(sctx);
         }
         {
            context sctx {ctx, &thumb(), ctx.bounds};
            sctx.bounds = thumb_bounds(sctx);
            thumb().draw(sctx);
         }

         if (_has_focus && ctx.enabled && get_theme().focus_ring_enabled)
         {
            auto&       cnv = ctx.canvas;
            auto        state = cnv.new_state();
            auto const& th = get_theme();
            auto        tb = thumb_bounds(ctx).inset(-2.0f, -2.0f);
            float       radius = std::min(tb.width(), tb.height()) * 0.5f;
            cnv.line_width(th.focus_ring_width);
            cnv.stroke_style(th.focus_ring_color);
            cnv.begin_path();
            cnv.add_round_rect(tb, radius);
            cnv.stroke();
         }
      }
   }

   bool slider_base::scroll(context const& ctx, point dir, point p)
   {
      auto sdir = scroll_direction();
      double new_value = value() + (_is_horiz ? dir.x*sdir.x + !dir.x*dir.y*-sdir.y : dir.y * -sdir.y) * 0.005;
      clamp(new_value, 0.0, 1.0);
      track_scroll(ctx, dir, p);
      edit_value(new_value);
      ctx.view.refresh(ctx);
      return true;
   }

   rect slider_base::track_bounds(context const& ctx) const
   {
      auto  limits_ = track().limits(ctx);
      auto  bounds = ctx.bounds;
      auto  th_bounds = thumb_bounds(ctx);

      if (_is_horiz)
      {
         bounds.height(std::min<float>(limits_.max.y, bounds.height()));
         auto w2 = th_bounds.width() / 2;
         bounds.left += w2;
         bounds.right -= w2;
         bounds = center_v(bounds, ctx.bounds);
      }
      else
      {
         bounds.width(std::min<float>(limits_.max.x, bounds.width()));
         auto h2 = th_bounds.height() / 2;
         bounds.top += h2;
         bounds.bottom -= h2;
         bounds = center_h(bounds, ctx.bounds);
      }
      return bounds;
   }

   rect slider_base::thumb_bounds(context const& ctx) const
   {
      auto  bounds = ctx.bounds;
      auto  w = bounds.width();
      auto  h = bounds.height();
      auto  limits_ = thumb().limits(ctx);
      auto  tmb_w = limits_.max.x;
      auto  tmb_h = limits_.max.y;

      if (_is_horiz)
      {
         bounds.width(tmb_w);
         bounds.height(tmb_h);
         bounds = center_v(bounds, ctx.bounds);
         return bounds.move((w - tmb_w) * value(), 0);
      }
      else
      {
         bounds.height(tmb_h);
         bounds.width(tmb_w);
         bounds = center_h(bounds, ctx.bounds);
         return bounds.move(0, (h - tmb_h) * (1.0 - value()));
         // Note: for vertical sliders, 0.0 is at the bottom, hence 1.0-value()
      }
   }

   point slider_base::focus_hot_point(context const& ctx)
   {
      return center_point(thumb_bounds(ctx));
   }

   double slider_base::value_from_point(context const& ctx, point p)
   {
      auto  bounds = ctx.bounds;
      auto  w = bounds.width();
      auto  h = bounds.height();

      auto  limits_ = thumb().limits(ctx);
      auto  tmb_w = limits_.max.x;
      auto  tmb_h = limits_.max.y;
      auto  new_value = 0.0;

      // Note: for vertical sliders, 0.0 is at the bottom, hence 1.0-computed_value
      if (_is_horiz)
         new_value = (p.x - (bounds.left + (tmb_w / 2))) / (w - tmb_w);
      else
         new_value = 1.0 - ((p.y - (bounds.top + (tmb_h / 2))) / (h - tmb_h));
      return clamp(new_value, 0.0, 1.0);
   }

   void slider_base::begin_tracking(context const& ctx, tracker_info& track_info)
   {
      auto tmb_bounds = thumb_bounds(ctx);
      if (tmb_bounds.includes(track_info.current))
      {
         auto cp = center_point(tmb_bounds);
         track_info.offset.x = track_info.current.x - cp.x;
         track_info.offset.y = track_info.current.y - cp.y;
      }
   }

   void slider_base::keep_tracking(context const& ctx, tracker_info& track_info)
   {
      if (track_info.current != track_info.previous)
      {
         double new_value = value_from_point(ctx, track_info.current);
         if (_value != new_value)
         {
            edit_value(new_value);
            ctx.view.refresh(ctx);
         }
      }
   }

   void slider_base::end_tracking(context const& ctx, tracker_info& track_info)
   {
      double new_value = value_from_point(ctx, track_info.current);
      if (_value != new_value)
      {
         edit_value(new_value);
         ctx.view.refresh(ctx);
      }
   }

   bool slider_base::pad_axis(context const& ctx, pad_axis_info info)
   {
      if (!ctx.enabled)
         return false;

      // Determine the input's axis orientation.
      bool horizontal_axis =
            info.axis == pad_axis::dpad_x
         || info.axis == pad_axis::left_x
         || info.axis == pad_axis::right_x;
      bool vertical_axis =
            info.axis == pad_axis::dpad_y
         || info.axis == pad_axis::left_y
         || info.axis == pad_axis::right_y;

      if (!horizontal_axis && !vertical_axis)
         return false;

      // Only respond to the axis that matches our own orientation.
      if (_is_horiz && !horizontal_axis) return false;
      if (!_is_horiz && !vertical_axis)  return false;

      // SDL Y axis: +y is downward; map to "down decreases value".
      float v = info.value;
      if (vertical_axis)
         v = -v;

      double delta = double(v)
                   * ctx.view.stick_value_speed()
                   * ctx.view.frame_dt();
      double new_val = value() + delta;
      clamp(new_val, 0.0, 1.0);
      if (new_val != value())
      {
         edit_value(new_val);
         ctx.view.refresh(ctx);
      }
      return true;
   }

   bool slider_base::cursor(context const& ctx, point p, cursor_tracking status)
   {
      // Pointer-driven focus, same as basic_button::cursor: when the cursor
      // moves over the slider, sync keyboard focus to it (unless the view
      // disabled hover_focus). Guarded by !focused() so it only fires on the
      // transition in. This is what makes focus_row highlights and focus
      // frames respond to mouse hover for sliders, not just buttons.
      if (status != cursor_tracking::leaving && !focused()
          && ctx.enabled && is_enabled() && ctx.view.hover_focus())
         ctx.view.focus(*this);
      return tracker<>::cursor(ctx, p, status);
   }

   bool slider_base::wants_focus() const
   {
      return true;
   }

   void slider_base::begin_focus(focus_request /*req*/)
   {
      _has_focus = true;
   }

   bool slider_base::end_focus()
   {
      _has_focus = false;
      return true;
   }

   bool slider_base::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled)
         return false;
      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;

      // Only consume the arrow axis that matches the slider's own
      // orientation. The off-axis arrows are deliberately left
      // unhandled so they fall through to view-level 2D focus
      // navigation (when enabled).
      double delta = 0.0;
      bool   absolute = false;
      double abs_val = 0.0;

      switch (k.key)
      {
         case key_code::left:
            if (!_is_horiz) return false;
            delta = -0.05;
            break;
         case key_code::right:
            if (!_is_horiz) return false;
            delta = +0.05;
            break;
         case key_code::down:
            if (_is_horiz) return false;
            delta = -0.05;
            break;
         case key_code::up:
            if (_is_horiz) return false;
            delta = +0.05;
            break;
         case key_code::page_down:
            delta = -0.1;
            break;
         case key_code::page_up:
            delta = +0.1;
            break;
         case key_code::home:
            absolute = true;
            abs_val = _is_horiz ? 0.0 : 1.0;
            break;
         case key_code::end:
            absolute = true;
            abs_val = _is_horiz ? 1.0 : 0.0;
            break;
         default:
            return false;
      }

      double new_value = absolute ? abs_val : (value() + delta);
      new_value = clamp(new_value, 0.0, 1.0);
      if (new_value != value())
      {
         edit_value(new_value);
         ctx.view.refresh(ctx);
      }
      return true;
   }

   void slider_base::value(double val)
   {
      _value = clamp(val, 0.0, 1.0);
   }

   double slider_base::value() const
   {
      return _value;
   }

   void basic_slider_base::edit_value(double val)
   {
      slider_base::edit_value(val);
      if (on_change)
         on_change(val);
   }

   void basic_selector_base::select(size_t val)
   {
      if (on_change)
         on_change(val);
   }
}

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/thumbwheel.hpp>
#include <elements/element/traversal.hpp>
#include <elements/support/theme.hpp>
#include <elements/view.hpp>
#include <algorithm>
#include <cmath>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // thumbwheel_base
   ////////////////////////////////////////////////////////////////////////////
   thumbwheel_base::thumbwheel_base(point init)
    : _value{init}
   {
      clamp(_value.x, 0.0, 1.0);
      clamp(_value.y, 0.0, 1.0);
   }

   void thumbwheel_base::prepare_subject(context& ctx)
   {
      proxy_base::prepare_subject(ctx);
      if (auto* rcvr = find_subject<receiver<double>*>(this))
         rcvr->value(_value.y);
      else if (auto* rcvr = find_subject<receiver<point>*>(this))
         rcvr->value(_value);
   }

   void thumbwheel_base::value(point val)
   {
      _value.x = clamp(val.x, 0.0, 1.0);
      _value.y = clamp(val.y, 0.0, 1.0);
      if (auto* rcvr = find_subject<receiver<double>*>(this))
         rcvr->value(_value.y);
      else if (auto* rcvr = find_subject<receiver<point>*>(this))
         rcvr->value(_value);
   }

   namespace
   {
      inline void edit_value(thumbwheel_base* this_, point val)
      {
         this_->value(val);
         if (this_->on_change)
         {
            auto new_val = this_->value();
            this_->on_change(new_val);
         }
      }
   }

   void thumbwheel_base::edit(view& view_, point val)
   {
      edit_value(this, val);
      receiver<point>::notify_edit(view_);
   }

   point thumbwheel_base::compute_value(context const& /*ctx*/, tracker_info& track_info)
   {
      point delta{
         track_info.current.x - track_info.previous.x,
         track_info.current.y - track_info.previous.y
      };

      double factor = 1.0 / get_theme().dial_linear_range;
      if (track_info.modifiers & mod_shift)
         factor /= 5.0;

      float x = _value.x + factor * delta.x;
      float y = _value.y + factor * -delta.y;
      return {clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0)};
   }

   void thumbwheel_base::keep_tracking(context const& ctx, tracker_info& track_info)
   {
      if (track_info.current != track_info.previous)
      {
         auto new_value = compute_value(ctx, track_info);
         if (_value != new_value)
         {
            edit_value(this, new_value);
            ctx.view.refresh(ctx);
         }
      }
   }

   bool thumbwheel_base::scroll(context const& ctx, point dir, point p)
   {
      auto sdir = scroll_direction();
      track_scroll(ctx, dir, p);
      edit_value(this,
         {
            _value.x + (sdir.x * dir.x * 0.005f),
            _value.y - (sdir.y * dir.y * 0.005f)
         }
      );
      ctx.view.refresh(ctx);
      return true;
   }

   bool thumbwheel_base::wants_focus() const
   {
      return true;
   }

   void thumbwheel_base::begin_focus(focus_request /*req*/)
   {
      _has_focus = true;
   }

   bool thumbwheel_base::end_focus()
   {
      _has_focus = false;
      return true;
   }

   void thumbwheel_base::draw(context const& ctx)
   {
      proxy_base::draw(ctx);
      if (_has_focus && ctx.enabled)
      {
         auto&       cnv = ctx.canvas;
         auto        state = cnv.new_state();
         auto const& th = get_theme();
         auto        b = ctx.bounds.inset(-2.0f, -2.0f);
         float       r = std::min(b.width(), b.height()) * 0.25f;
         cnv.line_width(th.focus_ring_width);
         cnv.stroke_style(th.focus_ring_color);
         cnv.begin_path();
         cnv.add_round_rect(b, r);
         cnv.stroke();
      }
   }

   bool thumbwheel_base::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled)
         return false;
      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;

      point v = _value;
      switch (k.key)
      {
         case key_code::left:
            v.x -= 0.05f;
            break;
         case key_code::right:
            v.x += 0.05f;
            break;
         case key_code::down:
            v.y -= 0.05f;
            break;
         case key_code::up:
            v.y += 0.05f;
            break;
         case key_code::page_down:
            v.y -= 0.1f;
            break;
         case key_code::page_up:
            v.y += 0.1f;
            break;
         case key_code::home:
            v = {0.0f, 1.0f};
            break;
         case key_code::end:
            v = {1.0f, 0.0f};
            break;
         default:
            return false;
      }

      v.x = std::clamp(v.x, 0.0f, 1.0f);
      v.y = std::clamp(v.y, 0.0f, 1.0f);
      if (v != _value)
      {
         edit_value(this, v);
         ctx.view.refresh(ctx);
      }
      return true;
   }
}

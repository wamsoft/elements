/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/button.hpp>
#include <elements/element/traversal.hpp>
#include <elements/support/theme.hpp>

namespace cycfi::elements
{
   bool basic_button::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled || !is_enabled())
         return false;

      if (btn.state != mouse_button::left || !ctx.bounds.includes(btn.pos))
      {
         tracking(false);
         ctx.view.refresh(ctx);
         return false;
      }

      if (btn.down)
      {
         tracking(true);
         on_tracking(ctx, begin_tracking);
      }
      else
      {
         tracking(false);
         on_tracking(ctx, end_tracking);
         if (on_click)
            on_click(true);
         ctx.view.refresh(ctx);
      }

      if (set_value(btn.down && ctx.bounds.includes(btn.pos)))
         ctx.view.refresh(ctx);
      return true;
   }

   element* basic_button::hit_test(context const& ctx, point p, bool leaf, bool /*control*/)
   {
      return proxy_base::hit_test(ctx, p, leaf, false); // accept non-control subjects
   }

   bool basic_button::cursor(context const& ctx, point /* p */, cursor_tracking status)
   {
      if (!is_enabled())
         return false;
      bool is_leaving = status != cursor_tracking::leaving;
      if (_state.hilite != is_leaving)
         hilite(is_leaving);
      refresh(ctx);
      return false;
   }

   void basic_button::drag(context const& ctx, mouse_button btn)
   {
      this->hilite(ctx.bounds.includes(btn.pos));
      if (set_value(ctx.bounds.includes(btn.pos)))
         ctx.view.refresh(ctx);
   }

   bool basic_button::wants_control() const
   {
      return true;
   }

   void basic_button::enable(bool state)
   {
      _state.enabled = state;
   }

   bool basic_button::is_enabled() const
   {
      return _state.enabled;
   }

   bool basic_button::set_value(bool val)
   {
      if (val != _state.value)
      {
         _state.value = val;
         return true;
      }
      return false;
   }

   void basic_button::tracking(bool val)
   {
      if (val != _state.tracking)
         _state.tracking = val;
   }
   void basic_button::hilite(bool val)
   {
      if (val != _state.hilite)
         _state.hilite = val;
   }

   void basic_button::focused(bool val)
   {
      if (val != _state.focus)
         _state.focus = val;
   }

   bool basic_button::wants_focus() const
   {
      return true;
   }

   void basic_button::begin_focus(focus_request /*req*/)
   {
      focused(true);
   }

   bool basic_button::end_focus()
   {
      focused(false);
      return true;
   }

   void basic_button::draw(context const& ctx)
   {
      // Let the styler render the body first.
      proxy_base::draw(ctx);

      // Then overlay a focus ring on top so it is visible regardless of
      // what the styler painted underneath.
      if (_state.focus && ctx.enabled && is_enabled())
      {
         auto&       cnv = ctx.canvas;
         auto        state = cnv.new_state();
         auto const& th = get_theme();
         auto        r = ctx.bounds.inset(-1.0f, -1.0f);
         cnv.line_width(th.focus_ring_width);
         cnv.stroke_style(th.focus_ring_color);
         cnv.begin_path();
         cnv.add_round_rect(r, th.button_corner_radius);
         cnv.stroke();
      }
   }

   void basic_button::activate(context const& ctx)
   {
      // Momentary: pulse value true → false, then fire on_click.
      set_value(true);
      if (on_click)
         on_click(true);
      set_value(false);
      ctx.view.refresh(ctx);
   }

   bool basic_button::key(context const& ctx, key_info k)
   {
      if (!ctx.enabled || !is_enabled())
         return false;

      if (k.action != key_action::press && k.action != key_action::repeat)
         return false;

      if (k.key != key_code::space && k.key != key_code::enter
          && k.key != key_code::kp_enter)
         return false;

      activate(ctx);
      return true;
   }

   /**
    * \brief
    *    Set the value of the button
    *
    * \param val
    *    A boolean value representing the value of the button `true` = ON,
    *    `false` = OPF.
    */
   void basic_button::value(bool val)
   {
      if (_state.value != val)
         set_value(val);
   }

   /**
    *  \brief
    *    Initiates editing the state of the button and sends notifications.
    *
    *    If `on_click` callback is set, it is called with the new state value
    *    `val`, and the `notify_edit(view_)` function is called which sends
    *    notifications about the state change.
    *
    *  \param view_
    *    A reference to the view.
    *
    *  \param val
    *    The new state value for the button: `true` if the button is ON, and
    *    `false` if OFF.
    */
   void basic_button::edit(view& view_, bool val)
   {
      if (on_click)
         on_click(val);
      receiver<bool>::notify_edit(view_);
   }

   bool basic_toggle_button::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled || !this->is_enabled())
         return false;

      if (btn.state != mouse_button::left || !ctx.bounds.includes(btn.pos))
      {
         this->tracking(false);
         ctx.view.refresh(ctx);
         return false;
      }

      if (btn.down)
      {
         this->tracking(true);
         if (this->set_value(!this->value()))   // toggle the state
         {
            ctx.view.refresh(ctx);              // we need to save the current state, the state
            _current_state = this->value();     // can change in the drag function and so we'll
         }                                      // need it later when the button is finally released
      }
      else
      {
         this->tracking(false);
         this->set_value(_current_state);
         if (this->on_click)
            this->on_click(this->value());
         ctx.view.refresh(ctx);
      }
      return true;
   }

   void basic_toggle_button::drag(context const& ctx, mouse_button btn)
   {
      this->hilite(ctx.bounds.includes(btn.pos));
      if (this->set_value(!_current_state ^ ctx.bounds.includes(btn.pos)))
         ctx.view.refresh(ctx);
   }

   void basic_toggle_button::activate(context const& ctx)
   {
      // Toggle: flip the persistent value and notify.
      bool const new_val = !this->value();
      this->set_value(new_val);
      _current_state = new_val;
      if (this->on_click)
         this->on_click(new_val);
      ctx.view.refresh(ctx);
   }

   void basic_latching_button::activate(context const& ctx)
   {
      // Latching: only fires from unlatched → latched. Once on, stays on
      // until reset programmatically (matches click() semantics).
      if (this->value())
         return;
      this->set_value(true);
      if (this->on_click)
         this->on_click(true);
      ctx.view.refresh(ctx);
   }

   bool basic_latching_button::click(context const& ctx, mouse_button btn)
   {
      if (btn.down && this->value())
         return false;

      if (btn.state != mouse_button::left || !ctx.bounds.includes(btn.pos))
      {
         this->tracking(false);
         ctx.view.refresh(ctx);
         return false;
      }

      if (btn.down)
      {
         this->tracking(true);
         this->on_tracking(ctx, this->begin_tracking);
      }
      else
      {
         this->tracking(false);
         this->on_tracking(ctx, this->end_tracking);
         if (this->on_click)
            this->on_click(true);
         ctx.view.refresh(ctx);
      }
      if (btn.down && this->set_value(ctx.bounds.includes(btn.pos)))
         ctx.view.refresh(ctx);
      return true;
   }

   void basic_choice::activate(context const& ctx)
   {
      // Choice: latch self, then deselect every sibling that is also a
      // selectable. Mirrors the release-half of basic_choice::click().
      if (this->value())
         return;
      this->set_value(true);
      if (this->on_click)
         this->on_click(true);

      auto [c, cctx] = find_composite(ctx);
      if (c)
      {
         for (std::size_t i = 0; i != c->size(); ++i)
         {
            if (auto e = find_element<selectable*>(&c->at(i)))
            {
               if (e == this)
                  e->select(true);
               else if (e->is_selected())
                  e->select(false);
            }
         }
         cctx->view.refresh(*cctx);
      }
      else
      {
         ctx.view.refresh(ctx);
      }
   }

   bool basic_choice::click(context const& ctx, mouse_button btn)
   {
      if (btn.state == mouse_button::left)
      {
         if (btn.down)
         {
            return basic_latching_button::click(ctx, btn);
         }
         else
         {
            auto r = basic_latching_button::click(ctx, btn);
            if (this->value())
            {
               auto [c, cctx] = find_composite(ctx);
               if (c)
               {
                  for (std::size_t i = 0; i != c->size(); ++i)
                  {
                     if (auto e = find_element<selectable*>(&c->at(i)))
                     {
                        if (e == this)
                        {
                           // Set the button
                           e->select(true);
                        }
                        else
                        {
                           if (e->is_selected())
                           {
                              // Reset the button
                              e->select(false);
                           }
                        }
                     }
                  }
               }
               cctx->view.refresh(*cctx);
            }
            return r;
         }
      }
      return false;
   }
}

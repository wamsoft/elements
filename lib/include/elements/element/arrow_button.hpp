/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ARROW_BUTTON_JUNE_12_2026)
#define ELEMENTS_ARROW_BUTTON_JUNE_12_2026

#include <elements/element/element.hpp>
#include <elements/element/slider.hpp>
#include <algorithm>
#include <functional>
#include <memory>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // arrow_button
   //
   // A small framed box with a filled triangle pointing left, right, up or
   // down. Reacts to mouse clicks and fires `on_step`. NOT focusable, so Tab
   // navigation skips it and the arrow keys keep driving the focused
   // sibling widget (typically a picker or slider).
   //
   // Holding the button down auto-repeats: the first `on_step` fires on press,
   // then again every `repeat_rate_ms` after an initial `repeat_delay_ms`
   // pause. Call `repeat(false)` for one step per click. Same defaults as
   // `atlas_stepper`, the image-material counterpart.
   ////////////////////////////////////////////////////////////////////////////
   class arrow_button : public element
   {
   public:

      enum direction { arrow_left, arrow_right, arrow_up, arrow_down };
      using on_step_function = std::function<void()>;

                              arrow_button(direction dir, on_step_function cb = {});

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return false; }
      bool                    click(context const& ctx, mouse_button btn) override;
      void                    drag(context const& ctx, mouse_button btn) override;
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;

      direction               dir() const { return _dir; }
      on_step_function        on_step;

      // Hold-to-repeat.
      void                    repeat(bool on) { _repeat = on; }
      bool                    repeat() const  { return _repeat; }
      void                    repeat_delay_ms(int ms) { _repeat_delay_ms = ms; }
      int                     repeat_delay_ms() const { return _repeat_delay_ms; }
      void                    repeat_rate_ms(int ms)  { _repeat_rate_ms = ms; }
      int                     repeat_rate_ms() const  { return _repeat_rate_ms; }

   private:

      void                    start_repeat(context const& ctx);
      void                    stop_repeat();

      direction               _dir;
      bool                    _pressed = false;
      bool                    _hovered = false;
      bool                    _held_inside = true;

      bool                    _repeat = true;
      int                     _repeat_delay_ms = 400;
      int                     _repeat_rate_ms = 60;
      std::shared_ptr<void>   _repeat_timer;
      // The repeat closure re-posts itself, so the body lives here and the
      // posted copy sees it weakly (otherwise it keeps itself alive forever).
      std::shared_ptr<std::function<void()>> _repeat_tick;
   };

   ////////////////////////////////////////////////////////////////////////////
   // make_step_arrows_for_slider
   //   Wire a pair of arrow_buttons to nudge a slider by `step_v` per click.
   //   step_v is in normalized 0..1 units (default 5%).
   //
   //   Returns {dec, inc}: the arrow that lowers the value first, the one that
   //   raises it second. The pair is named by what it *does*, not by where it
   //   sits, because the geometry flips with the orientation: a horizontal
   //   slider decreases to the left, a vertical one decreases downwards
   //   (value 0 is at the bottom). `vertical` only picks which way the
   //   triangles point; the caller still places them.
   ////////////////////////////////////////////////////////////////////////////
   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows_for_slider(std::shared_ptr<basic_slider_base> target,
                               double step_v = 0.05, bool vertical = false)
   {
      auto dec = share(arrow_button(
         vertical ? arrow_button::arrow_down : arrow_button::arrow_left,
         [w = std::weak_ptr<basic_slider_base>(target), step_v]() {
            if (auto t = w.lock())
            {
               double v = std::clamp(t->value() - step_v, 0.0, 1.0);
               t->edit_value(v);
            }
         }));
      auto inc = share(arrow_button(
         vertical ? arrow_button::arrow_up : arrow_button::arrow_right,
         [w = std::weak_ptr<basic_slider_base>(target), step_v]() {
            if (auto t = w.lock())
            {
               double v = std::clamp(t->value() + step_v, 0.0, 1.0);
               t->edit_value(v);
            }
         }));
      return {dec, inc};
   }
}

#endif

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
   // A small framed box with a filled left- or right-pointing triangle.
   // Reacts to mouse clicks and fires `on_step`. NOT focusable, so Tab
   // navigation skips it and the arrow keys keep driving the focused
   // sibling widget (typically a picker or slider).
   ////////////////////////////////////////////////////////////////////////////
   class arrow_button : public element
   {
   public:

      enum direction { arrow_left, arrow_right };
      using on_step_function = std::function<void()>;

                              arrow_button(direction dir, on_step_function cb = {});

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return false; }
      bool                    click(context const& ctx, mouse_button btn) override;
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;

      direction               dir() const { return _dir; }
      on_step_function        on_step;

   private:

      direction               _dir;
      bool                    _pressed = false;
      bool                    _hovered = false;
   };

   ////////////////////////////////////////////////////////////////////////////
   // make_step_arrows_for_slider
   //   Wire a pair of arrow_buttons to nudge a slider by `step_v` per click.
   //   step_v is in normalized 0..1 units (default 5%).
   ////////////////////////////////////////////////////////////////////////////
   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows_for_slider(std::shared_ptr<basic_slider_base> target, double step_v = 0.05)
   {
      auto l = share(arrow_button(arrow_button::arrow_left,
         [w = std::weak_ptr<basic_slider_base>(target), step_v]() {
            if (auto t = w.lock())
            {
               double v = std::clamp(t->value() - step_v, 0.0, 1.0);
               t->edit_value(v);
            }
         }));
      auto r = share(arrow_button(arrow_button::arrow_right,
         [w = std::weak_ptr<basic_slider_base>(target), step_v]() {
            if (auto t = w.lock())
            {
               double v = std::clamp(t->value() + step_v, 0.0, 1.0);
               t->edit_value(v);
            }
         }));
      return {l, r};
   }
}

#endif

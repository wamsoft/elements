/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/focus_row.hpp>
#include <elements/view.hpp>

namespace cycfi::elements
{
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
      // (slider drag start, picker click-to-cycle, etc.).
      if (auto* sub = proxy_base::hit_test(ctx, p, leaf, control))
         return sub;
      // Otherwise (typically the decorative label area) accept the hit
      // here so click() can move focus to the target.
      return this;
   }

   bool focus_row_element::click(context const& ctx, mouse_button btn)
   {
      // First let the subject handle the click. If the inner control
      // consumes it, that's the normal path — we MUST forward, otherwise
      // focus_row would swallow every click that vtile/composite
      // dispatched to us as the immediate-child-with-wants_control.
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

/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_FOCUS_ROW_JUNE_12_2026)
#define ELEMENTS_FOCUS_ROW_JUNE_12_2026

#include <elements/element/align.hpp>
#include <elements/element/element.hpp>
#include <elements/element/indirect.hpp>
#include <elements/element/label.hpp>
#include <elements/element/margin.hpp>
#include <elements/element/proxy.hpp>
#include <elements/element/size.hpp>
#include <elements/element/slider.hpp>
#include <elements/element/style/slider.hpp>
#include <elements/element/tile.hpp>
#include <elements/support/color.hpp>
#include <string>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // focus_row_element
   //   Wraps any element (typically an htile of [label, control]) and draws
   //   a row-wide highlight band when any descendant currently has focus.
   //   Focus stays on the inner control.
   //
   //   When constructed with a `target` element, clicking anywhere in the
   //   row (including the decorative label area) moves focus to the
   //   target — so labels behave as part of the focusable area for mouse
   //   input.
   ////////////////////////////////////////////////////////////////////////////
   class focus_row_element : public proxy_base
   {
   public:

                              focus_row_element() = default;
      explicit                focus_row_element(element_ptr target)
                               : _target(target) {}

      void                    draw(context const& ctx) override;
      element*                hit_test(context const& ctx, point p, bool leaf, bool control) override;
      bool                    click(context const& ctx, mouse_button btn) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return false; }

      void                    target(element_ptr t) { _target = t; }

   private:

      weak_element_ptr        _target;
   };

   template <concepts::Element Subject>
   inline proxy<remove_cvref_t<Subject>, focus_row_element>
   focus_row(Subject&& subject, element_ptr target = {})
   {
      return {std::forward<Subject>(subject), target};
   }

   ////////////////////////////////////////////////////////////////////////////
   // labeled_row
   //   Builds the htile internally so callers can write:
   //     labeled_row("System", shared_picker_ptr)
   //   The label gets a fixed left column (default 180px); the control fills
   //   the rest of the row. The control_ptr is held both as the visual
   //   content and as the click-focus target.
   ////////////////////////////////////////////////////////////////////////////
   inline auto labeled_row(
      std::string label_text,
      element_ptr control,
      float       label_width = 180.0f,
      float       font_size   = 1.0f)
   {
      auto lbl = hsize(label_width, align_left(hmargin({8, 8},
         label(std::move(label_text))
            .font_color(colors::white)
            .relative_font_size(font_size)
      )));
      auto row = htile(
         std::move(lbl),
         hold(control)
      );
      return focus_row(std::move(row), control);
   }

   // Overload for cases where the visual element and the focusable target
   // differ (e.g., a slider sandwiched between min/max labels).
   inline auto labeled_row(
      std::string label_text,
      element_ptr visual,
      element_ptr focus_target,
      float       label_width = 180.0f,
      float       font_size   = 1.0f)
   {
      auto lbl = hsize(label_width, align_left(hmargin({8, 8},
         label(std::move(label_text))
            .font_color(colors::white)
            .relative_font_size(font_size)
      )));
      auto row = htile(
         std::move(lbl),
         hold(visual)
      );
      return focus_row(std::move(row), focus_target);
   }

   ////////////////////////////////////////////////////////////////////////////
   // slider_with_range
   //   Slider with [min] [track] [max] decoration. Returns a struct whose
   //   `widget` field is the visual row and `focus` field is the inner
   //   slider (focusable). Use the `focus` element as the click-to-focus
   //   target when handing to the labeled_row overload that takes a
   //   separate focus target.
   ////////////////////////////////////////////////////////////////////////////
   struct ranged_slider
   {
      element_ptr widget;
      element_ptr focus;
   };

   inline ranged_slider slider_with_range(
      int min_v, int max_v, double initial = 0.5, float font_size = 1.0f)
   {
      auto sl = share(slider(
         basic_thumb<16>(colors::white),
         basic_track<6, false>(colors::white.opacity(0.4f)),
         initial
      ));
      auto row = share(htile(
         hsize(40 * font_size, align_right(
            label(std::to_string(min_v))
               .font_color(colors::white)
               .relative_font_size(font_size)
         )),
         hmargin({8, 8}, hold(sl)),
         hsize(40 * font_size, align_left(
            label(std::to_string(max_v))
               .font_color(colors::white)
               .relative_font_size(font_size)
         ))
      ));
      return {row, sl};
   }
}

#endif

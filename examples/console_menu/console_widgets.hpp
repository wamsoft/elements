#if !defined(ELEMENTS_EXAMPLES_CONSOLE_WIDGETS_HPP)
#define ELEMENTS_EXAMPLES_CONSOLE_WIDGETS_HPP

#include <elements.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace console_menu
{
   using namespace cycfi::elements;

   // ====================================================================
   // trigger_selector
   //   ◀ <current option> ▶
   //   Left/Right (and dpad_x / left_stick_x) cycle through options.
   //   Up/Down pass through to view-level focus navigation.
   //   Selection wraps around at the ends.
   // ====================================================================
   class trigger_selector : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              trigger_selector(
                                 std::vector<std::string> options,
                                 std::size_t initial = 0);

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return true; }
      void                    begin_focus(focus_request) override { _has_focus = true; }
      bool                    end_focus() override { _has_focus = false; return true; }

      bool                    click(context const& ctx, mouse_button btn) override;
      bool                    key(context const& ctx, key_info k) override;
      bool                    pad_axis(context const& ctx, pad_axis_info info) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);

      // Steps the selection by `delta` (wraps). Returns true if the
      // index changed. Public so external arrow_buttons can drive it.
      bool                    step(int delta);

      on_change_function      on_change;

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      std::chrono::steady_clock::time_point _last_pad_step{};
   };

   // ====================================================================
   // arrow_button
   //   A small framed box with a filled ◀ or ▶ triangle. Reacts to
   //   mouse / touch clicks and fires `on_step` — but is NOT a focus
   //   target (so Tab navigation skips it and the arrow keys keep
   //   driving the focused selector instead).
   //
   //   Wire it to any control via `on_step`:
   //     auto sel = share(trigger_selector(...));
   //     auto [larrow, rarrow] = make_step_arrows(sel);
   //   ...then place the three side-by-side inside a row:
   //     auto unit = htile(hold(larrow), hold(sel), hold(rarrow));
   //     labeled_row("System", unit, sel);
   // ====================================================================
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

   // (make_step_arrows helpers are declared further down, after every
   // target widget type has been introduced.)

   // ====================================================================
   // trigger_selector_arrows
   //   Same behavior as trigger_selector, but visually puts the ◀ / ▶
   //   triangles in their own framed "button" boxes flanking the value
   //   panel:
   //
   //     [◀]  [ current value ]  [▶]
   //
   //   Use this as an alternative visual when you want the arrows to
   //   look like distinct, clickable parts. Clicking on the left box
   //   decrements, the right box increments; clicking on the center
   //   just acquires focus.
   // ====================================================================
   class trigger_selector_arrows : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              trigger_selector_arrows(
                                 std::vector<std::string> options,
                                 std::size_t initial = 0);

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return true; }
      void                    begin_focus(focus_request) override { _has_focus = true; }
      bool                    end_focus() override { _has_focus = false; return true; }

      bool                    click(context const& ctx, mouse_button btn) override;
      bool                    key(context const& ctx, key_info k) override;
      bool                    pad_axis(context const& ctx, pad_axis_info info) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);
      bool                    step(int delta);

      on_change_function      on_change;

      static constexpr float arrow_box_w = 36.0f;
      static constexpr float gap         = 6.0f;

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      std::chrono::steady_clock::time_point _last_pad_step{};
   };

   // ====================================================================
   // segmented_selector
   //   [ Opt A | Opt B | Opt C ]   (selected is highlighted)
   //   Left/Right (and dpad_x / left_stick_x) move selection.
   //   Stops at the ends — does not wrap, so end-of-row Right falls
   //   through to view-level focus navigation.
   // ====================================================================
   class segmented_selector : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              segmented_selector(
                                 std::vector<std::string> options,
                                 std::size_t initial = 0);

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return true; }
      void                    begin_focus(focus_request) override { _has_focus = true; }
      bool                    end_focus() override { _has_focus = false; return true; }

      bool                    click(context const& ctx, mouse_button btn) override;
      bool                    key(context const& ctx, key_info k) override;
      bool                    pad_axis(context const& ctx, pad_axis_info info) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);

      // Steps the selection by `delta` (clamps at ends). Returns true
      // if the index changed. Public so external arrow_buttons can drive it.
      bool                    step(int delta);

      on_change_function      on_change;

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      std::chrono::steady_clock::time_point _last_pad_step{};
   };

   // ====================================================================
   // Helpers: build a pair of arrow_buttons already wired to step
   // a target widget. Returns {left, right} shared_ptrs. The arrows
   // hold a weak_ptr to the target, so the target's lifetime
   // controls whether the arrows are active.
   // ====================================================================
   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows(std::shared_ptr<trigger_selector> target)
   {
      auto l = share(arrow_button(arrow_button::arrow_left,
         [w = std::weak_ptr<trigger_selector>(target)]() {
            if (auto t = w.lock()) t->step(-1);
         }));
      auto r = share(arrow_button(arrow_button::arrow_right,
         [w = std::weak_ptr<trigger_selector>(target)]() {
            if (auto t = w.lock()) t->step(+1);
         }));
      return {l, r};
   }

   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows(std::shared_ptr<segmented_selector> target)
   {
      auto l = share(arrow_button(arrow_button::arrow_left,
         [w = std::weak_ptr<segmented_selector>(target)]() {
            if (auto t = w.lock()) t->step(-1);
         }));
      auto r = share(arrow_button(arrow_button::arrow_right,
         [w = std::weak_ptr<segmented_selector>(target)]() {
            if (auto t = w.lock()) t->step(+1);
         }));
      return {l, r};
   }

   // For sliders: arrows nudge value by `step_v` (default 0.05 = 5%).
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

   // ====================================================================
   // focus_row
   //   Wraps any element (typically an htile of [label, control]) and
   //   draws a row-wide highlight band when any descendant currently has
   //   focus. Focus itself stays on the inner control.
   //
   //   When constructed with a `target` element, clicking anywhere in the
   //   row (including the decorative label area) moves focus to the
   //   target — so labels behave as part of the focusable area for
   //   mouse / touch input.
   // ====================================================================
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
   inline proxy<cycfi::remove_cvref_t<Subject>, focus_row_element>
   focus_row(Subject&& subject, element_ptr target = {})
   {
      return {std::forward<Subject>(subject), target};
   }

   // Convenience: builds the htile internally so callers can write
   //   labeled_row("System", shared_trigger_selector_ptr)
   // The label gets a fixed left column (default 180px); the control fills
   // the rest of the row. The control_ptr is held both as the visual
   // content and as the click-focus target.
   inline auto labeled_row(
      std::string label_text,
      element_ptr control,
      float       label_width = 180.0f)
   {
      auto lbl = hsize(label_width, align_left(hmargin({8, 8},
         label(std::move(label_text)).font_color(colors::white)
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
      float       label_width = 180.0f)
   {
      auto lbl = hsize(label_width, align_left(hmargin({8, 8},
         label(std::move(label_text)).font_color(colors::white)
      )));
      auto row = htile(
         std::move(lbl),
         hold(visual)
      );
      return focus_row(std::move(row), focus_target);
   }

   // Helper: slider with [min] [track] [max] decoration, like config1.png.
   //   slider_with_range(0, 100, 0.5)
   //   The returned struct exposes the inner slider so that surrounding
   //   helpers (e.g. labeled_row) can target it for click-to-focus.
   struct ranged_slider
   {
      element_ptr widget;   // the htile of [min] [slider] [max]
      element_ptr focus;    // the inner slider (focusable)
   };

   inline ranged_slider slider_with_range(int min_v, int max_v, double initial = 0.5)
   {
      auto sl = share(slider(
         basic_thumb<16>(colors::white),
         basic_track<6, false>(colors::white.opacity(0.4f)),
         initial
      ));
      auto row = share(htile(
         hsize(40, align_right(
            label(std::to_string(min_v)).font_color(colors::white)
         )),
         hmargin({8, 8}, hold(sl)),
         hsize(40, align_left(
            label(std::to_string(max_v)).font_color(colors::white)
         ))
      ));
      return {row, sl};
   }
}

#endif

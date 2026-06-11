/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_PICKER_JUNE_12_2026)
#define ELEMENTS_PICKER_JUNE_12_2026

#include <elements/element/element.hpp>
#include <elements/element/arrow_button.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // cycle_picker
   //   < current option >
   //   Left/Right (and dpad_x / left_stick_x) cycle through options.
   //   Up/Down pass through to view-level focus navigation.
   //   Selection wraps around at the ends.
   ////////////////////////////////////////////////////////////////////////////
   class cycle_picker : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              cycle_picker(
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

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      std::chrono::steady_clock::time_point _last_pad_step{};
   };

   ////////////////////////////////////////////////////////////////////////////
   // framed_cycle_picker
   //   Same selection model as cycle_picker, but visually presents the
   //   left/right arrows as separate framed boxes flanking a value panel:
   //
   //     [<]  [ current value ]  [>]
   //
   //   Click on left box decrements, right box increments, center just
   //   acquires focus.
   ////////////////////////////////////////////////////////////////////////////
   class framed_cycle_picker : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              framed_cycle_picker(
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

   ////////////////////////////////////////////////////////////////////////////
   // segmented_picker
   //   [ Opt A | Opt B | Opt C ]   (selected segment is inverted)
   //   Left/Right (and dpad_x / left_stick_x) move selection.
   //   Stops at the ends — does not wrap — so end-of-row Right falls
   //   through to view-level focus navigation.
   ////////////////////////////////////////////////////////////////////////////
   class segmented_picker : public element
   {
   public:

      using on_change_function = std::function<void(std::size_t)>;

                              segmented_picker(
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

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      std::chrono::steady_clock::time_point _last_pad_step{};
   };

   ////////////////////////////////////////////////////////////////////////////
   // make_step_arrows
   //   Build a pair of arrow_buttons already wired to step a target
   //   picker. Returns {left, right} shared_ptrs. The arrows hold a
   //   weak_ptr to the target so the target's lifetime controls whether
   //   the arrows are active.
   ////////////////////////////////////////////////////////////////////////////
   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows(std::shared_ptr<cycle_picker> target)
   {
      auto l = share(arrow_button(arrow_button::arrow_left,
         [w = std::weak_ptr<cycle_picker>(target)]() {
            if (auto t = w.lock()) t->step(-1);
         }));
      auto r = share(arrow_button(arrow_button::arrow_right,
         [w = std::weak_ptr<cycle_picker>(target)]() {
            if (auto t = w.lock()) t->step(+1);
         }));
      return {l, r};
   }

   inline std::pair<std::shared_ptr<arrow_button>, std::shared_ptr<arrow_button>>
   make_step_arrows(std::shared_ptr<segmented_picker> target)
   {
      auto l = share(arrow_button(arrow_button::arrow_left,
         [w = std::weak_ptr<segmented_picker>(target)]() {
            if (auto t = w.lock()) t->step(-1);
         }));
      auto r = share(arrow_button(arrow_button::arrow_right,
         [w = std::weak_ptr<segmented_picker>(target)]() {
            if (auto t = w.lock()) t->step(+1);
         }));
      return {l, r};
   }
}

#endif

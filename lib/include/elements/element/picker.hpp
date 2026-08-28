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
   // picker_text_font
   //   Shared text-font state for the picker widgets. The option text is
   //   drawn with the theme label font by default; font_family() overrides
   //   the family (and its variable-font axes, e.g. "Noto Sans JP#wght=500").
   //   An empty or unregistered name keeps the theme font.
   ////////////////////////////////////////////////////////////////////////////
   class picker_text_font
   {
   public:

      void                    font_size(float s) { _font_size = s; }
      float                   font_size() const  { return _font_size; }

      void                    font_family(std::string name);
      std::string const&      font_family() const { return _font_family; }

      // Theme label font with font_size()/font_family() applied.
      font_descr              text_font() const;

   protected:

      float                   _font_size = 1.0f;
      std::string             _font_family;
      unsigned char           _font_weight = font_constants::weight_normal;
      unsigned char           _font_slant  = font_constants::slant_normal;
      bool                    _font_resolved = false;
   };

   ////////////////////////////////////////////////////////////////////////////
   // cycle_picker
   //   < current option >
   //   Left/Right (and dpad_x / left_stick_x) cycle through options.
   //   Up/Down pass through to view-level focus navigation.
   //   Selection wraps around at the ends.
   ////////////////////////////////////////////////////////////////////////////
   class cycle_picker : public element, public picker_text_font
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
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);
      void                    set_index(std::size_t i);
      bool                    step(int delta);
      void                    set_options(std::vector<std::string> options);
      void                    set_enabled(std::vector<bool> mask);
      bool                    option_enabled(std::size_t i) const;

      std::size_t             num_options() const { return _options.size(); }
      std::string const&      option_text(std::size_t i) const { return _options[i]; }

      bool                    focused() const { return _has_focus; }

      on_change_function      on_change;

   private:

      std::vector<std::string> _options;
      std::vector<bool>       _enabled;   // empty = all enabled
      std::size_t             _index;
      bool                    _has_focus = false;
      bool                    _pad_engaged = false;   // pad-axis hysteresis state
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
   class framed_cycle_picker : public element, public picker_text_font
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
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);
      void                    set_index(std::size_t i);
      bool                    step(int delta);
      void                    set_options(std::vector<std::string> options);

      bool                    focused() const { return _has_focus; }

      on_change_function      on_change;

      static constexpr float arrow_box_w = 36.0f;
      static constexpr float gap         = 6.0f;

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      bool                    _pad_engaged = false;   // pad-axis hysteresis state
   };

   ////////////////////////////////////////////////////////////////////////////
   // segmented_picker
   //   [ Opt A | Opt B | Opt C ]   (selected segment is inverted)
   //   Left/Right (and dpad_x / left_stick_x) move selection.
   //   Stops at the ends — does not wrap — so end-of-row Right falls
   //   through to view-level focus navigation.
   ////////////////////////////////////////////////////////////////////////////
   class segmented_picker : public element, public picker_text_font
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
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;

      std::size_t             index() const { return _index; }
      void                    select(std::size_t i);
      void                    set_index(std::size_t i);
      bool                    step(int delta);
      void                    set_options(std::vector<std::string> options);

      bool                    focused() const { return _has_focus; }

      on_change_function      on_change;

   private:

      std::vector<std::string> _options;
      std::size_t             _index;
      bool                    _has_focus = false;
      bool                    _pad_engaged = false;   // pad-axis hysteresis state
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

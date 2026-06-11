#if !defined(ELEMENTS_EXAMPLES_CONSOLE_BUTTON_STYLER_HPP)
#define ELEMENTS_EXAMPLES_CONSOLE_BUTTON_STYLER_HPP

#include <elements.hpp>

namespace console_menu
{
   using namespace cycfi::elements;

   // Shared helper: derive focus/value/hilite from the parent basic_button.
   namespace detail
   {
      inline bool focused_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->focused();
      }
      inline bool pressed_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->value();
      }
      inline bool hilite_of(context const& ctx)
      {
         auto btn = find_parent<basic_button*>(ctx);
         return btn && btn->hilite();
      }
   }

   // -----------------------------------------------------------------------
   // Invert styler: black body + white frame + white text.
   // When focused: white body + black text (frame omitted).
   // -----------------------------------------------------------------------
   struct console_button_styler : default_button_styler, text_writer
   {
      using base_type = console_button_styler;

      explicit console_button_styler(std::string text)
       : _text(std::move(text))
      {}

      // text_reader / text_writer
      std::string const& get_text() const override { return _text; }
      void               set_text(cycfi::string_view t) override { _text = std::string{t.data(), t.size()}; }

      void  draw(context const& ctx) override;

      color get_body_color() const override          { return colors::black; }
      color get_active_body_color() const override   { return colors::white; }
      color get_text_color() const override          { return colors::white; }
      float get_corner_radius() const override       { return 0.0f; }

   private:

      std::string _text;
   };

   inline void console_button_styler::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto bounds  = ctx.bounds;
      auto enabled = ctx.enabled;
      bool focus   = detail::focused_of(ctx);
      bool pressed = detail::pressed_of(ctx);
      bool hilite  = detail::hilite_of(ctx);

      if (pressed)
         bounds = bounds.move(1, 1);

      color bg, fg;
      if (focus)
      {
         bg = colors::white;
         fg = colors::black;
      }
      else if (hilite)
      {
         bg = rgba(60, 60, 60, 255);
         fg = colors::white;
      }
      else
      {
         bg = colors::black;
         fg = colors::white;
      }

      if (!enabled)
      {
         auto a = get_theme().disabled_opacity;
         bg = bg.opacity(a);
         fg = fg.opacity(a);
      }

      // Body
      cnv.fill_style(bg);
      cnv.fill_rect(bounds);

      // Frame (non-focus only)
      if (!focus)
      {
         cnv.line_width(2.0f);
         cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
         cnv.stroke_rect(bounds.inset(1, 1));
      }

      // Label
      auto const& th = get_theme();
      auto font = th.label_font;
      font = font.size(font._size * get_size());

      cnv.font(font);
      cnv.fill_style(fg);
      cnv.text_align(cnv.center | cnv.middle);
      cnv.fill_text(
         get_text(),
         {bounds.left + bounds.width() / 2.0f, bounds.top + bounds.height() / 2.0f}
      );
   }

   // -----------------------------------------------------------------------
   // Outline styler: black body + white frame + white text.
   // When focused: adds an outer colored ring (color set at construction).
   // -----------------------------------------------------------------------
   struct console_outline_button_styler : default_button_styler, text_writer
   {
      using base_type = console_outline_button_styler;

      static constexpr float outline_width = 3.0f;
      static constexpr float outline_gap   = 2.0f;
      static constexpr float reserve       = outline_width + outline_gap;

      console_outline_button_styler(std::string text, color outline)
       : _text(std::move(text)), _outline(outline)
      {}

      std::string const& get_text() const override { return _text; }
      void               set_text(cycfi::string_view t) override { _text = std::string{t.data(), t.size()}; }

      view_limits limits(basic_context const& ctx) const override
      {
         auto lim = default_button_styler::limits(ctx);
         lim.min.x += 2 * reserve;
         lim.min.y += 2 * reserve;
         if (lim.max.x < full_extent - 2 * reserve)
            lim.max.x += 2 * reserve;
         if (lim.max.y < full_extent - 2 * reserve)
            lim.max.y += 2 * reserve;
         return lim;
      }

      void  draw(context const& ctx) override;

      color get_body_color() const override          { return colors::black; }
      color get_active_body_color() const override   { return colors::black; }
      color get_text_color() const override          { return colors::white; }
      float get_corner_radius() const override       { return 0.0f; }

   private:

      std::string _text;
      color       _outline;
   };

   inline void console_outline_button_styler::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      auto outer   = ctx.bounds;
      auto inner   = outer.inset(reserve, reserve);
      auto enabled = ctx.enabled;
      bool focus   = detail::focused_of(ctx);
      bool pressed = detail::pressed_of(ctx);
      bool hilite  = detail::hilite_of(ctx);

      if (pressed)
         inner = inner.move(1, 1);

      color bg, fg;
      if (hilite && !focus)
      {
         bg = rgba(60, 60, 60, 255);
         fg = colors::white;
      }
      else
      {
         bg = colors::black;
         fg = colors::white;
      }

      if (!enabled)
      {
         auto a = get_theme().disabled_opacity;
         bg = bg.opacity(a);
         fg = fg.opacity(a);
      }

      // Inner body
      cnv.fill_style(bg);
      cnv.fill_rect(inner);

      // Inner white frame (always present)
      cnv.line_width(2.0f);
      cnv.stroke_style(colors::white.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
      cnv.stroke_rect(inner.inset(1, 1));

      // Outer color ring (only when focused)
      if (focus)
      {
         auto ring = outer.inset(outline_width / 2.0f, outline_width / 2.0f);
         cnv.line_width(outline_width);
         cnv.stroke_style(_outline.opacity(enabled ? 1.0f : get_theme().disabled_opacity));
         cnv.stroke_rect(ring);
      }

      // Label
      auto const& th = get_theme();
      auto font = th.label_font;
      font = font.size(font._size * get_size());

      cnv.font(font);
      cnv.fill_style(fg);
      cnv.text_align(cnv.center | cnv.middle);
      cnv.fill_text(
         get_text(),
         {inner.left + inner.width() / 2.0f, inner.top + inner.height() / 2.0f}
      );
   }

   // -----------------------------------------------------------------------
   // console_basic_button
   //   basic_button does activate Space/Enter, but its `activate()` pulses
   //   value true → on_click → false synchronously and only refreshes at
   //   the end — so the styler never paints a value=true frame, and the
   //   button doesn't *look* pressed when activated by keyboard. Mouse
   //   works because it holds value=true between mouse-down and mouse-up.
   //
   //   This subclass gives keyboard the same press / release feel:
   //   - on Space/Enter press: set value=true, refresh, return true.
   //   - on repeat:           consume, no-op.
   //   - on release:          fire on_click, set value=false, refresh.
   //   - on focus loss:       clear value (avoid stuck-pressed if focus
   //                          is moved while a key is held).
   // -----------------------------------------------------------------------
   class console_basic_button : public basic_button
   {
   public:

      bool key(context const& ctx, key_info k) override
      {
         if (!ctx.enabled || !is_enabled())
            return false;
         if (k.key != key_code::space
             && k.key != key_code::enter
             && k.key != key_code::kp_enter)
            return false;

         if (k.action == key_action::press)
         {
            if (!value())
            {
               set_value(true);
               ctx.view.refresh(ctx);
            }
            return true;
         }
         if (k.action == key_action::release)
         {
            if (value())
            {
               if (on_click)
                  on_click(true);
               set_value(false);
               ctx.view.refresh(ctx);
            }
            return true;
         }
         return true; // consume repeats so they don't bubble
      }

      bool end_focus() override
      {
         if (value())
            set_value(false);
         return basic_button::end_focus();
      }
   };

   // -----------------------------------------------------------------------
   // Factories matching the elements' `button(text)` shape (returns a
   // momentary console_basic_button whose subject is our styler).
   // -----------------------------------------------------------------------
   inline auto console_button(std::string text)
   {
      return momentary_button<console_basic_button>(
         console_button_styler{std::move(text)}
      );
   }

   inline auto console_outline_button(std::string text, color outline)
   {
      return momentary_button<console_basic_button>(
         console_outline_button_styler{std::move(text), outline}
      );
   }
}

#endif

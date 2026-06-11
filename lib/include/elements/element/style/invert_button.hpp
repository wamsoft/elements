/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_STYLE_INVERT_BUTTON_JUNE_12_2026)
#define ELEMENTS_STYLE_INVERT_BUTTON_JUNE_12_2026

#include <elements/element/button.hpp>
#include <elements/element/style/button.hpp>
#include <elements/support/color.hpp>
#include <infra/string_view.hpp>
#include <string>
#include <utility>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // invert_button_styler
   //   Black body, 2px white frame, white text.
   //   When focused: white body, black text (frame omitted).
   //   Press: 1-pixel down-right nudge for visual depression.
   ////////////////////////////////////////////////////////////////////////////
   struct invert_button_styler : default_button_styler, text_writer
   {
      using base_type = invert_button_styler;

      explicit invert_button_styler(std::string text)
       : _text(std::move(text))
      {}

      std::string const& get_text() const override { return _text; }
      void               set_text(string_view t) override { _text = std::string{t.data(), t.size()}; }

      void  draw(context const& ctx) override;

      color get_body_color() const override          { return colors::black; }
      color get_active_body_color() const override   { return colors::white; }
      color get_text_color() const override          { return colors::white; }
      float get_corner_radius() const override       { return 0.0f; }

   private:

      std::string _text;
   };

   ////////////////////////////////////////////////////////////////////////////
   // ring_button_styler
   //   Black body, inner white frame (always present), white text.
   //   When focused: adds an outer colored ring; the ring color is given
   //   at construction.
   ////////////////////////////////////////////////////////////////////////////
   struct ring_button_styler : default_button_styler, text_writer
   {
      using base_type = ring_button_styler;

      static constexpr float outline_width = 3.0f;
      static constexpr float outline_gap   = 2.0f;
      static constexpr float reserve       = outline_width + outline_gap;

      ring_button_styler(std::string text, color outline)
       : _text(std::move(text)), _outline(outline)
      {}

      std::string const& get_text() const override { return _text; }
      void               set_text(string_view t) override { _text = std::string{t.data(), t.size()}; }

      view_limits limits(basic_context const& ctx) const override;
      void        draw(context const& ctx) override;

      color get_body_color() const override          { return colors::black; }
      color get_active_body_color() const override   { return colors::black; }
      color get_text_color() const override          { return colors::white; }
      float get_corner_radius() const override       { return 0.0f; }

   private:

      std::string _text;
      color       _outline;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Factory functions matching the lib's button factory shape.
   //   invert_button("OK")
   //   ring_button("CANCEL", colors::indian_red)
   ////////////////////////////////////////////////////////////////////////////
   inline auto invert_button(std::string text)
   {
      return momentary_button(invert_button_styler{std::move(text)});
   }

   inline auto ring_button(std::string text, color outline)
   {
      return momentary_button(ring_button_styler{std::move(text), outline});
   }
}

#endif

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   Block text box — static text flowed into the element's rectangle by the
   block text backend (see support/block_text.hpp).

   Where `static_text_box` uses Elements' own width-greedy wrap, this one
   defers the line breaking to the host's text engine, so a caption drawn by
   the host and the same caption drawn here break at the same places. It also
   adds what a caption needs and a text box does not: horizontal alignment,
   extra line spacing, a base direction, and a cluster `count` limit for a
   typewriter reveal that never reflows.
=============================================================================*/
#if !defined(ELEMENTS_BLOCK_TEXT_BOX_AUGUST_13_2026)
#define ELEMENTS_BLOCK_TEXT_BOX_AUGUST_13_2026

#include <elements/element/element.hpp>
#include <elements/element/text.hpp>
#include <elements/support/block_text.hpp>
#include <elements/support/theme.hpp>

#include <string>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   class block_text_box
    : public element
    , public text_reader
    , public text_writer
    , public receiver<std::string>
   {
   public:

      using alignment = block_text_request::alignment;
      using direction = block_text_request::direction;

                              block_text_box(
                                 std::string text
                               , font font_   = get_theme().text_box_font
                               , float size   = 0   // <= 0 = the font's own size
                               , color color_ = get_theme().text_box_font_color
                              );

      view_limits             limits(basic_context const& ctx) const override;
      void                    layout(context const& ctx) override;
      void                    draw(context const& ctx) override;

      std::string const&      get_text() const override      { return _text; }
      void                    set_text(string_view text) override;
      std::string const&      value() const override         { return _text; }
      void                    value(string_view val) override { set_text(val); }

      void                    set_color(color c)             { _color = c; }
      color                   get_color() const              { return _color; }

      void                    set_align(int a);
      int                     get_align() const              { return _align; }
      void                    set_base_direction(int d);
      int                     get_base_direction() const     { return _base; }
      void                    set_line_spacing(float px);
      float                   get_line_spacing() const       { return _line_spacing; }

      // Typewriter reveal: draw only the first `count` clusters (-1 = all).
      // Line breaking is resolved for the whole text first, so advancing the
      // count never reflows what is already on screen.
      void                    set_count(int count);
      int                     get_count() const              { return _count; }

      // Clusters of the whole text — the value `set_count` counts up to.
      int                     total_count() const;

      // Height the laid-out text occupies at the last layout width (px).
      float                   content_height() const         { return _result.height; }
      int                     line_count() const             { return int(_result.lines.size()); }

      // Minimum width reported to a fit-to-content parent (same value cycfi's
      // static_text_box uses). A block narrower than this asks for its own
      // width instead.
      static constexpr float  default_min_width = 200;

   private:

      void                    relayout(float width, float height) const;

      std::string             _text;
      font                    _font;
      float                   _size;
      color                   _color;
      int                     _align = block_text_request::align_left;
      int                     _base = block_text_request::dir_auto;
      float                   _line_spacing = 0;
      int                     _count = -1;

      mutable block_text_result  _result;
      mutable float              _laid_out_width = -1;
      mutable float              _laid_out_height = -1;
      mutable int                _laid_out_count = -2;
      mutable bool               _dirty = true;
   };

   using block_text_box_ptr = std::shared_ptr<block_text_box>;
}

#endif

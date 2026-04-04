/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/glyphs.hpp>

#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // glyphs
   ////////////////////////////////////////////////////////////////////////////
   glyphs::glyphs(char const* first, char const* last)
    : _first(first)
    , _last(last)
   {
      CYCFI_ASSERT(_first, "Precondition failure: _first must not be null");
      CYCFI_ASSERT(_last, "Precondition failure: _last must not be null");
   }

   glyphs::glyphs(
      char const* first, char const* last
    , int pos_start, int pos_end
    , master_glyphs const& master
    , bool strip_leading_spaces
   )
    : _first(first)
    , _last(last)
    , _positions(&master.positions())
    , _pos_start(pos_start)
    , _pos_count(pos_end - pos_start)
    , _ascent(master._ascent)
    , _descent(master._descent)
    , _leading(master._leading)
    , _font(master._font)
    , _font_size(master._font_size)
   {
      CYCFI_ASSERT(_first, "Precondition failure: _first must not be null");
      CYCFI_ASSERT(_last, "Precondition failure: _last must not be null");
      CYCFI_ASSERT(_positions, "Precondition failure: _positions must not be null");

      // Strip leading spaces and newlines (same logic as original)
      auto strip_leading = [this](auto f)
      {
         unsigned codepoint;
         unsigned state = 0;
         int chars_skipped = 0;
         int bytes_skipped = 0;

         for (auto i = _first; i != _last; )
         {
            auto start = i;
            if (!decode_utf8(state, codepoint, uint8_t(*i)))
            {
               if (!f(codepoint))
                  break;
               bytes_skipped += int(i + 1 - start);
               ++chars_skipped;
            }
            ++i;
         }

         _first += bytes_skipped;
         _pos_start += chars_skipped;
         _pos_count -= chars_skipped;
      };

      if (strip_leading_spaces)
         strip_leading([](auto cp){ return !is_newline(cp) && is_space(cp); });
      strip_leading([](auto cp){ return is_newline(cp); });
   }

   void glyphs::draw(point pos, canvas& canvas_)
   {
      if (_first == _last)
         return;

      auto state = canvas_.new_state();

      // pos.y is the baseline position (caller uses layout metrics).
      // Convert to top-of-text using ascent so that fill_text
      // (which may use different internal metrics) doesn't mismatch.
      canvas_.font(_font, _font_size);
      canvas_.text_align(canvas::top | canvas::left);
      canvas_.fill_text(
         {_first, std::size_t(_last - _first)},
         point{pos.x, pos.y - _ascent}
      );
   }

   float glyphs::width() const
   {
      if (_first == _last || !_positions || _pos_count <= 0)
         return 0;

      float start_x = (*_positions)[_pos_start].x;
      auto const& last_pos = (*_positions)[_pos_start + _pos_count - 1];
      return (last_pos.x + last_pos.advance) - start_x;
   }

   glyphs::font_metrics glyphs::metrics() const
   {
      return {
         /*ascent=*/  _ascent,
         /*descent=*/ _descent,
         /*leading=*/ _leading,
      };
   }

   ////////////////////////////////////////////////////////////////////////////
   // master_glyphs
   ////////////////////////////////////////////////////////////////////////////
   master_glyphs::master_glyphs(
       char const* first, char const* last
     , font font_, float size
     , point start
   )
    : glyphs(first, last)
   {
      _font = font_;
      _font_size = size;
      _positions = &_owned_positions;
      build(start);
   }

   master_glyphs::master_glyphs(
      char const* first
    , char const* last
    , master_glyphs const& source
    , point start
   )
    : glyphs(first, last)
   {
      _font = source._font;
      _font_size = source._font_size;
      _ascent = source._ascent;
      _descent = source._descent;
      _leading = source._leading;
      _positions = &_owned_positions;
      build(start);
   }

   master_glyphs::master_glyphs(master_glyphs&& rhs)
    : glyphs(rhs._first, rhs._last)
   {
      _owned_positions = std::move(rhs._owned_positions);
      _positions = &_owned_positions;
      _pos_start = rhs._pos_start;
      _pos_count = rhs._pos_count;
      _ascent = rhs._ascent;
      _descent = rhs._descent;
      _leading = rhs._leading;
      _font = std::move(rhs._font);
      _font_size = rhs._font_size;

      rhs._positions = nullptr;
      rhs._pos_count = 0;
   }

   master_glyphs& master_glyphs::operator=(master_glyphs&& rhs)
   {
      if (&rhs != this)
      {
         _first = rhs._first;
         _last = rhs._last;
         _owned_positions = std::move(rhs._owned_positions);
         _positions = &_owned_positions;
         _pos_start = rhs._pos_start;
         _pos_count = rhs._pos_count;
         _ascent = rhs._ascent;
         _descent = rhs._descent;
         _leading = rhs._leading;
         _font = std::move(rhs._font);
         _font_size = rhs._font_size;

         rhs._positions = nullptr;
         rhs._pos_count = 0;
      }
      return *this;
   }

   master_glyphs::~master_glyphs()
   {
   }

   void master_glyphs::text(char const* first, char const* last, point start)
   {
      _first = first;
      _last = last;
      _owned_positions.clear();
      build(start);
   }

   void master_glyphs::break_lines(float width, std::vector<glyphs>& lines)
   {
      if (_first == _last)
         return;

      CYCFI_ASSERT(_positions, "Precondition failure: _positions must not be null");

      char const* first = _first;
      char const* space_pos = _first;
      int         start_pos_index = 0;
      int         space_pos_index = 0;
      float       start_x = _owned_positions.empty() ? 0 : _owned_positions[0].x;

      auto add_line = [&]()
      {
         glyphs glyph_{
            first, space_pos
          , start_pos_index, space_pos_index
          , *this
          , lines.size() > 0 // skip leading spaces if not first line
         };
         lines.push_back(std::move(glyph_));
         first = space_pos;
         start_pos_index = space_pos_index;
         if (start_pos_index < int(_owned_positions.size()))
            start_x = _owned_positions[start_pos_index].x;
      };

      int      pos_index = 0;
      unsigned codepoint;
      unsigned state = 0;

      for (auto i = _first; i != _last; ++i)
      {
         if (!decode_utf8(state, codepoint, uint8_t(*i)))
         {
            if (pos_index < int(_owned_positions.size()))
            {
               auto const& cp = _owned_positions[pos_index];

               // Check if we exceeded the line width
               if ((cp.x + cp.advance - start_x) > width)
               {
                  if (space_pos <= first)
                  {
                     // Hard break
                     space_pos_index = pos_index;
                     space_pos = i;
                  }
                  add_line();
               }
               else if (is_space(codepoint))
               {
                  space_pos_index = pos_index;
                  space_pos = i;

                  if ((space_pos_index != start_pos_index) && is_newline(codepoint))
                     add_line();
               }
            }
            ++pos_index;
         }
      }

      // Add the last line
      glyphs glyph_{
         first, _last
       , start_pos_index, _pos_count
       , *this
       , lines.size() > 1 // skip leading spaces if not first line
      };
      lines.push_back(std::move(glyph_));
   }

   void master_glyphs::build(point start)
   {
      _owned_positions.clear();

      // Dispatch to the glyph layout backend
      glyph_layout_backend::metrics m;
      get_glyph_layout_backend()->layout(
         _first, _last, _font, _font_size, start.x,
         _owned_positions, m
      );

      _ascent = m.ascent;
      _descent = m.descent;
      _leading = m.leading;

      if (_first != _last)
      {
         _positions = &_owned_positions;
         _pos_start = 0;
         _pos_count = int(_owned_positions.size());
      }
   }
}}

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/glyphs.hpp>

#include <richtext/TextLayout.hpp>
#include <richtext/TextStyle.hpp>
#include <richtext/FontManager.hpp>

#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace cycfi { namespace elements
{
   namespace
   {
      // Convert UTF-8 string to UTF-16
      std::u16string utf8_to_utf16(char const* first, char const* last)
      {
         std::u16string result;
         result.reserve(last - first);

         unsigned state = 0;
         unsigned codepoint = 0;
         for (auto p = first; p != last; ++p)
         {
            if (!decode_utf8(state, codepoint, uint8_t(*p)))
            {
               if (codepoint <= 0xFFFF)
               {
                  result.push_back(char16_t(codepoint));
               }
               else
               {
                  // Surrogate pair
                  codepoint -= 0x10000;
                  result.push_back(char16_t(0xD800 + (codepoint >> 10)));
                  result.push_back(char16_t(0xDC00 + (codepoint & 0x3FF)));
               }
            }
         }
         return result;
      }

      // Count UTF-8 bytes per Unicode codepoint in the string
      // Returns a vector with one entry per codepoint
      std::vector<int> utf8_byte_counts(char const* first, char const* last)
      {
         std::vector<int> counts;
         unsigned state = 0;
         unsigned codepoint = 0;
         int byte_count = 0;

         for (auto p = first; p != last; ++p)
         {
            ++byte_count;
            if (!decode_utf8(state, codepoint, uint8_t(*p)))
            {
               counts.push_back(byte_count);
               byte_count = 0;
            }
         }
         return counts;
      }

      // Build a richtext::TextStyle from a font object
      richtext::TextStyle make_text_style(font const& f, float size)
      {
         richtext::TextStyle style;
         style.fontSize = size;

         // Create a single-font collection from the font's file path
         // (registered in font.cpp using file path as name)
         if (!f.file().empty())
         {
            auto& fm = richtext::FontManager::instance();
            auto collection = fm.createCollection({f.file()});
            if (collection)
               style.fontCollection = collection;
         }
         return style;
      }

      // Build char_pos array from richtext TextLayout
      // Maps per-glyph data back to per-Unicode-codepoint positions
      void build_char_positions(
         richtext::TextLayout const& layout,
         char const* first, char const* last,
         std::vector<char_pos>& positions,
         float x_offset)
      {
         auto byte_counts = utf8_byte_counts(first, last);
         int num_chars = int(byte_counts.size());

         if (num_chars == 0)
            return;

         // Map UTF-16 indices to codepoint indices
         // Each codepoint may be 1 or 2 UTF-16 code units (surrogate pairs)
         auto u16text = layout.getText();
         std::vector<int> u16_to_cp(u16text.size(), -1);
         int cp_idx = 0;
         for (size_t i = 0; i < u16text.size() && cp_idx < num_chars; ++i)
         {
            u16_to_cp[i] = cp_idx;
            if (u16text[i] >= 0xD800 && u16text[i] <= 0xDBFF)
            {
               // High surrogate — the low surrogate at i+1 maps to same cp
               if (i + 1 < u16text.size())
                  u16_to_cp[i + 1] = cp_idx;
               ++i; // skip low surrogate
            }
            ++cp_idx;
         }

         // Initialize positions array
         positions.resize(num_chars);
         for (int i = 0; i < num_chars; ++i)
         {
            positions[i].x = 0;
            positions[i].advance = 0;
            positions[i].num_bytes = byte_counts[i];
         }

         // Fill in positions from glyph info
         size_t glyph_count = layout.getGlyphCount();
         for (size_t gi = 0; gi < glyph_count; ++gi)
         {
            auto const& glyph = layout.getGlyph(gi);
            int ci = -1;
            if (glyph.charIndex < u16_to_cp.size())
               ci = u16_to_cp[glyph.charIndex];
            if (ci < 0 || ci >= num_chars)
               continue;

            // Determine how many codepoints this glyph covers
            int next_ci = num_chars;
            if (gi + 1 < glyph_count)
            {
               auto const& next_glyph = layout.getGlyph(gi + 1);
               if (next_glyph.charIndex < u16_to_cp.size())
               {
                  int nci = u16_to_cp[next_glyph.charIndex];
                  if (nci > ci)
                     next_ci = nci;
               }
            }

            int span = next_ci - ci;
            float per_char_advance = glyph.advance / std::max(1, span);

            for (int k = 0; k < span && (ci + k) < num_chars; ++k)
            {
               positions[ci + k].x = x_offset + glyph.x + k * per_char_advance;
               positions[ci + k].advance = per_char_advance;
            }
         }
      }
   }

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

      // Get the x offset of the first character in this slice
      float start_x = 0;
      if (_positions && _pos_count > 0)
         start_x = (*_positions)[_pos_start].x;

      // Draw the text substring at the specified position
      canvas_.font(_font, _font_size);
      canvas_.fill_text(
         {_first, std::size_t(_last - _first)},
         point{pos.x - start_x, pos.y}
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

      if (_first == _last)
         return;

      // Convert UTF-8 to UTF-16 for richtext
      auto u16text = utf8_to_utf16(_first, _last);
      if (u16text.empty())
         return;

      // Create TextStyle from font
      auto style = make_text_style(_font, _font_size);

      // Layout the text
      richtext::TextLayout layout;
      layout.layout(u16text, style);

      // Extract metrics
      _ascent = std::abs(layout.getAscent());   // Elements expects positive ascent
      _descent = std::abs(layout.getDescent());  // Elements expects positive descent
      float height = _ascent + _descent;
      float total_height = layout.getHeight();
      _leading = total_height > height ? total_height - height : 0;

      // Build per-character positions
      build_char_positions(layout, _first, _last, _owned_positions, start.x);

      _positions = &_owned_positions;
      _pos_start = 0;
      _pos_count = int(_owned_positions.size());
   }
}}

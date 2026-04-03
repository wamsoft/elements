/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_GLYPHS_SEPTEMBER_26_2016)
#define ELEMENTS_GLYPHS_SEPTEMBER_26_2016

#include <infra/assert.hpp>
#include <infra/string_view.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/text_utils.hpp>
#include <vector>
#include <stdexcept>
#include <string>
#include <memory>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Per-character position data extracted from richtext TextLayout
   ////////////////////////////////////////////////////////////////////////////
   struct char_pos
   {
      float    x;           // x position relative to layout start
      float    advance;     // advance width
      int      num_bytes;   // number of UTF-8 bytes for this character
   };

   ////////////////////////////////////////////////////////////////////////////
   // glyphs: Text drawing and measuring utility
   ////////////////////////////////////////////////////////////////////////////
   class master_glyphs;

   class glyphs
   {
   public:
                           glyphs(
                              char const* first, char const* last
                            , int pos_start, int pos_end
                            , master_glyphs const& master
                            , bool strip_leading_spaces
                           );

      void                 draw(point pos, canvas& canvas_);
      float                width() const;

                           // for_each F signature:
                           // bool f(char const* utf8, float left, float right);
                           template <typename F>
      void                 for_each(F f);

      std::size_t          size() const      { return _last - _first; }
      char const*          begin() const     { return _first; }
      char const*          end() const       { return _last; }

      struct font_metrics
      {
         float             ascent;
         float             descent;
         float             leading;
      };

      font_metrics         metrics() const;

   protected:
                           glyphs(char const* first, char const* last);

      char const*                   _first;
      char const*                   _last;
      std::vector<char_pos> const*  _positions = nullptr;  // owned by master
      int                           _pos_start = 0;
      int                           _pos_count = 0;
      float                         _ascent = 0;
      float                         _descent = 0;
      float                         _leading = 0;
      font                          _font;
      float                         _font_size = 12;
   };

   ////////////////////////////////////////////////////////////////////////////
   struct failed_to_build_master_glyphs : std::runtime_error
   {
      failed_to_build_master_glyphs()
       : std::runtime_error("Error. Failed to build master glyphs.") {}
   };

   class master_glyphs : public glyphs
   {
   public:
                           master_glyphs(
                              char const* first, char const* last
                            , font font_, float size
                            , point start = {0, 0}
                           );

                           master_glyphs(
                              char const* first, char const* last
                            , master_glyphs const& source
                            , point start = {0, 0}
                           );

                           master_glyphs(
                              string_view str
                            , font font_, float size
                            , point start = {0, 0}
                           );

                           master_glyphs(
                              string_view str
                            , master_glyphs const& source
                            , point start = {0, 0}
                           );

                           master_glyphs(
                              std::string const& str
                            , font font_, float size
                            , point start = {0, 0}
                           );

                           master_glyphs(
                              std::string const& str
                            , master_glyphs const& source
                            , point start = {0, 0}
                           );

                           master_glyphs(master_glyphs&&);
      master_glyphs&       operator=(master_glyphs&& rhs);

                           ~master_glyphs();

      void                 break_lines(float width, std::vector<glyphs>& lines);
      void                 text(char const* first, char const* last, point start = {0, 0});
      void                 text(string_view str, point start = {0, 0});
      void                 text(std::string const& str, point start = {0, 0});

      // Access to owned position data (used by glyphs slices)
      std::vector<char_pos> const& positions() const { return _owned_positions; }

   private:
                           master_glyphs(master_glyphs const&) = delete;
      master_glyphs&       operator=(master_glyphs const& rhs) = delete;

      void                 build(point start = {0, 0});

      std::vector<char_pos>   _owned_positions;
   };

   ////////////////////////////////////////////////////////////////////////////
   inline master_glyphs::master_glyphs(
      string_view str
    , font font_, float size
    , point start
   )
    : master_glyphs(str.data(), str.data() + str.size(), font_, size, start)
   {}

   inline master_glyphs::master_glyphs(
      string_view str
    , master_glyphs const& source
    , point start
   )
    : master_glyphs(str.data(), str.data() + str.size(), source, start)
   {}

   inline master_glyphs::master_glyphs(
      std::string const& str
    , font font_, float size
    , point start
   )
    : master_glyphs(str.data(), str.data() + str.size(), font_, size, start)
   {}

   inline master_glyphs::master_glyphs(
      std::string const& str
    , master_glyphs const& source
    , point start
   )
    : master_glyphs(str.data(), str.data() + str.size(), source, start)
   {}

   inline void master_glyphs::text(string_view str, point start)
   {
      text(str.data(), str.data() + str.size(), start);
   }

   inline void master_glyphs::text(std::string const& str, point start)
   {
      text(str.data(), str.data() + str.size(), start);
   }

   template <typename F>
   inline void glyphs::for_each(F f)
   {
      CYCFI_ASSERT(_positions, "Precondition failure: _positions must not be null");

      if (_first == _last)
         return;

      float start_x = 0;
      if (_pos_count > 0)
         start_x = (*_positions)[_pos_start].x;

      for (int i = 0; i < _pos_count; ++i)
      {
         auto const& cp = (*_positions)[_pos_start + i];

         // Compute byte offset for this character position
         int byte_offset = 0;
         for (int j = 0; j < i; ++j)
            byte_offset += (*_positions)[_pos_start + j].num_bytes;

         float x = cp.x - start_x;
         if (!f(_first + byte_offset, x, x + cp.advance))
            break;
      }
   }
}}

#endif

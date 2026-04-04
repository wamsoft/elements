/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_TEXT_BACKEND_HPP)
#define ELEMENTS_TEXT_BACKEND_HPP

#include <elements/support/point.hpp>
#include <string_view>
#include <memory>

namespace cycfi { namespace elements
{
   class canvas;

   ////////////////////////////////////////////////////////////////////////////
   // Text metrics (duplicated from canvas for decoupling)
   ////////////////////////////////////////////////////////////////////////////
   struct text_metrics
   {
      float    ascent;
      float    descent;
      float    leading;
      point    size;
   };

   struct font_metrics
   {
      float    ascent;
      float    descent;
      float    height;
      float    leading;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Text rendering backend interface
   //
   // Implementations render text onto the canvas's internal ThorVG canvas.
   // The canvas reference provides access to font state, fill/stroke style,
   // alignment, transform matrix, and clip state.
   //
   // Implementations may cache glyph data, font objects, or other resources.
   // The virtual destructor ensures proper cleanup.
   ////////////////////////////////////////////////////////////////////////////
   class text_backend
   {
   public:
      virtual ~text_backend() = default;

      virtual void         fill_text(canvas& cnv, std::string_view utf8, point p) = 0;
      virtual void         stroke_text(canvas& cnv, std::string_view utf8, point p) = 0;
      virtual text_metrics measure_text(canvas& cnv, char const* utf8) = 0;
      virtual font_metrics measure_font(canvas& cnv) = 0;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Default (ThorVG) text backend — always available
   ////////////////////////////////////////////////////////////////////////////
   std::shared_ptr<text_backend> create_tvg_text_backend();
}}

#endif

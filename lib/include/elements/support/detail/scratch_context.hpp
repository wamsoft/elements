/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_DETAIL_SCRATCH_CONTEXT_SEPTEMBER_26_2016)
#define ELEMENTS_DETAIL_SCRATCH_CONTEXT_SEPTEMBER_26_2016

#include <thorvg.h>
#include <vector>
#include <cstdint>

namespace cycfi { namespace elements { namespace detail
{
   // Scratch context for off-screen operations (measurement, layout)
   // Uses a small ThorVG SwCanvas with a minimal buffer
   class scratch_context
   {
   public:

      scratch_context()
       : _buffer(4 * 4, 0)  // 4x4 pixel buffer (minimal)
      {
         _canvas = tvg::SwCanvas::gen();
         _canvas->target(_buffer.data(), 4, 4, 4, tvg::ColorSpace::ARGB8888);
      }

      ~scratch_context()
      {
         delete _canvas;
      }

      tvg::SwCanvas* canvas() const { return _canvas; }
      uint32_t*      buffer()       { return _buffer.data(); }

   private:

      scratch_context(scratch_context const&) = delete;

      tvg::SwCanvas*          _canvas;
      std::vector<uint32_t>   _buffer;
   };
}}}

#endif

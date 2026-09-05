/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_DETAIL_SCRATCH_CONTEXT_SEPTEMBER_26_2016)
#define ELEMENTS_DETAIL_SCRATCH_CONTEXT_SEPTEMBER_26_2016

#include <thorvg.h>
#include <memory>
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
   // 測定用 scratch canvas は関数内 static だとプロセス終了まで生き残り、
   // tvg::Initializer::term() が «まだ canvas がある» と言って早期 return する
   // (SwRenderer::term が false を返す)。 その結果 LoaderMgr::term() に届かず、
   // フォントローダが ThorVG の静的リストに残ったまま atexit で破棄されて、
   // 破棄済みのフォントマネージャを触って落ちる。
   //
   // «いつでも作り直せる» 一時領域なので、 終了前に明示的に捨てられるように
   // しておく。 次に必要になれば作り直される。
   scratch_context& shared_scratch();
   void release_shared_scratch();
}}}

#endif

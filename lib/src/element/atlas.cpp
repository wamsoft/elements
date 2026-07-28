/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/atlas.hpp>
#include <elements/support/context.hpp>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace cycfi::elements
{
   //---------------------------------------------------------------------
   // atlas_image
   //---------------------------------------------------------------------
   atlas_image::atlas_image(pixmap_ptr atlas, rect src,
                            bool stretch_h, bool stretch_v)
    : image(std::move(atlas))
    , _src(src)
    , _stretch_h(stretch_h)
    , _stretch_v(stretch_v)
   {}

   view_limits atlas_image::limits(basic_context const& /*ctx*/) const
   {
      auto w = _src.width();
      auto h = _src.height();
      point min_{w, h};
      point max_{_stretch_h ? full_extent : w,
                 _stretch_v ? full_extent : h};
      return {min_, max_};
   }

   point atlas_image::size() const
   {
      return {_src.width(), _src.height()};
   }

   rect atlas_image::source_rect(context const& /*ctx*/) const
   {
      return _src;
   }

   //---------------------------------------------------------------------
   // atlas_sprite
   //---------------------------------------------------------------------
   atlas_sprite::atlas_sprite(pixmap_ptr atlas, std::vector<rect> frames, bool native)
    : basic_sprite(std::move(atlas))
    , _frames(std::move(frames))
    , _native(native)
   {
      if (_frames.empty())
         throw std::runtime_error{
            "atlas_sprite requires at least one frame"};
   }

   point atlas_sprite::max_extent() const
   {
      float w = 0, h = 0;
      for (auto const& f : _frames)
      {
         w = std::max<float>(w, f.width());
         h = std::max<float>(h, f.height());
      }
      return {w, h};
   }

   view_limits atlas_sprite::limits(basic_context const& /*ctx*/) const
   {
      // 既定は frame 0 (全 frame 同寸法前提)。 native モードでは frame 間で寸法が
      // 違うので、 最大の frame に合わせて box を確保する (小さい frame は中央)。
      point sz = _native ? max_extent()
                         : point{_frames[0].width(), _frames[0].height()};
      return {sz, sz};
   }

   point atlas_sprite::size() const
   {
      if (_native)
         return max_extent();
      auto const& f = _frames[_index];
      return {f.width(), f.height()};
   }

   rect atlas_sprite::source_rect(context const& /*ctx*/) const
   {
      return _frames[_index];
   }

   void atlas_sprite::draw(context const& ctx)
   {
      if (!_native)
      {
         image::draw(ctx);   // 従来: 現 frame を bounds へ伸縮
         return;
      }
      // native: 現 frame を実寸のまま bounds 中央へ (伸縮しない)。
      auto src = source_rect(ctx);
      rect dest{0, 0, src.width(), src.height()};
      ctx.canvas.draw(pixmap(), src, center(dest, ctx.bounds));
   }

   void atlas_sprite::index(std::size_t i)
   {
      if (i < _frames.size())
         _index = i;
   }

   //---------------------------------------------------------------------
   // animated_sprite
   //---------------------------------------------------------------------
   animated_sprite::animated_sprite(pixmap_ptr atlas, std::vector<rect> frames,
                                    float fps, bool loop, bool native)
    : atlas_sprite(std::move(atlas), std::move(frames), native)
    , _fps(fps)
    , _loop(loop)
   {}

   void animated_sprite::draw(context const& ctx)
   {
      // 経過時間からフレーム index を算出 (最初の draw を基点にする)。
      if (!_started)
      {
         _t0 = std::chrono::steady_clock::now();
         _started = true;
      }
      std::size_t n = num_frames();
      if (n > 0 && _fps > 0.0f)
      {
         auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - _t0).count();
         double f = static_cast<double>(ms) * (_fps / 1000.0);
         std::size_t idx = _loop
            ? static_cast<std::size_t>(std::fmod(f, static_cast<double>(n)))
            : std::min<std::size_t>(static_cast<std::size_t>(f), n - 1);
         index(idx);
      }
      atlas_sprite::draw(ctx);
   }

   //---------------------------------------------------------------------
   // atlas_progress
   //---------------------------------------------------------------------
   atlas_progress::atlas_progress(pixmap_ptr atlas, rect track, rect fill,
                                  double value, bool vertical)
    : _atlas(std::move(atlas))
    , _track(track)
    , _fill(fill)
    , _value(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value))
    , _vertical(vertical)
   {}

   view_limits atlas_progress::limits(basic_context const& /*ctx*/) const
   {
      point sz{_track.width(), _track.height()};
      return {sz, sz};
   }

   void atlas_progress::draw(context const& ctx)
   {
      // 背景 (track) を bounds 全域に
      ctx.canvas.draw(*_atlas, _track, ctx.bounds);

      if (_value <= 0.0) return;

      // 前景 (fill) を value 分だけ。 src 矩形と dest 矩形を value で切詰める
      // (= 左/下端固定、 残り 1-value 分を捨てる)。
      auto sf = _fill;
      auto df = ctx.bounds;
      if (_vertical)
      {
         float h_src = sf.height() * static_cast<float>(_value);
         float h_dst = df.height() * static_cast<float>(_value);
         sf.top = sf.bottom - h_src;
         df.top = df.bottom - h_dst;
      }
      else
      {
         float w_src = sf.width()  * static_cast<float>(_value);
         float w_dst = df.width()  * static_cast<float>(_value);
         sf.right = sf.left + w_src;
         df.right = df.left + w_dst;
      }
      ctx.canvas.draw(*_atlas, sf, df);
   }

   void atlas_progress::set_value(double v)
   {
      if (v < 0.0) v = 0.0;
      if (v > 1.0) v = 1.0;
      _value = v;
   }
}

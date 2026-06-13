/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/atlas.hpp>
#include <elements/support/context.hpp>
#include <stdexcept>

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
   atlas_sprite::atlas_sprite(pixmap_ptr atlas, std::vector<rect> frames)
    : basic_sprite(std::move(atlas))
    , _frames(std::move(frames))
   {
      if (_frames.empty())
         throw std::runtime_error{
            "atlas_sprite requires at least one frame"};
   }

   view_limits atlas_sprite::limits(basic_context const& /*ctx*/) const
   {
      // 全 frame で同寸法を前提。 frame 0 を採用。
      auto const& f0 = _frames[0];
      point sz{f0.width(), f0.height()};
      return {sz, sz};
   }

   point atlas_sprite::size() const
   {
      auto const& f = _frames[_index];
      return {f.width(), f.height()};
   }

   rect atlas_sprite::source_rect(context const& /*ctx*/) const
   {
      return _frames[_index];
   }

   void atlas_sprite::index(std::size_t i)
   {
      if (i < _frames.size())
         _index = i;
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

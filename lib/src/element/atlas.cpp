/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/atlas.hpp>
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
}

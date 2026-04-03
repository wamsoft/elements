/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/pixmap.hpp>
#include <elements/support/resource_paths.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <elements/support/detail/stb_image.h>
#include <infra/assert.hpp>
#include <infra/filesystem.hpp>

#include <algorithm>

#include <thorvg.h>

namespace cycfi { namespace elements
{
   pixmap::pixmap(point size, float scale_)
    : _width(int(size.x))
    , _height(int(size.y))
    , _scale(scale_)
   {
      // Create an empty (transparent) picture
      if (_width > 0 && _height > 0)
      {
         std::vector<uint32_t> empty(_width * _height, 0);
         auto* pic = tvg::Picture::gen();
         if (pic && pic->load(empty.data(), _width, _height,
               tvg::ColorSpace::ARGB8888, true) == tvg::Result::Success)
         {
            _picture = pic;
         }
         else if (pic)
         {
            tvg::Paint::rel(pic);
         }
      }
   }

   pixmap::pixmap(fs::path const& path, float scale_)
    : _scale(scale_)
   {
      fs::path full_path = find_file(path);
      if (full_path.empty())
         throw failed_to_load_pixmap{"File does not exist."};

      auto* pic = tvg::Picture::gen();
      if (pic && pic->load(full_path.string().c_str()) == tvg::Result::Success)
      {
         float fw = 0;
         float fh = 0;
         pic->size(&fw, &fh);
         _width = std::max(0, int(fw));
         _height = std::max(0, int(fh));
         _picture = pic;
         return;
      }

      if (pic)
         tvg::Paint::rel(pic);

      // Fallback: load with stbi_image and convert to tvg::Picture
      int w = 0;
      int h = 0;
      int components = 0;
      uint8_t* src_data = stbi_load(full_path.string().c_str(), &w, &h, &components, 4);
      if (!src_data)
         throw failed_to_load_pixmap{"Failed to load pixmap."};

      // Convert RGBA to ARGB in temporary buffer
      std::vector<uint32_t> tmp(w * h);
      uint32_t* dest = tmp.data();
      for (int y = 0; y < h; ++y)
      {
         uint8_t* src = src_data + (y * w * 4);
         for (int x = 0; x < w; ++x)
         {
            uint8_t r = src[0];
            uint8_t g = src[1];
            uint8_t b = src[2];
            uint8_t a = src[3];
            *dest++ = (uint32_t(a) << 24) | (uint32_t(r) << 16) |
                      (uint32_t(g) << 8) | uint32_t(b);
            src += 4;
         }
      }
      stbi_image_free(src_data);

      // Load into tvg::Picture (copy=true so tmp can be released)
      pic = tvg::Picture::gen();
      if (!pic || pic->load(tmp.data(), w, h, tvg::ColorSpace::ARGB8888, true)
            != tvg::Result::Success)
      {
         if (pic)
            tvg::Paint::rel(pic);
         throw failed_to_load_pixmap{"Failed to load pixmap."};
      }

      _width = w;
      _height = h;
      _picture = pic;
   }

   pixmap::~pixmap()
   {
      release_picture();
   }

   extent pixmap::size() const
   {
      return {
         float(_width) / _scale,
         float(_height) / _scale
      };
   }

   float pixmap::scale() const
   {
      return _scale;
   }

   void pixmap::scale(float val)
   {
      _scale = val;
   }

   void pixmap::release_picture()
   {
      if (_picture)
      {
         tvg::Paint::rel(_picture);
         _picture = nullptr;
      }
   }

   void pixmap::set_picture(tvg::Picture* pic)
   {
      release_picture();
      _picture = pic;
      if (pic)
      {
         float fw = 0, fh = 0;
         pic->size(&fw, &fh);
         _width = std::max(0, int(fw));
         _height = std::max(0, int(fh));
      }
      else
      {
         _width = 0;
         _height = 0;
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // pixmap_context implementation
   ////////////////////////////////////////////////////////////////////////////
   pixmap_context::pixmap_context(pixmap& pm)
    : _target(&pm)
    , _width(pm._width)
    , _height(pm._height)
   {
      if (_width <= 0 || _height <= 0)
         return;

      _buffer.resize(_width * _height, 0);
      _canvas = tvg::SwCanvas::gen();
      if (!_canvas)
         return;

      _canvas->target(_buffer.data(), _width, _width, _height, tvg::ColorSpace::ARGB8888);

      // Render existing picture content into buffer
      if (pm._picture)
      {
         auto* dup = pm._picture->duplicate();
         if (dup)
         {
            _canvas->add(dup);
            _canvas->update();
            _canvas->draw(true);
            _canvas->sync();
            _canvas->remove();
         }
      }
   }

   pixmap_context::~pixmap_context()
   {
      flush();
      if (_canvas)
         delete _canvas;
   }

   pixmap_context::pixmap_context(pixmap_context&& rhs) noexcept
    : _target(rhs._target)
    , _buffer(std::move(rhs._buffer))
    , _canvas(rhs._canvas)
    , _width(rhs._width)
    , _height(rhs._height)
   {
      rhs._target = nullptr;
      rhs._canvas = nullptr;
      rhs._width = 0;
      rhs._height = 0;
   }

   pixmap_context& pixmap_context::operator=(pixmap_context&& rhs) noexcept
   {
      if (this != &rhs)
      {
         flush();
         if (_canvas)
            delete _canvas;

         _target = rhs._target;
         _buffer = std::move(rhs._buffer);
         _canvas = rhs._canvas;
         _width = rhs._width;
         _height = rhs._height;

         rhs._target = nullptr;
         rhs._canvas = nullptr;
         rhs._width = 0;
         rhs._height = 0;
      }
      return *this;
   }

   void pixmap_context::flush()
   {
      if (!_target || !_canvas || _buffer.empty())
         return;

      // Finalize any pending drawing
      _canvas->update();
      _canvas->draw(true);
      _canvas->sync();

      // Create new Picture from the buffer
      auto* pic = tvg::Picture::gen();
      if (pic && pic->load(_buffer.data(), _width, _height,
            tvg::ColorSpace::ARGB8888, true) == tvg::Result::Success)
      {
         _target->set_picture(pic);
      }
      else if (pic)
      {
         tvg::Paint::rel(pic);
      }

      // Clear state to prevent double-flush
      _target = nullptr;
   }
}}

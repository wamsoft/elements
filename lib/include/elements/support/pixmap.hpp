/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_PIXMAP_SEPTEMBER_5_2016)
#define ELEMENTS_PIXMAP_SEPTEMBER_5_2016

#include <vector>
#include <memory>
#include <cstdint>
#include <elements/support/point.hpp>
#include <infra/filesystem.hpp>
#include <stdexcept>

namespace tvg { struct Picture; struct SwCanvas; }

namespace cycfi { namespace elements
{
   class canvas;

   ////////////////////////////////////////////////////////////////////////////
   // Pixmaps
   ////////////////////////////////////////////////////////////////////////////
   struct failed_to_load_pixmap : std::runtime_error
   {
      using std::runtime_error::runtime_error;
   };

   class pixmap
   {
   public:

      explicit          pixmap(point size, float scale = 1);
      explicit          pixmap(fs::path const& path, float scale = 1);
                        pixmap(pixmap const& rhs) = delete;
                        pixmap(pixmap&& rhs);
                        ~pixmap();

      pixmap&           operator=(pixmap const& rhs) = delete;
      pixmap&           operator=(pixmap&& rhs);

      extent            size() const;
      float             scale() const;
      void              scale(float val);

      int               pixel_width() const  { return _width; }
      int               pixel_height() const { return _height; }

   private:

      friend class canvas;
      friend class pixmap_context;

      tvg::Picture*     picture() const { return _picture; }
      void              release_picture();
      void              set_picture(tvg::Picture* pic);

      tvg::Picture*     _picture = nullptr;
      int               _width = 0;
      int               _height = 0;
      float             _scale = 1.0f;
   };

   using pixmap_ptr = std::shared_ptr<pixmap>;

   ////////////////////////////////////////////////////////////////////////////
   // pixmap_context allows drawing into a pixmap using tvg::SwCanvas.
   // Call flush() to commit changes to the target pixmap.
   // Destructor automatically calls flush().
   ////////////////////////////////////////////////////////////////////////////
   class pixmap_context
   {
   public:

      explicit          pixmap_context(pixmap& pm);
                        ~pixmap_context();
                        pixmap_context(pixmap_context&& rhs) noexcept;

      pixmap_context&   operator=(pixmap_context&& rhs) noexcept;

      tvg::SwCanvas*    canvas() const { return _canvas; }
      uint32_t*         buffer() const { return const_cast<uint32_t*>(_buffer.data()); }
      int               width() const  { return _width; }
      int               height() const { return _height; }

      void              flush();

   private:
                        pixmap_context(pixmap_context const&) = delete;
      pixmap_context&   operator=(pixmap_context const&) = delete;

      pixmap*              _target = nullptr;
      std::vector<uint32_t> _buffer;
      tvg::SwCanvas*       _canvas = nullptr;
      int                  _width = 0;
      int                  _height = 0;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Inlines
   ////////////////////////////////////////////////////////////////////////////
   inline pixmap::pixmap(pixmap&& rhs)
    : _picture(rhs._picture)
    , _width(rhs._width)
    , _height(rhs._height)
    , _scale(rhs._scale)
   {
      rhs._picture = nullptr;
      rhs._width = 0;
      rhs._height = 0;
   }

   inline pixmap& pixmap::operator=(pixmap&& rhs)
   {
      if (this != &rhs)
      {
         release_picture();
         _picture = rhs._picture;
         _width = rhs._width;
         _height = rhs._height;
         _scale = rhs._scale;
         rhs._picture = nullptr;
         rhs._width = 0;
         rhs._height = 0;
      }
      return *this;
   }
}}

#endif

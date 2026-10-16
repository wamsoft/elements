/*=============================================================================
   Copyright (c) 2016-2026 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_RESOURCE_LOADER_JUNE_11_2026)
#define ELEMENTS_RESOURCE_LOADER_JUNE_11_2026

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // raw_image
   //
   // Decoded pixels handed over by a host without going through an image
   // codec. Pixels are 32-bit 0xAARRGGBB (B,G,R,A in memory on little
   // endian), top row first, row stride == width. This is the layout of
   // tvg::ColorSpace::ARGB8888 / ARGB8888S.
   //
   // premultiplied == true means color channels are already multiplied by
   // alpha. Such buffers are referenced by the pixmap without copying (they
   // are never modified). Straight-alpha buffers are copied, because the
   // renderer premultiplies its image buffer in place.
   ////////////////////////////////////////////////////////////////////////////
   struct raw_image
   {
      int                        width = 0;
      int                        height = 0;
      bool                       premultiplied = true;
      std::vector<std::uint32_t> pixels;
   };

   ////////////////////////////////////////////////////////////////////////////
   // resource_loader
   //
   // Abstract interface for reading resource bytes by logical name. The
   // engine itself never opens files directly; it goes through whatever
   // resource_loader is installed.
   //
   // The default implementation is filesystem_resource_loader (see
   // filesystem_resource_loader.hpp) which resolves names against paths
   // registered via add_search_path(). Platforms without a normal
   // filesystem (e.g. consoles, mobile sandboxes) can install a custom
   // loader that resolves names to embedded buffers or archive entries
   // via set_resource_loader().
   ////////////////////////////////////////////////////////////////////////////
   class resource_loader
   {
   public:
      virtual                       ~resource_loader() = default;

      // Returns true if the named resource is known. The default
      // implementation does a full read() and discards the bytes;
      // subclasses are encouraged to override with a cheaper check.
      virtual bool                  exists(std::string_view name);

      // Reads the bytes of the named resource. Returns an empty vector
      // when the resource does not exist. Names may be absolute paths
      // (file-based loaders accept them); portable code should use
      // relative logical names.
      virtual std::vector<std::uint8_t>
                                    read(std::string_view name) = 0;

      // Returns the named resource as decoded pixels, or nullptr when it
      // is not held in that form (the caller then falls back to read()
      // and decodes the bytes). pixmap asks this first, so a host can
      // register images that skip encoding and decoding entirely. The
      // default implementation holds no raw images.
      virtual std::shared_ptr<raw_image const>
                                    read_raw(std::string_view name);
   };

   ////////////////////////////////////////////////////////////////////////////
   // Process-wide resource loader access.
   //
   // get_resource_loader() lazily installs the default loader
   // (filesystem_resource_loader on builds with ELEMENTS_FILE_IO_SUPPORT;
   // a null loader otherwise). set_resource_loader(nullptr) restores the
   // default.
   ////////////////////////////////////////////////////////////////////////////
   resource_loader&                 get_resource_loader();
   void                             set_resource_loader(
                                       std::shared_ptr<resource_loader> loader);
}}

#endif

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_GLYPH_UTILS_HPP)
#define ELEMENTS_GLYPH_UTILS_HPP

#include <elements/support/font.hpp>
#include <vector>
#include <memory>
#include <string>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Per-character position data
   ////////////////////////////////////////////////////////////////////////////
   struct char_pos
   {
      float    x;           // x position relative to layout start
      float    advance;     // advance width
      int      num_bytes;   // number of UTF-8 bytes for this character
   };

   ////////////////////////////////////////////////////////////////////////////
   // Glyph layout backend interface
   //
   // Performs text shaping (glyph positioning) and font metrics extraction.
   // The default implementation uses HarfBuzz + FreeType directly.
   // The virtual destructor ensures proper cache cleanup.
   ////////////////////////////////////////////////////////////////////////////
   class glyph_layout_backend
   {
   public:
      virtual ~glyph_layout_backend() = default;

      struct metrics
      {
         float ascent;    // positive
         float descent;   // positive
         float leading;
      };

      // Perform text shaping and build per-character positions.
      // If first == last (empty text), only compute metrics using the font.
      virtual void layout(
         char const* first, char const* last,
         font const& f, float size,
         float x_offset,
         std::vector<char_pos>& positions,
         metrics& out_metrics
      ) = 0;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Font registration backend interface
   //
   // Registers fonts for use by the glyph layout backend.
   ////////////////////////////////////////////////////////////////////////////
   class font_backend
   {
   public:
      virtual ~font_backend() = default;

      // Initialize the font subsystem
      virtual void initialize() = 0;

      // Register a font file. Called for each font.
      virtual void register_font(std::string const& file_path) = 0;

      // Register a font from an in-memory buffer. The backend owns a copy of
      // the buffer; the caller is free to release `data` after this returns.
      // `key` is used as cache identifier (same role as `file_path` for
      // register_font). Default implementation is a no-op for backends that
      // don't support memory loading.
      virtual void register_font_buffer(std::string const& /*key*/,
                                        std::uint8_t const* /*data*/,
                                        std::size_t /*size*/) {}

      // Whether a registered font has variable axes (fvar). Used to prefer
      // the variable entry when a "#tag=val" instance suffix resolves against
      // a family that has both static and variable registrations. Backends
      // that cannot tell return false.
      virtual bool is_variable(std::string const& /*key*/) { return false; }
   };

   ////////////////////////////////////////////////////////////////////////////
   // Default (FreeType + HarfBuzz) backends — always available
   ////////////////////////////////////////////////////////////////////////////
   std::shared_ptr<glyph_layout_backend> create_ft_glyph_layout_backend();
   std::shared_ptr<font_backend>         create_ft_font_backend();

   ////////////////////////////////////////////////////////////////////////////
   // Global backend access
   ////////////////////////////////////////////////////////////////////////////
   void set_glyph_layout_backend(std::shared_ptr<glyph_layout_backend> b);
   std::shared_ptr<glyph_layout_backend> get_glyph_layout_backend();

   void set_font_backend(std::shared_ptr<font_backend> b);
   std::shared_ptr<font_backend> get_font_backend();
}}

#endif

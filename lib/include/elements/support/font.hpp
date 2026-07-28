/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_FONT_X_FEBRUARY_11_2020)
#define ELEMENTS_FONT_X_FEBRUARY_11_2020

#include <infra/string_view.hpp>
#include <string>
#include <cstdint>

namespace cycfi { namespace elements
{
   namespace font_constants
   {
      enum weight_enum
      {
         thin              = 10,
         extra_light       = 20,
         light             = 30,
         weight_normal     = 40,
         medium            = 50,
         semi_bold         = 60,
         bold              = 70,
         extra_bold        = 80,
         black             = 90,
         extra_black       = 95,
      };

      enum slant_enum
      {
         slant_normal      = 0,
         italic            = 90,
         oblique           = 100
      };

      enum stretch_enum
      {
         ultra_condensed	= 25,
         extra_condensed	= 31,
         condensed	      = 38,
         semi_condensed	   = 44,
         stretch_normal	   = 50,
         semi_expanded	   = 57,
         expanded	         = 63,
         extra_expanded	   = 75,
         ultra_expanded	   = 100
      };
   }

   struct font_descr
   {
      font_descr           normal() const;
      font_descr           size(float size_) const;

      font_descr           weight(font_constants::weight_enum w) const;
      font_descr           thin() const;
      font_descr           extra_light() const;
      font_descr           light() const;
      font_descr           weight_normal() const;
      font_descr           medium() const;
      font_descr           semi_bold() const;
      font_descr           bold() const;
      font_descr           extra_bold() const;
      font_descr           black() const;
      font_descr           extra_black() const;

      font_descr           style(font_constants::slant_enum s) const;
      font_descr           slant_normal() const;
      font_descr           italic() const;
      font_descr           oblique() const;

      font_descr           stretch(font_constants::stretch_enum s) const;
      font_descr           ultra_condensed() const;
      font_descr           extra_condensed() const;
      font_descr           condensed() const;
      font_descr           semi_condensed() const;
      font_descr           stretch_normal() const;
      font_descr           semi_expanded() const;
      font_descr           expanded() const;
      font_descr           extra_expanded() const;
      font_descr           ultra_expanded() const;

      string_view          _families;
      float                _size = 12;
      uint8_t              _weight = font_constants::weight_normal;
      uint8_t              _slant = font_constants::slant_normal;
      uint8_t              _stretch = font_constants::stretch_normal;
   };

   class font
   {
   public:
                           font();
                           font(font_descr descr);
                           font(font const& rhs);
                           font(font&& rhs) noexcept;
                           ~font();
      font&                operator=(font const& rhs);
      font&                operator=(font&& rhs) noexcept;
      explicit             operator bool() const;
      float                size() const { return _size; }

      // Access to font info for ThorVG
      std::string const&   family() const { return _family; }
      std::string const&   file() const   { return _file; }

   private:

      friend class canvas;
      std::string         _family;     // Font family name
      std::string         _file;       // TTF file path
      float               _size = 12;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Inlines
   ////////////////////////////////////////////////////////////////////////////
   inline font_descr font_descr::normal() const
   {
      font_descr r = *this;
      r._weight = font_constants::weight_normal;
      r._slant = font_constants::slant_normal;
      r._stretch = font_constants::stretch_normal;
      return r;
   }

   inline font_descr font_descr::size(float size_) const
   {
      font_descr r = *this;
      r._size = size_;
      return r;
   }

   inline font_descr font_descr::weight(font_constants::weight_enum w) const
   {
      font_descr r = *this;
      r._weight = w;
      return r;
   }

   inline font_descr font_descr::thin() const
   {
      font_descr r = *this;
      r._weight = font_constants::thin;
      return r;
   }

   inline font_descr font_descr::extra_light() const
   {
      font_descr r = *this;
      r._weight = font_constants::extra_light;
      return r;
   }

   inline font_descr font_descr::light() const
   {
      font_descr r = *this;
      r._weight = font_constants::light;
      return r;
   }

   inline font_descr font_descr::weight_normal() const
   {
      font_descr r = *this;
      r._weight = font_constants::weight_normal;
      return r;
   }

   inline font_descr font_descr::medium() const
   {
      font_descr r = *this;
      r._weight = font_constants::medium;
      return r;
   }

   inline font_descr font_descr::semi_bold() const
   {
      font_descr r = *this;
      r._weight = font_constants::semi_bold;
      return r;
   }

   inline font_descr font_descr::bold() const
   {
      font_descr r = *this;
      r._weight = font_constants::bold;
      return r;
   }

   inline font_descr font_descr::extra_bold() const
   {
      font_descr r = *this;
      r._weight = font_constants::extra_bold;
      return r;
   }

   inline font_descr font_descr::black() const
   {
      font_descr r = *this;
      r._weight = font_constants::black;
      return r;
   }

   inline font_descr font_descr::extra_black() const
   {
      font_descr r = *this;
      r._weight = font_constants::extra_black;
      return r;
   }

   inline font_descr font_descr::style(font_constants::slant_enum s) const
   {
      font_descr r = *this;
      r._slant = s;
      return r;
   }

   inline font_descr font_descr::slant_normal() const
   {
      font_descr r = *this;
      r._slant = font_constants::slant_normal;
      return r;
   }

   inline font_descr font_descr::italic() const
   {
      font_descr r = *this;
      r._slant = font_constants::italic;
      return r;
   }

   inline font_descr font_descr::oblique() const
   {
      font_descr r = *this;
      r._slant = font_constants::oblique;
      return r;
   }

   inline font_descr font_descr::stretch(font_constants::stretch_enum s) const
   {
      font_descr r = *this;
      r._stretch = s;
      return r;
   }

   inline font_descr font_descr::ultra_condensed() const
   {
      font_descr r = *this;
      r._stretch = font_constants::ultra_condensed;
      return r;
   }

   inline font_descr font_descr::extra_condensed() const
   {
      font_descr r = *this;
      r._stretch = font_constants::extra_condensed;
      return r;
   }

   inline font_descr font_descr::condensed() const
   {
      font_descr r = *this;
      r._stretch = font_constants::condensed;
      return r;
   }

   inline font_descr font_descr::semi_condensed() const
   {
      font_descr r = *this;
      r._stretch = font_constants::semi_condensed;
      return r;
   }

   inline font_descr font_descr::stretch_normal() const
   {
      font_descr r = *this;
      r._stretch = font_constants::stretch_normal;
      return r;
   }

   inline font_descr font_descr::semi_expanded() const
   {
      font_descr r = *this;
      r._stretch = font_constants::semi_expanded;
      return r;
   }

   inline font_descr font_descr::expanded() const
   {
      font_descr r = *this;
      r._stretch = font_constants::expanded;
      return r;
   }

   inline font_descr font_descr::extra_expanded() const
   {
      font_descr r = *this;
      r._stretch = font_constants::extra_expanded;
      return r;
   }

   inline font_descr font_descr::ultra_expanded() const
   {
      font_descr r = *this;
      r._stretch = font_constants::ultra_expanded;
      return r;
   }

   inline font::font()
   {}

   inline font::operator bool() const
   {
      return !_file.empty();
   }

   ////////////////////////////////////////////////////////////////////////////
   // Font registration
   //
   // Users must register all fonts they intend to use before creating
   // font objects. register_font() maps a family name + style attributes
   // to a TTF/OTF file path.
   //
   //    register_font("Noto Sans", "resources/NotoSans-Regular.ttf");
   //    register_font("Noto Sans", "resources/NotoSans-Bold.ttf",
   //       font_constants::bold);
   //    register_font("Noto Sans", "resources/NotoSans-Italic.ttf",
   //       font_constants::weight_normal, font_constants::italic);
   //
   // Returns the *embedded* font family name as extracted by ThorVG (e.g.
   // "Noto Sans JP" for a file whose internal name table says so). The font
   // is registered under both the caller-supplied `family` *and* the embedded
   // name (when they differ), so callers can look up by either. Returns an
   // empty string when the font failed to load or the loader could not
   // report an embedded name.
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font(
      std::string const&                family,
      std::string const&                file,
      font_constants::weight_enum       weight  = font_constants::weight_normal,
      font_constants::slant_enum        slant   = font_constants::slant_normal,
      font_constants::stretch_enum      stretch = font_constants::stretch_normal
   );

   ////////////////////////////////////////////////////////////////////////////
   // Register a font from an in-memory buffer. Useful when fonts are embedded
   // as resources (e.g. in a Win32 .rc) and there is no real on-disk path.
   //
   // `key` is an identifier used internally as the cache key (replaces the
   // file path in the path-based register_font). Re-using the same key with a
   // different `data` is a no-op (the first registration wins).
   //
   // Both ThorVG (with copy=true) and the FreeType backend take a copy of
   // `data`, so the caller can free the buffer after this returns.
   //
   // Returns the embedded font family name (same semantics as the path-based
   // `register_font` above).
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font_buffer(
      std::string const&                family,
      std::string const&                key,
      std::uint8_t const*               data,
      std::size_t                       size,
      font_constants::weight_enum       weight  = font_constants::weight_normal,
      font_constants::slant_enum        slant   = font_constants::slant_normal,
      font_constants::stretch_enum      stretch = font_constants::stretch_normal
   );

   ////////////////////////////////////////////////////////////////////////////
   // Scan a directory for TTF/OTF files and auto-register them.
   // Extracts family name and weight/slant from filename conventions.
   void load_fonts_from_directory(std::string const& dir);

   ////////////////////////////////////////////////////////////////////////////
   // parse_font_name — split a PostScript/filename-style font name such as
   // "NotoSansJP-Medium" into a human family ("Noto Sans JP") plus weight/slant/
   // stretch, using the same rules as load_fonts_from_directory. Used to turn a
   // source (e.g. PSD) font name into a font_descr that resolves against the
   // registered font map.
   struct parsed_font_name
   {
      std::string                       family;
      font_constants::weight_enum       weight  = font_constants::weight_normal;
      font_constants::slant_enum        slant   = font_constants::slant_normal;
      font_constants::stretch_enum      stretch = font_constants::stretch_normal;
   };
   parsed_font_name parse_font_name(std::string const& name);

   ////////////////////////////////////////////////////////////////////////////
   // font_family_available — true if `family` is registered (i.e. usable for
   // drawing). If false, a matching font file must be added to the fonts
   // directory (see load_fonts_from_directory).
   bool font_family_available(std::string const& family);
}}

#endif

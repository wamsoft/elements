/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman
   Copyright (c) 2020 Michał Urbański

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/font.hpp>
#include <elements/support/glyph_utils.hpp>
#include <elements/support/resource_loader.hpp>
#include <infra/assert.hpp>
#include <infra/filesystem.hpp>

#include <thorvg.h>


#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cmath>

namespace cycfi { namespace elements
{
   namespace
   {
      inline void ltrim(std::string& s)
      {
         s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](int ch) { return ch != ' ' && ch != '"'; }
         ));
      }

      inline void rtrim(std::string& s)
      {
         s.erase(std::find_if(s.rbegin(), s.rend(),
            [](int ch) { return ch != ' ' && ch != '"'; }
         ).base(), s.end());
      }

      inline void trim(std::string& s)
      {
         ltrim(s);
         rtrim(s);
      }

      ////////////////////////////////////////////////////////////////////////
      // Font registry — manually populated via register_font()
      ////////////////////////////////////////////////////////////////////////
      struct font_entry
      {
         std::string    file;
         std::uint8_t   weight;
         std::uint8_t   slant;
         std::uint8_t   stretch;
      };

      using font_map_type = std::map<std::string, std::vector<font_entry>>;

      font_map_type& font_map()
      {
         static font_map_type map_;
         return map_;
      }

      std::mutex& font_map_mutex()
      {
         static std::mutex mtx_;
         return mtx_;
      }

      font_entry const* match(font_descr descr)
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());

         std::istringstream str(std::string{descr._families});
         std::string family;
         while (getline(str, family, ','))
         {
            trim(family);
            if (auto i = font_map().find(family); i != font_map().end())
            {
               int min_diff = 10000;
               font_entry const* best = nullptr;
               for (auto const& entry : i->second)
               {
                  // Biased score: slant (3.0) > weight (1.0) > stretch (0.25)
                  auto diff =
                     (std::abs(int(descr._weight) - int(entry.weight)) * 1.0) +
                     (std::abs(int(descr._slant) - int(entry.slant)) * 3.0) +
                     (std::abs(int(descr._stretch) - int(entry.stretch)) * 0.25)
                     ;
                  if (diff < min_diff)
                  {
                     min_diff = diff;
                     best = &entry;
                  }
               }
               if (best)
                  return best;
            }
         }
         return nullptr;
      }

      std::string find_matched_family(font_descr descr)
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());

         std::istringstream str(std::string{descr._families});
         std::string family;
         while (getline(str, family, ','))
         {
            trim(family);
            if (font_map().find(family) != font_map().end())
               return family;
         }
         return {};
      }

      ////////////////////////////////////////////////////////////////////////
      // Derive the ThorVG font name from a file path or buffer key by
      // stripping the directory and extension. Matches the convention used
      // by text_backend_tvg (stem_from_path).
      ////////////////////////////////////////////////////////////////////////
      std::string stem_from_path(std::string const& path)
      {
         auto slash = path.find_last_of("/\\");
         auto start = (slash != std::string::npos) ? slash + 1 : 0;
         auto dot = path.rfind('.');
         auto end = (dot != std::string::npos && dot > start) ? dot : path.size();
         return path.substr(start, end - start);
      }
   }

   namespace
   {
      // Query the font that was just registered into ThorVG and return its
      // embedded family name (from the OTF/TTF name table). Empty if ThorVG
      // could not extract a name. Helper for both register_font variants.
      std::string query_embedded_family(std::string const& thorvg_key)
      {
         tvg::TextInfo tinfo{};
         if (tvg::Text::info(thorvg_key.c_str(), tinfo) == tvg::Result::Success
             && tinfo.family && *tinfo.family)
         {
            return tinfo.family;
         }
         return {};
      }

      // Add an alias entry to font_map under `family_alias`, pointing to the
      // same on-disk file/key as the original registration. Caller-side font
      // matching (label_font etc.) can then resolve either the caller-supplied
      // family name or the embedded one.
      void add_family_alias(std::string const& family_alias,
         std::string const& file_key,
         font_constants::weight_enum weight,
         font_constants::slant_enum slant,
         font_constants::stretch_enum stretch)
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file_key;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         font_map()[family_alias].push_back(std::move(entry));
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // register_font — public API
   //
   // Reads the font bytes through the active resource_loader and registers
   // the result via the in-memory path. This keeps a single code path for
   // font lifetime: the buffer is owned by the FT backend and by ThorVG
   // (via copy=true), and the loader does not need to keep the bytes
   // around.
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font(
      std::string const&                family,
      std::string const&                file,
      font_constants::weight_enum       weight,
      font_constants::slant_enum        slant,
      font_constants::stretch_enum      stretch)
   {
      auto bytes = get_resource_loader().read(file);
      if (bytes.empty())
         return {};

      // Register in internal font map. The file path stays the entry key
      // so canvas state and glyph layout (which keys FT_Face by f.file())
      // can find the registered face.
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         font_map()[family].push_back(std::move(entry));
      }

      // ThorVG indexes fonts by name. text_backend_tvg derives that name
      // by stripping the directory and extension from canvas state's
      // font_file, so register the same stem here.
      auto thorvg_name = stem_from_path(file);
      tvg::Text::load(
         thorvg_name.c_str(),
         reinterpret_cast<const char*>(bytes.data()),
         static_cast<uint32_t>(bytes.size()),
         "ttf",
         /*copy=*/true);

      // Ask ThorVG for the font's embedded family name. If it's different
      // from the caller-supplied family, also expose it as a font_map alias
      // so lookups by either name resolve.
      auto embedded = query_embedded_family(thorvg_name);
      if (!embedded.empty() && embedded != family)
         add_family_alias(embedded, file, weight, slant, stretch);

      // FreeType side uses the original file string as the cache key, to
      // match glyph_layout_ft.cpp's get_face(f.file()) lookup.
      get_font_backend()->initialize();
      get_font_backend()->register_font_buffer(file, bytes.data(), bytes.size());

      return embedded;
   }

   ////////////////////////////////////////////////////////////////////////////
   // register_font_buffer — public API (in-memory font registration)
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font_buffer(
      std::string const&                family,
      std::string const&                key,
      std::uint8_t const*               data,
      std::size_t                       size,
      font_constants::weight_enum       weight,
      font_constants::slant_enum        slant,
      font_constants::stretch_enum      stretch)
   {
      if (!data || size == 0 || key.empty())
         return {};

      // Register in internal font map. `key` plays the role normally taken
      // by the file path, so canvas/text rendering will use it as identifier.
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = key;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         font_map()[family].push_back(std::move(entry));
      }

      // Register with ThorVG (copy=true so we don't need to keep the
      // caller's buffer alive for ThorVG).
      tvg::Text::load(
         key.c_str(),
         reinterpret_cast<const char*>(data),
         static_cast<uint32_t>(size),
         "ttf",
         /*copy=*/true);

      // Ask ThorVG for the embedded family name. If different from the
      // caller-supplied family, also expose it as a font_map alias.
      auto embedded = query_embedded_family(key);
      if (!embedded.empty() && embedded != family)
         add_family_alias(embedded, key, weight, slant, stretch);

      // Register with the active font backend (FreeType etc.). The backend
      // takes its own copy of the buffer.
      get_font_backend()->initialize();
      get_font_backend()->register_font_buffer(key, data, size);

      return embedded;
   }

   ////////////////////////////////////////////////////////////////////////////
   // font
   ////////////////////////////////////////////////////////////////////////////
   font::font(font_descr descr)
   {
      auto match_ptr = match(descr);
      if (match_ptr)
      {
         _file = match_ptr->file;
         _family = find_matched_family(descr);
      }
      _size = descr._size;
   }

   font::font(font const& rhs)
    : _family(rhs._family)
    , _file(rhs._file)
    , _size(rhs._size)
   {
   }

   font& font::operator=(font const& rhs)
   {
      if (&rhs != this)
      {
         _family = rhs._family;
         _file = rhs._file;
         _size = rhs._size;
      }
      return *this;
   }

   font::font(font&& rhs) noexcept
    : _family(std::move(rhs._family))
    , _file(std::move(rhs._file))
    , _size(rhs._size)
   {
   }

   font& font::operator=(font&& rhs) noexcept
   {
      if (&rhs != this)
      {
         _family = std::move(rhs._family);
         _file = std::move(rhs._file);
         _size = rhs._size;
      }
      return *this;
   }

   font::~font()
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   // load_fonts_from_directory — scan and auto-register fonts
   ////////////////////////////////////////////////////////////////////////////
   namespace
   {
      struct font_file_info
      {
         std::string family;
         font_constants::weight_enum weight = font_constants::weight_normal;
         font_constants::slant_enum slant = font_constants::slant_normal;
         font_constants::stretch_enum stretch = font_constants::stretch_normal;
      };

      // Insert spaces before uppercase letters following lowercase:
      // "OpenSans" → "Open Sans", "RobotoMono" → "Roboto Mono"
      std::string expand_camel_case(std::string const& name)
      {
         std::string result;
         for (size_t i = 0; i < name.size(); ++i)
         {
            if (i > 0 && std::isupper(name[i]) && std::islower(name[i - 1]))
               result += ' ';
            result += name[i];
         }
         return result;
      }

      font_file_info parse_font_filename(std::string const& stem)
      {
         font_file_info info;

         // Split on '-' to separate family from style
         auto dash_pos = stem.find('-');
         std::string family_part = (dash_pos != std::string::npos) ? stem.substr(0, dash_pos) : stem;
         std::string style_part = (dash_pos != std::string::npos) ? stem.substr(dash_pos + 1) : "";

         // Expand CamelCase: "OpenSans" → "Open Sans"
         info.family = expand_camel_case(family_part);

         // Remove "Condensed" from family and add stretch
         if (info.family.find("Condensed") != std::string::npos)
         {
            // e.g. "Open Sans Condensed" → family "Open Sans Condensed", stretch condensed
            info.stretch = font_constants::condensed;
         }

         // Parse style part for weight/slant
         // Convert to lowercase for matching
         std::string style_lower = style_part;
         std::transform(style_lower.begin(), style_lower.end(), style_lower.begin(), ::tolower);

         // Check for "variablefont" pattern (e.g. "VariableFont_wght")
         // These are typically regular weight
         if (style_lower.find("variablefont") != std::string::npos)
         {
            // Check if italic is in the style
            if (style_lower.find("italic") != std::string::npos)
               info.slant = font_constants::italic;
            return info;
         }

         // Weight detection
         if (style_lower.find("thin") != std::string::npos)
            info.weight = font_constants::thin;
         else if (style_lower.find("extralight") != std::string::npos ||
                  style_lower.find("extra_light") != std::string::npos)
            info.weight = font_constants::extra_light;
         else if (style_lower.find("semibold") != std::string::npos ||
                  style_lower.find("semi_bold") != std::string::npos ||
                  style_lower.find("demibold") != std::string::npos)
            info.weight = font_constants::semi_bold;
         else if (style_lower.find("extrabold") != std::string::npos)
            info.weight = font_constants::extra_bold;
         else if (style_lower.find("bold") != std::string::npos)
            info.weight = font_constants::bold;
         else if (style_lower.find("black") != std::string::npos)
            info.weight = font_constants::black;
         else if (style_lower.find("medium") != std::string::npos)
            info.weight = font_constants::medium;
         else if (style_lower.find("light") != std::string::npos)
            info.weight = font_constants::light;

         // Slant detection
         if (style_lower.find("italic") != std::string::npos)
            info.slant = font_constants::italic;
         else if (style_lower.find("oblique") != std::string::npos)
            info.slant = font_constants::oblique;

         return info;
      }
   }

   void load_fonts_from_directory(std::string const& dir)
   {
#if defined(ELEMENTS_FILE_IO_SUPPORT)
      fs::path font_dir(dir);
      if (!fs::exists(font_dir) || !fs::is_directory(font_dir))
         return;

      for (auto const& entry : fs::directory_iterator(font_dir))
      {
         if (!entry.is_regular_file())
            continue;

         auto ext = entry.path().extension().string();
         std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
         if (ext != ".ttf" && ext != ".otf")
            continue;

         auto stem = entry.path().stem().string();
         auto file_path = entry.path().string();

         auto info = parse_font_filename(stem);

         // Register through the standard memory-based path. The active
         // resource_loader will read the bytes (the default loader uses
         // an absolute path, so it just reads from disk).
         register_font(info.family, file_path, info.weight, info.slant, info.stretch);
      }
#else
      (void)dir;
#endif
   }
}}

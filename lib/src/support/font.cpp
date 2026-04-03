/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman
   Copyright (c) 2020 Michał Urbański

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/font.hpp>
#include <infra/assert.hpp>
#include <infra/filesystem.hpp>

#include <richtext/FontManager.hpp>

#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <vector>
#include <fstream>
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
      // richtext FontManager integration
      ////////////////////////////////////////////////////////////////////////
      std::once_flag                fm_init_flag;
      std::set<std::string>         fm_registered_files;
      std::mutex                    fm_registered_mutex;

      void init_font_manager()
      {
         auto& fm = richtext::FontManager::instance();
         fm.initialize();

         // File-based font data loader
         fm.setFontDataLoader(
            [](std::string const& file_path) -> richtext::FontDataBuffer
            {
               std::ifstream file(file_path, std::ios::binary);
               if (!file.is_open())
                  return nullptr;
               auto data = std::make_shared<std::vector<uint8_t>>(
                  std::istreambuf_iterator<char>(file),
                  std::istreambuf_iterator<char>()
               );
               if (data->empty())
                  return nullptr;
               return data;
            }
         );
      }

      void ensure_fm_font_registered(std::string const& file_path)
      {
         std::call_once(fm_init_flag, init_font_manager);

         std::lock_guard<std::mutex> lock(fm_registered_mutex);
         if (fm_registered_files.find(file_path) == fm_registered_files.end())
         {
            auto& fm = richtext::FontManager::instance();
            if (fm.registerFont(file_path, file_path))
               fm_registered_files.insert(file_path);
         }
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // register_font — public API
   ////////////////////////////////////////////////////////////////////////////
   void register_font(
      std::string const&                family,
      std::string const&                file,
      font_constants::weight_enum       weight,
      font_constants::slant_enum        slant,
      font_constants::stretch_enum      stretch)
   {
      // Register in internal font map
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         font_map()[family].push_back(std::move(entry));
      }

      // Also register with richtext::FontManager
      ensure_fm_font_registered(file);
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
}}

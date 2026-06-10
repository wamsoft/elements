/*=============================================================================
   Copyright (c) 2016-2026 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if defined(ELEMENTS_FILE_IO_SUPPORT)

#include <elements/support/filesystem_resource_loader.hpp>

#include <cstdio>
#include <string>

namespace cycfi { namespace elements
{
   void filesystem_resource_loader::add_search_path(
      fs::path const& path, bool search_first)
   {
      std::lock_guard<std::mutex> lock(_mutex);
      if (search_first)
         _paths.insert(_paths.begin(), path);
      else
         _paths.push_back(path);
   }

   fs::path filesystem_resource_loader::resolve(std::string_view name)
   {
      fs::path query{std::string{name}};
      if (query.is_absolute())
         return fs::exists(query) ? query : fs::path{};

      std::lock_guard<std::mutex> lock(_mutex);
      for (auto const& base : _paths)
      {
         fs::path candidate = base / query;
         if (fs::exists(candidate))
            return candidate;
      }
      return {};
   }

   bool filesystem_resource_loader::exists(std::string_view name)
   {
      return !resolve(name).empty();
   }

   std::vector<std::uint8_t> filesystem_resource_loader::read(std::string_view name)
   {
      fs::path full_path = resolve(name);
      if (full_path.empty())
         return {};

      std::string p = full_path.string();
      auto* f = std::fopen(p.c_str(), "rb");
      if (!f)
         return {};

      std::fseek(f, 0, SEEK_END);
      auto raw_size = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);

      if (raw_size <= 0)
      {
         std::fclose(f);
         return {};
      }

      std::vector<std::uint8_t> buffer(static_cast<std::size_t>(raw_size));
      auto read_count = std::fread(buffer.data(), 1, buffer.size(), f);
      std::fclose(f);

      if (read_count != buffer.size())
         buffer.resize(read_count);
      return buffer;
   }
}}

#endif // ELEMENTS_FILE_IO_SUPPORT

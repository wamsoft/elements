/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/resource_paths.hpp>
#include <elements/support/resource_loader.hpp>

#if defined(ELEMENTS_FILE_IO_SUPPORT)
# include <elements/support/filesystem_resource_loader.hpp>
#endif

namespace cycfi { namespace elements
{
   void add_search_path(fs::path const& path, bool search_first)
   {
#if defined(ELEMENTS_FILE_IO_SUPPORT)
      // Route to the active loader if it is a filesystem_resource_loader.
      // Custom loaders manage their own resolution logic.
      if (auto* fs_loader =
             dynamic_cast<filesystem_resource_loader*>(&get_resource_loader()))
      {
         fs_loader->add_search_path(path, search_first);
      }
#else
      (void)path; (void)search_first;
#endif
   }

   fs::path find_file(fs::path const& file)
   {
#if defined(ELEMENTS_FILE_IO_SUPPORT)
      if (auto* fs_loader =
             dynamic_cast<filesystem_resource_loader*>(&get_resource_loader()))
      {
         return fs_loader->resolve(file.string());
      }
#else
      (void)file;
#endif
      return {};
   }
}}

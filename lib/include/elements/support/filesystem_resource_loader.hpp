/*=============================================================================
   Copyright (c) 2016-2026 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_FILESYSTEM_RESOURCE_LOADER_JUNE_11_2026)
#define ELEMENTS_FILESYSTEM_RESOURCE_LOADER_JUNE_11_2026

#include <elements/support/resource_loader.hpp>
#include <infra/filesystem.hpp>

#include <mutex>
#include <vector>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // filesystem_resource_loader
   //
   // The default loader on platforms with a normal filesystem. Resolves
   // logical names by searching a list of registered directories
   // (add_search_path). Absolute paths are accepted as-is.
   //
   // Compiled only when ELEMENTS_FILE_IO_SUPPORT is defined.
   ////////////////////////////////////////////////////////////////////////////
   class filesystem_resource_loader : public resource_loader
   {
   public:
      void                          add_search_path(
                                       fs::path const& path,
                                       bool search_first = false);

      // Resolve a logical name to an absolute filesystem path, if it
      // exists. Returns an empty path if not found. Useful for callers
      // that need an actual path (e.g. file dialogs) rather than bytes.
      fs::path                      resolve(std::string_view name);

      bool                          exists(std::string_view name) override;
      std::vector<std::uint8_t>     read(std::string_view name) override;

   private:
      std::vector<fs::path>         _paths;
      std::mutex                    _mutex;
   };
}}

#endif

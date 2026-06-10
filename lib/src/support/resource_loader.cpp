/*=============================================================================
   Copyright (c) 2016-2026 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/resource_loader.hpp>

#if defined(ELEMENTS_FILE_IO_SUPPORT)
# include <elements/support/filesystem_resource_loader.hpp>
#endif

#include <memory>
#include <mutex>

namespace cycfi { namespace elements
{
   bool resource_loader::exists(std::string_view name)
   {
      return !read(name).empty();
   }

   namespace
   {
#if !defined(ELEMENTS_FILE_IO_SUPPORT)
      // Fallback loader for builds without filesystem support. Returns
      // empty for every name; applications must install their own loader
      // via set_resource_loader().
      class null_resource_loader : public resource_loader
      {
      public:
         std::vector<std::uint8_t> read(std::string_view) override
         {
            return {};
         }
      };
#endif

      std::shared_ptr<resource_loader>& loader_slot()
      {
         static std::shared_ptr<resource_loader> instance;
         return instance;
      }

      std::mutex& loader_mutex()
      {
         static std::mutex m;
         return m;
      }

      std::shared_ptr<resource_loader> make_default_loader()
      {
#if defined(ELEMENTS_FILE_IO_SUPPORT)
         return std::make_shared<filesystem_resource_loader>();
#else
         return std::make_shared<null_resource_loader>();
#endif
      }
   }

   resource_loader& get_resource_loader()
   {
      std::lock_guard<std::mutex> lock(loader_mutex());
      auto& slot = loader_slot();
      if (!slot)
         slot = make_default_loader();
      return *slot;
   }

   void set_resource_loader(std::shared_ptr<resource_loader> loader)
   {
      std::lock_guard<std::mutex> lock(loader_mutex());
      loader_slot() = loader ? std::move(loader) : make_default_loader();
   }
}}

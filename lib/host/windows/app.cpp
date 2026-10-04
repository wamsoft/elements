/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/app.hpp>
#include <elements/support/detail/scratch_context.hpp>
#include <elements/support/font.hpp>
#include <infra/filesystem.hpp>
#include <windows.h>
#include <shlobj.h>
#include <cstring>
#include <ole2.h>
#include <thorvg.h>

#ifndef ELEMENTS_HOST_ONLY_WIN7
#include <shellscalingapi.h>
#endif

namespace cycfi::elements
{
   app::app(std::string name)
   {
      _app_name = name;

#if !defined(ELEMENTS_HOST_ONLY_WIN7)
      SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif

      OleInitialize(nullptr);

      // Initialize ThorVG rendering engine
      tvg::Initializer::init(4);

   }

   app::~app()
   {
      // Drop the measuring canvas first: while it lives, ThorVG's renderer
      // refuses to terminate, Initializer::term() returns before unloading
      // the font loaders, and their static destructors then touch the
      // already destroyed font manager at exit (an abort on macOS).
      detail::release_shared_scratch();
      tvg::Initializer::term();
   }

   void app::run()
   {
      MSG messages;
      while (_running && GetMessage(&messages, nullptr, 0, 0) > 0)
      {
         TranslateMessage(&messages);
         DispatchMessage(&messages);
      }
   }

   void app::stop()
   {
      _running = false;
      OleUninitialize();
   }

   fs::path app_data_path()
   {
      LPWSTR path = nullptr;
      SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr, &path);
      return fs::path{path};
   }
}


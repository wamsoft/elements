/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/app.hpp>
#include <elements/support/detail/scratch_context.hpp>
#include <elements/support/font.hpp>
#include <elements/support/resource_paths.hpp>
#include <infra/filesystem.hpp>
#include <SDL3/SDL.h>
#include <thorvg.h>
#include <vector>

namespace cycfi::elements
{
   // Defined in base_view.cpp (SDL)
   void dispatch_sdl_event(SDL_Event const& e);
   void poll_and_repaint_all();

   app::app(std::string name)
   {
      _app_name = name;

      SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);

      // Initialize ThorVG rendering engine
      tvg::Initializer::init(4);

#if defined(ELEMENTS_FILE_IO_SUPPORT)
      // Load fonts from exe-relative resources directory
      auto base_path = SDL_GetBasePath();  // SDL3: returns const char*, no free needed
      if (base_path)
      {
         fs::path exe_dir(base_path);
         std::vector<fs::path> res_dirs{exe_dir / "resources"};
#if defined(__APPLE__)
         // In an .app bundle SDL_GetBasePath() is Contents/Resources/, which
         // is where the bundle keeps the resource files themselves.
         res_dirs.push_back(exe_dir);
#endif
         for (auto const& res_dir : res_dirs)
         {
            if (!fs::exists(res_dir))
               continue;
            add_search_path(res_dir);
            auto fonts_dir = res_dir / "fonts";
            if (fs::exists(fonts_dir))
               load_fonts_from_directory(fonts_dir.string());
            load_fonts_from_directory(res_dir.string());
         }
      }
#endif
   }

   app::~app()
   {
      // Drop the measuring canvas first: while it lives, ThorVG's renderer
      // refuses to terminate, Initializer::term() returns before unloading
      // the font loaders, and their static destructors then touch the
      // already destroyed font manager at exit (an abort on macOS).
      detail::release_shared_scratch();
      tvg::Initializer::term();
      SDL_Quit();
   }

   void app::run()
   {
      SDL_Event e;
      while (_running)
      {
         while (SDL_PollEvent(&e))
         {
            if (e.type == SDL_EVENT_QUIT)
            {
               _running = false;
               break;
            }
            dispatch_sdl_event(e);
         }
         // Poll task_queue and repaint dirty views
         poll_and_repaint_all();
         SDL_Delay(1);
      }
   }

   void app::stop()
   {
      _running = false;
      SDL_Event e = {};
      e.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&e);
   }

   fs::path app_data_path()
   {
      auto path = SDL_GetPrefPath("cycfi", "elements");  // SDL3: const char*, no free
      if (path)
         return fs::path(path);
#ifdef _WIN32
      return fs::path("C:/ProgramData");
#else
      return fs::path("/tmp");
#endif
   }
}

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/app.hpp>
#include <elements/support/font.hpp>
#include <elements/support/resource_paths.hpp>
#include <infra/filesystem.hpp>
#include <SDL3/SDL.h>
#include <thorvg.h>

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

      // Load fonts from exe-relative resources directory
      auto base_path = SDL_GetBasePath();  // SDL3: returns const char*, no free needed
      if (base_path)
      {
         fs::path exe_dir(base_path);
         auto res_dir = exe_dir / "resources";
         add_search_path(res_dir);

         auto fonts_dir = res_dir / "fonts";
         if (fs::exists(fonts_dir))
            load_fonts_from_directory(fonts_dir.string());
         if (fs::exists(res_dir))
            load_fonts_from_directory(res_dir.string());
      }
   }

   app::~app()
   {
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
         // Poll ASIO tasks and repaint dirty views
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

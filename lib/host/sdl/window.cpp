/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/window.hpp>
#include <SDL3/SDL.h>
#include <map>
#include <algorithm>

namespace cycfi::elements
{
   namespace
   {
      struct window_info
      {
         window*        self = nullptr;
         view_limits    limits_ = {};
      };

      // SDL3 removed SDL_SetWindowData/SDL_GetWindowData.
      // Use a global map instead.
      std::map<SDL_Window*, window_info*>& window_info_map()
      {
         static std::map<SDL_Window*, window_info*> m;
         return m;
      }

      window_info* get_window_info(SDL_Window* w)
      {
         auto& m = window_info_map();
         auto it = m.find(w);
         return it != m.end() ? it->second : nullptr;
      }
   }

   window::window(std::string const& name, int style_, rect const& bounds)
   {
      SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
      if (style_ & window::resizable)
         flags |= SDL_WINDOW_RESIZABLE;

      int w = int(bounds.width());
      int h = int(bounds.height());
      if (w <= 0) w = 800;
      if (h <= 0) h = 600;

      _window = SDL_CreateWindow(name.c_str(), w, h, flags);

      if (bounds.left >= 0 && bounds.top >= 0)
         SDL_SetWindowPosition(_window, int(bounds.left), int(bounds.top));

      auto* info = new window_info{this};
      window_info_map()[_window] = info;
   }

   window::~window()
   {
      if (_window)
      {
         auto* info = get_window_info(_window);
         if (info)
         {
            window_info_map().erase(_window);
            delete info;
         }
         SDL_DestroyWindow(_window);
      }
   }

   point window::size() const
   {
      int w, h;
      SDL_GetWindowSize(_window, &w, &h);
      return {float(w), float(h)};
   }

   void window::size(point const& p)
   {
      SDL_SetWindowSize(_window, int(p.x), int(p.y));
   }

   void window::limits(view_limits limits_)
   {
      auto* info = get_window_info(_window);
      if (info)
         info->limits_ = limits_;

      // Constrain current size to limits (matching Win32 behavior)
      // limits are in logical (unscaled) coordinates.
      // SDL GetWindowSize returns points. With HIGH_PIXEL_DENSITY,
      // we need to convert limits to the same coordinate space.
      float scale = SDL_GetWindowDisplayScale(_window);
      if (scale <= 0) scale = 1.0f;

      int cw, ch;
      SDL_GetWindowSize(_window, &cw, &ch);

      // Convert logical limits to points (SDL window coordinates)
      // Points = logical * scale (for HIGH_PIXEL_DENSITY windows)
      int min_w = int(limits_.min.x * scale);
      int min_h = int(limits_.min.y * scale);
      int max_w = (limits_.max.x < full_extent) ? int(limits_.max.x * scale) : 100000;
      int max_h = (limits_.max.y < full_extent) ? int(limits_.max.y * scale) : 100000;

      int nw = std::max(min_w, std::min(cw, max_w));
      int nh = std::max(min_h, std::min(ch, max_h));
      if (nw != cw || nh != ch)
         SDL_SetWindowSize(_window, nw, nh);

      // Also update SDL min/max in scaled coordinates
      SDL_SetWindowMinimumSize(_window, min_w, min_h);
      if (limits_.max.x < full_extent && limits_.max.y < full_extent)
         SDL_SetWindowMaximumSize(_window, max_w, max_h);
   }

   point window::position() const
   {
      int x, y;
      SDL_GetWindowPosition(_window, &x, &y);
      return {float(x), float(y)};
   }

   void window::position(point const& p)
   {
      SDL_SetWindowPosition(_window, int(p.x), int(p.y));
   }

   // host() is defined inline in window.hpp
}

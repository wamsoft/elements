/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements/base_view.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/resource_paths.hpp>
#include <SDL3/SDL.h>
#include <chrono>
#include <map>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <unordered_map>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Internal view state
   ////////////////////////////////////////////////////////////////////////////
   namespace
   {
      using time_point = std::chrono::time_point<std::chrono::steady_clock>;
      using key_map = std::map<key_code, key_action>;

      struct view_state
      {
         base_view*     vptr = nullptr;
         SDL_Window*    window = nullptr;
         SDL_Renderer*  renderer = nullptr;
         SDL_Texture*   texture = nullptr;
         uint32_t*      pixel_buffer = nullptr;
         int            w = 0;
         int            h = 0;
         float          scale = 1.0f;
         bool           is_dragging = false;
         bool           mouse_in_window = false;
         time_point     click_start = {};
         int            click_count = 0;
         key_map        keys = {};
         bool           needs_refresh = true;
      };

      // Map SDL window IDs to view states
      std::map<SDL_WindowID, view_state*>& view_registry()
      {
         static std::map<SDL_WindowID, view_state*> reg;
         return reg;
      }

      view_state* get_view_state(SDL_WindowID window_id)
      {
         auto& reg = view_registry();
         auto it = reg.find(window_id);
         return (it != reg.end()) ? it->second : nullptr;
      }

      view_state* get_view_state_for(SDL_Window* w)
      {
         if (!w) return nullptr;
         return get_view_state(SDL_GetWindowID(w));
      }

      SDL_Cursor* current_cursor = nullptr;

      void ensure_buffer(view_state* vs)
      {
         int w, h;
         SDL_GetWindowSize(vs->window, &w, &h);
         vs->scale = SDL_GetWindowDisplayScale(vs->window);
         if (vs->scale <= 0) vs->scale = 1.0f;

         // Use pixel size for the buffer (accounts for high-DPI)
         int pw, ph;
         SDL_GetWindowSizeInPixels(vs->window, &pw, &ph);

         if (pw != vs->w || ph != vs->h)
         {
            vs->w = pw;
            vs->h = ph;

            delete[] vs->pixel_buffer;
            vs->pixel_buffer = new uint32_t[pw * ph];

            if (vs->texture)
               SDL_DestroyTexture(vs->texture);
            vs->texture = SDL_CreateTexture(
               vs->renderer,
               SDL_PIXELFORMAT_ARGB8888,
               SDL_TEXTUREACCESS_STREAMING,
               pw, ph
            );
         }
      }

      void do_paint(view_state* vs)
      {
         if (!vs || !vs->vptr || !vs->window)
            return;

         ensure_buffer(vs);

         // Clear to opaque white
         auto total = vs->w * vs->h;
         std::fill_n(vs->pixel_buffer, total, 0xFFFFFFFF);

         // Render via canvas
         {
            canvas cnv{vs->pixel_buffer,
               (uint32_t)vs->w, (uint32_t)vs->h, vs->scale};
            vs->vptr->draw(cnv);
         }

         // Upload to texture and present
         SDL_UpdateTexture(vs->texture, nullptr,
            vs->pixel_buffer, vs->w * sizeof(uint32_t));
         SDL_RenderClear(vs->renderer);
         SDL_RenderTexture(vs->renderer, vs->texture, nullptr, nullptr);
         SDL_RenderPresent(vs->renderer);

         vs->needs_refresh = false;
      }

      ////////////////////////////////////////////////////////////////////////
      // Key mapping
      ////////////////////////////////////////////////////////////////////////
      key_code translate_sdl_key(SDL_Scancode sc)
      {
         switch (sc)
         {
            case SDL_SCANCODE_A: return key_code::a;
            case SDL_SCANCODE_B: return key_code::b;
            case SDL_SCANCODE_C: return key_code::c;
            case SDL_SCANCODE_D: return key_code::d;
            case SDL_SCANCODE_E: return key_code::e;
            case SDL_SCANCODE_F: return key_code::f;
            case SDL_SCANCODE_G: return key_code::g;
            case SDL_SCANCODE_H: return key_code::h;
            case SDL_SCANCODE_I: return key_code::i;
            case SDL_SCANCODE_J: return key_code::j;
            case SDL_SCANCODE_K: return key_code::k;
            case SDL_SCANCODE_L: return key_code::l;
            case SDL_SCANCODE_M: return key_code::m;
            case SDL_SCANCODE_N: return key_code::n;
            case SDL_SCANCODE_O: return key_code::o;
            case SDL_SCANCODE_P: return key_code::p;
            case SDL_SCANCODE_Q: return key_code::q;
            case SDL_SCANCODE_R: return key_code::r;
            case SDL_SCANCODE_S: return key_code::s;
            case SDL_SCANCODE_T: return key_code::t;
            case SDL_SCANCODE_U: return key_code::u;
            case SDL_SCANCODE_V: return key_code::v;
            case SDL_SCANCODE_W: return key_code::w;
            case SDL_SCANCODE_X: return key_code::x;
            case SDL_SCANCODE_Y: return key_code::y;
            case SDL_SCANCODE_Z: return key_code::z;

            case SDL_SCANCODE_0: return key_code::_0;
            case SDL_SCANCODE_1: return key_code::_1;
            case SDL_SCANCODE_2: return key_code::_2;
            case SDL_SCANCODE_3: return key_code::_3;
            case SDL_SCANCODE_4: return key_code::_4;
            case SDL_SCANCODE_5: return key_code::_5;
            case SDL_SCANCODE_6: return key_code::_6;
            case SDL_SCANCODE_7: return key_code::_7;
            case SDL_SCANCODE_8: return key_code::_8;
            case SDL_SCANCODE_9: return key_code::_9;

            case SDL_SCANCODE_RETURN:    return key_code::enter;
            case SDL_SCANCODE_ESCAPE:    return key_code::escape;
            case SDL_SCANCODE_BACKSPACE: return key_code::backspace;
            case SDL_SCANCODE_TAB:       return key_code::tab;
            case SDL_SCANCODE_SPACE:     return key_code::space;
            case SDL_SCANCODE_DELETE:    return key_code::_delete;

            case SDL_SCANCODE_RIGHT: return key_code::right;
            case SDL_SCANCODE_LEFT:  return key_code::left;
            case SDL_SCANCODE_DOWN:  return key_code::down;
            case SDL_SCANCODE_UP:    return key_code::up;

            case SDL_SCANCODE_HOME:     return key_code::home;
            case SDL_SCANCODE_END:      return key_code::end;
            case SDL_SCANCODE_PAGEUP:   return key_code::page_up;
            case SDL_SCANCODE_PAGEDOWN: return key_code::page_down;
            case SDL_SCANCODE_INSERT:   return key_code::insert;

            case SDL_SCANCODE_LSHIFT:   return key_code::left_shift;
            case SDL_SCANCODE_RSHIFT:   return key_code::right_shift;
            case SDL_SCANCODE_LCTRL:    return key_code::left_control;
            case SDL_SCANCODE_RCTRL:    return key_code::right_control;
            case SDL_SCANCODE_LALT:     return key_code::left_alt;
            case SDL_SCANCODE_RALT:     return key_code::right_alt;
            case SDL_SCANCODE_LGUI:     return key_code::left_super;
            case SDL_SCANCODE_RGUI:     return key_code::right_super;

            case SDL_SCANCODE_F1:  return key_code::f1;
            case SDL_SCANCODE_F2:  return key_code::f2;
            case SDL_SCANCODE_F3:  return key_code::f3;
            case SDL_SCANCODE_F4:  return key_code::f4;
            case SDL_SCANCODE_F5:  return key_code::f5;
            case SDL_SCANCODE_F6:  return key_code::f6;
            case SDL_SCANCODE_F7:  return key_code::f7;
            case SDL_SCANCODE_F8:  return key_code::f8;
            case SDL_SCANCODE_F9:  return key_code::f9;
            case SDL_SCANCODE_F10: return key_code::f10;
            case SDL_SCANCODE_F11: return key_code::f11;
            case SDL_SCANCODE_F12: return key_code::f12;

            case SDL_SCANCODE_MINUS:        return key_code::minus;
            case SDL_SCANCODE_EQUALS:       return key_code::equal;
            case SDL_SCANCODE_LEFTBRACKET:  return key_code::left_bracket;
            case SDL_SCANCODE_RIGHTBRACKET: return key_code::right_bracket;
            case SDL_SCANCODE_BACKSLASH:    return key_code::backslash;
            case SDL_SCANCODE_SEMICOLON:    return key_code::semicolon;
            case SDL_SCANCODE_APOSTROPHE:   return key_code::apostrophe;
            case SDL_SCANCODE_GRAVE:        return key_code::grave_accent;
            case SDL_SCANCODE_COMMA:        return key_code::comma;
            case SDL_SCANCODE_PERIOD:       return key_code::period;
            case SDL_SCANCODE_SLASH:        return key_code::slash;

            case SDL_SCANCODE_KP_ENTER:    return key_code::kp_enter;
            case SDL_SCANCODE_KP_0:        return key_code::kp_0;
            case SDL_SCANCODE_KP_1:        return key_code::kp_1;
            case SDL_SCANCODE_KP_2:        return key_code::kp_2;
            case SDL_SCANCODE_KP_3:        return key_code::kp_3;
            case SDL_SCANCODE_KP_4:        return key_code::kp_4;
            case SDL_SCANCODE_KP_5:        return key_code::kp_5;
            case SDL_SCANCODE_KP_6:        return key_code::kp_6;
            case SDL_SCANCODE_KP_7:        return key_code::kp_7;
            case SDL_SCANCODE_KP_8:        return key_code::kp_8;
            case SDL_SCANCODE_KP_9:        return key_code::kp_9;
            case SDL_SCANCODE_KP_PLUS:     return key_code::kp_add;
            case SDL_SCANCODE_KP_MINUS:    return key_code::kp_subtract;
            case SDL_SCANCODE_KP_MULTIPLY: return key_code::kp_multiply;
            case SDL_SCANCODE_KP_DIVIDE:   return key_code::kp_divide;
            case SDL_SCANCODE_KP_PERIOD:   return key_code::kp_decimal;

            default: return key_code::unknown;
         }
      }

      int get_modifiers(SDL_Keymod mod)
      {
         int mods = 0;
         if (mod & SDL_KMOD_SHIFT) mods |= mod_shift;
         if (mod & SDL_KMOD_CTRL)  mods |= mod_control | mod_action;
         if (mod & SDL_KMOD_ALT)   mods |= mod_alt;
         if (mod & SDL_KMOD_GUI)   mods |= mod_super;
         return mods;
      }

      // -------------------------------------------------------------
      // Gamepad: opened controllers + SDL → elements code translation
      // -------------------------------------------------------------
      std::unordered_map<SDL_JoystickID, SDL_Gamepad*>& open_pads()
      {
         static std::unordered_map<SDL_JoystickID, SDL_Gamepad*> pads;
         return pads;
      }

      pad_button translate_sdl_pad_button(Uint8 b)
      {
         switch (b)
         {
            case SDL_GAMEPAD_BUTTON_SOUTH:         return pad_button::a;
            case SDL_GAMEPAD_BUTTON_EAST:          return pad_button::b;
            case SDL_GAMEPAD_BUTTON_WEST:          return pad_button::x;
            case SDL_GAMEPAD_BUTTON_NORTH:         return pad_button::y;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:       return pad_button::dpad_up;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:     return pad_button::dpad_down;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:     return pad_button::dpad_left;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:    return pad_button::dpad_right;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return pad_button::lb;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return pad_button::rb;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK:    return pad_button::l3;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK:   return pad_button::r3;
            case SDL_GAMEPAD_BUTTON_BACK:          return pad_button::back;
            case SDL_GAMEPAD_BUTTON_START:         return pad_button::start;
            case SDL_GAMEPAD_BUTTON_GUIDE:         return pad_button::guide;
            default:                               return pad_button::unknown;
         }
      }

      pad_axis translate_sdl_pad_axis(Uint8 a)
      {
         switch (a)
         {
            case SDL_GAMEPAD_AXIS_LEFTX:           return pad_axis::left_x;
            case SDL_GAMEPAD_AXIS_LEFTY:           return pad_axis::left_y;
            case SDL_GAMEPAD_AXIS_RIGHTX:          return pad_axis::right_x;
            case SDL_GAMEPAD_AXIS_RIGHTY:          return pad_axis::right_y;
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:    return pad_axis::lt;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:   return pad_axis::rt;
            default:                               return pad_axis::unknown;
         }
      }

      // SDL gamepad events aren't tied to a window. Route them to the
      // view whose window currently has SDL input focus; if none does,
      // fall back to any registered view.
      base_view* gamepad_target_view()
      {
         base_view* fallback = nullptr;
         for (auto& [wid, vs] : view_registry())
         {
            if (!vs || !vs->vptr) continue;
            if (!fallback) fallback = vs->vptr;
            if (vs->window
                && (SDL_GetWindowFlags(vs->window) & SDL_WINDOW_INPUT_FOCUS))
               return vs->vptr;
         }
         return fallback;
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // Event dispatch (called from app::run)
   ////////////////////////////////////////////////////////////////////////////
   void dispatch_sdl_event(SDL_Event const& e)
   {
      // Gamepad events aren't tied to a window. Handle them first and
      // route to whichever view holds keyboard focus.
      switch (e.type)
      {
         case SDL_EVENT_GAMEPAD_ADDED:
         {
            // SDL3: e.gdevice.which is the joystick instance ID.
            if (auto* gp = SDL_OpenGamepad(e.gdevice.which))
               open_pads()[e.gdevice.which] = gp;
            return;
         }
         case SDL_EVENT_GAMEPAD_REMOVED:
         {
            auto it = open_pads().find(e.gdevice.which);
            if (it != open_pads().end())
            {
               SDL_CloseGamepad(it->second);
               open_pads().erase(it);
            }
            return;
         }
         case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
         case SDL_EVENT_GAMEPAD_BUTTON_UP:
         {
            if (auto* tv = gamepad_target_view())
            {
               auto btn = translate_sdl_pad_button(e.gbutton.button);
               if (btn != pad_button::unknown)
                  tv->pad_button_event({btn, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN});
            }
            return;
         }
         case SDL_EVENT_GAMEPAD_AXIS_MOTION:
         {
            if (auto* tv = gamepad_target_view())
            {
               auto ax = translate_sdl_pad_axis(e.gaxis.axis);
               if (ax != pad_axis::unknown)
               {
                  // Normalize int16 range to [-1, 1]. Triggers report
                  // 0..32767; analog sticks -32768..32767. Both are
                  // handled by the same formula because consumers ignore
                  // the negative half of triggers.
                  float v = e.gaxis.value / 32767.0f;
                  if (v < -1.0f) v = -1.0f;
                  if (v >  1.0f) v =  1.0f;
                  tv->pad_axis_event({ax, v});
               }
            }
            return;
         }
         default:
            break;
      }

      SDL_WindowID wid = 0;

      // SDL3: window events are individual event types
      if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
         wid = e.window.windowID;
      else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
         wid = e.button.windowID;
      else if (e.type == SDL_EVENT_MOUSE_MOTION)
         wid = e.motion.windowID;
      else if (e.type == SDL_EVENT_MOUSE_WHEEL)
         wid = e.wheel.windowID;
      else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
         wid = e.key.windowID;
      else if (e.type == SDL_EVENT_TEXT_INPUT)
         wid = e.text.windowID;
      else if (e.type == SDL_EVENT_DROP_FILE || e.type == SDL_EVENT_DROP_TEXT)
         wid = e.drop.windowID;
      else
         return;

      auto* vs = get_view_state(wid);
      if (!vs || !vs->vptr) return;
      auto* view = vs->vptr;

      // SDL3: window events are separate event types
      if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
      {
         switch (e.type)
         {
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_SHOWN:
               vs->needs_refresh = true;
               do_paint(vs);
               break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
               vs->needs_refresh = true;
               do_paint(vs);
               break;

            case SDL_EVENT_WINDOW_MOUSE_ENTER:
               vs->mouse_in_window = true;
               break;

            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
               vs->mouse_in_window = false;
               view->cursor({-1, -1}, cursor_tracking::leaving);
               break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
               view->begin_focus();
               break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
               view->end_focus();
               break;
         }
         return;
      }

      switch (e.type)
      {
         case SDL_EVENT_MOUSE_BUTTON_DOWN:
         {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - vs->click_start;
            if (elapsed > std::chrono::milliseconds(400))
               vs->click_count = 0;
            vs->click_start = now;
            ++vs->click_count;
            vs->is_dragging = true;

            mouse_button::what btn = mouse_button::left;
            if (e.button.button == SDL_BUTTON_MIDDLE) btn = mouse_button::middle;
            if (e.button.button == SDL_BUTTON_RIGHT)  btn = mouse_button::right;

            float s = vs->scale;
            mouse_button mb{
               true, vs->click_count, btn,
               get_modifiers(SDL_GetModState()),
               {e.button.x / s, e.button.y / s}
            };
            view->click(mb);
            break;
         }

         case SDL_EVENT_MOUSE_BUTTON_UP:
         {
            vs->is_dragging = false;
            mouse_button::what btn = mouse_button::left;
            if (e.button.button == SDL_BUTTON_MIDDLE) btn = mouse_button::middle;
            if (e.button.button == SDL_BUTTON_RIGHT)  btn = mouse_button::right;

            float s = vs->scale;
            mouse_button mb{
               false, vs->click_count, btn,
               get_modifiers(SDL_GetModState()),
               {e.button.x / s, e.button.y / s}
            };
            view->click(mb);
            break;
         }

         case SDL_EVENT_MOUSE_MOTION:
         {
            float s = vs->scale;
            point pos{e.motion.x / s, e.motion.y / s};

            if (vs->is_dragging)
            {
               mouse_button mb{
                  true, vs->click_count, mouse_button::left,
                  get_modifiers(SDL_GetModState()), pos
               };
               view->drag(mb);
            }
            else
            {
               view->cursor(pos, cursor_tracking::hovering);
            }
            break;
         }

         case SDL_EVENT_MOUSE_WHEEL:
         {
            float dx = e.wheel.x;
            float dy = e.wheel.y;

            float mx, my;
            SDL_GetMouseState(&mx, &my);
            float s = vs->scale;
            view->scroll(
               {dx * 20.0f, dy * 20.0f},
               {mx / s, my / s}
            );
            break;
         }

         case SDL_EVENT_KEY_DOWN:
         case SDL_EVENT_KEY_UP:
         {
            auto kc = translate_sdl_key(e.key.scancode);
            auto action = (e.type == SDL_EVENT_KEY_DOWN)
               ? (e.key.repeat ? key_action::repeat : key_action::press)
               : key_action::release;

            vs->keys[kc] = action;

            key_info ki{
               kc, action,
               get_modifiers(e.key.mod)
            };
            view->key(ki);
            break;
         }

         case SDL_EVENT_TEXT_INPUT:
         {
            const char* p = e.text.text;
            while (*p)
            {
               uint32_t cp = 0;
               int len = 1;
               auto b = (uint8_t)*p;
               if (b < 0x80)       { cp = b; len = 1; }
               else if (b < 0xC0)  { ++p; continue; }
               else if (b < 0xE0)  { cp = b & 0x1F; len = 2; }
               else if (b < 0xF0)  { cp = b & 0x0F; len = 3; }
               else                { cp = b & 0x07; len = 4; }

               for (int i = 1; i < len && p[i]; ++i)
                  cp = (cp << 6) | (((uint8_t)p[i]) & 0x3F);

               text_info ti{cp};
               view->text(ti);
               p += len;
            }
            break;
         }

         case SDL_EVENT_DROP_FILE:
         {
            // Minimal drop support
            // SDL3: e.drop.data is const char*, no need to free
            break;
         }
      }

      // Repaint after any event that might have changed state
      if (vs->needs_refresh)
         do_paint(vs);
   }

   ////////////////////////////////////////////////////////////////////////////
   // Poll all views and repaint dirty ones (called from app::run)
   ////////////////////////////////////////////////////////////////////////////
   void poll_and_repaint_all()
   {
      for (auto& [wid, vs] : view_registry())
      {
         if (vs && vs->vptr)
         {
            vs->vptr->poll();
            if (vs->needs_refresh)
               do_paint(vs);
         }
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // base_view implementation
   ////////////////////////////////////////////////////////////////////////////
   base_view::base_view(extent size_)
   {
      _view = nullptr;
      _embedded_size = size_;
   }

   base_view::base_view(host_window_handle h)
   {
      _view = h;

      auto* vs = new view_state;
      vs->vptr = this;
      vs->window = h;
      vs->renderer = SDL_CreateRenderer(h, nullptr);

      auto wid = SDL_GetWindowID(h);
      view_registry()[wid] = vs;

      // Enable text input events for this window
      SDL_StartTextInput(h);
   }

   base_view::~base_view()
   {
      auto* vs = get_view_state_for(_view);
      if (vs)
      {
         SDL_StopTextInput(vs->window);

         auto wid = SDL_GetWindowID(vs->window);
         view_registry().erase(wid);

         if (vs->texture)
            SDL_DestroyTexture(vs->texture);
         if (vs->renderer)
            SDL_DestroyRenderer(vs->renderer);
         delete[] vs->pixel_buffer;
         delete vs;
      }
   }

   void base_view::refresh()
   {
      auto* vs = get_view_state_for(_view);
      if (vs)
         vs->needs_refresh = true;
   }

   void base_view::refresh(rect /* area */)
   {
      refresh();
   }

   point base_view::cursor_pos() const
   {
      float x, y;
      SDL_GetMouseState(&x, &y);
      auto* vs = get_view_state_for(_view);
      float s = vs ? vs->scale : 1.0f;
      return {x / s, y / s};
   }

   extent base_view::size() const
   {
      auto* vs = get_view_state_for(_view);
      if (!vs || !vs->window) {
         // Embedded モード: SDL_Window を持たない。コンストラクタで受けた
         // サイズをそのまま返す。
         return _embedded_size;
      }
      int w, h;
      SDL_GetWindowSize(vs->window, &w, &h);
      return {float(w) / vs->scale, float(h) / vs->scale};
   }

   void base_view::size(extent size_)
   {
      auto* vs = get_view_state_for(_view);
      if (!vs || !vs->window) {
         // Embedded (host_view 非経由) モードでは _embedded_size を更新するだけ。
         _embedded_size = size_;
         return;
      }
      SDL_SetWindowSize(vs->window,
         int(size_.x * vs->scale), int(size_.y * vs->scale));
      vs->needs_refresh = true;
   }

   // host() is defined inline in base_view.hpp

   ////////////////////////////////////////////////////////////////////////////
   // Free functions
   ////////////////////////////////////////////////////////////////////////////
   std::string clipboard()
   {
      auto* text = SDL_GetClipboardText();
      if (!text) return {};
      std::string result(text);
      // SDL3: SDL_GetClipboardText returns SDL-managed string, no free needed
      return result;
   }

   void clipboard(std::string const& text)
   {
      SDL_SetClipboardText(text.c_str());
   }

   void set_cursor(cursor_type type)
   {
      SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
      switch (type)
      {
         case cursor_type::arrow:        id = SDL_SYSTEM_CURSOR_DEFAULT; break;
         case cursor_type::ibeam:        id = SDL_SYSTEM_CURSOR_TEXT; break;
         case cursor_type::cross_hair:   id = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
         case cursor_type::hand:         id = SDL_SYSTEM_CURSOR_POINTER; break;
         case cursor_type::h_resize:     id = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
         case cursor_type::v_resize:     id = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
         default: break;
      }
      if (current_cursor)
         SDL_DestroyCursor(current_cursor);
      current_cursor = SDL_CreateSystemCursor(id);
      SDL_SetCursor(current_cursor);
   }

   point scroll_direction()
   {
      return {1.0f, 1.0f};
   }
}

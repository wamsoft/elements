// AccessKit + SDL3 minimal spike (no Elements).
//
// What it checks, per OS:
//   - the adapter is attached AFTER the window is shown (like every real
//     host: Elements' SDL host, game engines),
//   - bounds in window pixels (HiDPI: pixel density applied),
//   - focus following Tab, actions (Focus / Click) coming back,
//   - a live region announcement,
//   - Unix: every handler runs on another thread, so the tree is built
//     from a mutex-protected copy of the state.
//
// Keys: Tab = next button, Space = press focused button, Esc = quit.
#include <SDL3/SDL.h>
#include <accesskit.h>

#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#endif

#include <cstdio>
#include <mutex>
#include <string>

#if defined(__linux__)
#define A11Y_UNIX 1
#endif

namespace
{
   constexpr accesskit_node_id root_id = 1;
   constexpr accesskit_node_id button_ids[] = {10, 11};
   constexpr accesskit_node_id announce_id = 2;
   char const* const button_labels[] = {"First button", "Second button"};

   // logical (window points) layout
   constexpr SDL_FRect button_rects[] = {{40, 60, 300, 80}, {40, 180, 300, 80}};

   struct state
   {
      std::mutex  mtx;
      int         focus = 0;          // 0 / 1
      int         pressed[2] = {0, 0};
      std::string announcement;
      float       density = 1.0f;     // logical -> pixel
   };

   state g;
   Uint32 g_user_event = 0;

   accesskit_node* build_button(int i, float d)
   {
      auto* n = accesskit_node_new(ACCESSKIT_ROLE_BUTTON);
      std::string label = std::string(button_labels[i]);
      accesskit_node_set_label(n, label.c_str());
      auto const& r = button_rects[i];
      accesskit_node_set_bounds(n, accesskit_rect{
         r.x * d, r.y * d, (r.x + r.w) * d, (r.y + r.h) * d});
      accesskit_node_add_action(n, ACCESSKIT_ACTION_FOCUS);
      accesskit_node_add_action(n, ACCESSKIT_ACTION_CLICK);
      return n;
   }

   // caller holds g.mtx
   accesskit_tree_update* build_full_locked(bool with_info)
   {
      auto* u = accesskit_tree_update_with_capacity_and_focus(4, button_ids[g.focus]);
      if (with_info)
      {
         auto* info = accesskit_tree_info_new(root_id);
         accesskit_tree_info_set_toolkit_name(info, "Elements spike");
         accesskit_tree_update_set_tree_info(u, info);
      }
      auto* root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
      accesskit_node_set_label(root, "A11y SDL Minimal");
      accesskit_node_push_child(root, button_ids[0]);
      accesskit_node_push_child(root, button_ids[1]);
      if (!g.announcement.empty())
         accesskit_node_push_child(root, announce_id);
      accesskit_tree_update_push_node(u, root_id, root);
      for (int i = 0; i < 2; ++i)
         accesskit_tree_update_push_node(u, button_ids[i], build_button(i, g.density));
      if (!g.announcement.empty())
      {
         auto* a = accesskit_node_new(ACCESSKIT_ROLE_LABEL);
         accesskit_node_set_value(a, g.announcement.c_str());
         accesskit_node_set_live(a, ACCESSKIT_LIVE_POLITE);
         accesskit_tree_update_push_node(u, announce_id, a);
      }
      return u;
   }

   accesskit_tree_update* activation(void*)
   {
      std::lock_guard lock(g.mtx);
      std::printf("[a11y] activated\n");
      std::fflush(stdout);
      return build_full_locked(true);
   }

   accesskit_tree_update* update_factory(void*)
   {
      std::lock_guard lock(g.mtx);
      return build_full_locked(false);
   }

   void deactivation(void*)
   {
      std::printf("[a11y] deactivated\n");
      std::fflush(stdout);
   }

   void action(accesskit_action_request* req, void*)
   {
      // Any thread. SDL_PushEvent is thread-safe.
      SDL_Event e{};
      e.type = g_user_event;
      e.user.code = int(req->action);
      e.user.data1 = reinterpret_cast<void*>(uintptr_t(req->target_node));
      accesskit_action_request_free(req);
      SDL_PushEvent(&e);
   }

   // ---- per-platform adapter ------------------------------------------------
   struct adapter
   {
#if defined(_WIN32)
      HWND hwnd = nullptr;
      accesskit_windows_adapter* a = nullptr;

      static LRESULT CALLBACK subclass(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
      {
         auto* self = reinterpret_cast<adapter*>(ref);
         if (m == WM_GETOBJECT)
         {
            auto r = accesskit_windows_adapter_handle_wm_getobject(self->a, wp, lp, activation, nullptr);
            if (r.has_value)
               return r.value;
         }
         else if (m == WM_SETFOCUS || m == WM_KILLFOCUS)
         {
            if (auto* ev = accesskit_windows_adapter_update_window_focus_state(self->a, m == WM_SETFOCUS))
               accesskit_windows_queued_events_raise(ev);
         }
         return DefSubclassProc(h, m, wp, lp);
      }

      void attach(SDL_Window* w)
      {
         hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
         a = accesskit_windows_adapter_new(hwnd, GetForegroundWindow() == hwnd, action, nullptr);
         SetWindowSubclass(hwnd, subclass, 1, reinterpret_cast<DWORD_PTR>(this));
      }
      void update()
      {
         if (auto* ev = accesskit_windows_adapter_update_if_active(a, update_factory, nullptr))
            accesskit_windows_queued_events_raise(ev);
      }
      void focus(bool) {}
      void bounds(SDL_Window*) {}
      void detach()
      {
         RemoveWindowSubclass(hwnd, subclass, 1);
         accesskit_windows_adapter_free(a);
      }
#elif defined(__APPLE__)
      accesskit_macos_subclassing_adapter* a = nullptr;

      void attach(SDL_Window* w)
      {
         void* nswin = SDL_GetPointerProperty(
            SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
         // SDL3's NSWindow subclass; keyboard focus sits on the window, so
         // forward accessibilityFocusedUIElement to the content view.
         accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window");
         a = accesskit_macos_subclassing_adapter_for_window(nswin, activation, nullptr, action, nullptr);
      }
      void update()
      {
         if (auto* ev = accesskit_macos_subclassing_adapter_update_if_active(a, update_factory, nullptr))
            accesskit_macos_queued_events_raise(ev);
      }
      void focus(bool f)
      {
         if (auto* ev = accesskit_macos_subclassing_adapter_update_view_focus_state(a, f))
            accesskit_macos_queued_events_raise(ev);
      }
      void bounds(SDL_Window*) {}
      void detach() { accesskit_macos_subclassing_adapter_free(a); }
#elif defined(A11Y_UNIX)
      accesskit_unix_adapter* a = nullptr;

      void attach(SDL_Window*)
      {
         a = accesskit_unix_adapter_new(activation, nullptr, action, nullptr, deactivation, nullptr);
      }
      void update() { accesskit_unix_adapter_update_if_active(a, update_factory, nullptr); }
      void focus(bool f) { accesskit_unix_adapter_update_window_focus_state(a, f); }
      void bounds(SDL_Window* w)
      {
         int x, y, ww, hh, t = 0, l = 0, b = 0, r = 0;
         SDL_GetWindowPosition(w, &x, &y);
         SDL_GetWindowSize(w, &ww, &hh);
         SDL_GetWindowBordersSize(w, &t, &l, &b, &r);
         accesskit_unix_adapter_set_root_window_bounds(a,
            accesskit_rect{double(x - l), double(y - t), double(x + ww + r), double(y + hh + b)},
            accesskit_rect{double(x), double(y), double(x + ww), double(y + hh)});
      }
      void detach() { accesskit_unix_adapter_free(a); }
#endif
   };

   void draw(SDL_Renderer* ren)
   {
      SDL_SetRenderDrawColor(ren, 35, 35, 37, 255);
      SDL_RenderClear(ren);
      std::lock_guard lock(g.mtx);
      for (int i = 0; i < 2; ++i)
      {
         if (g.pressed[i] & 1)
            SDL_SetRenderDrawColor(ren, 60, 120, 200, 255);
         else
            SDL_SetRenderDrawColor(ren, 80, 80, 85, 255);
         SDL_RenderFillRect(ren, &button_rects[i]);
         if (g.focus == i)
         {
            SDL_SetRenderDrawColor(ren, 255, 200, 0, 255);
            SDL_RenderRect(ren, &button_rects[i]);
         }
      }
      SDL_RenderPresent(ren);
   }
}

int main(int, char**)
{
   if (!SDL_Init(SDL_INIT_VIDEO))
   {
      std::printf("SDL_Init: %s\n", SDL_GetError());
      return 1;
   }
   g_user_event = SDL_RegisterEvents(1);

   SDL_Window* win = nullptr;
   SDL_Renderer* ren = nullptr;
   SDL_CreateWindowAndRenderer("A11y SDL Minimal", 380, 300,
      SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren);
   SDL_SetRenderLogicalPresentation(ren, 380, 300, SDL_LOGICAL_PRESENTATION_STRETCH);
   SDL_ShowWindow(win);
   draw(ren);

   std::printf("video driver: %s, pixel density %.2f\n",
      SDL_GetCurrentVideoDriver(), SDL_GetWindowPixelDensity(win));
   std::fflush(stdout);

   {
      std::lock_guard lock(g.mtx);
      g.density = SDL_GetWindowPixelDensity(win);
   }

   // Attach to an already visible window on purpose.
   adapter ad;
   ad.attach(win);
   ad.bounds(win);

   auto press = [&](int i)
   {
      {
         std::lock_guard lock(g.mtx);
         g.pressed[i]++;
         g.announcement = std::string(button_labels[i]) + " pressed "
            + std::to_string(g.pressed[i]) + " times";
      }
      ad.update();
   };
   auto set_focus = [&](int i)
   {
      {
         std::lock_guard lock(g.mtx);
         g.focus = i;
      }
      ad.update();
   };

   bool running = true;
   while (running)
   {
      SDL_Event e;
      if (!SDL_WaitEventTimeout(&e, 100))
         continue;
      switch (e.type)
      {
         case SDL_EVENT_QUIT:
            running = false;
            break;
         case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_ESCAPE)
               running = false;
            else if (e.key.key == SDLK_TAB)
               set_focus(1 - g.focus);
            else if (e.key.key == SDLK_SPACE)
               press(g.focus);
            break;
         case SDL_EVENT_WINDOW_FOCUS_GAINED:
         case SDL_EVENT_WINDOW_FOCUS_LOST:
            ad.focus(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
            break;
         case SDL_EVENT_WINDOW_MOVED:
         case SDL_EVENT_WINDOW_RESIZED:
         case SDL_EVENT_WINDOW_SHOWN:
            ad.bounds(win);
            break;
         case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
         {
            std::lock_guard lock(g.mtx);
            g.density = SDL_GetWindowPixelDensity(win);
         }
            ad.update();
            break;
         default:
            if (e.type == g_user_event)
            {
               auto target = accesskit_node_id(reinterpret_cast<uintptr_t>(e.user.data1));
               int i = target == button_ids[0] ? 0 : target == button_ids[1] ? 1 : -1;
               std::printf("[a11y] action %d on node %llu\n", e.user.code,
                  (unsigned long long)target);
               std::fflush(stdout);
               if (i >= 0)
               {
                  if (e.user.code == ACCESSKIT_ACTION_FOCUS)
                     set_focus(i);
                  else if (e.user.code == ACCESSKIT_ACTION_CLICK)
                  {
                     set_focus(i);
                     press(i);
                  }
               }
            }
            break;
      }
      draw(ren);
   }

   ad.detach();
   SDL_DestroyRenderer(ren);
   SDL_DestroyWindow(win);
   SDL_Quit();
   return 0;
}

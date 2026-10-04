/*=============================================================================
   Accessibility spike: AccessKit wired to a real Elements view (Windows).

   Phase 0 of docs/accessibility.md. The tree is hand-mapped here (no walker
   yet); what this checks is the plumbing:

     - attaching to a window that is already visible (both hosts create and
       show the window before the app gets control, which rules out
       AccessKit's subclassing adapter — it panics on a visible window),
     - bounds in view coordinates -> client pixels,
     - focus following Tab / arrow navigation,
     - AT actions (Focus / Click / Increment / Decrement / SetValue) routed
       back to the UI thread and executed as the equivalent key input,
     - a live region for announcements.

   Try it with Narrator (Win+Ctrl+Enter) or NVDA, and inspect the tree with
   Accessibility Insights for Windows.
=============================================================================*/
#include <elements.hpp>

#include <windows.h>
#include <commctrl.h>
#include <accesskit.h>

#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
#include <SDL3/SDL.h>
#endif

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

using namespace cycfi::elements;

auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

namespace
{
   constexpr accesskit_node_id root_id = 1;
   constexpr accesskit_node_id announce_id = 2;

   struct item
   {
      accesskit_node_id id;
      accesskit_role    role;
      std::string       label;
      element_ptr       e;
   };

   struct spike
   {
      view&                         v;
      HWND                          hwnd = nullptr;
      accesskit_windows_adapter*    adapter = nullptr;
      std::vector<item>             items;
      std::string                   announcement;
      std::string                   last_signature;

      explicit spike(view& v_) : v(v_) {}

      item const* find(accesskit_node_id id) const
      {
         for (auto const& i : items)
            if (i.id == id)
               return &i;
         return nullptr;
      }

      static bool is_focused(element& e)
      {
         if (auto b = dynamic_cast<basic_button*>(&e))
            return b->focused();
         if (auto s = dynamic_cast<slider_base*>(&e))
            return s->focused();
         return false;
      }

      accesskit_node_id focus_id() const
      {
         for (auto const& i : items)
            if (is_focused(*i.e))
               return i.id;
         return root_id;
      }

      // view (user) coordinates -> client pixels of the attached HWND
      float pixel_scale() const
      {
         RECT rc;
         GetClientRect(hwnd, &rc);
         auto sz = v.size();
         return sz.x > 0 ? float(rc.right - rc.left) / sz.x : 1.0f;
      }

      accesskit_node* build_item(item const& i, float scale) const
      {
         auto* n = accesskit_node_new(i.role);
         accesskit_node_set_label(n, i.label.c_str());

         rect r; extent nat;
         if (v.element_bounds(*i.e, r, nat))
         {
            accesskit_rect ar{
               r.left * scale, r.top * scale, r.right * scale, r.bottom * scale};
            accesskit_node_set_bounds(n, ar);
         }

         accesskit_node_add_action(n, ACCESSKIT_ACTION_FOCUS);
         if (auto b = dynamic_cast<basic_button*>(i.e.get()))
         {
            accesskit_node_add_action(n, ACCESSKIT_ACTION_CLICK);
            if (i.role == ACCESSKIT_ROLE_CHECK_BOX)
               accesskit_node_set_toggled(
                  n, b->value() ? ACCESSKIT_TOGGLED_TRUE : ACCESSKIT_TOGGLED_FALSE);
            if (!b->is_enabled())
               accesskit_node_set_disabled(n);
         }
         else if (auto s = dynamic_cast<slider_base*>(i.e.get()))
         {
            double pct = s->value() * 100.0;
            accesskit_node_set_numeric_value(n, pct);
            accesskit_node_set_min_numeric_value(n, 0);
            accesskit_node_set_max_numeric_value(n, 100);
            accesskit_node_set_numeric_value_step(n, 1);
            accesskit_node_set_value(n, (std::to_string(int(pct + 0.5)) + "%").c_str());
            accesskit_node_add_action(n, ACCESSKIT_ACTION_INCREMENT);
            accesskit_node_add_action(n, ACCESSKIT_ACTION_DECREMENT);
            accesskit_node_add_action(n, ACCESSKIT_ACTION_SET_VALUE);
         }
         return n;
      }

      accesskit_tree_update* build_full(bool with_tree_info) const
      {
         float scale = pixel_scale();
         auto* u = accesskit_tree_update_with_capacity_and_focus(
            items.size() + 2, focus_id());
         if (with_tree_info)
         {
            auto* info = accesskit_tree_info_new(root_id);
            accesskit_tree_info_set_toolkit_name(info, "Elements");
            accesskit_tree_update_set_tree_info(u, info);
         }

         auto* root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
         accesskit_node_set_label(root, "A11y Spike");
         for (auto const& i : items)
            accesskit_node_push_child(root, i.id);
         accesskit_node_push_child(root, announce_id);
         accesskit_tree_update_push_node(u, root_id, root);

         for (auto const& i : items)
            accesskit_tree_update_push_node(u, i.id, build_item(i, scale));

         auto* ann = accesskit_node_new(ACCESSKIT_ROLE_LABEL);
         accesskit_node_set_value(ann, announcement.c_str());
         accesskit_node_set_live(ann, ACCESSKIT_LIVE_POLITE);
         accesskit_tree_update_push_node(u, announce_id, ann);
         return u;
      }

      // Cheap change detection for the spike: anything AT-visible goes into
      // a string; when it differs, push a full update.
      std::string signature() const
      {
         std::string s = std::to_string(focus_id()) + "|" + announcement;
         float scale = pixel_scale();
         for (auto const& i : items)
         {
            rect r; extent nat;
            if (v.element_bounds(*i.e, r, nat))
               s += "|" + std::to_string(int(r.left * scale)) + ","
                  + std::to_string(int(r.top * scale)) + ","
                  + std::to_string(int(r.right * scale)) + ","
                  + std::to_string(int(r.bottom * scale));
            if (auto b = dynamic_cast<basic_button*>(i.e.get()))
               s += b->value() ? "+1" : "+0";
            else if (auto sl = dynamic_cast<slider_base*>(i.e.get()))
               s += "+" + std::to_string(int(sl->value() * 1000));
         }
         return s;
      }

      void push_if_changed()
      {
         auto sig = signature();
         if (sig == last_signature)
            return;
         last_signature = sig;
         auto* events = accesskit_windows_adapter_update_if_active(
            adapter,
            [](void* ud) -> accesskit_tree_update*
            {
               return static_cast<spike*>(ud)->build_full(false);
            },
            this);
         if (events)
            accesskit_windows_queued_events_raise(events);
      }

      void send_key(key_code k)
      {
         v.key(key_info{k, key_action::press, 0});
         v.key(key_info{k, key_action::release, 0});
      }

      // UI thread
      void perform(accesskit_action action, accesskit_node_id target,
         bool has_num, double num)
      {
         auto const* i = find(target);
         if (!i)
            return;
         auto e = i->e;
         std::printf("[a11y] action %d on %s\n", int(action), i->label.c_str());

         switch (action)
         {
            case ACCESSKIT_ACTION_FOCUS:
               v.focus(e);
               break;
            case ACCESSKIT_ACTION_CLICK:
               // focus() is deferred to the task queue; queue the key after it
               v.focus(e);
               v.post([this] { send_key(key_code::enter); });
               break;
            case ACCESSKIT_ACTION_INCREMENT:
               v.focus(e);
               v.post([this] { send_key(key_code::right); });
               break;
            case ACCESSKIT_ACTION_DECREMENT:
               v.focus(e);
               v.post([this] { send_key(key_code::left); });
               break;
            case ACCESSKIT_ACTION_SET_VALUE:
               if (has_num)
                  if (auto s = std::dynamic_pointer_cast<slider_base>(e))
                  {
                     s->value(std::clamp(num / 100.0, 0.0, 1.0));
                     v.refresh(*s);
                  }
               break;
            default:
               break;
         }
      }
   };

   spike* g_spike = nullptr;

   void action_handler(accesskit_action_request* req, void* ud)
   {
      // May be called on a non-UI thread. view::post is thread-safe.
      auto* sp = static_cast<spike*>(ud);
      auto action = req->action;
      auto target = req->target_node;
      bool has_num = req->data.has_value
         && req->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE;
      double num = has_num ? req->data.value.numeric_value : 0.0;
      accesskit_action_request_free(req);
      sp->v.post([=] { sp->perform(action, target, has_num, num); });
   }

   LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
      UINT_PTR, DWORD_PTR ref)
   {
      auto* sp = reinterpret_cast<spike*>(ref);
      switch (msg)
      {
         case WM_GETOBJECT:
         {
            auto r = accesskit_windows_adapter_handle_wm_getobject(
               sp->adapter, wp, lp,
               [](void* ud) -> accesskit_tree_update*
               {
                  std::printf("[a11y] activated (AT connected)\n");
                  auto* s = static_cast<spike*>(ud);
                  s->last_signature = s->signature();
                  return s->build_full(true);
               },
               sp);
            if (r.has_value)
               return r.value;
            break;
         }
         case WM_SETFOCUS:
         case WM_KILLFOCUS:
         {
            auto* events = accesskit_windows_adapter_update_window_focus_state(
               sp->adapter, msg == WM_SETFOCUS);
            if (events)
               accesskit_windows_queued_events_raise(events);
            break;
         }
      }
      return DefSubclassProc(hwnd, msg, wp, lp);
   }

   HWND native_hwnd(view& v)
   {
#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
      return static_cast<HWND>(SDL_GetPointerProperty(
         SDL_GetWindowProperties(v.host()), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#else
      return v.host();   // the "ElementsView" child window; it takes keyboard focus
#endif
   }
}

int main(int /*argc*/, char* /*argv*/[])
{
   app _app("A11y Spike");
   window _win(_app.name());
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);
   view_.arrow_focus_navigation(true);

   spike sp(view_);
   g_spike = &sp;

   auto hello = share(button("Say hello"));
   auto other = share(button("Another button"));
   auto check = share(check_box("Enable sound"));
   auto vol   = share(slider(basic_thumb<25>(), basic_track<5, false>(colors::black), 0.5));

   hello->on_click = [&sp](bool) { sp.announcement = "Hello!"; };
   other->on_click = [&sp](bool) { sp.announcement = "Another button pressed"; };

   sp.items = {
      {10, ACCESSKIT_ROLE_BUTTON,    "Say hello",      hello},
      {11, ACCESSKIT_ROLE_BUTTON,    "Another button", other},
      {12, ACCESSKIT_ROLE_CHECK_BOX, "Enable sound",   check},
      {13, ACCESSKIT_ROLE_SLIDER,    "Volume",         vol},
   };

   view_.content(
      margin({20, 20, 20, 20},
         vtile_spaced(15.0,
            initial_focus(hold(hello)),
            hold(other),
            align_left(hold(check)),
            hsize(300, hold(vol))
         )
      ),
      background
   );

   sp.hwnd = native_hwnd(view_);
   if (!sp.hwnd)
   {
      std::printf("[a11y] no native window handle\n");
      return 1;
   }
   sp.adapter = accesskit_windows_adapter_new(
      sp.hwnd, GetFocus() == sp.hwnd, action_handler, &sp);
   SetWindowSubclass(sp.hwnd, subclass_proc, 1, reinterpret_cast<DWORD_PTR>(&sp));

   // Change polling (spike only). The design replaces this with dirty
   // tracking in view::poll().
   std::function<void()> tick;
   tick = [&]
   {
      sp.push_if_changed();
      view_.post(std::chrono::milliseconds(50), tick);
   };
   view_.post(std::chrono::milliseconds(50), tick);

   _app.run();

   RemoveWindowSubclass(sp.hwnd, subclass_proc, 1);
   accesskit_windows_adapter_free(sp.adapter);
   g_spike = nullptr;
   return 0;
}

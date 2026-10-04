/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/a11y/accesskit_host.hpp>
#include <elements/view.hpp>

#include <accesskit.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
# include <windows.h>
# include <commctrl.h>
#endif

#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
# include <SDL3/SDL.h>
#endif

#if defined(__APPLE__)
# include <objc/message.h>
# include <objc/runtime.h>
#endif

#if (defined(__linux__) || defined(__DragonFly__) || defined(__FreeBSD__) \
   || defined(__NetBSD__) || defined(__OpenBSD__)) && !defined(__ANDROID__)
# define ELEMENTS_A11Y_UNIX 1
#endif

namespace cycfi::elements::a11y
{
   namespace
   {
      constexpr accesskit_node_id root_gid = 1;
      constexpr std::uint64_t local_mask = (std::uint64_t(1) << 56) - 1;

      // Slot-qualified id: the top byte carries the slot.
      accesskit_node_id gid(int slot, node_id local)
      {
         return (std::uint64_t(slot + 1) << 56) | (local & local_mask);
      }

      accesskit_role to_ak(role r)
      {
         switch (r)
         {
            case role::none:
            case role::generic:              return ACCESSKIT_ROLE_GROUP;
            case role::window:               return ACCESSKIT_ROLE_WINDOW;
            case role::dialog:               return ACCESSKIT_ROLE_DIALOG;
            case role::label:                return ACCESSKIT_ROLE_LABEL;
            case role::heading:              return ACCESSKIT_ROLE_HEADING;
            case role::image:                return ACCESSKIT_ROLE_IMAGE;
            case role::button:
            case role::toggle_button:        return ACCESSKIT_ROLE_BUTTON;
            case role::check_box:            return ACCESSKIT_ROLE_CHECK_BOX;
            case role::radio_button:         return ACCESSKIT_ROLE_RADIO_BUTTON;
            case role::tab:                  return ACCESSKIT_ROLE_TAB;
            case role::slider:               return ACCESSKIT_ROLE_SLIDER;
            case role::spin_button:          return ACCESSKIT_ROLE_SPIN_BUTTON;
            case role::menu_item:            return ACCESSKIT_ROLE_MENU_ITEM;
            case role::text_input:           return ACCESSKIT_ROLE_TEXT_INPUT;
            case role::multiline_text_input: return ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT;
            case role::progress_indicator:   return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
            case role::status:               return ACCESSKIT_ROLE_STATUS;
            case role::list:                 return ACCESSKIT_ROLE_LIST;
            case role::list_item:            return ACCESSKIT_ROLE_LIST_ITEM;
         }
         return ACCESSKIT_ROLE_GROUP;
      }

      // Static text: AccessKit takes its text as the value. A heading is
      // named like any other node.
      bool is_text_role(role r)
      {
         return r == role::label || r == role::status;
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   struct accesskit_host::impl : std::enable_shared_from_this<accesskit_host::impl>
   {
      struct slot_data
      {
         int                  z = 0;
         bool                 modal = false;
         a11y::transform      xf;
         perform_function     perform;
         snapshot_function    snapshot_now;
         transform_function   get_transform;
         snapshot             snap;
         bool                 has_snap = false;
      };

      std::mutex              mtx;
      std::map<int, slot_data> slots;
      std::string             window_label;
      std::atomic<bool>       active{false};
      std::function<void(bool)> active_changed;
      bool                    need_full = true;
      std::vector<std::pair<int, node>> pending;
      std::unordered_map<accesskit_node_id, std::pair<int, node_id>> reverse;
      bool                    factory_called = false;
      bool                    ui_thread_activation = true;
      std::function<void()>   cleanup;

#if defined(_WIN32)
      HWND                          hwnd = nullptr;
      accesskit_windows_adapter*    adapter = nullptr;
#elif defined(__APPLE__)
      accesskit_macos_subclassing_adapter* adapter = nullptr;
      void*                         nswindow = nullptr;
#elif defined(ELEMENTS_A11Y_UNIX)
      accesskit_unix_adapter*       adapter = nullptr;
#endif

      // ---- composition (callers hold mtx) --------------------------------

      std::vector<std::pair<int, slot_data*>> visible_slots()
      {
         std::vector<std::pair<int, slot_data*>> v;
         for (auto& [k, s] : slots)
            v.emplace_back(k, &s);
         std::stable_sort(v.begin(), v.end(),
            [](auto const& a, auto const& b) { return a.second->z < b.second->z; });
         // A modal slot hides everything below it.
         for (std::size_t i = v.size(); i-- > 0;)
         {
            if (v[i].second->modal)
            {
               v.erase(v.begin(), v.begin() + std::ptrdiff_t(i));
               break;
            }
         }
         return v;
      }

      accesskit_node_id focus_gid(std::vector<std::pair<int, slot_data*>> const& vis)
      {
         for (std::size_t i = vis.size(); i-- > 0;)
         {
            auto const& s = *vis[i].second;
            if (s.has_snap && s.snap.focus && s.snap.focus != s.snap.root)
               return gid(vis[i].first, s.snap.focus);
         }
         return root_gid;
      }

      accesskit_node* convert(node const& n, int slot, a11y::transform const& xf)
      {
         auto* a = accesskit_node_new(to_ak(n.role));
         if (!n.name.empty())
         {
            if (is_text_role(n.role))
               accesskit_node_set_value(a, n.name.c_str());
            else
               accesskit_node_set_label(a, n.name.c_str());
         }
         if (!n.description.empty())
            accesskit_node_set_description(a, n.description.c_str());
         // An edit field always has a value, even empty (UIA offers the Value
         // pattern only when there is one).
         bool edit = n.role == role::text_input || n.role == role::multiline_text_input;
         if ((!n.value.empty() || edit) && !is_text_role(n.role))
            accesskit_node_set_value(a, n.value.c_str());
         if (n.num_value)
            accesskit_node_set_numeric_value(a, *n.num_value);
         if (n.num_min)
            accesskit_node_set_min_numeric_value(a, *n.num_min);
         if (n.num_max)
            accesskit_node_set_max_numeric_value(a, *n.num_max);
         if (n.num_step)
            accesskit_node_set_numeric_value_step(a, *n.num_step);

         switch (n.role)
         {
            case role::check_box:
            case role::radio_button:
            case role::toggle_button:
               accesskit_node_set_toggled(a, n.has(state::checked)
                  ? ACCESSKIT_TOGGLED_TRUE : ACCESSKIT_TOGGLED_FALSE);
               break;
            case role::tab:
            case role::menu_item:
            case role::list_item:
               accesskit_node_set_selected(a, n.has(state::selected));
               break;
            default:
               break;
         }
         if (n.has(state::disabled))
            accesskit_node_set_disabled(a);
         if (n.has(state::read_only))
            accesskit_node_set_read_only(a);
         if (n.has(state::modal))
            accesskit_node_set_modal(a);
         if (n.live == live::polite)
            accesskit_node_set_live(a, ACCESSKIT_LIVE_POLITE);
         else if (n.live == live::assertive)
            accesskit_node_set_live(a, ACCESSKIT_LIVE_ASSERTIVE);

         if (n.actions & bit(action::focus))
            accesskit_node_add_action(a, ACCESSKIT_ACTION_FOCUS);
         if (n.actions & bit(action::click))
            accesskit_node_add_action(a, ACCESSKIT_ACTION_CLICK);
         if (n.actions & bit(action::increment))
            accesskit_node_add_action(a, ACCESSKIT_ACTION_INCREMENT);
         if (n.actions & bit(action::decrement))
            accesskit_node_add_action(a, ACCESSKIT_ACTION_DECREMENT);
         if (n.actions & bit(action::set_value))
            accesskit_node_add_action(a, ACCESSKIT_ACTION_SET_VALUE);

         if (!n.bounds.is_empty())
         {
            accesskit_node_set_bounds(a, accesskit_rect{
               n.bounds.left * xf.sx + xf.tx, n.bounds.top * xf.sy + xf.ty,
               n.bounds.right * xf.sx + xf.tx, n.bounds.bottom * xf.sy + xf.ty});
         }
         for (auto c : n.children)
            accesskit_node_push_child(a, gid(slot, c));

         reverse[gid(slot, n.id)] = {slot, n.id};
         return a;
      }

      accesskit_node* root_node(std::vector<std::pair<int, slot_data*>> const& vis)
      {
         auto* r = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
         std::string label = window_label;
         for (auto const& [k, s] : vis)
         {
            if (!s->has_snap || s->snap.nodes.empty())
               continue;
            auto const& sroot = s->snap.nodes[0];
            if (label.empty())
               label = sroot.name;
            for (auto c : sroot.children)
               accesskit_node_push_child(r, gid(k, c));
         }
         if (!label.empty())
            accesskit_node_set_label(r, label.c_str());
         return r;
      }

      accesskit_tree_update* build_full()
      {
         auto vis = visible_slots();
         std::size_t count = 1;
         for (auto const& [k, s] : vis)
            count += s->has_snap ? s->snap.nodes.size() : 0;

         reverse.clear();
         auto* u = accesskit_tree_update_with_capacity_and_focus(count, focus_gid(vis));
         auto* info = accesskit_tree_info_new(root_gid);
         accesskit_tree_info_set_toolkit_name(info, "Elements");
         accesskit_tree_update_set_tree_info(u, info);
         accesskit_tree_update_push_node(u, root_gid, root_node(vis));
         for (auto const& [k, s] : vis)
         {
            if (!s->has_snap)
               continue;
            for (std::size_t i = 1; i < s->snap.nodes.size(); ++i)
            {
               auto const& n = s->snap.nodes[i];
               accesskit_tree_update_push_node(u, gid(k, n.id), convert(n, k, s->xf));
            }
         }
         need_full = false;
         pending.clear();
         return u;
      }

      accesskit_tree_update* build_delta()
      {
         if (need_full)
            return build_full();
         auto vis = visible_slots();
         auto* u = accesskit_tree_update_with_capacity_and_focus(
            pending.size() + 1, focus_gid(vis));
         accesskit_tree_update_push_node(u, root_gid, root_node(vis));
         for (auto const& [k, n] : pending)
         {
            auto it = slots.find(k);
            if (it == slots.end() || !it->second.has_snap || n.id == it->second.snap.root)
               continue;
            bool shown = std::any_of(vis.begin(), vis.end(),
               [k = k](auto const& p) { return p.first == k; });
            if (!shown)
               continue;
            accesskit_tree_update_push_node(u, gid(k, n.id), convert(n, k, it->second.xf));
         }
         pending.clear();
         return u;
      }

      // ---- AccessKit callbacks -------------------------------------------

      static accesskit_tree_update* on_activate(void* ud)
      {
         auto* self = static_cast<impl*>(ud);
         const bool was = self->active.exchange(true);
         if (!was && self->active_changed)
            self->active_changed(true);

         // On Windows and macOS this runs on the UI thread: ask the sources
         // for their tree right now, so the first answer is complete.
         if (self->ui_thread_activation)
         {
            std::vector<std::pair<int, snapshot_function>> ask;
            {
               std::lock_guard lock(self->mtx);
               for (auto& [k, s] : self->slots)
                  if (s.snapshot_now)
                     ask.emplace_back(k, s.snapshot_now);
            }
            std::vector<std::pair<int, snapshot>> got;
            for (auto& [k, f] : ask)
               got.emplace_back(k, f());
            std::lock_guard lock(self->mtx);
            for (auto& [k, snap] : got)
            {
               auto it = self->slots.find(k);
               if (it != self->slots.end())
               {
                  if (it->second.get_transform)
                     it->second.xf = it->second.get_transform();
                  it->second.snap = std::move(snap);
                  it->second.has_snap = true;
               }
            }
         }
         std::lock_guard lock(self->mtx);
         return self->build_full();
      }

      static accesskit_tree_update* on_update(void* ud)
      {
         auto* self = static_cast<impl*>(ud);
         std::lock_guard lock(self->mtx);
         self->factory_called = true;
         return self->build_delta();
      }

      static void on_deactivate(void* ud)
      {
         auto* self = static_cast<impl*>(ud);
         const bool was = self->active.exchange(false);
         if (was && self->active_changed)
            self->active_changed(false);
      }

      static void on_action(accesskit_action_request* req, void* ud)
      {
         auto* self = static_cast<impl*>(ud);
         std::optional<action> act;
         switch (req->action)
         {
            case ACCESSKIT_ACTION_FOCUS:      act = action::focus; break;
            case ACCESSKIT_ACTION_CLICK:      act = action::click; break;
            case ACCESSKIT_ACTION_INCREMENT:  act = action::increment; break;
            case ACCESSKIT_ACTION_DECREMENT:  act = action::decrement; break;
            case ACCESSKIT_ACTION_SET_VALUE:  act = action::set_value; break;
            default: break;
         }
         action_arg arg;
         if (req->data.has_value)
         {
            if (req->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE)
               arg.number = req->data.value.numeric_value;
            else if (req->data.value.tag == ACCESSKIT_ACTION_DATA_VALUE && req->data.value.value)
               arg.text = std::string(req->data.value.value);
         }
         auto target = req->target_node;
         accesskit_action_request_free(req);
         if (!act)
            return;
#if defined(_WIN32)
         // UIA calls in on its own threads. Hand the action to the window's
         // thread, so perform functions run where the UI lives and the
         // message also wakes a host that sleeps in its message loop.
         if (GetWindowThreadProcessId(self->hwnd, nullptr) != GetCurrentThreadId())
         {
            {
               std::lock_guard lock(self->mtx);
               self->posted.push_back({target, *act, std::move(arg)});
            }
            PostMessageW(self->hwnd, action_message(), 0, 0);
            return;
         }
#endif
         self->dispatch(target, *act, std::move(arg));
      }

      void dispatch(accesskit_node_id target, action act, action_arg arg)
      {
         perform_function perform;
         node_id local = 0;
         {
            std::lock_guard lock(mtx);
            auto r = reverse.find(target);
            if (r == reverse.end())
               return;
            auto it = slots.find(r->second.first);
            if (it == slots.end())
               return;
            perform = it->second.perform;
            local = r->second.second;
         }
         if (perform)
            perform(local, act, std::move(arg));
      }

      // ---- platform ------------------------------------------------------

#if defined(_WIN32)
      struct posted_action
      {
         accesskit_node_id target;
         action            act;
         action_arg        arg;
      };
      std::vector<posted_action> posted;

      static UINT action_message()
      {
         static UINT const msg = RegisterWindowMessageW(L"cycfi.elements.a11y.action");
         return msg;
      }

      static LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
         UINT_PTR id, DWORD_PTR ref)
      {
         auto* self = reinterpret_cast<impl*>(ref);
         if (msg == action_message())
         {
            std::vector<posted_action> q;
            {
               std::lock_guard lock(self->mtx);
               q.swap(self->posted);
            }
            for (auto& a : q)
               self->dispatch(a.target, a.act, std::move(a.arg));
            return 0;
         }
         switch (msg)
         {
            case WM_GETOBJECT:
            {
               auto r = accesskit_windows_adapter_handle_wm_getobject(
                  self->adapter, wp, lp, &impl::on_activate, self);
               if (r.has_value)
                  return r.value;
               break;
            }
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
               if (auto* ev = accesskit_windows_adapter_update_window_focus_state(
                     self->adapter, msg == WM_SETFOCUS))
                  accesskit_windows_queued_events_raise(ev);
               break;
            case WM_NCDESTROY:
               RemoveWindowSubclass(hwnd, &impl::subclass_proc, id);
               break;
         }
         return DefSubclassProc(hwnd, msg, wp, lp);
      }
#endif

      void attach(void* native_window)
      {
#if defined(_WIN32)
         // Not the subclassing adapter: it panics on a window that is
         // already visible, and hosts attach after showing the window.
         hwnd = static_cast<HWND>(native_window);
         adapter = accesskit_windows_adapter_new(hwnd, GetFocus() == hwnd,
            &impl::on_action, this);
         SetWindowSubclass(hwnd, &impl::subclass_proc, 0xE1A11, reinterpret_cast<DWORD_PTR>(this));
#elif defined(__APPLE__)
         nswindow = native_window;
         adapter = accesskit_macos_subclassing_adapter_for_window(native_window,
            &impl::on_activate, this, &impl::on_action, this);
#elif defined(ELEMENTS_A11Y_UNIX)
         (void)native_window;
         // Every handler runs on another thread here; the tree is answered
         // from the last snapshots the sources pushed.
         ui_thread_activation = false;
         adapter = accesskit_unix_adapter_new(&impl::on_activate, this,
            &impl::on_action, this, &impl::on_deactivate, this);
#else
         (void)native_window;
#endif
      }

      void detach()
      {
         if (cleanup)
            cleanup();
#if defined(_WIN32)
         if (adapter)
         {
            RemoveWindowSubclass(hwnd, &impl::subclass_proc, 0xE1A11);
            accesskit_windows_adapter_free(adapter);
         }
#elif defined(__APPLE__)
         if (adapter)
            accesskit_macos_subclassing_adapter_free(adapter);
#elif defined(ELEMENTS_A11Y_UNIX)
         if (adapter)
            accesskit_unix_adapter_free(adapter);
#endif
         adapter = nullptr;
      }

      void push()
      {
         if (!active)
            return;
         factory_called = false;
#if defined(_WIN32)
         if (adapter)
            if (auto* ev = accesskit_windows_adapter_update_if_active(adapter, &impl::on_update, this))
               accesskit_windows_queued_events_raise(ev);
#elif defined(__APPLE__)
         if (adapter)
            if (auto* ev = accesskit_macos_subclassing_adapter_update_if_active(adapter, &impl::on_update, this))
               accesskit_macos_queued_events_raise(ev);
#elif defined(ELEMENTS_A11Y_UNIX)
         if (adapter)
            accesskit_unix_adapter_update_if_active(adapter, &impl::on_update, this);
#endif
         if (!factory_called)
         {
            // Not listening after all: start over with a full tree.
            std::lock_guard lock(mtx);
            pending.clear();
            need_full = true;
         }
      }
   };

   ////////////////////////////////////////////////////////////////////////////
   namespace
   {
      class slot_sink : public sink
      {
      public:

         slot_sink(std::weak_ptr<accesskit_host::impl> host, int slot)
          : _host(std::move(host))
          , _slot(slot)
         {}

         bool is_active() const override
         {
            auto h = _host.lock();
            return h && h->active;
         }

         void tree_changed(snapshot const& full, update const& delta) override
         {
            auto h = _host.lock();
            if (!h)
               return;
            accesskit_host::transform_function get_xf;
            {
               std::lock_guard lock(h->mtx);
               auto it = h->slots.find(_slot);
               if (it == h->slots.end())
                  return;
               get_xf = it->second.get_transform;
            }
            std::optional<a11y::transform> xf;
            if (get_xf)
               xf = get_xf();
            {
               std::lock_guard lock(h->mtx);
               auto it = h->slots.find(_slot);
               if (it == h->slots.end())
                  return;
               auto& s = it->second;
               if (xf && (xf->sx != s.xf.sx || xf->sy != s.xf.sy
                  || xf->tx != s.xf.tx || xf->ty != s.xf.ty))
               {
                  s.xf = *xf;
                  h->need_full = true;    // every bounds moves
               }
               s.snap = full;
               s.has_snap = true;
               if (!h->active)
               {
                  // Nobody listens yet (the source pushes anyway, e.g. for a
                  // speech log): keep the latest tree only; activation sends
                  // it in full.
                  h->pending.clear();
                  h->need_full = true;
                  return;
               }
               if (delta.full)
                  h->need_full = true;
               else if (!h->need_full)
                  for (auto const& n : delta.nodes)
                     h->pending.emplace_back(_slot, n);
            }
            h->push();
         }

      private:

         std::weak_ptr<accesskit_host::impl> _host;
         int                                 _slot;
      };
   }

   ////////////////////////////////////////////////////////////////////////////
   accesskit_host::accesskit_host()
    : _impl(std::make_shared<impl>())
   {}

   accesskit_host::~accesskit_host()
   {
      _impl->detach();
   }

   std::unique_ptr<accesskit_host> accesskit_host::attach(void* native_window)
   {
      std::unique_ptr<accesskit_host> h{new accesskit_host()};
      h->_impl->attach(native_window);
      return h;
   }

   std::shared_ptr<sink> accesskit_host::add_source(int slot, perform_function perform,
      snapshot_function snapshot_now, transform_function get_transform)
   {
      {
         std::lock_guard lock(_impl->mtx);
         auto& s = _impl->slots[slot];
         s.perform = std::move(perform);
         s.snapshot_now = std::move(snapshot_now);
         s.get_transform = std::move(get_transform);
         if (s.get_transform)
            s.xf = s.get_transform();
         _impl->need_full = true;
      }
      return std::make_shared<slot_sink>(_impl, slot);
   }

   void accesskit_host::remove_source(int slot)
   {
      {
         std::lock_guard lock(_impl->mtx);
         _impl->slots.erase(slot);
         _impl->need_full = true;
      }
      flush();
   }

   void accesskit_host::set_transform(int slot, a11y::transform xf)
   {
      std::lock_guard lock(_impl->mtx);
      auto it = _impl->slots.find(slot);
      if (it != _impl->slots.end())
      {
         it->second.xf = xf;
         _impl->need_full = true;
      }
   }

   void accesskit_host::set_z(int slot, int z)
   {
      std::lock_guard lock(_impl->mtx);
      auto it = _impl->slots.find(slot);
      if (it != _impl->slots.end() && it->second.z != z)
      {
         it->second.z = z;
         _impl->need_full = true;
      }
   }

   void accesskit_host::set_modal(int slot, bool modal)
   {
      std::lock_guard lock(_impl->mtx);
      auto it = _impl->slots.find(slot);
      if (it != _impl->slots.end() && it->second.modal != modal)
      {
         it->second.modal = modal;
         _impl->need_full = true;
      }
   }

   void accesskit_host::set_window_label(std::string label)
   {
      std::lock_guard lock(_impl->mtx);
      _impl->window_label = std::move(label);
   }

   void accesskit_host::window_focus(bool focused)
   {
#if defined(__APPLE__)
      if (_impl->adapter)
         if (auto* ev = accesskit_macos_subclassing_adapter_update_view_focus_state(_impl->adapter, focused))
            accesskit_macos_queued_events_raise(ev);
#elif defined(ELEMENTS_A11Y_UNIX)
      if (_impl->adapter)
         accesskit_unix_adapter_update_window_focus_state(_impl->adapter, focused);
#else
      (void)focused;    // Windows: the subclass sees WM_SETFOCUS itself
#endif
   }

   void accesskit_host::window_bounds(rect outer, rect inner)
   {
#if defined(ELEMENTS_A11Y_UNIX)
      if (_impl->adapter)
         accesskit_unix_adapter_set_root_window_bounds(_impl->adapter,
            accesskit_rect{outer.left, outer.top, outer.right, outer.bottom},
            accesskit_rect{inner.left, inner.top, inner.right, inner.bottom});
#else
      (void)outer;
      (void)inner;
#endif
   }

   bool accesskit_host::is_active() const
   {
      return _impl->active;
   }

   void accesskit_host::on_active_changed(std::function<void(bool)> f)
   {
      _impl->active_changed = std::move(f);
   }

   float accesskit_host::native_scale() const
   {
#if defined(__APPLE__)
      if (_impl->nswindow)
      {
         // [NSWindow backingScaleFactor]
         using fn = double (*)(void*, SEL);
         return float(reinterpret_cast<fn>(objc_msgSend)(
            _impl->nswindow, sel_registerName("backingScaleFactor")));
      }
#endif
      return 1.0f;
   }

   void accesskit_host::flush()
   {
      if (!_impl->active)
         return;
      _impl->push();
   }

   ////////////////////////////////////////////////////////////////////////////
   // Native Elements windows
   ////////////////////////////////////////////////////////////////////////////
#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
   namespace
   {
      struct sdl_watch
      {
         accesskit_host*   host;
         SDL_WindowID      id;
         SDL_Window*       window;

         void update_bounds() const
         {
            int x, y, w, h, t = 0, l = 0, b = 0, r = 0;
            SDL_GetWindowPosition(window, &x, &y);
            SDL_GetWindowSize(window, &w, &h);
            SDL_GetWindowBordersSize(window, &t, &l, &b, &r);
            host->window_bounds(
               rect{float(x - l), float(y - t), float(x + w + r), float(y + h + b)},
               rect{float(x), float(y), float(x + w), float(y + h)});
         }

         static bool on_event(void* ud, SDL_Event* e)
         {
            auto* self = static_cast<sdl_watch*>(ud);
            if (e->type < SDL_EVENT_WINDOW_FIRST || e->type > SDL_EVENT_WINDOW_LAST
               || e->window.windowID != self->id)
               return true;
            switch (e->type)
            {
               case SDL_EVENT_WINDOW_FOCUS_GAINED: self->host->window_focus(true); break;
               case SDL_EVENT_WINDOW_FOCUS_LOST:   self->host->window_focus(false); break;
               case SDL_EVENT_WINDOW_SHOWN:
               case SDL_EVENT_WINDOW_MOVED:
               case SDL_EVENT_WINDOW_RESIZED:      self->update_bounds(); break;
               default: break;
            }
            return true;
         }
      };
   }
#endif

#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
   std::unique_ptr<accesskit_host> accesskit_host::attach_sdl(SDL_Window* win)
   {
      auto props = SDL_GetWindowProperties(win);
      void* native = nullptr;
# if defined(_WIN32)
      native = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
# elif defined(__APPLE__)
      // SDL keeps the keyboard focus on its NSWindow subclass; forward the
      // focused-element query to the content view the adapter lives on.
      // The class is patched for the life of the process, and patching it
      // twice panics (attach_sdl again after the host was dropped).
      static bool const forwarder = [] {
         accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window");
         return true;
      }();
      (void)forwarder;
      native = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
# endif
      (void)props;
      auto h = attach(native);

      auto watch = std::make_shared<sdl_watch>(sdl_watch{h.get(), SDL_GetWindowID(win), win});
      SDL_AddEventWatch(&sdl_watch::on_event, watch.get());
      watch->update_bounds();
      h->_impl->cleanup = [watch]() { SDL_RemoveEventWatch(&sdl_watch::on_event, watch.get()); };
      return h;
   }
#endif

   std::unique_ptr<accesskit_host> attach_accesskit(view& v)
   {
      std::unique_ptr<accesskit_host> h;
      accesskit_host::transform_function get_xf;

#if defined(ELEMENTS_HOST_UI_LIBRARY_WIN32)
      // The view's own child window takes the keyboard focus.
      HWND hwnd = v.host();
      h = accesskit_host::attach(hwnd);
      get_xf = [&v, hwnd]()
      {
         RECT rc;
         GetClientRect(hwnd, &rc);
         auto sz = v.size();
         float f = sz.x > 0 ? float(rc.right - rc.left) / sz.x : 1.0f;
         return a11y::transform{f, f, 0, 0};
      };
      {
         wchar_t title[256] = {};
         GetWindowTextW(GetAncestor(hwnd, GA_ROOT), title, 256);
         char utf8[768] = {};
         WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof utf8, nullptr, nullptr);
         if (v.a11y_name().empty())
            h->set_window_label(utf8);
      }
#elif defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
      SDL_Window* win = v.host();
      h = accesskit_host::attach_sdl(win);
      get_xf = [&v, win]()
      {
         int w = 0, hh = 0;
# if defined(ELEMENTS_A11Y_UNIX)
         SDL_GetWindowSize(win, &w, &hh);            // AT-SPI: window coordinates
# else
         SDL_GetWindowSizeInPixels(win, &w, &hh);    // UIA / AppKit: pixels
# endif
         auto sz = v.size();
         float f = sz.x > 0 ? float(w) / sz.x : 1.0f;
         return a11y::transform{f, f, 0, 0};
      };
      if (v.a11y_name().empty())
         if (auto* t = SDL_GetWindowTitle(win))
            h->set_window_label(t);
#endif

      if (!h)
         return h;

      auto s = h->add_source(0,
         [&v](node_id id, action act, action_arg arg) { v.a11y_perform(id, act, std::move(arg)); },
         [&v]() { return v.a11y_snapshot(); },
         std::move(get_xf));
      v.a11y_sink(std::move(s));
      return h;
   }
}

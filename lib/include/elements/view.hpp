/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_VIEW_AUGUST_15_2016)
#define ELEMENTS_VIEW_AUGUST_15_2016

#include <infra/support.hpp>
#include <elements/base_view.hpp>
#include <elements/support/rect.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/theme.hpp>
#include <elements/element/element.hpp>
#include <elements/element/layer.hpp>
#include <elements/element/size.hpp>
#include <elements/element/indirect.hpp>
#include <elements/support/context.hpp>

#ifdef _WIN32
#include <windows.h>
#endif
#include <elements/support/task_queue.hpp>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <map>

namespace cycfi::elements
{
   class context;
   class window;
   class idle_tasks;

   class view : public base_view
   {
   public:
                              view(extent size_);
                              view(host_view_handle h);
                              view(window& win);
                              ~view();

      void                    draw(canvas& cnv) override;
      void                    click(mouse_button btn) override;
      void                    drag(mouse_button btn) override;
      void                    cursor(point p, cursor_tracking status) override;
      void                    scroll(point dir, point p) override;
      bool                    key(key_info const& k) override;
      bool                    text(text_info const& info) override;
      bool                    pad_button_event(pad_button_info info) override;
      void                    pad_axis_event(pad_axis_info info) override;
      void                    begin_focus() override;
      void                    end_focus() override;
      void                    relinquish_focus();

      // Move keyboard focus to a specific element.
      //
      // The element must already be part of this view's element tree. The
      // request is deferred to the next idle tick (mirroring view::add /
      // view::remove), so it is safe to call from anywhere — including
      // an element callback that itself runs inside an event dispatch.
      void                    focus(element_ptr e);

      // When enabled, arrow keys that no focused widget consumed are
      // translated into 2D directional focus moves (e.g., Right picks the
      // nearest focusable widget to the right of the currently focused
      // one, biased toward the same row). Defaults to false to preserve
      // legacy behavior. Sliders / dials / thumbwheels handle arrows
      // themselves so they keep adjusting their value — they win over
      // focus navigation.
      void                    arrow_focus_navigation(bool on);
      bool                    arrow_focus_navigation() const;

      // When enabled (together with arrow_focus_navigation), an arrow move
      // that walks off the edge of the focusable set wraps around to the
      // opposite edge (console-style menu looping). Defaults to false to
      // preserve legacy behavior.
      void                    arrow_focus_wrap(bool on);
      bool                    arrow_focus_wrap() const;

      // When enabled, disabled elements (is_enabled() == false) are
      // skipped by arrow-based 2D focus navigation. Defaults to false to
      // preserve legacy behavior.
      void                    focus_skip_disabled(bool on);
      bool                    focus_skip_disabled() const;

      // Chooses which widget an arrow key focuses when nothing is focused
      // yet (e.g. a dialog opened without initial focus). Defaults to
      // false = the first focusable in collection order, preserving legacy
      // behavior. When enabled, the arrow picks the widget furthest in the
      // pressed direction instead — Left focuses the leftmost widget,
      // Right the rightmost, Up the topmost, Down the bottommost. Useful
      // for confirmation dialogs that deliberately start unfocused so that
      // the direction pressed selects the option on that side.
      void                    arrow_focus_enter_directional(bool on);
      bool                    arrow_focus_enter_directional() const;

      // Explicit focus-navigation override for arrow_focus_navigation.
      // Called with the currently focused element and the direction
      // (0=left, 1=right, 2=up, 3=down) before the geometric pick. Return
      // the element to focus, or nullptr to fall back to the default
      // geometric navigation. Lets hosts drive navigation from a
      // declarative per-widget map (e.g. JSON "focus_nav") when the
      // geometric heuristic does not match the intended layout.
      using focus_nav_override_function =
         std::function<element*(element* current, int dir)>;
      void                    focus_nav_override(focus_nav_override_function f);

      // When enabled, moving the mouse over a focusable widget also moves
      // keyboard focus to it, so the hovered widget and the focused widget
      // stay in sync (pointer-driven focus). Defaults to true. Widgets opt
      // in via basic_button::cursor(); other element types may consult
      // view::hover_focus() to mirror the behavior.
      void                    hover_focus(bool on);
      bool                    hover_focus() const;

      // Returns true if the element that currently holds keyboard focus
      // consumes text input (i.e. an editable input_box / text box is
      // focused). Hosts use this to drive the platform on-screen keyboard:
      // show it while a text field is focused, hide it otherwise. This is
      // the same predicate used internally to suppress non-forced shortcuts
      // while typing (see focus_consumes_text()).
      bool                    focus_wants_text_input() { return focus_consumes_text(); }

      // Move keyboard focus to a specific element already in the view tree
      // (addressed by raw pointer). No-op if the element is not found.
      void                    focus(element& e);

      // Resolve the focus hot point (view coordinates) of the element
      // that currently holds keyboard focus — where a host should place
      // the pointer for cursor-warp navigation. Picks the shallowest
      // element on the focus chain with a custom hot point (e.g. a
      // choice-nav group pointing at its selected member), else the
      // deepest wants_focus() leaf with the default bounds center.
      // Returns false when nothing holds focus.
      bool                    focused_hot_point(point& out);

      // ---- Gamepad button → key synthesis -----------------------------
      // Map a gamepad button to a key event. When the bound pad button is
      // pressed and no shortcut intercepts it, the bound key_info is
      // dispatched through view::key() as if a real key were hit. Defaults
      // are installed in the view constructor:
      //   A → Enter, B → Esc, X → Shift+Tab, Y → Tab, D-Pad → arrows.
      void                    bind_pad_button(pad_button btn, key_code key, int mods = 0);
      void                    unbind_pad_button(pad_button btn);

      // ---- Shortcuts (work for both keys and pad buttons) -------------
      // When a bound input fires, the target element's activation path
      // runs (basic_button::activate for buttons) or the callback is
      // invoked. By default the shortcut is suppressed while a text-
      // consuming widget (e.g. an editable input_box) currently holds
      // focus; pass `force=true` to bypass that check.
      void                    bind_shortcut(key_info key, element_ptr target, bool force = false);
      void                    bind_shortcut(pad_button btn, element_ptr target, bool force = false);
      void                    bind_shortcut(key_info key, std::function<void()> cb, bool force = false);
      void                    bind_shortcut(pad_button btn, std::function<void()> cb, bool force = false);
      void                    unbind_shortcut(key_info key);
      void                    unbind_shortcut(pad_button btn);

      // ---- Analog axis modes ------------------------------------------
      // Per-group axis mode. Defaults:
      //   D-Pad   : both
      //   L-Stick : focus
      //   R-Stick : value
      //   Trigger : disabled
      void                    dpad_mode(pad_axis_mode m);
      void                    left_stick_mode(pad_axis_mode m);
      void                    right_stick_mode(pad_axis_mode m);
      void                    trigger_mode(pad_axis_mode m);
      pad_axis_mode           dpad_mode() const;
      pad_axis_mode           left_stick_mode() const;
      pad_axis_mode           right_stick_mode() const;
      pad_axis_mode           trigger_mode() const;

      // Below the deadzone an axis is treated as zero. Default 0.15.
      void                    stick_deadzone(float v);
      float                   stick_deadzone() const;

      // Speed (in 0..1 units / second) applied to value-mode axes when
      // delivered to a widget's pad_axis(). Default 1.0.
      void                    stick_value_speed(float per_sec);
      float                   stick_value_speed() const;

      // Focus-mode axis auto-repeat timing. delay_ms is the pause before
      // the first repeat (default 400). rate_ms is the fixed repeat
      // interval; 0 (default) keeps the legacy magnitude-scaled interval
      // (60..250ms depending on how far the stick is pushed).
      void                    axis_repeat(int delay_ms, int rate_ms);
      int                     axis_repeat_delay() const;
      int                     axis_repeat_rate() const;

      // Time elapsed (seconds) between the previous poll() and the
      // current one. Used by widgets implementing pad_axis to scale the
      // per-frame value step.
      float                   frame_dt() const { return _frame_dt; }
      void                    track_drop(drop_info const& info, cursor_tracking status) override;
      bool                    drop(drop_info const& info) override;
      void                    poll() override;

      void                    layout();
      void                    layout(element& element);
      float                   scale() const;
      void                    scale(float val);

      void                    refresh() override;
      void                    refresh(rect area) override;
      void                    refresh(context const& ctx, rect area);
      void                    refresh(element& element, int outward = 0);
      void                    refresh(context const& ctx, int outward = 0);

      // Partial redraw: look up an element's current bounds (user coordinates)
      // without triggering a refresh. Walks the tree the same way
      // `refresh(element&)` does, but captures the rect instead of queuing it,
      // and does so synchronously (no deferred task). An offscreen host uses
      // this to turn "this element changed" into a dirty rectangle.
      // `natural` receives the element's current natural size (limits().min);
      // it can exceed the bounds when the content grew without a re-layout
      // (a label whose text got longer), which is exactly the case where a
      // bounds-sized dirty rectangle would leave stale pixels behind.
      // Returns false if the element is not found / the view has not laid out.
      bool                    element_bounds(element& e, rect& out,
                                             extent& natural);

      // Partial redraw: restrict the bounds reported by `view_bounds(view)`
      // (device coordinates) for the duration of one draw pass. Composite /
      // layer elements cull children that do not intersect it, so an
      // offscreen host redrawing only a dirty rectangle skips building the
      // shapes for everything outside that rectangle — the bulk of the cost.
      // An empty rect (the default) means "the whole view", i.e. no culling.
      // The host sets this right before draw() and clears it after.
      rect                    draw_bounds() const { return _draw_bounds; }
      void                    draw_bounds(rect r) { _draw_bounds = r; }

      struct undo_redo_task
      {
         std::function<void()> undo;
         std::function<void()> redo;
      };

      void                    add_undo(undo_redo_task t);
      bool                    has_undo();
      bool                    has_redo();
      bool                    undo();
      bool                    redo();

      using content_type = layer_composite;
      using layers_type = layer_composite::container_type;
      using scaled_content = scale_element<indirect<reference<layer_composite>>>;

      scaled_content&         main_element()         { return _main_element; }
      scaled_content const&   main_element() const   { return _main_element; }

      content_type&           content();
      content_type const&     content() const;
      void                    content(std::initializer_list<element_ptr> list);

                              template <typename... E>
      void                    content(E&&... elements);

      using layers_vector = std::vector<element_ptr>;

      void                    add(element_ptr e, bool focus = true);
      void                    remove(element_ptr e);
      void                    move_to_front(element_ptr e);
      void                    move_to_back(element_ptr e);
      bool                    is_open(element_ptr e);
      layers_vector const&    layers() const;

      view_limits             limits() const;
      mouse_button            current_button() const;

      using change_limits_function = std::function<void(view_limits limits_)>;
      change_limits_function on_change_limits;

      using steady_timer_ptr = std::shared_ptr<detail::timer_handle>;

                              template <typename T, typename F>
      steady_timer_ptr        post(T duration, F f);

                              template <typename F>
      void                    post(F f);

      using tracking = element::tracking;

      using track_function = std::function<void(element& e, tracking state)>;
      track_function on_tracking = [](element& /* e */, tracking /* state */) {};

      void                    manage_on_tracking(element& e, tracking state);

      using context_function = element::context_function;
      void                    in_context_do(element& e, context_function f);


   private:

      scaled_content          make_scaled_content() { return elements::scale(1.0, link(_content)); }

      layer_composite         _content;
      scaled_content          _main_element;

      void                    set_limits();

      rect                    _current_bounds;
      // 部分再描画中の描画対象領域 (device 座標)。 空 = view 全体 (既定)。
      rect                    _draw_bounds = {};
      // element_bounds() 実行中フラグ + 捕捉した矩形 (refresh を横取りする)。
      bool                    _capturing_bounds = false;
      rect                    _captured_bounds = {};
      extent                  _captured_natural = {0, 0};
      view_limits             _current_limits = {{0, 0}, { full_extent, full_extent}};
      mouse_button            _current_button;
      bool                    _is_focus = false;
      bool                    _arrow_focus_nav = false;
      bool                    _arrow_focus_wrap = false;
      focus_nav_override_function _focus_nav_override;
      bool                    _focus_skip_disabled = false;
      bool                    _arrow_focus_enter_dir = false;
      bool                    _hover_focus = true;

      struct pad_key_binding
      {
         key_code key;
         int      mods;
      };
      std::map<pad_button, pad_key_binding> _pad_key_bindings;

      struct shortcut_target
      {
         std::weak_ptr<element> target;
         std::function<void()>  callback;
         bool                   force = false;
      };

      // key_info is a small POD: store (key, modifiers) in std::pair to
      // avoid relying on operator< for key_info itself.
      using key_shortcut_key = std::pair<key_code, int>;
      std::map<key_shortcut_key, shortcut_target> _key_shortcuts;
      std::map<pad_button, shortcut_target>       _pad_shortcuts;

      bool                    focus_consumes_text();
      void                    fire_shortcut(shortcut_target const& t);

      // Pad axis state ----------------------------------------------------
      struct axis_state
      {
         float                                       current = 0.0f; // -1..+1 after deadzone
         int                                         dir = 0;        // -1, 0, +1 (sign of last triggered move)
         std::chrono::steady_clock::time_point       next_repeat{};
         bool                                        value_active = false; // last value-mode dispatch was non-zero
      };
      axis_state              _axis_states[8] = {};   // indexed by pad_axis enum value

      pad_axis_mode           _dpad_mode         = pad_axis_mode::both;
      pad_axis_mode           _left_stick_mode   = pad_axis_mode::focus;
      pad_axis_mode           _right_stick_mode  = pad_axis_mode::value;
      pad_axis_mode           _trigger_mode      = pad_axis_mode::disabled;
      float                   _stick_deadzone    = 0.15f;
      float                   _stick_value_speed = 1.0f;
      int                     _axis_repeat_delay_ms = 400;
      int                     _axis_repeat_rate_ms  = 0;   // 0 = magnitude-scaled
      float                   _frame_dt          = 0.0f;
      std::chrono::steady_clock::time_point _last_poll_time{};

      pad_axis_mode           mode_for(pad_axis a) const;
      void                    process_pad_axes(std::chrono::steady_clock::time_point now);
      void                    synthesize_axis_key(pad_axis axis, int sign);

      using undo_stack_type = std::stack<undo_redo_task>;
      undo_stack_type         _undo_stack;
      undo_stack_type         _redo_stack;

      detail::task_queue      _tasks;

      using time_point = std::chrono::steady_clock::time_point;
      using tracking_map = std::map<element*, time_point>;

      tracking_map            _tracking;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Inlines
   ////////////////////////////////////////////////////////////////////////////

   // The functions below are declared in context.hpp. They are defined here due
   // to the forward declaration of `view` in the header file, preventing direct
   // access to the actual `view` class. These functions are explicitly marked
   // for forced inlining using CYCFI_FORCE_INLINE (defined in infra/support.hpp).
   //
   // If you encounter an undefined reference to any of these functions during
   // the linking phase, it indicates the necessity to include
   // <elements/view.hpp> in the .hpp or .cpp file of the calling code.

   // declared in context.hpp
   CYCFI_FORCE_INLINE point cursor_pos(view const& v)
   {
      return v.cursor_pos();
   }

   // declared in context.hpp
   CYCFI_FORCE_INLINE rect view_bounds(view const& v)
   {
      // 部分再描画中は描画対象矩形を返す (composite / layer の子カリングが
      // これを見るので、 矩形外の要素は shape 生成ごとスキップされる)。
      auto db = v.draw_bounds();
      if (!db.is_empty())
         return db;
      auto size = v.size();
      return rect{0, 0, size.x, size.y};
   }

   inline bool view::has_undo()
   {
      return !_undo_stack.empty();
   }

   inline bool  view::has_redo()
   {
      return !_redo_stack.empty();
   }

   inline view::content_type& view::content()
   {
      return _content;
   }

   inline view::content_type const& view::content() const
   {
      return _content;
   }

   inline void view::content(std::initializer_list<element_ptr> list)
   {
      _content.end_focus();
      _content = list;
      std::reverse(_content.begin(), _content.end());
      set_limits();
   }

   namespace detail
   {
      template <typename E>
      inline element_ptr add_element(E&& e)
      {
         return share(std::forward<E>(e));
      }

      template <typename E>
      inline std::shared_ptr<E> add_element(std::shared_ptr<E> ep)
      {
         return ep;
      }
   }

   template <typename... E>
   inline void view::content(E&&... elements)
   {
      _content.end_focus();
      _content = {detail::add_element(std::forward<E>(elements))...};
      std::reverse(_content.begin(), _content.end());
      set_limits();
   }

   inline void view::add(element_ptr e, bool focus_top)
   {
      // We'll defer this call just to be safe, to give the trigger that
      // initiated this call (e.g. button on_click) a chance to return.
      if (e)
      {
         // Return early if the element is already in the view's content list.
         if (std::find(_content.begin(), _content.end(), e) != _content.end())
            return;

         _tasks.post(
            [e, this, focus_top]
            {
               auto wants_focus = focus_top && e->wants_focus();
               // End the current focus if the new element wants to be the focus.
               if (wants_focus)
                  end_focus();

               // Add the new element to the top and lay it out
               _content.push_back(e);
               layout(*e);

               // Make the new element the new focus if it wants to.
               if (wants_focus)
               {
                  // Restore previous focus or make the top most layer the focus
                  auto req = focus_top?
                     element::focus_request::from_top :
                     element::focus_request::restore_previous
                     ;

                  _main_element.begin_focus(req);
                  refresh();
                  _is_focus = _main_element.focus();
               }
            }
         );
      }
   }

   inline void view::remove(element_ptr e)
   {
      // We want to dismiss the element, but we can't do it immediately
      // because we need to retain the trigger that initiated this call (e.g.
      // button on_click), otherwise there's nothing to return to. So, we
      // post a function that is called at idle time.
      if (e)
      {
         _tasks.post(
            [e, this]
            {
               auto i = std::find(_content.begin(), _content.end(), e);
               if (i != _content.end())
               {
                  // Relinquish the focus if the element to be removed is the current focus
                  auto ix = i - _content.begin();
                  if (_content.focus_index() == ix)
                     relinquish_focus();

                  // Remove the element.
                  _content.erase(i);

                  // Lay it out
                  layout();

                  // Restore previous focus
                  _main_element.begin_focus(element::focus_request::restore_previous);
                  refresh();
                  _is_focus = _main_element.focus();
               }
            }
         );
      }
   }

   inline bool view::is_open(element_ptr e)
   {
      auto i = std::find(_content.begin(), _content.end(), e);
      return i != _content.end();
   }

   inline void view::move_to_front(element_ptr e)
   {
      if (e && _content.back() != e)
      {
         _tasks.post(
            [e, this]
            {
               auto i = std::find(_content.begin(), _content.end(), e);
               if (i != _content.end())
               {
                  end_focus();
                  std::rotate(i, i+1, _content.end());
                  _content.reset();
                  layout();
                  begin_focus();
               }
            }
         );
      }
   }

   inline void view::move_to_back(element_ptr e)
   {
      if (e && _content.front() != e)
      {
         _tasks.post(
            [e, this]
            {
               auto i = std::find(_content.begin(), _content.end(), e);
               if (i != _content.end())
               {
                  end_focus();
                  std::rotate(_content.begin(), i, i+1);
                  _content.reset();
                  layout();
                  begin_focus();
               }
            }
         );
      }
   }

   inline view::layers_vector const& view::layers() const
   {
      return _content;
   }

   inline view_limits view::limits() const
   {
      return _current_limits;
   }

   inline mouse_button view::current_button() const
   {
      return _current_button;
   }

   template <typename T, typename F>
   inline view::steady_timer_ptr view::post(T duration, F f)
   {
      return _tasks.post_after(
         std::chrono::duration_cast<detail::task_queue::clock::duration>(duration),
         std::move(f)
      );
   }

   template <typename F>
   inline void view::post(F f)
   {
      _tasks.post(std::move(f));
   }
}

#endif

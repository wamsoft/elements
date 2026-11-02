/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_A11Y_ACCESSKIT_HOST_OCTOBER_4_2026)
#define ELEMENTS_A11Y_ACCESSKIT_HOST_OCTOBER_4_2026

#include <elements/support/a11y.hpp>
#include <functional>
#include <memory>
#include <string>

#if defined(__ANDROID__)
# include <jni.h>
#endif

namespace cycfi::elements
{
   class view;
}

#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
struct SDL_Window;
#endif

////////////////////////////////////////////////////////////////////////////////
// OS accessibility through AccessKit (UIA / NSAccessibility / AT-SPI /
// UIAccessibility / Android AccessibilityNodeProvider).
//
// Built only with ELEMENTS_A11Y_ACCESSKIT=ON (target elements_a11y_accesskit).
// No AccessKit type appears here.
//
// One accesskit_host serves one native window. Sources — a view, an
// elements_modal overlay session, anything that produces a11y snapshots — are
// registered in slots and composed under the window's root node, bottom to
// top by z; a modal slot hides the slots below it. Each source gets an
// a11y::sink to push into, and a perform function that receives AT actions:
// on the window's thread on Windows (posted to the window, which also wakes a
// host idling in its message loop), macOS and iOS, from another thread on
// Unix, on the Android UI thread on Android.
// view::a11y_perform posts to the UI thread itself.
//
// Native Elements windows: attach_accesskit(view) does all of it.
// Embedding hosts (a game engine compositing Elements into its own window):
// create the host for the main window and add one slot per overlay.
// docs/accessibility.md §3.
////////////////////////////////////////////////////////////////////////////////
namespace cycfi::elements::a11y
{
   // View coordinates -> what the OS adapter wants: physical pixels relative
   // to the window's client area on Windows and macOS (to the view on iOS:
   // points x contentScaleFactor), window coordinates on
   // Unix (AT-SPI on Wayland works in logical units).
   struct transform
   {
      float    sx = 1.0f;
      float    sy = 1.0f;
      float    tx = 0.0f;
      float    ty = 0.0f;
   };

   class accesskit_host;
   std::unique_ptr<accesskit_host> attach_accesskit(view& v);

   class accesskit_host
   {
   public:

      // The window: an HWND on Windows, an NSWindow* on macOS (it must have a
      // content view), the UIView* to serve on iOS. Ignored on Unix, where
      // the adapter needs no handle. Works on a window that is already
      // visible.
      static std::unique_ptr<accesskit_host> attach(void* native_window);

#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)
      // An SDL3 window (any SDL window, not only Elements' own): takes the
      // native handle from the window properties, forwards the macOS focus
      // query, and watches SDL window events for focus and bounds (Unix).
      static std::unique_ptr<accesskit_host> attach_sdl(SDL_Window* window);
#endif

                              ~accesskit_host();
                              accesskit_host(accesskit_host const&) = delete;
      accesskit_host&         operator=(accesskit_host const&) = delete;

      using perform_function = std::function<void(node_id, action, action_arg)>;
      using snapshot_function = std::function<snapshot()>;
      using transform_function = std::function<a11y::transform()>;

      // `snapshot_now` (optional) is called on the UI thread when an AT first
      // connects on platforms that ask on the UI thread (Windows, macOS), so
      // the very first answer is the real tree. `get_transform` is evaluated
      // on the UI thread whenever the source pushes a change.
      std::shared_ptr<sink>   add_source(int slot, perform_function perform,
                                 snapshot_function snapshot_now = {},
                                 transform_function get_transform = {});
      void                    remove_source(int slot);
      void                    set_transform(int slot, a11y::transform xf);
      void                    set_z(int slot, int z);
      void                    set_modal(int slot, bool modal);

      void                    set_window_label(std::string label);

      // Window state the adapter cannot see by itself (Unix, macOS with a
      // toolkit that keeps keyboard focus on the window).
      void                    window_focus(bool focused);
      // Unix / X11 only: the window's outer (with decorations) and inner
      // bounds in screen coordinates.
      void                    window_bounds(rect outer, rect inner);

      // True once an assistive technology has asked for the tree.
      bool                    is_active() const;

      // Called when is_active() changes: on the window's thread on Windows,
      // macOS and iOS (inside the AT's request, so keep it short: queue work, don't
      // run scripts), from another thread on Unix. Set it right after attach.
      void                    on_active_changed(std::function<void(bool)> f);

      // What the adapter counts as one physical pixel per window point: the
      // NSWindow's backingScaleFactor on macOS, the view's contentScaleFactor
      // on iOS, 1 elsewhere. A host that
      // renders at one pixel per point (an SDL window without high pixel
      // density on a Retina screen) multiplies its transform by this.
      float                   native_scale() const;

      // iOS with attach_sdl: SDL replaces its window's root view when it
      // creates a GL or Metal view, and the adapter lives on one view. Call
      // this after the host may have done that (it is cheap; a host can call
      // it every frame); returns true when the adapter moved to a new view.
      // A no-op elsewhere.
      bool                    refresh_native_view();

#if defined(__ANDROID__)
      // Android. The framework asks a View, not a window, and only on its UI
      // thread, while the toolkit usually runs elsewhere (SDL's thread). So
      // the host's View overrides getAccessibilityNodeProvider() and
      // onHoverEvent() and forwards them here from the UI thread (`host` is
      // that View). attach(nullptr) / attach_sdl make the adapter; the tree
      // is answered from the latest snapshots the sources pushed.
      jobject                 android_create_node_info(JNIEnv* env, jobject host, jint virtual_view_id);
      jobject                 android_find_focus(JNIEnv* env, jobject host, jint focus_type);
      bool                    android_perform_action(JNIEnv* env, jobject host,
                                 jint virtual_view_id, jint action, jobject arguments);
      bool                    android_on_hover(JNIEnv* env, jobject host, jint action, jfloat x, jfloat y);

      // Changes reach the adapter on the UI thread only: flush() calls
      // `request` (from any thread), which must arrange for android_update()
      // to run on the UI thread (e.g. Activity.runOnUiThread).
      void                    android_on_update_request(std::function<void()> request);
      void                    android_update(JNIEnv* env, jobject host);

      // AccessibilityManager state (TalkBack on / off). The adapter itself
      // never learns that the screen reader went away.
      void                    android_set_enabled(bool enabled);
#endif

      // Push what changed to AccessKit. Sources call this from their sink;
      // call it after set_transform / set_z / set_modal.
      void                    flush();

      struct impl;

   private:

      friend std::unique_ptr<accesskit_host> attach_accesskit(view& v);

                              accesskit_host();
      std::shared_ptr<impl>   _impl;    // sinks hold it weakly
   };

   // Native Elements windows (Win32 or SDL3 host): connect a view's window to
   // the OS. Keep the returned object alive as long as the view.
   std::unique_ptr<accesskit_host> attach_accesskit(view& v);
}

#endif

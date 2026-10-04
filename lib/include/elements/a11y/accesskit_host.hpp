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

namespace cycfi::elements
{
   class view;
}

////////////////////////////////////////////////////////////////////////////////
// OS accessibility through AccessKit (UIA / NSAccessibility / AT-SPI).
//
// Built only with ELEMENTS_A11Y_ACCESSKIT=ON (target elements_a11y_accesskit).
// No AccessKit type appears here.
//
// One accesskit_host serves one native window. Sources — a view, an
// elements_modal overlay session, anything that produces a11y snapshots — are
// registered in slots and composed under the window's root node, bottom to
// top by z; a modal slot hides the slots below it. Each source gets an
// a11y::sink to push into, and a perform function that receives AT actions
// (from any thread; view::a11y_perform posts to the UI thread itself).
//
// Native Elements windows: attach_accesskit(view) does all of it.
// Embedding hosts (a game engine compositing Elements into its own window):
// create the host for the main window and add one slot per overlay.
// docs/accessibility.md §3.
////////////////////////////////////////////////////////////////////////////////
namespace cycfi::elements::a11y
{
   // View coordinates -> what the OS adapter wants: physical pixels relative
   // to the window's client area on Windows and macOS, window coordinates on
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
      // content view). Ignored on Unix, where the adapter needs no handle.
      // Works on a window that is already visible.
      static std::unique_ptr<accesskit_host> attach(void* native_window);

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

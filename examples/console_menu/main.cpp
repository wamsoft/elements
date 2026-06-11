/*=============================================================================
   Console-style menu UI demo.

   Demonstrates:
   - console_button_styler: black body, white frame, white text.
     When focused: white body, black text (invert).
   - console_outline_button_styler: same body, plus an outer colored
     ring when focused. Ring color is supplied at construction.
   - trigger_selector: ◀ value ▶, left/right cycles through options
     (wraps), up/down pass through to view-level focus navigation.
   - segmented_selector: horizontal segments, left/right moves the
     selected segment; stops at the ends so end-of-row arrows fall
     through to focus navigation.
   - focus_row / labeled_row: row-wide highlight when any descendant
     holds focus. The label on the left is decorative; focus sits on
     the control on the right.
=============================================================================*/
#include <elements.hpp>

#include "console_button_styler.hpp"
#include "console_widgets.hpp"

using namespace cycfi::elements;
using namespace console_menu;

namespace
{
   auto constexpr bkd_color = rgba(12, 12, 16, 255);
   auto background = box(bkd_color);

   // Top row: two normal-style demo buttons (invert + outline).
   auto make_button_demo_row()
   {
      auto b1 = console_button("INVERT");
      auto b2 = console_outline_button("OUTLINE", colors::indian_red);
      auto b3 = console_outline_button("OUTLINE 2", colors::dodger_blue);

      return group(
         "Button styles (Space/Enter to activate)",
         margin({10, 35, 10, 10},
            htile_spaced(15.0f,
               std::move(b1),
               std::move(b2),
               std::move(b3)
            )
         )
      );
   }

   // Config rows.
   auto make_config_panel()
   {
      // Plain inline-arrow selector.
      auto system_sel = share(trigger_selector(
         {"PC-8081 mk2SR", "PC-9801 VM", "X68000 EXPERT"}, 0
      ));

      // Standalone arrow buttons driving a trigger_selector. The arrows
      // are NOT focusable — Tab skips them, the arrow keys still target
      // the focused selector — but they are mouse/touch operable.
      auto bios_sel = share(trigger_selector({"NORMAL", "TURBO", "COMPAT"}, 0));
      auto bios_arrows = make_step_arrows(bios_sel);
      element_ptr bios_unit = share(htile_spaced(4.0f,
         hsize(36, hold(bios_arrows.first)),
         hold(bios_sel),
         hsize(36, hold(bios_arrows.second))
      ));

      // Standalone arrow buttons driving a slider — same pattern.
      auto vol_pair  = slider_with_range(0, 100, 0.5);
      auto vol_slider = std::dynamic_pointer_cast<basic_slider_base>(vol_pair.focus);
      auto vol_arrows = make_step_arrows_for_slider(vol_slider, 0.05);
      element_ptr vol_unit = share(htile_spaced(4.0f,
         hsize(36, hold(vol_arrows.first)),
         hold(vol_pair.widget),
         hsize(36, hold(vol_arrows.second))
      ));

      auto display_mode = share(segmented_selector({"Full", "Dot by dot"}, 0));
      auto msg_fx       = share(segmented_selector({"ON", "OFF"}, 0));
      auto sound_fx     = share(segmented_selector({"ON", "OFF"}, 0));
      auto fullscreen   = share(segmented_selector({"Fullscreen", "Window"}, 1));
      auto resolution   = share(segmented_selector({"640x400", "1280x800"}, 0));

      // Integrated outer-arrow variant — for comparison with the
      // composed standalone-arrows pattern above.
      auto language = share(trigger_selector_arrows({"English", "Japanese"}, 0));

      auto win_scale = slider_with_range(0, 100, 0.4);

      return group(
         "Config (arrows / D-Pad / sticks to navigate, < > to change)",
         margin({10, 35, 10, 10},
            vtile_spaced(6.0f,
               labeled_row("System (inline)",        system_sel),
               labeled_row("BIOS (arrow parts)",     bios_unit,     bios_sel),
               labeled_row("Display mode",           display_mode),
               labeled_row("Message FX",             msg_fx),
               labeled_row("Sound FX",               sound_fx),
               labeled_row("Volume (arrow parts)",   vol_unit,      vol_pair.focus),
               labeled_row("Window scale",           win_scale.widget, win_scale.focus),
               labeled_row("Resolution",             resolution),
               labeled_row("Mode",                   fullscreen),
               labeled_row("Language (integrated)",  language)
            )
         )
      );
   }

   // Hint bar at the bottom: shows the key + pad equivalents.
   auto make_hint_bar()
   {
      auto hint = [](std::string text) {
         return label(std::move(text))
            .font_color(colors::white.opacity(0.7f))
            .font_size(13.0f);
      };
      return margin({10, 8, 10, 4},
         htile_spaced(20.0f,
            hint("< > Select"),
            hint("Enter / A  Activate"),
            hint("Esc  / B  Back")
         )
      );
   }
}

int main(int /*argc*/, char* /*argv*/[])
{
   app _app("Console Menu");
   window _win(_app.name());
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);
   view_.arrow_focus_navigation(true);

   auto top_row    = make_button_demo_row();
   auto config     = make_config_panel();
   auto hints      = make_hint_bar();

   // Initial focus: leave to the first focusable widget that wants_focus
   // along the layout traversal. Pressing Tab/A on launch will land on
   // the INVERT button.
   view_.content(
      margin({20, 20, 20, 20},
         vtile_spaced(15.0f,
            std::move(top_row),
            std::move(config),
            std::move(hints)
         )
      ),
      background
   );

   _app.run();
   return 0;
}

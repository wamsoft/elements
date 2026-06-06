/*=============================================================================
   Key-driven operation demo for the Elements (ThorVG port) library.

   Tab / Shift+Tab  : move keyboard focus between widgets (wraps around)
   Space / Enter    : activate the focused button (momentary / toggle /
                      latching / choice / button_menu)
   Arrow keys       : adjust the focused slider / dial / thumbwheel.
                      When arrow_focus_navigation is enabled and the
                      focused widget does NOT consume arrows (i.e., the
                      focus is on a button / check_box / radio / input),
                      arrows move the focus 2D-wise to the nearest
                      focusable widget in that direction. Sliders, dials,
                      and thumbwheels keep ownership of arrow keys for
                      value adjustment.
   PageUp / PageDown: ±0.1 on the focused slider / dial / thumbwheel
   Home / End       : jump to min / max on the focused slider / dial /
                      thumbwheel

   The initial focus is placed on the "Momentary" button via the
   initial_focus(...) wrapper; the "Focus the slider" button uses the
   programmatic view::focus(element_ptr) API to move focus to a slider.
=============================================================================*/
#include <elements.hpp>

using namespace cycfi::elements;

auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

auto make_buttons_panel()
{
   auto mbtn = share(button("Momentary"));
   auto tbtn = share(toggle_button("Toggle", 1.0, colors::royal_blue.opacity(0.4)));
   auto lbtn = share(latching_button("Latching", 1.0, colors::green.level(0.7).opacity(0.4)));
   auto reset = share(button("Reset Latch"));

   lbtn->on_click =
      [reset = get(reset)](bool) mutable
      {
         if (auto p = reset.lock())
            p->enable(true);
      };
   reset->on_click =
      [lbtn = get(lbtn), reset = get(reset)](bool) mutable
      {
         if (auto p = lbtn.lock())
            p->value(false);
         if (auto p = reset.lock())
            p->enable(false);
      };
   reset->enable(false);

   return
      group("Buttons (Space/Enter)",
         margin({10, 35, 10, 10},
            vtile_spaced(10.0,
               // Mark this button as the initial focus on startup.
               initial_focus(hold(mbtn)),
               hold(tbtn),
               hold(lbtn),
               hold(reset)
            )
         )
      );
}

auto make_check_and_radio_panel()
{
   auto cb1 = check_box("Check one");
   auto cb2 = check_box("Check two");
   auto cb3 = check_box("Check three");

   auto rb1 = radio_button("Choice A");
   auto rb2 = radio_button("Choice B");
   auto rb3 = radio_button("Choice C");
   rb1.select(true);

   return
      htile_spaced(20.0,
         group("Check (Space)",
            margin({10, 35, 10, 10},
               vtile_spaced(8.0,
                  align_left(cb1),
                  align_left(cb2),
                  align_left(cb3)
               )
            )
         ),
         group("Radio (Space)",
            margin({10, 35, 10, 10},
               vtile_spaced(8.0,
                  align_left(rb1),
                  align_left(rb2),
                  align_left(rb3)
               )
            )
         )
      );
}

auto make_value_panel(view& view_, std::shared_ptr<basic_slider_base>& focus_target)
{
   // Two horizontal sliders: ←/→ adjust value, ↑/↓ pass through to focus nav.
   auto sl1 = share(slider(
      basic_thumb<25>(),
      basic_track<5, false>(colors::black),
      0.5
   ));
   focus_target = sl1;

   auto sl2 = share(slider(
      basic_thumb<25>(),
      basic_track<5, false>(colors::black),
      0.25
   ));

   // Dial: ←/→ adjust value, ↑/↓ pass through to focus nav.
   auto dial_ = share(dial(
      radial_marks<20>(basic_knob<50>()),
      0.3
   ));

   // Vertical thumbwheel: ↑/↓ adjust value, ←/→ pass through to focus nav.
   auto compose_item =
      [](std::size_t index)
      {
         auto text = "Item " + std::to_string(index + 1);
         return share(
            hsize(100, align_center(
               heading(text)
                  .font_color(get_theme().indicator_hilite_color)
                  .font_size(16)
            ))
         );
      };
   auto wheel = share(vthumbwheel(10, compose_item));

   return
      group("Sliders / Dial / Thumbwheel (axis-only arrows)",
         margin({10, 35, 10, 10},
            vtile_spaced(15.0,
               hold(sl1),
               hold(sl2),
               htile_spaced(20.0,
                  align_center(hold(dial_)),
                  layer(hsize(120, hold(wheel)), frame{})
               )
            )
         )
      );
}

auto make_menu_panel()
{
   auto popup = button_menu("Open Menu", menu_position::bottom_right);

   auto skf = shortcut_key{key_code::f, mod_action};
   auto item_a = menu_item("Apple", skf);
   auto item_b = menu_item("Banana");
   auto item_c = menu_item("Cherry");

   auto menu_layer =
      layer(
         vtile(item_a, item_b, item_c),
         panel{}
      );

   popup.menu(hsize(200, menu_layer));

   return
      group("Menu (Space/Enter to open, then arrows + Enter)",
         margin({10, 35, 10, 10}, popup)
      );
}

auto make_input_panel()
{
   return
      group("Text input (still works)",
         margin({10, 35, 10, 10},
            vtile_spaced(8.0,
               input_box("Type here").first,
               input_box("And here").first
            )
         )
      );
}

auto make_ui(view& view_, std::shared_ptr<basic_slider_base>& slider_target)
{
   auto buttons    = make_buttons_panel();
   auto checkradio = make_check_and_radio_panel();
   auto values     = make_value_panel(view_, slider_target);
   auto menu       = make_menu_panel();
   auto inputs     = make_input_panel();

   auto focus_to_slider = button("Move focus to slider");
   focus_to_slider.on_click =
      [&view_, w = std::weak_ptr<basic_slider_base>(slider_target)](bool) mutable
      {
         if (auto sl = w.lock())
            view_.focus(sl);
      };

   auto arrow_nav_toggle = check_box("Arrow keys move focus (2D nav)");
   arrow_nav_toggle.value(view_.arrow_focus_navigation());
   arrow_nav_toggle.on_click =
      [&view_](bool on)
      {
         view_.arrow_focus_navigation(on);
      };

   return
      margin({20, 20, 20, 20},
         vtile_spaced(15.0,
            htile_spaced(15.0,
               buttons,
               checkradio
            ),
            values,
            htile_spaced(15.0, menu, inputs),
            htile_spaced(15.0,
               focus_to_slider,
               align_left(arrow_nav_toggle)
            )
         )
      );
}

int main(int /*argc*/, char* /*argv*/[])
{
   app _app("Key Driven");
   window _win(_app.name());
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);

   // Enable arrow-key 2D focus navigation by default for the demo. Toggle
   // the check_box at the bottom to compare with the legacy Tab-only mode.
   view_.arrow_focus_navigation(true);

   std::shared_ptr<basic_slider_base> slider_target;

   view_.content(
      make_ui(view_, slider_target),
      background
   );

   _app.run();
   return 0;
}

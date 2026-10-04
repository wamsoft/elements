/*=============================================================================
   Accessibility: an Elements window exposed to screen readers.

   attach_accesskit(view) connects the view's window to the OS accessibility
   API (UIA on Windows, NSAccessibility on macOS, AT-SPI on Linux) through
   AccessKit. The tree itself comes from the widgets (element::accessible);
   a11y_label & co. fill in what a widget cannot know.

   Try it with Narrator (Win+Ctrl+Enter) or NVDA, VoiceOver (Cmd+F5), or
   Orca (Super+Alt+S). Tab / Shift+Tab move the focus, Space / Enter press,
   arrows adjust.
=============================================================================*/
#include <elements.hpp>
#include <elements/a11y/accesskit_host.hpp>

using namespace cycfi::elements;

auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

int main(int /*argc*/, char* /*argv*/[])
{
   app _app("Accessibility");
   window _win(_app.name());
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);
   view_.arrow_focus_navigation(true);

   auto save   = share(button("Save"));
   auto reset  = share(button("Reset"));
   auto sound  = share(check_box("Enable sound"));
   auto easy   = share(radio_button("Easy"));
   auto normal = share(radio_button("Normal"));
   auto hard   = share(radio_button("Hard"));
   auto volume = share(slider(basic_thumb<25>(), basic_track<5, false>(colors::black), 0.5));
   auto speed  = share(cycle_picker({"Slow", "Medium", "Fast"}, 1));
   auto name   = share(input_box("Player name").first);
   auto status = share(label("Ready"));

   normal->select(true);

   save->on_click = [&](bool)
   {
      status->set_text("Saved");
      view_.refresh(*status);
      view_.announce("Settings saved");
   };
   reset->on_click = [&](bool)
   {
      volume->value(0.5);
      view_.refresh(*volume);
      view_.announce("Settings reset");
   };

   view_.content(
      margin({20, 20, 20, 20},
         vtile_spaced(12.0,
            a11y_role(a11y::role::heading, heading("Game settings")),
            htile_spaced(10.0, initial_focus(hold(save)), hold(reset)),
            align_left(hold(sound)),
            group("Difficulty",
               margin({10, 35, 10, 10},
                  vtile_spaced(6.0, align_left(hold(easy)), align_left(hold(normal)),
                     align_left(hold(hard)))
               )
            ),
            a11y_label("Volume", hold(volume)),
            a11y_label("Game speed", hold(speed)),
            hold(name),
            a11y_live(a11y::live::polite, hold(status))
         )
      ),
      background
   );

   // One line: the view's window now talks to the screen reader.
   auto a11y = a11y::attach_accesskit(view_);

   _app.run();
   return 0;
}

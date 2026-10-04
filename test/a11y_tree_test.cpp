//---------------------------------------------------------------------------
// a11y_tree_test — accessibility tree of a headless view (no window, no OS
// adapter): what element::accessible() declares, what the walker builds,
// what AT actions do, and what the sink receives.
//
// Run: a11y_tree_test <repo-root>     (fonts are loaded from resources/)
//---------------------------------------------------------------------------
#include <elements.hpp>
#include <elements/support/detail/scratch_context.hpp>
#include <thorvg.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>

using namespace cycfi::elements;

namespace
{
   int failures = 0;

   void check(bool ok, char const* what)
   {
      std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
      if (!ok)
         ++failures;
   }

   constexpr int kW = 420;
   constexpr int kH = 640;

   struct recording_sink : a11y::sink
   {
      int               calls = 0;
      a11y::update      last;

      void tree_changed(a11y::snapshot const& /* full */, a11y::update const& delta) override
      {
         ++calls;
         last = delta;
      }
   };

   a11y::node const* by_name(a11y::snapshot const& s, std::string const& name)
   {
      for (auto const& n : s.nodes)
         if (n.name == name)
            return &n;
      return nullptr;
   }

   a11y::node const* by_role(a11y::snapshot const& s, a11y::role r)
   {
      for (auto const& n : s.nodes)
         if (n.role == r)
            return &n;
      return nullptr;
   }

   void draw(view& v)
   {
      std::vector<std::uint32_t> buf(std::size_t(kW) * kH);
      canvas cnv{buf.data(), kW, kH, 1.0f};
      v.draw(cnv);
   }

   void settle(view& v)
   {
      for (int i = 0; i != 4; ++i)
         v.poll();
   }

   void dump(a11y::snapshot const& s)
   {
      for (auto const& n : s.nodes)
         std::printf("    %-18s %s\n", a11y::id_string(n).substr(0, 18).c_str(),
            a11y::describe(n).c_str());
   }
}

int run()
{
   view v(extent{kW, kH});
   v.a11y_name("Test window");

   int hello_clicks = 0;
   auto hello  = share(button("Say hello"));
   auto other  = share(button("Another button"));
   auto check_ = share(check_box("Enable sound"));
   auto radio1 = share(radio_button("Choice A"));
   auto radio2 = share(radio_button("Choice B"));
   auto vol    = share(slider(basic_thumb<25>(), basic_track<5, false>(colors::black), 0.5));
   auto pick   = share(cycle_picker({"Low", "Mid", "High"}));
   auto input  = share(input_box("Your name").first);
   auto hidden = share(hidable(label("Hidden label")));
   auto pages  = share(deck(label("Page A"), label("Page B")));

   hello->on_click = [&](bool) { ++hello_clicks; };
   radio1->select(true);
   hidden->is_hidden = true;

   v.content(
      margin({10, 10, 10, 10},
         vtile_spaced(6.0,
            initial_focus(hold(hello)),
            hold(other),
            align_left(hold(check_)),
            align_left(hold(radio1)),
            align_left(hold(radio2)),
            a11y_label("Volume", hold(vol)),
            hold(pick),
            hold(input),
            label("Plain label"),
            hold(hidden),
            a11y_label("Logo", fixed_size({40, 20}, box(colors::red))),
            hold(pages)
         )
      ),
      box(colors::black)
   );

   v.begin_focus();
   draw(v);
   settle(v);

   std::printf("[tree]\n");
   auto s = v.a11y_snapshot();
   dump(s);

   std::printf("[roles and names]\n");
   check(s.nodes.size() > 1 && s.nodes[0].role == a11y::role::window
      && s.nodes[0].name == "Test window", "root is the named window");
   auto* n_hello = by_name(s, "Say hello");
   check(n_hello && n_hello->role == a11y::role::button, "button named from its caption");
   auto* n_check = by_name(s, "Enable sound");
   check(n_check && n_check->role == a11y::role::check_box && !n_check->has(a11y::state::checked),
      "check box, unchecked");
   auto* n_r1 = by_name(s, "Choice A");
   auto* n_r2 = by_name(s, "Choice B");
   check(n_r1 && n_r1->role == a11y::role::radio_button && n_r1->has(a11y::state::checked),
      "radio button A, checked");
   auto* n_vol = by_name(s, "Volume");
   check(n_vol && n_vol->role == a11y::role::slider && n_vol->value == "50%"
      && n_vol->num_value && *n_vol->num_value == 50.0, "slider named by a11y_label, 50%");
   auto* n_pick = by_role(s, a11y::role::spin_button);
   check(n_pick && n_pick->value == "Low", "cycle picker: spin button showing Low");
   auto* n_input = by_role(s, a11y::role::text_input);
   check(n_input && n_input->name == "Your name", "input box named by its placeholder");
   check(by_name(s, "Plain label") != nullptr, "plain label");
   check(by_name(s, "Hidden label") == nullptr, "hidden subtree left out");
   auto* n_logo = by_name(s, "Logo");
   check(n_logo && n_logo->role == a11y::role::image, "a11y_label on a decoration makes an image");
   check(by_name(s, "Page A") && !by_name(s, "Page B"), "deck: only the selected page");
   if (n_hello)
      std::printf("    hello bounds: %.0f,%.0f %.0fx%.0f\n", n_hello->bounds.left,
         n_hello->bounds.top, n_hello->bounds.width(), n_hello->bounds.height());
   check(n_hello && n_hello->bounds.left == 10 && n_hello->bounds.top == 10
      && n_hello->bounds.width() > 50 && n_hello->bounds.height() > 10,
      "bounds in view coordinates");
   check(n_hello && s.focus == n_hello->id && n_hello->has(a11y::state::focused),
      "initial focus on the hello button");

   // Ids are stable across rebuilds
   auto s2 = v.a11y_snapshot();
   auto* n_hello2 = by_name(s2, "Say hello");
   check(n_hello && n_hello2 && n_hello->id == n_hello2->id, "ids stable across snapshots");
   check(a11y::diff(&s, s2).empty(), "no change, empty diff");

   std::printf("[actions]\n");
   v.a11y_perform(n_hello->id, a11y::action::click);
   settle(v);
   check(hello_clicks == 1, "click on a button runs on_click");

   v.a11y_perform(n_check->id, a11y::action::click);
   settle(v);
   s = v.a11y_snapshot();
   n_check = by_name(s, "Enable sound");
   check(n_check && n_check->has(a11y::state::checked), "click toggles the check box");

   v.a11y_perform(n_r2->id, a11y::action::click);
   settle(v);
   s = v.a11y_snapshot();
   n_r1 = by_name(s, "Choice A");
   n_r2 = by_name(s, "Choice B");
   check(n_r2 && n_r2->has(a11y::state::checked) && n_r1 && !n_r1->has(a11y::state::checked),
      "click selects radio B and clears A");

   v.a11y_perform(n_vol->id, a11y::action::increment);
   settle(v);
   s = v.a11y_snapshot();
   n_vol = by_name(s, "Volume");
   check(n_vol && n_vol->value == "55%", "increment steps the slider by 5");

   a11y::action_arg eighty;
   eighty.number = 80.0;
   v.a11y_perform(n_vol->id, a11y::action::set_value, eighty);
   settle(v);
   s = v.a11y_snapshot();
   n_vol = by_name(s, "Volume");
   check(n_vol && n_vol->value == "80%", "set_value moves the slider to 80%");

   v.a11y_perform(n_pick->id, a11y::action::increment);
   settle(v);
   s = v.a11y_snapshot();
   n_pick = by_role(s, a11y::role::spin_button);
   check(n_pick && n_pick->value == "Mid", "increment steps the picker to Mid");

   a11y::action_arg abc;
   abc.text = "abc";
   v.a11y_perform(n_input->id, a11y::action::set_value, abc);
   settle(v);
   s = v.a11y_snapshot();
   n_input = by_role(s, a11y::role::text_input);
   check(n_input && n_input->value == "abc", "set_value fills the input box");

   auto* n_other = by_name(s, "Another button");
   v.a11y_perform(n_other->id, a11y::action::focus);
   settle(v);
   s = v.a11y_snapshot();
   n_other = by_name(s, "Another button");
   check(n_other && s.focus == n_other->id, "focus action moves keyboard focus");

   std::printf("[sink]\n");
   auto sink = std::make_shared<recording_sink>();
   v.a11y_sink(sink);
   settle(v);
   check(sink->calls == 1 && sink->last.full, "first push is the full tree");

   int before = sink->calls;
   settle(v);
   check(sink->calls == before, "nothing pushed while nothing changes");

   v.announce("Saved");
   settle(v);
   auto* ann = by_role(v.a11y_snapshot(), a11y::role::status);
   bool ann_in_delta = false;
   for (auto const& n : sink->last.nodes)
      if (n.role == a11y::role::status && n.name == "Saved" && n.live == a11y::live::polite)
         ann_in_delta = true;
   check(ann && ann_in_delta, "announce pushes a polite live region");

   v.a11y_perform(n_vol->id, a11y::action::decrement);
   settle(v);
   bool only_slider = sink->last.nodes.size() == 1 && sink->last.nodes[0].name == "Volume"
      && sink->last.nodes[0].value == "75%";
   check(only_slider, "a value change pushes just that node");

   {
      // A sink attached before the first draw (what an overlay host does):
      // the tree must still arrive once the view has been laid out.
      view v2(extent{200, 100});
      v2.content(margin({10, 10, 10, 10}, button("Late")), box(colors::black));
      auto early = std::make_shared<recording_sink>();
      v2.a11y_sink(early);
      settle(v2);
      std::vector<std::uint32_t> b2(200 * 100);
      canvas c2{b2.data(), 200, 100, 1.0f};
      v2.draw(c2);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));   // past the 33 ms throttle
      settle(v2);
      bool got = false;
      for (auto const& n : early->last.nodes)
         if (n.name == "Late")
            got = true;
      check(got, "sink attached before the first draw gets the tree after it");
   }

   {
      // speech_lines: what a screen reader would say about a change
      a11y::snapshot a = v.a11y_snapshot();
      v.a11y_perform(n_vol->id, a11y::action::focus);
      settle(v);
      a11y::snapshot b = v.a11y_snapshot();
      auto lines = a11y::speech_lines(&a, b);
      check(lines.size() == 1 && lines[0] == "[focus] Volume, slider, 75%",
         "speech_lines: focus moved to the slider");
      a = b;
      v.a11y_perform(n_vol->id, a11y::action::increment);
      settle(v);
      lines = a11y::speech_lines(&a, v.a11y_snapshot());
      check(lines.size() == 1 && lines[0] == "[value] Volume, 80%",
         "speech_lines: value change of the focused control");
   }

   std::printf("[json]\n");
   auto json = a11y::to_json(v.a11y_snapshot());
   std::printf("    %.200s...\n", json.c_str());
   check(json.rfind("{\"focus\":", 0) == 0 && json.find("\"role\":\"slider\"") != std::string::npos,
      "json dump");

   return failures;
}

int main(int argc, char* argv[])
{
   std::setvbuf(stdout, nullptr, _IONBF, 0);
   std::string root = argc > 1 ? argv[1] : ".";

   // Normally done by the app / overlay_session.
   tvg::Initializer::init(0);
   load_fonts_from_directory(root + "/resources/fonts");
   load_fonts_from_directory(root + "/resources");

   int n = run();

   detail::release_shared_scratch();
   tvg::Initializer::term();

   std::printf("%s (%d failure%s)\n", n ? "FAILED" : "PASSED", n, n == 1 ? "" : "s");
   return n ? 1 : 0;
}

//---------------------------------------------------------------------------
// modal_a11y_test — accessibility of an elements_modal (JSON) screen, headless:
// the "a11y" key, stable ids from "id", descriptions from the focus help line
// ("strings_on_focus"), values from "display_var", labeled_row naming its
// control, visible_var hiding, the screen title, language switching, and AT
// actions through overlay_session.
//
// Run: modal_a11y_test <repo-root>     (fonts are loaded from resources/)
//---------------------------------------------------------------------------
#include <elements.hpp>
#include <elements_modal/modal.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ce = cycfi::elements;
namespace a11y = cycfi::elements::a11y;

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
   constexpr int kH = 480;

   char const* kScreen = R"json({
      "size": [420, 480],
      "lang": "en",
      "strings": {
         "t.title": { "en": "Settings",       "ja": "設定" },
         "t.save":  { "en": "Save",           "ja": "保存" },
         "t.help":  { "en": "Saves the game", "ja": "ゲームを保存します" },
         "t.vol":   { "en": "Volume",         "ja": "音量" }
      },
      "vars": { "help": "-", "vol_text": "50%", "show_secret": "0" },
      "a11y": { "title_id": "t.title", "announce": "Settings opened" },
      "content": { "type": "margin", "padding": 20, "child": {
         "type": "vtile", "gap": 8, "children": [
            { "type": "button", "id": "btn_save", "text_id": "t.save",
              "initial_focus": true, "strings_on_focus": { "help": "t.help" } },
            { "type": "button", "id": "btn_icon", "text": " ",
              "a11y": "Open menu" },
            { "type": "labeled_row", "label_id": "t.vol", "child":
               { "type": "slider", "id": "vol", "display_var": "vol_text" } },
            { "type": "label", "text": "Secret", "visible_var": "show_secret" },
            { "type": "label", "text": "Ready", "a11y": { "role": "status", "live": "polite" } },
            { "type": "vtile", "id": "group1", "children": [
               { "type": "label", "text": "Inner text" } ] }
         ] } }
   })json";

   a11y::node const* by_name(a11y::snapshot const& s, std::string const& name)
   {
      for (auto const& n : s.nodes)
         if (n.name == name)
            return &n;
      return nullptr;
   }

   a11y::node const* by_id(a11y::snapshot const& s, std::string const& id)
   {
      for (auto const& n : s.nodes)
         if (a11y::id_string(n) == id)
            return &n;
      return nullptr;
   }

   void frame(elements_modal::overlay_session& sess, std::vector<std::uint32_t>& buf)
   {
      elements_modal::overlay_session::render_rect r;
      for (int i = 0; i != 3; ++i)
      {
         sess.update();
         sess.render_to_buffer(buf.data(), kW, kH, kW, kH, r);
      }
   }

   int run()
   {
      std::vector<std::uint32_t> buf(std::size_t(kW) * kH);
      elements_modal::overlay_session sess;
      if (!sess.start(kScreen, kW, kH, 1.0f, nullptr))
      {
         std::printf("start failed\n");
         return 1;
      }
      frame(sess, buf);

      auto s = sess.a11y_snapshot();
      std::printf("[tree]\n");
      for (auto const& n : s.nodes)
         std::printf("    %-18s %s\n", a11y::id_string(n).substr(0, 18).c_str(),
            a11y::describe(n).c_str());

      std::printf("[json keys and derivation]\n");
      check(!s.nodes.empty() && s.nodes[0].name == "Settings", "screen title from \"a11y\".title_id");
      auto* save = by_id(s, "btn_save");
      check(save && save->name == "Save" && save->role == a11y::role::button,
         "\"id\" becomes the node id; name from text_id");
      check(save && save->description == "Saves the game",
         "description from strings_on_focus (focus help line)");
      check(save && save->has(a11y::state::focused), "initial focus");
      auto* icon = by_id(s, "btn_icon");
      check(icon && icon->name == "Open menu", "\"a11y\": string names a button without text");
      auto* vol = by_id(s, "vol");
      check(vol && vol->name == "Volume", "labeled_row label names its control");
      // The slider writes display_var itself (default format: "50").
      check(vol && vol->value == "50", "value from display_var");
      int volume_labels = 0;
      for (auto const& n : s.nodes)
         if (n.name == "Volume")
            ++volume_labels;
      check(volume_labels == 1, "labeled_row label not read twice");
      check(!by_name(s, "Secret"), "visible_var=0 hides the element");
      auto* status = by_name(s, "Ready");
      check(status && status->role == a11y::role::status && status->live == a11y::live::polite,
         "\"a11y\" role and live");
      check(!by_id(s, "group1"), "layout-type id is not applied");
      check(by_name(s, "Inner text") != nullptr, "children of a layout are still read");
      bool announced = false;
      for (auto const& n : s.nodes)
         if (n.role == a11y::role::status && n.name == "Settings opened")
            announced = true;
      check(announced, "\"a11y\".announce on entering the screen");

      std::printf("[changes]\n");
      sess.set_var("show_secret", "1");
      frame(sess, buf);
      check(by_name(sess.a11y_snapshot(), "Secret") != nullptr, "visible_var=1 shows it again");

      sess.set_language("ja");
      frame(sess, buf);
      s = sess.a11y_snapshot();
      save = by_id(s, "btn_save");
      check(s.nodes[0].name == "設定", "title follows the language");
      check(save && save->name == "保存" && save->description == "ゲームを保存します",
         "name and description follow the language");
      vol = by_id(s, "vol");
      check(vol && vol->name == "音量", "labeled_row name follows the language");
      check(save && save->id == by_id(s, "btn_save")->id, "ids survive a language switch");

      std::printf("[actions]\n");
      check(sess.a11y_perform("vol", "increment"), "a11y_perform by dump id");
      frame(sess, buf);
      vol = by_id(sess.a11y_snapshot(), "vol");
      check(vol && vol->num_value && *vol->num_value > 50.0, "increment moved the slider");
      check(!sess.a11y_perform("nope", "click"), "unknown node is reported");

      auto json = sess.a11y_dump_json();
      check(json.find("\"id\":\"btn_save\"") != std::string::npos, "dump uses the JSON ids");
      return failures;
   }
}

int main(int argc, char* argv[])
{
   std::setvbuf(stdout, nullptr, _IONBF, 0);
   std::string root = argc > 1 ? argv[1] : ".";
   elements_modal::init();
   ce::load_fonts_from_directory(root + "/resources/fonts");
   ce::load_fonts_from_directory(root + "/resources");

   int n = run();
   elements_modal::shutdown();

   std::printf("%s (%d failure%s)\n", n ? "FAILED" : "PASSED", n, n == 1 ? "" : "s");
   return n ? 1 : 0;
}

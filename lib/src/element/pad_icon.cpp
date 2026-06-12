/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/pad_icon.hpp>
#include <elements/element/element.hpp>
#include <elements/element/label.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/context.hpp>
#include <elements/support/font.hpp>
#include <elements/support/theme.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cycfi::elements
{
   namespace
   {
      //---------------------------------------------------------------------
      // Theme metadata
      //---------------------------------------------------------------------
      struct theme_data
      {
         const char*  font_file;        // basename inside <base>/<dir>/
         const char*  font_family;      // family name (== file stem)
         const char*  vector_subdir;    // subdir of base e.g., "xbox"
         const char*  vector_prefix;    // SVG filename prefix e.g., "xbox_button_a"
         // logical / native name → kenney basename (no extension, no path)
         std::unordered_map<std::string_view, std::string_view> name_map;
         // kenney basename → codepoint (parsed from map.txt at first use)
         std::unordered_map<std::string, std::uint32_t> codepoint_map;
         bool codepoints_loaded = false;
      };

      // Per-theme name → kenney basename. Compile-time tables, populated below.
      // Logical (Steam Input) AND theme-native names are mixed in the same map
      // so callers can use whichever feels natural for the screen.

      const std::unordered_map<std::string_view, std::string_view>&
      xbox_name_map()
      {
         static const std::unordered_map<std::string_view, std::string_view> m = {
            // face buttons
            {"face_south", "xbox_button_a"},
            {"face_east",  "xbox_button_b"},
            {"face_west",  "xbox_button_x"},
            {"face_north", "xbox_button_y"},
            {"a", "xbox_button_a"}, {"b", "xbox_button_b"},
            {"x", "xbox_button_x"}, {"y", "xbox_button_y"},
            // dpad
            {"dpad_up",    "xbox_dpad_up"},
            {"dpad_down",  "xbox_dpad_down"},
            {"dpad_left",  "xbox_dpad_left"},
            {"dpad_right", "xbox_dpad_right"},
            {"up",    "xbox_dpad_up"},    {"down",  "xbox_dpad_down"},
            {"left",  "xbox_dpad_left"},  {"right", "xbox_dpad_right"},
            // shoulders / triggers
            {"lb", "xbox_lb"}, {"rb", "xbox_rb"},
            {"lt", "xbox_lt"}, {"rt", "xbox_rt"},
            // sticks
            {"lstick", "xbox_stick_l"}, {"rstick", "xbox_stick_r"},
            {"lstick_press", "xbox_ls"}, {"rstick_press", "xbox_rs"},
            {"ls", "xbox_ls"}, {"rs", "xbox_rs"},
            // menus
            {"start",   "xbox_button_start"},
            {"back",    "xbox_button_back"},
            {"select",  "xbox_button_back"},
            {"menu",    "xbox_button_menu"},
            {"view",    "xbox_button_view"},
            {"share",   "xbox_button_share"},
            {"guide",   "xbox_guide"},
            {"home",    "xbox_guide"},
         };
         return m;
      }

      const std::unordered_map<std::string_view, std::string_view>&
      ps_name_map()
      {
         static const std::unordered_map<std::string_view, std::string_view> m = {
            // face buttons — physical positions
            {"face_south", "playstation_button_cross"},
            {"face_east",  "playstation_button_circle"},
            {"face_west",  "playstation_button_square"},
            {"face_north", "playstation_button_triangle"},
            {"cross",    "playstation_button_cross"},
            {"circle",   "playstation_button_circle"},
            {"square",   "playstation_button_square"},
            {"triangle", "playstation_button_triangle"},
            // dpad
            {"dpad_up",    "playstation_dpad_up"},
            {"dpad_down",  "playstation_dpad_down"},
            {"dpad_left",  "playstation_dpad_left"},
            {"dpad_right", "playstation_dpad_right"},
            {"up",    "playstation_dpad_up"},    {"down",  "playstation_dpad_down"},
            {"left",  "playstation_dpad_left"},  {"right", "playstation_dpad_right"},
            // shoulders / triggers
            {"lb", "playstation_trigger_l1"}, {"rb", "playstation_trigger_r1"},
            {"lt", "playstation_trigger_l2"}, {"rt", "playstation_trigger_r2"},
            {"l1", "playstation_trigger_l1"}, {"r1", "playstation_trigger_r1"},
            {"l2", "playstation_trigger_l2"}, {"r2", "playstation_trigger_r2"},
            // sticks
            {"lstick", "playstation_stick_l"}, {"rstick", "playstation_stick_r"},
            {"lstick_press", "playstation_button_l3"},
            {"rstick_press", "playstation_button_r3"},
            {"l3", "playstation_button_l3"}, {"r3", "playstation_button_r3"},
            // menus
            {"start",     "playstation5_button_options"},
            {"options",   "playstation5_button_options"},
            {"back",      "playstation3_button_select"},
            {"select",    "playstation3_button_select"},
            {"share",     "playstation4_button_share"},
            {"touchpad",  "playstation5_button_touchpad"},
            {"home",      "playstation_button_playstation"},
            {"playstation", "playstation_button_playstation"},
         };
         return m;
      }

      const std::unordered_map<std::string_view, std::string_view>&
      switch_name_map()
      {
         static const std::unordered_map<std::string_view, std::string_view> m = {
            // Nintendo's letter positions are rotated 90° from Xbox.
            //   A = east (right), B = south (bottom), X = north (top), Y = west (left)
            {"face_south", "switch_button_b"},
            {"face_east",  "switch_button_a"},
            {"face_west",  "switch_button_y"},
            {"face_north", "switch_button_x"},
            {"a", "switch_button_a"}, {"b", "switch_button_b"},
            {"x", "switch_button_x"}, {"y", "switch_button_y"},
            // dpad
            {"dpad_up",    "switch_dpad_up"},
            {"dpad_down",  "switch_dpad_down"},
            {"dpad_left",  "switch_dpad_left"},
            {"dpad_right", "switch_dpad_right"},
            {"up",    "switch_dpad_up"},    {"down",  "switch_dpad_down"},
            {"left",  "switch_dpad_left"},  {"right", "switch_dpad_right"},
            // shoulders / triggers — Switch uses L/R + ZL/ZR
            {"lb", "switch_button_l"},  {"rb", "switch_button_r"},
            {"lt", "switch_button_zl"}, {"rt", "switch_button_zr"},
            {"l",  "switch_button_l"},  {"r",  "switch_button_r"},
            {"zl", "switch_button_zl"}, {"zr", "switch_button_zr"},
            {"sl", "switch_button_sl"}, {"sr", "switch_button_sr"},
            // sticks
            {"lstick", "switch_stick_l"}, {"rstick", "switch_stick_r"},
            {"lstick_press", "switch_stick_l_press"},
            {"rstick_press", "switch_stick_r_press"},
            // menus
            {"start",   "switch_button_plus"},
            {"plus",    "switch_button_plus"},
            {"back",    "switch_button_minus"},
            {"select",  "switch_button_minus"},
            {"minus",   "switch_button_minus"},
            {"home",    "switch_button_home"},
            {"capture", "switch_button_capture"},
            {"share",   "switch_button_capture"},
         };
         return m;
      }

      const std::unordered_map<std::string_view, std::string_view>&
      keyboard_name_map()
      {
         static const std::unordered_map<std::string_view, std::string_view> m = {
            // Action-style logical names mapped to common keys
            {"face_south", "keyboard_enter"},
            {"face_east",  "keyboard_escape"},
            {"face_west",  "keyboard_shift"},
            {"face_north", "keyboard_tab"},
            {"dpad_up",    "keyboard_arrow_up"},
            {"dpad_down",  "keyboard_arrow_down"},
            {"dpad_left",  "keyboard_arrow_left"},
            {"dpad_right", "keyboard_arrow_right"},
            {"up",    "keyboard_arrow_up"},    {"down",  "keyboard_arrow_down"},
            {"left",  "keyboard_arrow_left"},  {"right", "keyboard_arrow_right"},
            {"start",  "keyboard_enter"},
            {"back",   "keyboard_escape"},
            {"select", "keyboard_escape"},
            // Direct key names: pass through (caller passes "keyboard_a" etc).
            // We don't enumerate them all; resolve_pad_icon_svg_path falls back
            // to using the name directly as the kenney basename if it starts
            // with "keyboard_" or "mouse_".
         };
         return m;
      }

      //---------------------------------------------------------------------
      // Theme registry
      //---------------------------------------------------------------------
      // Indexed by static_cast<int>(pad_theme) - 1; pad_theme::none has no slot.
      struct theme_static
      {
         const char* dir;
         const char* font_file;
         const char* font_family;
      };

      const theme_static kThemeStatic[] = {
         { "xbox",     "kenney_input_xbox_series.ttf",        "kenney_input_xbox_series" },
         { "ps",       "kenney_input_playstation_series.ttf", "kenney_input_playstation_series" },
         { "switch",   "kenney_input_nintendo_switch.ttf",    "kenney_input_nintendo_switch" },
         { "keyboard", "kenney_input_keyboard_&_mouse.ttf",   "kenney_input_keyboard___mouse" },
      };
      constexpr int kNumThemes = sizeof(kThemeStatic) / sizeof(kThemeStatic[0]);

      // 1-based mapping from pad_theme to index into kThemeStatic.
      int theme_index(pad_theme t)
      {
         switch (t) {
            case pad_theme::xbox:     return 0;
            case pad_theme::ps:       return 1;
            case pad_theme::switch_:  return 2;
            case pad_theme::keyboard: return 3;
            default: return -1;
         }
      }

      //---------------------------------------------------------------------
      // Mutable globals
      //---------------------------------------------------------------------
      std::mutex   g_mutex;
      pad_theme    g_current_theme = pad_theme::none;
      std::string  g_base_dir;
      // Per-theme codepoint maps + registered family name.
      struct theme_runtime
      {
         std::unordered_map<std::string, std::uint32_t> codepoints;
         std::string registered_family; // empty until load_pad_icon_fonts succeeds
         bool        codepoints_attempted = false;
      };
      theme_runtime g_runtime[kNumThemes];

      // SVG pixmap cache (keyed by absolute path).
      std::unordered_map<std::string, pixmap_ptr> g_svg_cache;

      //---------------------------------------------------------------------
      // Helpers
      //---------------------------------------------------------------------
      const std::unordered_map<std::string_view, std::string_view>&
      name_map_for(pad_theme t)
      {
         switch (t) {
            case pad_theme::xbox:     return xbox_name_map();
            case pad_theme::ps:       return ps_name_map();
            case pad_theme::switch_:  return switch_name_map();
            case pad_theme::keyboard: return keyboard_name_map();
            default: {
               static const std::unordered_map<std::string_view, std::string_view> empty;
               return empty;
            }
         }
      }

      // Parse a Kenney map.txt: lines like "xbox_button_a: U+E004".
      // No-throw; ignores malformed lines.
      void load_codepoints_for(pad_theme t)
      {
         int idx = theme_index(t);
         if (idx < 0) return;

         auto& rt = g_runtime[idx];
         if (rt.codepoints_attempted) return;
         rt.codepoints_attempted = true;

         std::string base;
         {
            std::lock_guard<std::mutex> lk(g_mutex);
            base = g_base_dir;
         }
         if (base.empty()) return;

         // map.txt filenames don't fully match font file stem on disk for
         // keyboard (which uses "& Mouse" / spaces).  Probe a couple of paths.
         std::string dir = base + "/" + kThemeStatic[idx].dir;
         std::vector<std::string> candidates;
         std::string stem = std::string(kThemeStatic[idx].font_family);
         candidates.push_back(dir + "/" + stem + "_map.txt");
         // keyboard pack actually ships as "kenney_input_keyboard_&_mouse_map.txt"
         if (t == pad_theme::keyboard) {
            candidates.push_back(dir + "/kenney_input_keyboard_&_mouse_map.txt");
         }

         std::ifstream f;
         for (auto const& c : candidates) {
            f.open(c, std::ios::binary);
            if (f) break;
         }
         if (!f) return;

         std::string line;
         while (std::getline(f, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = line.substr(0, colon);
            // Find "U+" pattern
            auto upos = line.find("U+", colon);
            if (upos == std::string::npos) continue;
            std::string hex = line.substr(upos + 2);
            // trim
            while (!hex.empty() && std::isspace(static_cast<unsigned char>(hex.back())))
               hex.pop_back();
            if (hex.empty()) continue;
            std::uint32_t cp = 0;
            try { cp = std::stoul(hex, nullptr, 16); } catch (...) { continue; }
            // trim name leading/trailing whitespace
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
               name.pop_back();
            std::size_t lead = 0;
            while (lead < name.size() && std::isspace(static_cast<unsigned char>(name[lead])))
               ++lead;
            if (lead) name.erase(0, lead);
            if (name.empty()) continue;
            rt.codepoints[name] = cp;
         }
      }

      // Resolve theme-specific name → kenney basename. Returns empty string
      // if not in map. For keyboard theme: if `name` starts with "keyboard_"
      // or "mouse_", return it verbatim as the basename (passthrough).
      std::string resolve_kenney_basename(std::string_view name, pad_theme t)
      {
         auto const& nm = name_map_for(t);
         if (auto it = nm.find(name); it != nm.end()) {
            return std::string(it->second);
         }
         if (t == pad_theme::keyboard) {
            if (name.rfind("keyboard_", 0) == 0 ||
                name.rfind("mouse_", 0)    == 0)
               return std::string(name);
         }
         return {};
      }

      pixmap_ptr load_pixmap_safe(std::string const& path)
      {
         try {
            return std::make_shared<pixmap>(path, 1.0f);
         } catch (...) {
            return {};
         }
      }

      // For Xbox / PS face buttons, Kenney ships a "color" variant whose
      // basename has "color_" inserted between "_button_" and the letter
      // (e.g. xbox_button_color_a). Return the colored basename if such a
      // pattern is present; otherwise return the input unchanged.
      std::string to_color_basename(std::string const& basename)
      {
         auto pos = basename.find("_button_");
         if (pos == std::string::npos) return basename;
         return basename.substr(0, pos + 8 /* "_button_" */) +
                "color_" + basename.substr(pos + 8);
      }
   } // anonymous

   //---------------------------------------------------------------------
   // Public API
   //---------------------------------------------------------------------
   pad_theme parse_pad_theme(std::string_view name)
   {
      if (name == "xbox" || name == "Xbox")            return pad_theme::xbox;
      if (name == "ps" || name == "playstation"
          || name == "PlayStation")                    return pad_theme::ps;
      if (name == "switch" || name == "Switch"
          || name == "nintendo_switch")                return pad_theme::switch_;
      if (name == "keyboard" || name == "kb"
          || name == "Keyboard")                       return pad_theme::keyboard;
      return pad_theme::none;
   }

   const char* pad_theme_name(pad_theme t)
   {
      switch (t) {
         case pad_theme::xbox:     return "xbox";
         case pad_theme::ps:       return "ps";
         case pad_theme::switch_:  return "switch";
         case pad_theme::keyboard: return "keyboard";
         default: return "none";
      }
   }

   pad_theme get_pad_theme()
   {
      std::lock_guard<std::mutex> lk(g_mutex);
      return g_current_theme;
   }

   void set_pad_theme(pad_theme t)
   {
      std::lock_guard<std::mutex> lk(g_mutex);
      g_current_theme = t;
   }

   void set_pad_icon_base_dir(std::string path)
   {
      std::lock_guard<std::mutex> lk(g_mutex);
      g_base_dir = std::move(path);
   }

   const std::string& get_pad_icon_base_dir()
   {
      std::lock_guard<std::mutex> lk(g_mutex);
      return g_base_dir;
   }

   bool load_pad_icon_fonts()
   {
      std::string base;
      {
         std::lock_guard<std::mutex> lk(g_mutex);
         base = g_base_dir;
      }
      if (base.empty()) return false;

      bool any = false;
      for (int i = 0; i < kNumThemes; ++i) {
         // Try declared file first; for keyboard also try the literal Kenney
         // filename with '&' which survives the copy step.
         std::vector<std::string> candidates;
         std::string dir = base + "/" + kThemeStatic[i].dir;
         candidates.push_back(dir + "/" + kThemeStatic[i].font_file);
         if (i == 3) { // keyboard
            candidates.push_back(dir + "/kenney_input_keyboard_&_mouse.ttf");
         }

         for (auto const& path : candidates) {
            std::ifstream probe(path, std::ios::binary);
            if (!probe) continue;
            probe.close();
            auto family = register_font(kThemeStatic[i].font_family, path);
            if (!family.empty()) {
               g_runtime[i].registered_family = family;
               any = true;
            } else {
               // register_font may return empty if ThorVG couldn't extract a
               // family name; still treat the original key as the family.
               g_runtime[i].registered_family = kThemeStatic[i].font_family;
               any = true;
            }
            break;
         }
      }
      return any;
   }

   std::string_view pad_icon_font_family(pad_theme t)
   {
      int idx = theme_index(t);
      if (idx < 0) return {};
      return g_runtime[idx].registered_family;
   }

   std::string resolve_pad_icon_svg_path(std::string_view logical_name, pad_theme t)
   {
      int idx = theme_index(t);
      if (idx < 0) return {};
      auto basename = resolve_kenney_basename(logical_name, t);
      if (basename.empty()) return {};

      std::string base;
      {
         std::lock_guard<std::mutex> lk(g_mutex);
         base = g_base_dir;
      }
      if (base.empty()) return {};

      return base + "/" + kThemeStatic[idx].dir + "/vector/" + basename + ".svg";
   }

   std::uint32_t resolve_pad_icon_codepoint(std::string_view logical_name, pad_theme t)
   {
      int idx = theme_index(t);
      if (idx < 0) return 0;
      auto basename = resolve_kenney_basename(logical_name, t);
      if (basename.empty()) return 0;

      load_codepoints_for(t);
      auto const& cm = g_runtime[idx].codepoints;
      auto it = cm.find(basename);
      return (it != cm.end()) ? it->second : 0;
   }

   //---------------------------------------------------------------------
   // pad_font_icon — font-mode pad アイコンを 1 element として組む helper。
   // 内部は label + Kenney 同梱 TTF + UTF-8 化した codepoint。 codepoint /
   // family が解決できなければ `[name]` フォールバックを同じ色で返す。
   //
   // 元々 elements_modal の dispatch 内に同じロジックがあったが、 サンプル /
   // 直接 lib 利用側でも同じものを呼べるよう外出し。
   //---------------------------------------------------------------------
   std::shared_ptr<element>
   pad_font_icon(std::string_view name, float size, color c)
   {
      auto cp  = resolve_pad_icon_codepoint(name);
      auto fam = pad_icon_font_family();
      if (cp == 0 || fam.empty())
      {
         // Fallback: `[name]` の素 label。 codepoint 不在でも layout は維持。
         return share(label("[" + std::string(name) + "]")
            .relative_font_size(size)
            .font_color(c));
      }

      // codepoint を UTF-8 化して 1 文字 string にする。
      char buf[8] = {0};
      int n = 0;
      if (cp < 0x80) {
         buf[n++] = static_cast<char>(cp);
      } else if (cp < 0x800) {
         buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
         buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
         buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
         buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
         buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
      } else {
         buf[n++] = static_cast<char>(0xF0 | (cp >> 18));
         buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
         buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
         buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
      }
      // fam は pad_icon_font_family() の program-lifetime な std::string を
      // 指す string_view なので font_descr の string_view メンバに乗せて OK。
      font_descr fd{fam};
      return share(label(std::string(buf, n))
         .relative_font_size(size)
         .font_color(c)
         .font(fd));
   }

   //---------------------------------------------------------------------
   // pad_icon element
   //---------------------------------------------------------------------
   pad_icon::pad_icon(std::string logical_name, float target_height,
                      bool colored, bool outline)
    : _name(std::move(logical_name))
    , _target_height(target_height)
    , _colored(colored)
    , _outline(outline)
    , _theme_at_construct(get_pad_theme())
   {}

   void pad_icon::ensure_loaded() const
   {
      if (_tried) return;
      _tried = true;

      auto path = resolve_pad_icon_svg_path(_name, _theme_at_construct);
      if (path.empty()) return;

      // colored: 同じディレクトリで "_button_<x>" → "_button_color_<x>" に
      // 置換した候補を先に試す。 存在しない場合 (Switch face buttons など)
      // は plain にフォールバック。
      auto try_load = [this](std::string const& p) -> bool {
         {
            std::lock_guard<std::mutex> lk(g_mutex);
            auto it = g_svg_cache.find(p);
            if (it != g_svg_cache.end()) {
               _pixmap = it->second;
               return _pixmap != nullptr;
            }
         }
         auto pm = load_pixmap_safe(p);
         std::lock_guard<std::mutex> lk(g_mutex);
         g_svg_cache[p] = pm; // negative cache OK (pm may be null)
         _pixmap = pm;
         return pm != nullptr;
      };

      // outline フラグ用に「<stem>_outline.svg」を試すヘルパ。 try したいベース
      // path に対し、 outline が要求されていれば *_outline.svg → ベースの順で
      // 試す。 outline 非要求ならそのままベースを試す。
      auto try_with_outline = [this, &try_load](std::string const& base_path) -> bool {
         if (_outline) {
            auto dot = base_path.rfind(".svg");
            if (dot != std::string::npos) {
               std::string outline_path =
                  base_path.substr(0, dot) + "_outline.svg";
               if (outline_path != base_path && try_load(outline_path)) return true;
            }
         }
         return try_load(base_path);
      };

      if (_colored) {
         // resolve_pad_icon_svg_path で組まれたフルパスから basename だけ
         // 抽出して color 化、 残りを再構成。
         auto slash = path.rfind('/');
         std::string dir = (slash == std::string::npos) ? std::string{}
                                                        : path.substr(0, slash + 1);
         std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
         // strip .svg
         auto dot = base.rfind(".svg");
         std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
         std::string colored_path = dir + to_color_basename(stem) + ".svg";
         if (colored_path != path && try_with_outline(colored_path)) return;
      }
      try_with_outline(path);
   }

   view_limits pad_icon::limits(basic_context const&) const
   {
      ensure_loaded();
      float w = _target_height;
      float h = _target_height;
      if (_pixmap) {
         auto s = _pixmap->size();
         if (s.y > 0) {
            float aspect = s.x / s.y;
            w = _target_height * aspect;
         }
      }
      return {{w, h}, {w, h}};
   }

   void pad_icon::draw(context const& ctx)
   {
      ensure_loaded();
      auto& cnv = ctx.canvas;
      auto bounds = ctx.bounds;
      if (_pixmap) {
         auto sz = _pixmap->size();
         elements::rect src{0, 0, sz.x, sz.y};
         cnv.draw(*_pixmap, src, bounds);
         return;
      }
      // Placeholder: gray rounded rect
      auto st = cnv.new_state();
      cnv.fill_style(rgba(60, 60, 60, 255));
      cnv.fill_round_rect(bounds, 6.0f);
      cnv.line_width(1.0f);
      cnv.stroke_style(rgba(160, 160, 160, 255));
      cnv.stroke_round_rect(bounds, 6.0f);
   }
}

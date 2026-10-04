/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/a11y.hpp>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace cycfi::elements::a11y
{
   node const* snapshot::find(node_id id) const
   {
      if (_index.size() != nodes.size())
         reindex();
      auto i = _index.find(id);
      return i == _index.end() ? nullptr : &nodes[i->second];
   }

   void snapshot::reindex() const
   {
      _index.clear();
      _index.reserve(nodes.size());
      for (std::size_t i = 0; i != nodes.size(); ++i)
         _index.emplace(nodes[i].id, i);
   }

   update diff(snapshot const* prev, snapshot const& next)
   {
      update u;
      u.focus = next.focus;
      if (!prev || prev->root != next.root)
      {
         u.full = true;
         u.focus_changed = true;
         u.nodes = next.nodes;
         return u;
      }
      u.focus_changed = prev->focus != next.focus;
      for (auto const& n : next.nodes)
      {
         auto const* p = prev->find(n.id);
         if (!p || !(*p == n))
            u.nodes.push_back(n);
      }
      return u;
   }

   char const* role_name(role r)
   {
      switch (r)
      {
         case role::none:                 return "none";
         case role::generic:              return "group";
         case role::window:               return "window";
         case role::dialog:               return "dialog";
         case role::label:                return "label";
         case role::heading:              return "heading";
         case role::image:                return "image";
         case role::button:               return "button";
         case role::toggle_button:        return "toggle button";
         case role::check_box:            return "check box";
         case role::radio_button:         return "radio button";
         case role::tab:                  return "tab";
         case role::slider:               return "slider";
         case role::spin_button:          return "spin button";
         case role::menu_item:            return "menu item";
         case role::text_input:           return "edit";
         case role::multiline_text_input: return "multiline edit";
         case role::progress_indicator:   return "progress bar";
         case role::status:               return "status";
         case role::list:                 return "list";
         case role::list_item:            return "list item";
      }
      return "?";
   }

   std::optional<role> role_from_name(std::string_view s)
   {
      static constexpr std::pair<char const*, role> aliases[] = {
         {"group", role::generic}, {"generic", role::generic},
         {"text", role::label}, {"checkbox", role::check_box},
         {"toggle_button", role::toggle_button}, {"check_box", role::check_box},
         {"radio_button", role::radio_button}, {"spin_button", role::spin_button},
         {"menu_item", role::menu_item}, {"text_input", role::text_input},
         {"multiline_text_input", role::multiline_text_input},
         {"progress", role::progress_indicator},
         {"progress_indicator", role::progress_indicator},
         {"list_item", role::list_item},
      };
      for (auto const& [name, r] : aliases)
         if (s == name)
            return r;
      for (int i = 0; i <= int(role::list_item); ++i)
         if (s == role_name(role(i)))
            return role(i);
      return std::nullopt;
   }

   namespace
   {
      constexpr char const* action_names[] = {
         "focus", "click", "increment", "decrement", "set_value",
         "set_text_selection"
      };

      struct state_name { std::uint32_t bit; char const* name; };
      constexpr state_name state_names[] = {
         {state::focusable, "focusable"},
         {state::focused,   "focused"},
         {state::disabled,  "disabled"},
         {state::checked,   "checked"},
         {state::selected,  "selected"},
         {state::expanded,  "expanded"},
         {state::read_only, "read_only"},
         {state::modal,     "modal"},
      };

      void json_string(std::string& out, std::string_view s)
      {
         out += '"';
         for (unsigned char c : s)
         {
            switch (c)
            {
               case '"':  out += "\\\""; break;
               case '\\': out += "\\\\"; break;
               case '\n': out += "\\n"; break;
               case '\r': out += "\\r"; break;
               case '\t': out += "\\t"; break;
               default:
                  if (c < 0x20)
                  {
                     char buf[8];
                     std::snprintf(buf, sizeof buf, "\\u%04x", c);
                     out += buf;
                  }
                  else
                  {
                     out += char(c);
                  }
            }
         }
         out += '"';
      }

      void json_number(std::string& out, double v)
      {
         char buf[32];
         if (std::floor(v) == v && std::fabs(v) < 1e15)
            std::snprintf(buf, sizeof buf, "%.0f", v);
         else
            std::snprintf(buf, sizeof buf, "%.4g", v);
         out += buf;
      }
   }

   char const* action_name(action a)
   {
      auto i = unsigned(a);
      return i < std::size(action_names) ? action_names[i] : "?";
   }

   std::optional<action> action_from_name(std::string_view name)
   {
      for (unsigned i = 0; i != std::size(action_names); ++i)
         if (name == action_names[i])
            return action(i);
      return std::nullopt;
   }

   std::uint32_t state_from_name(std::string_view name)
   {
      for (auto const& s : state_names)
         if (name == s.name)
            return s.bit;
      return 0;
   }

   std::string id_string(node const& n)
   {
      if (!n.debug_id.empty())
         return n.debug_id;
      char buf[24];
      std::snprintf(buf, sizeof buf, "#%016llx", static_cast<unsigned long long>(n.id));
      return buf;
   }

   std::string describe(node const& n)
   {
      std::string s = n.name;
      auto add = [&](std::string_view part)
      {
         if (part.empty())
            return;
         if (!s.empty())
            s += ", ";
         s += part;
      };
      if (n.role != role::label && n.role != role::status)
         add(role_name(n.role));
      if (n.role == role::check_box || n.role == role::toggle_button || n.role == role::radio_button)
         add(n.has(state::checked) ? "checked" : "not checked");
      if ((n.role == role::tab || n.role == role::list_item) && n.has(state::selected))
         add("selected");
      add(n.value);
      if (n.has(state::disabled))
         add("unavailable");
      if (!n.description.empty())
         s += " \xE2\x80\x94 " + n.description;    // em dash
      return s;
   }

   std::string to_json(snapshot const& snap)
   {
      std::string out;
      out.reserve(256 * snap.nodes.size() + 64);

      auto const* f = snap.find(snap.focus);
      out += "{\"focus\":";
      json_string(out, f ? id_string(*f) : std::string{});
      out += ",\"nodes\":[";

      bool first = true;
      for (auto const& n : snap.nodes)
      {
         if (!first)
            out += ',';
         first = false;

         out += "{\"id\":";
         json_string(out, id_string(n));
         out += ",\"role\":";
         json_string(out, role_name(n.role));
         if (!n.name.empty())
         {
            out += ",\"name\":";
            json_string(out, n.name);
         }
         if (!n.value.empty())
         {
            out += ",\"value\":";
            json_string(out, n.value);
         }
         if (!n.description.empty())
         {
            out += ",\"description\":";
            json_string(out, n.description);
         }
         if (n.num_value)
         {
            out += ",\"num\":[";
            json_number(out, *n.num_value);
            out += ',';
            json_number(out, n.num_min.value_or(0));
            out += ',';
            json_number(out, n.num_max.value_or(0));
            out += ']';
         }
         if (n.states)
         {
            out += ",\"states\":[";
            bool sf = true;
            for (auto const& sn : state_names)
            {
               if (!n.has(sn.bit))
                  continue;
               if (!sf)
                  out += ',';
               sf = false;
               json_string(out, sn.name);
            }
            out += ']';
         }
         if (n.actions)
         {
            out += ",\"actions\":[";
            bool af = true;
            for (unsigned i = 0; i != std::size(action_names); ++i)
            {
               if (!(n.actions & (1u << i)))
                  continue;
               if (!af)
                  out += ',';
               af = false;
               json_string(out, action_names[i]);
            }
            out += ']';
         }
         if (n.selection)
         {
            auto const& s = *n.selection;
            out += ",\"selection\":[" + std::to_string(s.anchor.run) + ',' +
               std::to_string(s.anchor.index) + ',' + std::to_string(s.focus.run) + ',' +
               std::to_string(s.focus.index) + ']';
         }
         if (n.live != live::off)
         {
            out += ",\"live\":";
            json_string(out, n.live == live::assertive ? "assertive" : "polite");
         }
         out += ",\"rect\":[";
         json_number(out, std::round(n.bounds.left));
         out += ',';
         json_number(out, std::round(n.bounds.top));
         out += ',';
         json_number(out, std::round(n.bounds.width()));
         out += ',';
         json_number(out, std::round(n.bounds.height()));
         out += ']';
         if (!n.children.empty())
         {
            out += ",\"children\":[";
            bool cf = true;
            for (auto cid : n.children)
            {
               if (!cf)
                  out += ',';
               cf = false;
               auto const* c = snap.find(cid);
               json_string(out, c ? id_string(*c) : std::string{});
            }
            out += ']';
         }
         out += '}';
      }
      out += "]}";
      return out;
   }

   namespace
   {
      // "[caret] x" / "[caret] (end)" / "[selected] text"
      std::string caret_line(node const& n)
      {
         auto const& sel = *n.selection;
         auto char_at = [&n](text_position p, std::size_t& off, std::size_t& len) {
            off = 0;
            len = 0;
            if (p.run >= n.text_runs.size())
               return false;
            auto const& r = n.text_runs[p.run];
            for (std::uint32_t i = 0; i < p.index && i < r.char_lengths.size(); ++i)
               off += r.char_lengths[i];
            if (p.index >= r.char_lengths.size())
               return false;
            len = r.char_lengths[p.index];
            return true;
         };
         if (sel.anchor == sel.focus)
         {
            std::size_t off, len;
            if (!char_at(sel.focus, off, len))
               return "[caret] (end)";
            std::string c = n.text_runs[sel.focus.run].text.substr(off, len);
            if (c == " ")
               c = "(space)";
            else if (c == "\n")
               c = "(line end)";
            return "[caret] " + c;
         }
         // A selection: the text between the two ends (same run only; good
         // enough for a log line).
         auto a = sel.anchor, f = sel.focus;
         if (f.run < a.run || (f.run == a.run && f.index < a.index))
            std::swap(a, f);
         std::string text;
         for (std::uint32_t r = a.run; r <= f.run && r < n.text_runs.size(); ++r)
         {
            auto const& run = n.text_runs[r];
            std::size_t b = 0, e = run.text.size();
            std::size_t off, len;
            if (r == a.run && char_at(a, off, len))
               b = off;
            if (r == f.run)
               e = char_at(f, off, len) ? off : run.text.size();
            if (e > b)
               text += run.text.substr(b, e - b);
         }
         return "[selected] " + text;
      }
   }

   std::vector<std::string> speech_lines(snapshot const* prev, snapshot const& next)
   {
      std::vector<std::string> out;
      auto const* f = next.find(next.focus);
      bool focus_moved = !prev || prev->focus != next.focus;
      if (focus_moved && f && f->id != next.root)
         out.push_back("[focus] " + describe(*f));

      if (!prev)
         return out;

      constexpr std::uint32_t toggles = state::checked | state::selected;
      for (auto const& n : next.nodes)
      {
         auto const* p = prev->find(n.id);
         if (n.live != live::off)
         {
            // A live region speaks when its text changes (or appears).
            if (!p || p->name != n.name || p->value != n.value)
            {
               std::string text = n.name;
               // announce() makes a repeat differ by a trailing U+200B
               while (text.size() >= 3 && text.compare(text.size() - 3, 3, "\xE2\x80\x8B") == 0)
                  text.resize(text.size() - 3);
               if (!n.value.empty() && n.value != text)
                  text += (text.empty() ? "" : " ") + n.value;
               if (!text.empty())
                  out.push_back(std::string(n.live == live::assertive ? "[assertive] " : "[polite] ") + text);
            }
            continue;
         }
         if (!p || n.id != next.focus || focus_moved)
            continue;
         // The focused control changed under the user.
         if (p->value != n.value && !n.value.empty())
            out.push_back("[value] " + (n.name.empty() ? std::string{} : n.name + ", ") + n.value);
         // The caret moved in an edit field (without typing): the character
         // it lands on, as a screen reader echoes arrow keys.
         else if (p->value == n.value && n.selection && p->selection != n.selection)
            out.push_back(caret_line(n));
         if ((p->states & toggles) != (n.states & toggles))
            out.push_back("[state] " + describe(n));
      }
      return out;
   }

   node_id hash_id(std::string_view s)
   {
      // FNV-1a 64
      std::uint64_t h = 1469598103934665603ull;
      for (unsigned char c : s)
      {
         h ^= c;
         h *= 1099511628211ull;
      }
      return h ? h : 1;
   }
}

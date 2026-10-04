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
      }
      return "?";
   }

   namespace
   {
      constexpr char const* action_names[] = {
         "focus", "click", "increment", "decrement", "set_value"
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
      if (n.role == role::tab && n.has(state::selected))
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

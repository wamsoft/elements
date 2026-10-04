/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_SUPPORT_A11Y_OCTOBER_4_2026)
#define ELEMENTS_SUPPORT_A11Y_OCTOBER_4_2026

#include <elements/support/rect.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
// Accessibility (screen reader) semantic model.
//
// Elements draws everything itself, so the OS sees one opaque window. This
// is the toolkit-side half of exposing the UI to assistive technology:
//
//   element::accessible()   each element declares its role / name / value
//   view::a11y_snapshot()   walks the element tree into a flat node table
//   a11y::diff()            what changed since the previous snapshot
//   a11y::sink              where updates go (an OS adapter, a test, a REPL)
//
// Nothing here depends on an OS API or on AccessKit; the adapter that turns
// snapshots into UIA / NSAccessibility / AT-SPI lives outside the core.
// Design notes: docs/accessibility.md.
////////////////////////////////////////////////////////////////////////////////
namespace cycfi::elements::a11y
{
   enum class role : std::uint8_t
   {
      none,             // transparent: no node; children lift to the parent
      generic,          // grouping only
      window,
      dialog,
      label,
      heading,
      image,
      button,
      toggle_button,
      check_box,
      radio_button,
      tab,
      slider,
      spin_button,      // cycle pickers: one value out of a list, stepped
      menu_item,
      text_input,
      multiline_text_input,
      progress_indicator,
      status,           // live region
      list,             // a list of list_item children (choices, a menu)
      list_item
   };

   enum class live : std::uint8_t { off, polite, assertive };

   // Node state bits (node::states)
   namespace state
   {
      enum : std::uint32_t
      {
         focusable   = 1u << 0,
         focused     = 1u << 1,
         disabled    = 1u << 2,
         checked     = 1u << 3,
         selected    = 1u << 4,
         expanded    = 1u << 5,
         read_only   = 1u << 6,
         modal       = 1u << 7
      };
   }

   enum class action : std::uint8_t
   {
      focus,
      click,
      increment,
      decrement,
      set_value
   };

   constexpr std::uint32_t bit(action a) { return 1u << unsigned(a); }

   struct action_arg
   {
      std::optional<double>      number;
      std::optional<std::string> text;
   };

   using node_id = std::uint64_t;

   ////////////////////////////////////////////////////////////////////////////
   // One node of a snapshot
   ////////////////////////////////////////////////////////////////////////////
   struct node
   {
      node_id                 id = 0;
      a11y::role              role = role::none;
      std::string             name;
      std::string             description;
      std::string             value;         // displayed value ("50%", the text)
      std::optional<double>   num_value;
      std::optional<double>   num_min;
      std::optional<double>   num_max;
      std::optional<double>   num_step;
      std::uint32_t           states = 0;
      std::uint32_t           actions = 0;   // bit(action)
      a11y::live              live = live::off;
      rect                    bounds;        // view coordinates
      std::vector<node_id>    children;
      std::string             debug_id;      // explicit id, if any (dumps)

      bool                    has(std::uint32_t s) const { return (states & s) != 0; }
      bool                    operator==(node const&) const = default;
   };

   ////////////////////////////////////////////////////////////////////////////
   // What element::accessible() fills in
   //
   // An element that sets `role` becomes a node. With `leaf`, its subtree
   // is not exposed (a button's inner label is part of the button, not a
   // sibling); `name_from_content` then joins the names found inside.
   //
   // `is_override` marks a wrapper (a11y_label(...) and friends): the walker
   // applies its non-empty fields to the first node created in its subtree,
   // or makes a node for the wrapper itself when the subtree has none (an
   // image with a label).
   ////////////////////////////////////////////////////////////////////////////
   struct info
   {
      a11y::role              role = role::none;
      std::string             name;
      std::string             description;
      std::string             value;
      std::optional<double>   num_value;
      std::optional<double>   num_min;
      std::optional<double>   num_max;
      std::optional<double>   num_step;
      std::uint32_t           states = 0;
      std::uint32_t           actions = 0;
      a11y::live              live = live::off;
      std::string             id;            // stable id across rebuilds
      bool                    hidden = false;
      bool                    leaf = false;
      bool                    name_from_content = false;
      bool                    is_override = false;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Snapshot: the whole tree as a flat table, root first (pre-order)
   ////////////////////////////////////////////////////////////////////////////
   struct snapshot
   {
      node_id                 root = 0;
      node_id                 focus = 0;
      std::vector<node>       nodes;

      // Lookup by id. The index is rebuilt lazily when `nodes` changed size;
      // call reindex() after editing nodes in place.
      node const*             find(node_id id) const;
      void                    reindex() const;

   private:

      mutable std::unordered_map<node_id, std::size_t> _index;
   };

   // Changed / new nodes since `prev`. A node that disappeared needs no
   // entry: it is gone once no parent lists it among its children.
   struct update
   {
      std::vector<node>       nodes;
      node_id                 focus = 0;
      bool                    full = false;
      bool                    focus_changed = false;

      bool                    empty() const { return nodes.empty() && !focus_changed && !full; }
   };

   update                     diff(snapshot const* prev, snapshot const& next);

   ////////////////////////////////////////////////////////////////////////////
   // Output side
   ////////////////////////////////////////////////////////////////////////////
   class sink
   {
   public:

      virtual                 ~sink() = default;

      // false: no assistive technology is listening; the view skips the
      // work entirely.
      virtual bool            is_active() const { return true; }

      // `full` is the complete current tree (adapters that must answer an
      // activation from another thread keep a copy of it).
      virtual void            tree_changed(snapshot const& full, update const& delta) = 0;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Utilities
   ////////////////////////////////////////////////////////////////////////////
   char const*                role_name(role r);
   char const*                action_name(action a);
   std::optional<action>      action_from_name(std::string_view name);

   // Names as written by scripts and layout files: "button", "check_box"
   // (also "checkbox"), "group", "list_item", "text" (= label),
   // "progress" ... and the role_name() spellings ("check box").
   std::optional<role>        role_from_name(std::string_view name);

   // "focusable", "focused", "disabled", "checked", "selected", "expanded",
   // "read_only", "modal" -> the state bit; 0 when unknown.
   std::uint32_t              state_from_name(std::string_view name);

   // The string form of a node id used by dumps and by REPL commands:
   // the explicit id when there is one, else "#<hex>".
   std::string                id_string(node const& n);

   // One line approximating what a screen reader says for the node,
   // e.g. "Volume, slider, 75%".
   std::string                describe(node const& n);

   // JSON dump (flat: "nodes" array, children by id). See docs/accessibility.md.
   std::string                to_json(snapshot const& s);

   // What changed between two trees, as lines approximating what a screen
   // reader would say — a speech log for checking without one:
   //    [focus] Volume, slider, 75% — BGM level
   //    [value] Volume, 80%
   //    [state] Enable sound, check box, checked
   //    [polite] Settings saved
   // `prev` may be null (first tree: focus line only).
   std::vector<std::string>   speech_lines(snapshot const* prev, snapshot const& next);

   node_id                    hash_id(std::string_view s);
}

#endif

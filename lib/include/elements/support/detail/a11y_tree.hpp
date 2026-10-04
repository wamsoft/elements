/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_SUPPORT_DETAIL_A11Y_TREE_OCTOBER_4_2026)
#define ELEMENTS_SUPPORT_DETAIL_A11Y_TREE_OCTOBER_4_2026

#include <elements/support/a11y.hpp>
#include <functional>
#include <unordered_set>

namespace cycfi::elements
{
   class context;
   class element;
}

namespace cycfi::elements::a11y::detail
{
   // Element-tree walker behind view::a11y_snapshot() / view::a11y_perform().
   struct walk_input
   {
      element const*                      focus_leaf = nullptr;
      std::unordered_set<element const*>  focus_path;

      // Target mode: when a node with this id is reached, `on_target` runs
      // with the element and its live context, and the walk stops.
      node_id                             target = 0;
      std::function<void(context const&, element&)> on_target;
   };

   // Appends the nodes under `root_ctx` to `out`, as children of
   // out.nodes[parent]. Returns true when the target (if any) was reached.
   bool walk(context const& root_ctx, element& root, std::uint64_t seed,
      std::size_t parent, snapshot& out, walk_input const& in);
}

#endif

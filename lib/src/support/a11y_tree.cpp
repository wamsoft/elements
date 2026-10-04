/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/detail/a11y_tree.hpp>
#include <elements/element/composite.hpp>
#include <elements/element/indirect.hpp>
#include <elements/element/layer.hpp>
#include <elements/element/proxy.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/context.hpp>
#include <typeinfo>

namespace cycfi::elements::a11y::detail
{
   namespace
   {
      std::uint64_t mix(std::uint64_t a, std::uint64_t b)
      {
         a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
         return a;
      }

      // Seed for a child: structural position (index) plus the element's
      // type. Stable while the tree keeps its shape, and different for the
      // two places a shared element may appear.
      std::uint64_t child_seed(std::uint64_t parent, std::size_t ix, element const& e)
      {
         return mix(mix(parent, ix + 1), typeid(e).hash_code());
      }

      void apply(info& base, info const& ov)
      {
         if (ov.role != role::none)
            base.role = ov.role;
         if (!ov.name.empty())
            base.name = ov.name;
         if (!ov.description.empty())
            base.description = ov.description;
         if (!ov.value.empty())
            base.value = ov.value;
         if (ov.num_value)
            base.num_value = ov.num_value;
         if (ov.num_min)
            base.num_min = ov.num_min;
         if (ov.num_max)
            base.num_max = ov.num_max;
         if (ov.num_step)
            base.num_step = ov.num_step;
         base.states |= ov.states;
         base.actions |= ov.actions;
         if (ov.live != live::off)
            base.live = ov.live;
         if (!ov.id.empty())
            base.id = ov.id;
      }

      // Visit the children of `e` the way drawing does: composites through
      // their own iteration, proxies with prepare_subject (so margins and
      // alignment give the subject its real bounds), indirections through
      // the held element. Decks expose only the selected page; in a layer, a
      // modal child hides every layer below it. `f` returns false to stop.
      template <typename F>
      void for_children(context const& ctx, element& e, F&& f)
      {
         if (auto* d = dynamic_cast<deck_element*>(&e))
         {
            auto ix = d->selected();
            if (ix < d->size())
            {
               auto& c = d->at(ix);
               context cctx{ctx, &c, d->bounds_of(ctx, ix)};
               f(cctx, c, ix);
            }
            return;
         }
         if (auto* l = dynamic_cast<layer_element*>(&e))
         {
            // Index 0 is the topmost layer.
            std::size_t end = l->size();
            for (std::size_t ix = 0; ix != l->size(); ++ix)
            {
               auto& c = l->at(ix);
               context pctx{ctx, &c, l->bounds_of(ctx, ix)};
               info probe;
               c.accessible(pctx, probe);
               if (!probe.hidden && (probe.states & state::modal))
               {
                  end = ix + 1;
                  break;
               }
            }
            // Bottom first, so reading order follows stacking order.
            for (std::size_t k = end; k-- > 0;)
            {
               auto& c = l->at(k);
               context cctx{ctx, &c, l->bounds_of(ctx, k)};
               if (!f(cctx, c, k))
                  return;
            }
            return;
         }
         if (auto* c = dynamic_cast<composite_base*>(&e))
         {
            c->for_each_visible(ctx,
               [&](element& child, std::size_t ix, rect const& r)
               {
                  context cctx{ctx, &child, r};
                  return !f(cctx, child, ix);
               });
            return;
         }
         if (auto* p = dynamic_cast<proxy_base*>(&e))
         {
            context sctx{ctx, &p->subject(), ctx.bounds};
            p->prepare_subject(sctx);
            f(sctx, p->subject(), 0);
            p->restore_subject(sctx);
            return;
         }
         if (auto* i = dynamic_cast<indirect_base*>(&e))
         {
            context sctx{ctx, &i->get(), ctx.bounds};
            f(sctx, i->get(), 0);
         }
      }

      class walker
      {
      public:

         walker(snapshot& out, walk_input const& in)
          : _out(out)
          , _in(in)
         {
            for (auto const& n : out.nodes)
               _used.insert(n.id);
         }

         bool done() const { return _done; }

         // Returns true when a node was created in the subtree (which is
         // what consumes a pending override).
         bool visit(context const& ctx, element& e, std::size_t parent,
            std::uint64_t seed, info const* pending, rect const& clip)
         {
            if (_done)
               return false;

            info i;
            e.accessible(ctx, i);
            if (i.hidden)
               return false;

            // Device bounds, cut down by every ancestor's: an element may be
            // laid out wider than what shows (the text inside a scrolling
            // input box), and AT should get what is on screen.
            rect here = clip_to(clip, device_bounds(ctx));

            if (i.is_override)
            {
               info merged = pending ? *pending : info{};
               apply(merged, i);
               merged.is_override = true;
               bool made = false;
               for_children(ctx, e,
                  [&](context const& cctx, element& child, std::size_t ix)
                  {
                     made |= visit(cctx, child, parent, child_seed(seed, ix, child),
                        made ? nullptr : &merged, here);
                     return !_done;
                  });
               if (!made && !_done && (merged.role != role::none || !merged.name.empty()))
               {
                  // Nothing inside became a node (an image, a decoration):
                  // the wrapper stands for it.
                  info self = merged;
                  if (self.role == role::none)
                     self.role = role::image;
                  self.leaf = true;
                  make_node(ctx, e, self, parent, seed, here);
                  made = true;
               }
               return made;
            }

            if (i.role != role::none)
            {
               if (pending)
                  apply(i, *pending);
               if (i.leaf && i.name.empty() && i.name_from_content)
                  collect_names(ctx, e, i.name);
               // A label with no text is decoration.
               if (i.role == role::label && i.name.empty() && i.value.empty())
                  return false;
               auto ix = make_node(ctx, e, i, parent, seed, here);
               if (!i.leaf && !_done)
               {
                  for_children(ctx, e,
                     [&](context const& cctx, element& child, std::size_t cix)
                     {
                        visit(cctx, child, ix, child_seed(seed, cix, child), nullptr, here);
                        return !_done;
                     });
               }
               return true;
            }

            bool made = false;
            for_children(ctx, e,
               [&](context const& cctx, element& child, std::size_t ix)
               {
                  made |= visit(cctx, child, parent, child_seed(seed, ix, child),
                     made ? nullptr : pending, here);
                  return !_done;
               });
            return made;
         }

      private:

         void collect_names(context const& ctx, element& e, std::string& out)
         {
            for_children(ctx, e,
               [&](context const& cctx, element& child, std::size_t)
               {
                  info ci;
                  child.accessible(cctx, ci);
                  if (ci.hidden)
                     return true;
                  if (!ci.name.empty())
                  {
                     if (!out.empty())
                        out += ' ';
                     out += ci.name;
                     return true;
                  }
                  collect_names(cctx, child, out);
                  return true;
               });
         }

         static rect clip_to(rect const& clip, rect const& r)
         {
            auto x = intersection(clip, r);
            if (x.right < x.left)
               x.right = x.left;
            if (x.bottom < x.top)
               x.bottom = x.top;
            return x;
         }

         static rect device_bounds(context const& ctx)
         {
            auto tl = ctx.canvas.user_to_device(ctx.bounds.top_left());
            auto br = ctx.canvas.user_to_device(ctx.bounds.bottom_right());
            return {tl.x, tl.y, br.x, br.y};
         }

         std::size_t make_node(context const& ctx, element& e, info const& i,
            std::size_t parent, std::uint64_t seed, rect const& bounds)
         {
            node n;
            n.role = i.role;
            n.name = i.name;
            n.description = i.description;
            n.value = i.value;
            n.num_value = i.num_value;
            n.num_min = i.num_min;
            n.num_max = i.num_max;
            n.num_step = i.num_step;
            n.states = i.states;
            n.actions = i.actions;
            n.live = i.live;

            if (e.wants_focus())
               n.states |= state::focusable;
            if (!ctx.enabled || !e.is_enabled())
               n.states |= state::disabled;

            n.bounds = bounds;

            if (!i.id.empty())
            {
               n.id = hash_id(i.id);
               n.debug_id = i.id;
            }
            else
            {
               n.id = seed ? seed : 1;
            }
            while (_used.count(n.id))
               n.id = mix(n.id, 0x5bd1e995);
            _used.insert(n.id);

            if (_in.focus_path.count(&e) && (i.leaf || &e == _in.focus_leaf))
            {
               n.states |= state::focused;
               _out.focus = n.id;
            }

            auto id = n.id;
            _out.nodes[parent].children.push_back(id);
            _out.nodes.push_back(std::move(n));
            auto ix = _out.nodes.size() - 1;

            if (_in.target && id == _in.target && _in.on_target)
            {
               _done = true;
               _in.on_target(ctx, e);
            }
            return ix;
         }

         snapshot&                     _out;
         walk_input const&             _in;
         std::unordered_set<node_id>   _used;
         bool                          _done = false;
      };
   }

   bool walk(context const& root_ctx, element& root, std::uint64_t seed,
      std::size_t parent, snapshot& out, walk_input const& in)
   {
      walker w{out, in};
      w.visit(root_ctx, root, parent, seed, nullptr, out.nodes[parent].bounds);
      return w.done();
   }
}

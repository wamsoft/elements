/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/view.hpp>
#include <elements/window.hpp>
#include <elements/element/button.hpp>
#include <elements/element/composite.hpp>
#include <elements/element/focus.hpp>
#include <elements/element/indirect.hpp>
#include <elements/element/proxy.hpp>
#include <elements/support/context.hpp>
#include <elements/support/detail/scratch_context.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

 namespace cycfi::elements
 {
   // Populate the default gamepad → key bindings on a freshly
   // constructed view. D-Pad buttons are intentionally *not* bound here;
   // they feed the axis-mode machinery instead so dpad_mode() applies
   // uniformly. Face buttons get the conventional accept / cancel /
   // focus-back / focus-next mapping.
   static void install_default_pad_key_bindings(view& v)
   {
      using pb = pad_button;
      v.bind_pad_button(pb::a, key_code::enter);
      v.bind_pad_button(pb::b, key_code::escape);
      v.bind_pad_button(pb::x, key_code::tab,   mod_shift);
      v.bind_pad_button(pb::y, key_code::tab);
   }

   view::view(extent size_)
    : base_view(size_)
    , _main_element(make_scaled_content())
   {
      install_default_pad_key_bindings(*this);
   }

   view::view(host_view_handle h)
    : base_view(h)
    , _main_element(make_scaled_content())
   {
      install_default_pad_key_bindings(*this);
   }

   view::view(window& win)
    : base_view(win.host())
    , _main_element(make_scaled_content())
   {
      install_default_pad_key_bindings(*this);
      on_change_limits = [&win](view_limits limits_)
      {
         win.limits(limits_);
      };
      win.limits(_current_limits);
   }

   view::~view()
   {
      _tasks.stop();
   }

   void view::set_limits()
   {
      if (_content.empty())
         return;

      // Use a scratch context for off-screen measurement
      static detail::scratch_context scratch;
      canvas cnv{scratch.buffer(), 4, 4};

      // Update the limits and constrain the window size to the limits
      basic_context bctx{*this, cnv};
      auto limits_ = _main_element.limits(bctx);
      if (limits_.min != _current_limits.min || limits_.max != _current_limits.max)
      {
         _current_limits = limits_;
         if (on_change_limits)
            on_change_limits(limits_);
      }
   }

   void view::draw(canvas& cnv)
   {
      if (_content.empty())
         return;

      // Update the limits and constrain the window size to the limits
      set_limits();

      auto size_ = size();
      rect subj_bounds = {0, 0, size_.x, size_.y};
      context ctx{*this, cnv, &_main_element, subj_bounds};

      // layout the subject only if the window bounds changes
      if (subj_bounds != _current_bounds)
      {
         _current_bounds = subj_bounds;
         _main_element.layout(ctx);
      }

      // draw the subject
      _main_element.draw(ctx);
   }

   namespace
   {
      template <typename F, typename This>
      void with_context_do(F f, This& self, rect _current_bounds)
      {
         // Use a scratch context for off-screen operations
         static detail::scratch_context scratch;
         canvas cnv{scratch.buffer(), 4, 4};
         context ctx{self, cnv, &self.main_element(), _current_bounds};

         f(ctx, self.main_element());
      }
   }

   void view::layout()
   {
      if (_current_bounds.is_empty())
         return;

      with_context_do(
         [](auto const& ctx, auto& _main_element) { _main_element.layout(ctx); },
         *this, _current_bounds
      );

      refresh();
   }

   void view::layout(element& element)
   {
      if (_current_bounds.is_empty())
         return;

      with_context_do(
         [](auto const& ctx, auto& _main_element) { _main_element.layout(ctx); },
         *this, _current_bounds
      );

      refresh(element);
   }

   float view::scale() const
   {
      return _main_element.scale();
   }

   void view::scale(float val)
   {
      _main_element.scale(val);
      refresh();
   }

   void view::refresh()
   {
      // Allow refresh to be called from another thread
      _tasks.post(
         [this]()
         {
            base_view::refresh();
         }
      );
   }

   void view::refresh(rect area)
   {
      // Allow refresh to be called from another thread
      _tasks.post(
         [this, area]()
         {
            base_view::refresh(area);
         }
      );
   }

   void view::refresh(context const& ctx, rect area)
   {
      auto tl = ctx.canvas.user_to_device(area.top_left());
      auto br = ctx.canvas.user_to_device(area.bottom_right());
      refresh({tl.x, tl.y, br.x, br.y});
   }

   void view::refresh(element& element, int outward)
   {
      if (_current_bounds.is_empty())
         return;

      _tasks.post(
         [this, &element, outward]()
         {
            with_context_do(
               [&element, outward](auto const& ctx, auto& _main_element)
               {
                  _main_element.refresh(ctx, element, outward);
               },
               *this, _current_bounds
            );
         }
      );
   }

   void view::refresh(context const& ctx, int outward)
   {
      context const* ctx_ptr = &ctx;
      while (outward > 0 && ctx_ptr)
      {
         --outward;
         ctx_ptr = ctx_ptr->parent;
      }
      if (ctx_ptr)
      {
         auto tl = ctx.canvas.user_to_device(ctx_ptr->bounds.top_left());
         auto br = ctx.canvas.user_to_device(ctx_ptr->bounds.bottom_right());
         rect r{tl.x, tl.y, br.x, br.y};
         // element_bounds() 実行中は refresh せず矩形を捕まえるだけ。
         // 併せて要素の自然サイズも拾う (bounds を超えていれば、 その分だけ
         // ダーティ矩形を広げないと描画のはみ出しが消え残る)。
         if (_capturing_bounds)
         {
            _captured_bounds = r;
            if (ctx_ptr->element)
               _captured_natural = ctx_ptr->element->limits(ctx).min;
            return;
         }
         refresh(r);
      }
   }

   bool view::element_bounds(element& e, rect& out, extent& natural)
   {
      if (_current_bounds.is_empty())
         return false;
      _capturing_bounds = true;
      _captured_bounds = {};
      _captured_natural = {0, 0};
      with_context_do(
         [&e](auto const& ctx, auto& _main_element)
         {
            _main_element.refresh(ctx, e, 0);
         },
         *this, _current_bounds
      );
      _capturing_bounds = false;
      if (_captured_bounds.is_empty())
         return false;
      out = _captured_bounds;
      natural = _captured_natural;
      return true;
   }

   void view::click(mouse_button btn)
   {
      _current_button = btn;
      if (_content.empty())
         return;

      with_context_do(
         [btn, this](auto const& ctx, auto& _main_element)
         {
            if (_main_element.click(ctx, btn))
               _is_focus = _main_element.focus();
            else if (btn.down)
               elements::relinquish_focus(_content, ctx);
            refresh(_main_element);
         },
         *this, _current_bounds
      );
   }

   void view::drag(mouse_button btn)
   {
      _current_button = btn;
      if (_content.empty())
         return;

      with_context_do(
         [btn](auto const& ctx, auto& _main_element)
         {
            _main_element.drag(ctx, btn);
         },
         *this, _current_bounds
      );
   }

   void view::cursor(point p, cursor_tracking status)
   {
      if (_content.empty())
         return;

      with_context_do(
         [p, status](auto const& ctx, auto& _main_element)
         {
            if (!_main_element.cursor(ctx, p, status))
               set_cursor(cursor_type::arrow);
         },
         *this, _current_bounds
      );
   }

   void view::scroll(point dir, point p)
   {
      if (_content.empty())
         return;

      with_context_do(
         [dir, p](auto const& ctx, auto& _main_element)
         {
            _main_element.scroll(ctx, dir, p);
         },
         *this, _current_bounds
      );
   }

   namespace
   {
      // Depth-first descent: if 'target' lives anywhere under 'current',
      // call focus(index) on every composite_base on the path so that a
      // subsequent begin_focus(restore_previous) walk reaches the target.
      bool descend_set_focus(element& current, element const* target)
      {
         if (&current == target)
            return true;

         if (auto* c = dynamic_cast<composite_base*>(&current))
         {
            for (std::size_t i = 0; i != c->size(); ++i)
            {
               if (descend_set_focus(c->at(i), target))
               {
                  c->focus(i);
                  return true;
               }
            }
            return false;
         }

         if (auto* p = dynamic_cast<proxy_base*>(&current))
            return descend_set_focus(p->subject(), target);

         // indirect<reference<X>> / indirect<shared_element<X>> wrappers
         // (used by view's main element and by link()/hold()) are NOT
         // proxy_base — they delegate via indirect_base::get().
         if (auto* i = dynamic_cast<indirect_base*>(&current))
            return descend_set_focus(i->get(), target);

         return false;
      }

      struct focusable_entry
      {
         element* el;
         rect     bounds;
      };

      // Walk through any number of nested proxy_base layers until we find
      // a composite_base. Used to decide whether a proxy is just wrapping a
      // composite (descend through it) or is itself the focus-owning leaf
      // (e.g. basic_button wraps a styler, with no composite underneath).
      bool proxy_chain_has_composite(element* e)
      {
         while (e)
         {
            if (dynamic_cast<composite_base*>(e))
               return true;
            if (auto* p = dynamic_cast<proxy_base*>(e))
            {
               e = &p->subject();
               continue;
            }
            if (auto* i = dynamic_cast<indirect_base*>(e))
            {
               e = &i->get();
               continue;
            }
            return false;
         }
         return false;
      }

      // Collect every currently-visible focusable element together with the
      // bounds it occupies in 'ctx'. Used for 2D directional focus
      // navigation. We treat as a focusable any proxy / leaf that returns
      // wants_focus() == true and whose subject chain does not bottom out
      // in another composite (which would mean it is just a wrapper).
      void collect_focusables(
         context const& ctx, element& current,
         std::vector<focusable_entry>& out)
      {
         if (auto* c = dynamic_cast<composite_base*>(&current))
         {
            c->for_each_visible(ctx,
               [&](element& child, std::size_t /*ix*/, rect const& r)
               {
                  context cctx{ctx, &child, r};
                  collect_focusables(cctx, child, out);
                  return false;
               });
            return;
         }

         if (auto* p = dynamic_cast<proxy_base*>(&current))
         {
            // proxy が wants_control() == false の場合は subtree ごと無視。
            // hidable が is_hidden 時に true を返さなくなるので、 非表示 pane
            // 内の widget を arrow nav / TAB cycle に拾わなくする。
            // 注意: wants_focus ではなく wants_control を見ること。
            // focus_row_element (= labeled_row のベース) のように
            // 「自分自身は focusable ではないが children は focusable」な
            // 装飾系 proxy は wants_control=true / wants_focus=false で
            // 区別される。 ここで wants_focus を見ると装飾系ごと脱落する。
            if (!current.wants_control())
               return;
            // focus_unit_element: subtree 全体を 1 つの focusable として
            // 扱う (下の composite には降りない)。
            if (dynamic_cast<focus_unit_element*>(&current))
            {
               out.push_back({&current, ctx.bounds});
               return;
            }
            if (proxy_chain_has_composite(&p->subject()))
            {
               context sctx{ctx, &p->subject(), ctx.bounds};
               collect_focusables(sctx, p->subject(), out);
            }
            else if (current.wants_focus())
            {
               out.push_back({&current, ctx.bounds});
            }
            else
            {
               context sctx{ctx, &p->subject(), ctx.bounds};
               collect_focusables(sctx, p->subject(), out);
            }
            return;
         }

         // indirect / reference / shared_element wrappers: always descend.
         // wants_focus / focus / key are already forwarded to the held
         // element, so for collection we should look at that element.
         if (auto* i = dynamic_cast<indirect_base*>(&current))
         {
            context sctx{ctx, &i->get(), ctx.bounds};
            collect_focusables(sctx, i->get(), out);
            return;
         }

         if (current.wants_focus())
            out.push_back({&current, ctx.bounds});
      }

      // Walk down the focus chain from 'current' and append every element
      // on the path. composite_base steps via focus_index(); proxy_base
      // steps via subject(). The chain terminates at the first leaf or at
      // a composite with no focus.
      void walk_focus_path(element& current, std::vector<element*>& path)
      {
         path.push_back(&current);
         if (auto* c = dynamic_cast<composite_base*>(&current))
         {
            int idx = c->focus_index();
            if (idx >= 0 && idx < int(c->size()))
               walk_focus_path(c->at(idx), path);
            return;
         }
         if (auto* p = dynamic_cast<proxy_base*>(&current))
         {
            walk_focus_path(p->subject(), path);
            return;
         }
         if (auto* i = dynamic_cast<indirect_base*>(&current))
         {
            walk_focus_path(i->get(), path);
            return;
         }
      }

      enum class arrow_dir { left, right, up, down };

      // Pick the focusable that best matches a 2D move in 'dir' starting
      // from 'cur_bounds'. Score = distance along the move axis plus a
      // heavy multiplier for misalignment on the perpendicular axis, so a
      // "Right" press prefers a widget in the same horizontal band.
      element* pick_directional(
         std::vector<focusable_entry> const& list,
         element const* current,
         rect const& cur_bounds,
         arrow_dir dir)
      {
         point cur_c = center_point(cur_bounds);
         element* best = nullptr;
         float best_score = std::numeric_limits<float>::max();

         for (auto const& f : list)
         {
            if (f.el == current)
               continue;

            point c = center_point(f.bounds);
            float dx = c.x - cur_c.x;
            float dy = c.y - cur_c.y;

            // Require the candidate to be unambiguously on the requested
            // side (small epsilon to ignore co-aligned widgets).
            bool in_dir = false;
            switch (dir)
            {
               case arrow_dir::left:  in_dir = dx < -0.5f; break;
               case arrow_dir::right: in_dir = dx >  0.5f; break;
               case arrow_dir::up:    in_dir = dy < -0.5f; break;
               case arrow_dir::down:  in_dir = dy >  0.5f; break;
            }
            if (!in_dir)
               continue;

            float primary, secondary;
            if (dir == arrow_dir::left || dir == arrow_dir::right)
            {
               primary   = std::abs(dx);
               secondary = std::abs(dy);
            }
            else
            {
               primary   = std::abs(dy);
               secondary = std::abs(dx);
            }
            float score = primary + secondary * 4.0f;

            if (score < best_score)
            {
               best_score = score;
               best = f.el;
            }
         }
         return best;
      }

      // Entry point for arrow navigation when nothing is focused yet:
      // pick the widget furthest along 'dir' (Left -> leftmost, Right ->
      // rightmost, Up -> topmost, Down -> bottommost). Ties on the primary
      // axis are broken by the perpendicular axis, then by collection
      // order, so the result is stable.
      element* pick_extreme(
         std::vector<focusable_entry> const& list,
         arrow_dir dir)
      {
         element* best = nullptr;
         float best_primary = 0.0f;
         float best_cross = 0.0f;
         for (auto const& f : list)
         {
            float const cx = (f.bounds.left + f.bounds.right) * 0.5f;
            float const cy = (f.bounds.top + f.bounds.bottom) * 0.5f;
            // Score so that "larger is better" in every direction.
            float primary, cross;
            switch (dir)
            {
               case arrow_dir::left:  primary = -cx; cross = -cy; break;
               case arrow_dir::right: primary =  cx; cross = -cy; break;
               case arrow_dir::up:    primary = -cy; cross = -cx; break;
               default:               primary =  cy; cross = -cx; break;
            }
            if (!best
               || primary > best_primary
               || (primary == best_primary && cross > best_cross))
            {
               best = f.el;
               best_primary = primary;
               best_cross = cross;
            }
         }
         return best;
      }

      // True when 'e' is 'top' itself or lives on top's proxy / indirect
      // subject chain. collect_focusables stores the outermost wrapper
      // (hsize / vsize etc.) while hosts registering focus_nav_override
      // targets typically hold the inner widget (the shared button), so
      // matching has to look through the chain.
      bool entry_contains(element* top, element const* e)
      {
         element* cur = top;
         while (cur)
         {
            if (cur == e)
               return true;
            if (auto* p = dynamic_cast<proxy_base*>(cur))
            {
               cur = &p->subject();
               continue;
            }
            if (auto* i = dynamic_cast<indirect_base*>(cur))
            {
               cur = &i->get();
               continue;
            }
            break;
         }
         return false;
      }

      // Wrap-around fallback for pick_directional: when a move in 'dir'
      // finds no candidate (we are at the edge), pick the candidate
      // nearest the *opposite* edge — i.e. moving Down past the bottom
      // lands on the topmost widget — biased toward staying in the same
      // perpendicular band, mirroring pick_directional's scoring.
      //
      // Candidates whose center sits at the same primary-axis position as
      // the current widget are ignored: in a single-column (or single-row)
      // layout every candidate ties on the primary axis, so the score
      // degenerates to the perpendicular distance and a Left/Right press
      // would jump to the vertical neighbour instead. There is nothing to
      // wrap to on that axis — treat it as a no-op.
      element* pick_wrapped(
         std::vector<focusable_entry> const& list,
         element const* current,
         rect const& cur_bounds,
         arrow_dir dir)
      {
         point cur_c = center_point(cur_bounds);
         element* best = nullptr;
         float best_score = std::numeric_limits<float>::max();

         for (auto const& f : list)
         {
            if (f.el == current)
               continue;

            point c = center_point(f.bounds);
            // Moving Down past the bottom wraps to the topmost candidate
            // (minimize c.y); Up wraps to the bottommost (minimize -c.y);
            // Right wraps to the leftmost; Left wraps to the rightmost.
            float primary, secondary;
            switch (dir)
            {
               case arrow_dir::left:  primary = -c.x; secondary = std::abs(c.y - cur_c.y); break;
               case arrow_dir::right: primary =  c.x; secondary = std::abs(c.y - cur_c.y); break;
               case arrow_dir::up:    primary = -c.y; secondary = std::abs(c.x - cur_c.x); break;
               case arrow_dir::down:  primary =  c.y; secondary = std::abs(c.x - cur_c.x); break;
               default: return nullptr;
            }
            bool const horizontal = dir == arrow_dir::left || dir == arrow_dir::right;
            float const primary_delta = horizontal ? c.x - cur_c.x : c.y - cur_c.y;
            if (std::abs(primary_delta) <= 0.5f)   // same threshold as pick_directional
               continue;
            float score = primary + secondary * 4.0f;
            if (score < best_score)
            {
               best_score = score;
               best = f.el;
            }
         }
         return best;
      }
   }

   void view::arrow_focus_navigation(bool on)
   {
      _arrow_focus_nav = on;
   }

   bool view::arrow_focus_navigation() const
   {
      return _arrow_focus_nav;
   }

   void view::arrow_focus_wrap(bool on)
   {
      _arrow_focus_wrap = on;
   }

   void view::focus_nav_override(focus_nav_override_function f)
   {
      _focus_nav_override = std::move(f);
   }

   bool view::arrow_focus_wrap() const
   {
      return _arrow_focus_wrap;
   }

   void view::focus_skip_disabled(bool on)
   {
      _focus_skip_disabled = on;
   }

   void view::arrow_focus_enter_directional(bool on)
   {
      _arrow_focus_enter_dir = on;
   }

   bool view::arrow_focus_enter_directional() const
   {
      return _arrow_focus_enter_dir;
   }

   bool view::focus_skip_disabled() const
   {
      return _focus_skip_disabled;
   }

   void view::hover_focus(bool on)
   {
      _hover_focus = on;
   }

   bool view::hover_focus() const
   {
      return _hover_focus;
   }

   bool view::focused_hot_point(point& out)
   {
      if (_content.empty())
         return false;

      // Collect the current focus chain (top-down).
      std::vector<element*> path;
      with_context_do(
         [&path](auto const& /*ctx*/, auto& _main_element)
         {
            walk_focus_path(_main_element, path);
         },
         *this, _current_bounds
      );

      // The shallowest custom hot point wins (a wrapping group knows
      // better than its inner leaf); fall back to the deepest
      // wants_focus() leaf with the default center.
      element* target = nullptr;
      for (element* e : path)
         if (e->has_custom_focus_hot_point())
         {
            target = e;
            break;
         }
      if (!target)
      {
         for (element* e : path)
            if (e->wants_focus())
               target = e;
      }
      if (!target)
         return false;

      bool ok = false;
      in_context_do(*target,
         [&ok, &out, target](context const& ectx)
         {
            out = target->focus_hot_point(ectx);
            ok = true;
         }
      );
      return ok;
   }

   void view::focus(element& e)
   {
      // Address the element by raw pointer; it lives in the view tree for
      // the lifetime of this deferred task. Posted (rather than run inline)
      // so it is safe to call from inside an event dispatch — e.g. from
      // basic_button::cursor() while the cursor walk is still in progress.
      element* ep = &e;
      _tasks.post(
         [this, ep]()
         {
            if (_content.empty())
               return;
            // Refresh only the outgoing and incoming focus elements — a
            // full-view refresh here defeats partial redraw on hosts that
            // track damage rects (the focus frames are all that changed).
            std::vector<element*> prev_path;
            walk_focus_path(_main_element, prev_path);
            _main_element.end_focus();
            if (descend_set_focus(_main_element, ep))
            {
               _main_element.begin_focus(element::focus_request::restore_previous);
               if (!prev_path.empty())
                  refresh(*prev_path.back());
               refresh(*ep);
               _is_focus = _main_element.focus();
            }
         }
      );
   }

   void view::focus(element_ptr e)
   {
      if (!e)
         return;
      _tasks.post(
         [this, e]()
         {
            // Bail out cleanly if the view already shut down (task_queue
            // stopped during ~view).
            if (_content.empty())
               return;

            // Targeted refresh — see focus(element&) above.
            std::vector<element*> prev_path;
            walk_focus_path(_main_element, prev_path);
            _main_element.end_focus();
            if (descend_set_focus(_main_element, e.get()))
            {
               _main_element.begin_focus(element::focus_request::restore_previous);
               if (!prev_path.empty())
                  refresh(*prev_path.back());
               refresh(*e);
               _is_focus = _main_element.focus();
            }
         }
      );
   }

   bool view::key(key_info const& k)
   {
      if (_content.empty())
         return false;

      // Key shortcut takes priority over normal dispatch. Only fires on
      // press / repeat — release events don't trigger shortcuts.
      if (k.action == key_action::press || k.action == key_action::repeat)
      {
         auto it = _key_shortcuts.find({k.key, k.modifiers});
         if (it != _key_shortcuts.end())
         {
            auto const& t = it->second;
            if (t.force || !focus_consumes_text())
            {
               fire_shortcut(t);
               return true;
            }
         }
      }

      bool handled = false;
      with_context_do(
         [k, &handled](auto const& ctx, auto& _main_element)
         {
             handled = _main_element.key(ctx, k);
         },
         *this, _current_bounds
      );

      // Arrow-based 2D focus navigation, opt-in via
      // view::arrow_focus_navigation(true). Runs only when the focused
      // widget did NOT consume the arrow itself (so slider / dial /
      // thumbwheel keep ownership of arrow keys for value adjustment).
      if (!handled
          && _arrow_focus_nav
          && (k.action == key_action::press || k.action == key_action::repeat))
      {
         arrow_dir d;
         bool is_arrow = true;
         switch (k.key)
         {
            case key_code::left:  d = arrow_dir::left;  break;
            case key_code::right: d = arrow_dir::right; break;
            case key_code::up:    d = arrow_dir::up;    break;
            case key_code::down:  d = arrow_dir::down;  break;
            default: is_arrow = false; d = arrow_dir::left; break;
         }
         if (is_arrow)
         {
            bool const wrap = _arrow_focus_wrap;
            bool const skip_disabled = _focus_skip_disabled;
            bool const enter_directional = _arrow_focus_enter_dir;
            auto const& nav_override = _focus_nav_override;
            with_context_do(
               [d, wrap, skip_disabled, enter_directional, &nav_override,
                &handled]
               (auto const& ctx, auto& _main_element)
               {
                  std::vector<focusable_entry> list;
                  collect_focusables(ctx, _main_element, list);
                  if (skip_disabled)
                     list.erase(
                        std::remove_if(list.begin(), list.end(),
                           [](focusable_entry const& f)
                           { return !f.el->is_enabled(); }),
                        list.end());
                  if (list.empty())
                     return;

                  // Find which entry in 'list' currently owns the focus by
                  // walking the focus chain top-down and matching against
                  // collected entries. composite_base::focus() only returns
                  // its immediate child, so a deep button is never directly
                  // returned — we have to scan the whole path.
                  std::vector<element*> path;
                  walk_focus_path(_main_element, path);

                  element* cur_collected = nullptr;
                  rect     cur_bounds;
                  for (element* pe : path)
                  {
                     for (auto const& f : list)
                        if (f.el == pe)
                        {
                           cur_collected = pe;
                           cur_bounds = f.bounds;
                           break;
                        }
                     if (cur_collected)
                        break;
                  }

                  element* target = nullptr;
                  if (!cur_collected)
                  {
                     // No matching focus yet. Legacy behavior takes the
                     // first focusable; with enter_directional the arrow
                     // instead lands on the widget furthest in the pressed
                     // direction (Right -> rightmost, and so on).
                     target = enter_directional
                        ? pick_extreme(list, d)
                        : list.front().el;
                  }
                  else
                  {
                     // Declarative override first (JSON "focus_nav" etc.).
                     // The collected entry is the outermost wrapper, while
                     // the host maps inner widgets — query the override
                     // down the proxy chain, then resolve the returned
                     // (possibly inner) element back to a collected entry.
                     // Falls back to the geometric pick when unmapped.
                     if (nav_override)
                     {
                        element* o = nullptr;
                        element* c = cur_collected;
                        while (c)
                        {
                           if ((o = nav_override(c, static_cast<int>(d))))
                              break;
                           if (auto* p = dynamic_cast<proxy_base*>(c))
                           {
                              c = &p->subject();
                              continue;
                           }
                           if (auto* i = dynamic_cast<indirect_base*>(c))
                           {
                              c = &i->get();
                              continue;
                           }
                           break;
                        }
                        if (o)
                        {
                           for (auto const& f : list)
                              if (entry_contains(f.el, o))
                              {
                                 target = f.el;
                                 break;
                              }
                        }
                     }
                     if (!target)
                     {
                        target = pick_directional(
                           list, cur_collected, cur_bounds, d);
                        if (!target && wrap)
                           target = pick_wrapped(
                              list, cur_collected, cur_bounds, d);
                     }
                  }

                  if (!target || target == cur_collected)
                     return;

                  // Refresh only the outgoing and incoming focus rects — a
                  // whole-view refresh defeats partial redraw on hosts that
                  // track damage rects. The bounds come from the collected
                  // list (no tree walk / re-measure). Inflate a little for
                  // focus frames drawn around the widget.
                  rect target_bounds;
                  for (auto const& f : list)
                     if (f.el == target)
                     {
                        target_bounds = f.bounds;
                        break;
                     }
                  auto inflate =
                     [](rect r)
                     {
                        r.left -= 8; r.top -= 8;
                        r.right += 8; r.bottom += 8;
                        return r;
                     };
                  _main_element.end_focus();
                  if (descend_set_focus(_main_element, target))
                  {
                     _main_element.begin_focus(
                        element::focus_request::restore_previous);
                     handled = true;
                     if (cur_collected)
                        ctx.view.refresh(ctx, inflate(cur_bounds));
                     ctx.view.refresh(ctx, inflate(target_bounds));
                  }
               },
               *this, _current_bounds
            );
            _is_focus = handled;
         }
      }

      // Wrap focus around when Tab / Shift+Tab walks off either end.
      // composite_base::key returns false when no further sibling wants
      // focus; here we reset the chain and pick the first / last focusable
      // from the opposite end so the user perceives a loop.
      if (!handled
          && (k.action == key_action::press || k.action == key_action::repeat)
          && k.key == key_code::tab)
      {
         bool reverse = (k.modifiers & mod_shift) != 0;
         with_context_do(
            [reverse, &handled](auto const& ctx, auto& _main_element)
            {
               // Targeted refresh — see the arrow-nav branch above.
               std::vector<element*> prev_path;
               walk_focus_path(_main_element, prev_path);
               _main_element.end_focus();
               _main_element.begin_focus(
                  reverse
                     ? element::focus_request::from_bottom
                     : element::focus_request::from_top
               );
               handled = _main_element.focus() != nullptr;
               if (handled)
               {
                  if (!prev_path.empty())
                     ctx.view.refresh(*prev_path.back());
                  std::vector<element*> new_path;
                  walk_focus_path(_main_element, new_path);
                  if (!new_path.empty())
                     ctx.view.refresh(*new_path.back());
               }
            },
            *this, _current_bounds
         );
         _is_focus = handled;
      }

      return handled;
   }

   bool view::text(text_info const& info)
   {
      if (_content.empty())
         return false;

      bool handled = false;
      with_context_do(
         [info, &handled](auto const& ctx, auto& _main_element)
         {
             handled = _main_element.text(ctx, info);
         },
         *this, _current_bounds
      );
      return handled;
   }

   // -------------------------------------------------------------------
   // Pad-button → key binding
   // -------------------------------------------------------------------
   void view::bind_pad_button(pad_button btn, key_code key, int mods)
   {
      _pad_key_bindings[btn] = {key, mods};
   }

   void view::unbind_pad_button(pad_button btn)
   {
      _pad_key_bindings.erase(btn);
   }

   // -------------------------------------------------------------------
   // Shortcut registry
   // -------------------------------------------------------------------
   void view::bind_shortcut(key_info key, element_ptr target, bool force)
   {
      _key_shortcuts[{key.key, key.modifiers}] = {target, {}, force};
   }

   void view::bind_shortcut(pad_button btn, element_ptr target, bool force)
   {
      _pad_shortcuts[btn] = {target, {}, force};
   }

   void view::bind_shortcut(key_info key, std::function<void()> cb, bool force)
   {
      _key_shortcuts[{key.key, key.modifiers}] = {{}, std::move(cb), force};
   }

   void view::bind_shortcut(pad_button btn, std::function<void()> cb, bool force)
   {
      _pad_shortcuts[btn] = {{}, std::move(cb), force};
   }

   void view::unbind_shortcut(key_info key)
   {
      _key_shortcuts.erase({key.key, key.modifiers});
   }

   void view::unbind_shortcut(pad_button btn)
   {
      _pad_shortcuts.erase(btn);
   }

   // Returns true if any element along the current focus chain has
   // consumes_text() == true. Used to decide whether to suppress
   // non-forced shortcuts.
   bool view::focus_consumes_text()
   {
      bool result = false;
      with_context_do(
         [&result](auto const& /*ctx*/, auto& _main_element)
         {
            std::vector<element*> path;
            walk_focus_path(_main_element, path);
            for (element* e : path)
               if (e->consumes_text())
               {
                  result = true;
                  break;
               }
         },
         *this, _current_bounds
      );
      return result;
   }

   void view::fire_shortcut(shortcut_target const& t)
   {
      // Deferred so callers (key/pad event) can return cleanly first.
      _tasks.post(
         [this, t]()
         {
            if (t.callback)
            {
               t.callback();
               base_view::refresh();
               return;
            }
            auto el = t.target.lock();
            if (!el)
               return;
            // Run activate() with the element's own context. We pass a
            // lambda capturing the raw pointer; lifetime is held via the
            // shared_ptr we just locked.
            element* raw = el.get();
            in_context_do(*raw,
               [raw](context const& ectx)
               {
                  if (auto* btn = dynamic_cast<basic_button*>(raw))
                     btn->activate(ectx);
                  // Non-button targets are currently no-ops. Future
                  // widgets can opt in by exposing their own activate.
               }
            );
            base_view::refresh();
         }
      );
   }

   bool view::pad_button_event(pad_button_info info)
   {
      // 1. Pad-button shortcut takes priority. Fires on press only.
      if (info.down)
      {
         auto sit = _pad_shortcuts.find(info.button);
         if (sit != _pad_shortcuts.end())
         {
            auto const& t = sit->second;
            if (t.force || !focus_consumes_text())
            {
               fire_shortcut(t);
               return true;
            }
         }
      }

      // 2. D-Pad buttons feed the axis machinery (so dpad_mode applies).
      //    They do NOT participate in pad→key synthesis.
      auto feed_dpad_axis = [this](pad_axis ax, int sign, bool down)
      {
         auto& st = _axis_states[static_cast<int>(ax)];
         if (down)
         {
            st.current = float(sign);
         }
         else if (st.current * float(sign) > 0.0f)
         {
            // Only clear if we were currently driving this direction —
            // protects against weird states where opposite directions
            // are held simultaneously.
            st.current = 0.0f;
            st.dir = 0;
            st.next_repeat = {};
         }
      };

      switch (info.button)
      {
         case pad_button::dpad_left:
            feed_dpad_axis(pad_axis::dpad_x, -1, info.down); return true;
         case pad_button::dpad_right:
            feed_dpad_axis(pad_axis::dpad_x, +1, info.down); return true;
         case pad_button::dpad_up:
            feed_dpad_axis(pad_axis::dpad_y, -1, info.down); return true;
         case pad_button::dpad_down:
            feed_dpad_axis(pad_axis::dpad_y, +1, info.down); return true;
         default: break;
      }

      // 3. Other buttons: pad → key synthesis (press only).
      if (!info.down)
         return false;
      auto bit = _pad_key_bindings.find(info.button);
      if (bit == _pad_key_bindings.end())
         return false;

      key_info ki{bit->second.key, key_action::press, bit->second.mods};
      bool r = this->key(ki);
      key_info kr{bit->second.key, key_action::release, bit->second.mods};
      this->key(kr);
      return r;
   }

   void view::pad_axis_event(pad_axis_info info)
   {
      int idx = static_cast<int>(info.axis);
      if (idx < 0 || idx >= 8)
         return;

      float v = info.value;
      // Triggers come in as [0, 1]; sticks come in as [-1, +1].
      if (info.axis == pad_axis::lt || info.axis == pad_axis::rt)
      {
         if (v < 0.0f) v = 0.0f;
      }
      else
      {
         // Snap deadzone region to zero so downstream checks are simple.
         if (std::abs(v) < _stick_deadzone)
            v = 0.0f;
      }
      auto& st = _axis_states[idx];
      st.current = v;
      if (v == 0.0f)
      {
         st.dir = 0;
         st.next_repeat = {};
      }
   }

   // -------------------------------------------------------------------
   // Axis mode getters / setters
   // -------------------------------------------------------------------
   void view::dpad_mode(pad_axis_mode m)        { _dpad_mode = m; }
   void view::left_stick_mode(pad_axis_mode m)  { _left_stick_mode = m; }
   void view::right_stick_mode(pad_axis_mode m) { _right_stick_mode = m; }
   void view::trigger_mode(pad_axis_mode m)     { _trigger_mode = m; }
   pad_axis_mode view::dpad_mode() const        { return _dpad_mode; }
   pad_axis_mode view::left_stick_mode() const  { return _left_stick_mode; }
   pad_axis_mode view::right_stick_mode() const { return _right_stick_mode; }
   pad_axis_mode view::trigger_mode() const     { return _trigger_mode; }

   void view::stick_deadzone(float v)           { _stick_deadzone = v; }
   float view::stick_deadzone() const           { return _stick_deadzone; }
   void view::stick_value_speed(float per_sec)  { _stick_value_speed = per_sec; }
   float view::stick_value_speed() const        { return _stick_value_speed; }

   void view::axis_repeat(int delay_ms, int rate_ms)
   {
      if (delay_ms > 0)
         _axis_repeat_delay_ms = delay_ms;
      _axis_repeat_rate_ms = (rate_ms > 0) ? rate_ms : 0;
   }
   int view::axis_repeat_delay() const          { return _axis_repeat_delay_ms; }
   int view::axis_repeat_rate() const           { return _axis_repeat_rate_ms; }

   pad_axis_mode view::mode_for(pad_axis a) const
   {
      switch (a)
      {
         case pad_axis::dpad_x:
         case pad_axis::dpad_y:  return _dpad_mode;
         case pad_axis::left_x:
         case pad_axis::left_y:  return _left_stick_mode;
         case pad_axis::right_x:
         case pad_axis::right_y: return _right_stick_mode;
         case pad_axis::lt:
         case pad_axis::rt:      return _trigger_mode;
         default:                return pad_axis_mode::disabled;
      }
   }

   void view::synthesize_axis_key(pad_axis axis, int sign)
   {
      key_code kc = key_code::unknown;
      switch (axis)
      {
         case pad_axis::dpad_x:
         case pad_axis::left_x:
         case pad_axis::right_x:
            kc = (sign > 0) ? key_code::right : key_code::left;
            break;
         case pad_axis::dpad_y:
         case pad_axis::left_y:
         case pad_axis::right_y:
            // SDL convention: +y is downward.
            kc = (sign > 0) ? key_code::down : key_code::up;
            break;
         case pad_axis::lt:
            kc = key_code::page_down;
            break;
         case pad_axis::rt:
            kc = key_code::page_up;
            break;
         default:
            return;
      }
      key_info ki_press{kc, key_action::press, 0};
      this->key(ki_press);
      key_info ki_release{kc, key_action::release, 0};
      this->key(ki_release);
   }

   void view::suspend_pad_nav(bool on)
   {
      _pad_nav_suspended = on;
   }

   void view::process_pad_axes(std::chrono::steady_clock::time_point now)
   {
      if (_content.empty())
         return;

      // Suspended (this view is covered by a higher input view): drop any
      // held-axis state so a pad direction held while it was covered does not
      // keep navigating this view's focus, and skip synthesis entirely.
      if (_pad_nav_suspended)
      {
         for (auto& st : _axis_states)
         {
            st.dir = 0;
            st.next_repeat = {};
            st.value_active = false;
         }
         return;
      }

      constexpr float threshold       = 0.5f;
      auto const initial_delay        = std::chrono::milliseconds(_axis_repeat_delay_ms);

      // Locate the deepest focus-owning element once — value-mode axes
      // all dispatch to the same leaf. We can't just take path.back():
      // for proxy-based widgets (basic_dial, basic_button, thumbwheel)
      // the path continues down into the styler subject, which is a
      // plain element that returns wants_focus() == false. Take the
      // deepest element that still answers wants_focus() == true; that
      // is the override-owning class (slider_base / basic_dial / ...)
      // and the one whose pad_axis() / key() overrides matter.
      element* focus_leaf = nullptr;
      with_context_do(
         [&focus_leaf](auto const& /*ctx*/, auto& _main_element)
         {
            std::vector<element*> path;
            walk_focus_path(_main_element, path);
            for (element* e : path)
               if (e->wants_focus())
                  focus_leaf = e;
         },
         *this, _current_bounds
      );

      pad_axis const axes[] = {
         pad_axis::dpad_x,  pad_axis::dpad_y,
         pad_axis::left_x,  pad_axis::left_y,
         pad_axis::right_x, pad_axis::right_y,
         pad_axis::lt,      pad_axis::rt
      };

      for (pad_axis axis : axes)
      {
         int idx = static_cast<int>(axis);
         auto& st = _axis_states[idx];
         pad_axis_mode const mode = mode_for(axis);
         if (mode == pad_axis_mode::disabled)
         {
            st.dir = 0;
            continue;
         }

         float const v = st.current;
         bool consumed_by_value = false;

         // Value-mode dispatch. Zero values are delivered exactly once on the
         // non-zero -> zero transition (st.value_active) so widgets can detect
         // "release" by state instead of call cadence. Polls only run on frames
         // that actually present; with render caching the cadence has gaps, so
         // cadence-based (quiet-window) release inference misfires there.
         if ((mode == pad_axis_mode::value || mode == pad_axis_mode::both)
             && (v != 0.0f || st.value_active) && focus_leaf)
         {
            pad_axis_info info{axis, v};
            in_context_do(*focus_leaf,
               [focus_leaf, info, &consumed_by_value](context const& ectx)
               {
                  consumed_by_value = focus_leaf->pad_axis(ectx, info);
               }
            );
            st.value_active = (v != 0.0f);
         }

         bool run_focus_path =
            (mode == pad_axis_mode::focus)
            || (mode == pad_axis_mode::both && !consumed_by_value);

         if (!run_focus_path)
         {
            st.dir = 0;
            st.next_repeat = {};
            continue;
         }

         int sign = 0;
         if (v >  threshold) sign = +1;
         else if (v < -threshold) sign = -1;

         if (sign == 0)
         {
            st.dir = 0;
            st.next_repeat = {};
         }
         else if (st.dir != sign)
         {
            // Edge: fresh push past threshold.
            synthesize_axis_key(axis, sign);
            st.dir = sign;
            st.next_repeat = now + initial_delay;
         }
         else if (now >= st.next_repeat)
         {
            synthesize_axis_key(axis, sign);
            int rep_ms = _axis_repeat_rate_ms;
            if (rep_ms <= 0)
            {
               // Legacy magnitude-scaled interval (faster when pushed hard).
               float const mag = std::min(1.0f, std::abs(v));
               rep_ms = int(60 + (1.0f - mag) * 190);
            }
            st.next_repeat = now + std::chrono::milliseconds(rep_ms);
         }
      }
   }

   void view::add_undo(undo_redo_task f)
   {
      _undo_stack.push(f);
      if (has_redo())
      {
         // clear the redo stack
         undo_stack_type empty{};
         _redo_stack.swap(empty);
      }
   }

   bool view::undo()
   {
      if (has_undo())
      {
         auto t = _undo_stack.top();
         _undo_stack.pop();
         _redo_stack.push(t);
         t.undo();  // execute undo function
         return true;
      }
      return false;
   }

   bool view::redo()
   {
      if (has_redo())
      {
         auto t = _redo_stack.top();
         _undo_stack.push(t);
         _redo_stack.pop();
         t.redo();  // execute redo function
         return true;
      }
      return false;
   }

   void view::begin_focus()
   {
      if (_content.empty() || !_is_focus)
         return;

      _main_element.begin_focus(element::focus_request::restore_previous);
      refresh();
   }

   void view::end_focus()
   {
      if (_content.empty())
         return;

      _main_element.end_focus();
      refresh();
   }

   void view::relinquish_focus()
   {
      if (_content.focus_index() != -1)
      {
         with_context_do(
            [this](auto const& ctx, auto& /*_main_element*/)
            {
               ctx.element->in_context_do(ctx, _content,
                  [this](auto const& ctx)
                  {
                     elements::relinquish_focus(_content, ctx);
                  }
               );
            },
            *this, _current_bounds
         );
      }
      _is_focus = false;
   }

   void view::track_drop(drop_info const& info, cursor_tracking status)
   {
      if (_content.empty())
         return;

      with_context_do(
         [info, status](auto const& ctx, auto& _main_element)
         {
            _main_element.track_drop(ctx, info, status);
         },
         *this, _current_bounds
      );
   }

   bool view::drop(drop_info const& info)
   {
      if (_content.empty())
         return false;

      bool handled = false;
      with_context_do(
         [info, &handled](auto const& ctx, auto& _main_element)
         {
            handled = _main_element.drop(ctx, info);
         },
         *this, _current_bounds
      );
      return handled;
   }

   void view::poll()
   {
      _tasks.poll();

      auto now = std::chrono::steady_clock::now();

      // Update the inter-frame delta. Clamp big gaps (debugger pause,
      // long task work) so a single weird frame can't run a slider all
      // the way to its max.
      if (_last_poll_time != std::chrono::steady_clock::time_point{})
      {
         float dt =
            std::chrono::duration<float>(now - _last_poll_time).count();
         if (dt < 0.0f) dt = 0.0f;
         if (dt > 0.1f) dt = 0.1f;
         _frame_dt = dt;
      }
      _last_poll_time = now;

      process_pad_axes(now);

      if (!_tracking.empty())
      {
         for (auto it = _tracking.cbegin(); it != _tracking.cend(); /**/)
         {
            using namespace std::chrono_literals;
            if ((now - it->second) > 1s)
            {
               on_tracking(*it->first, tracking::end_tracking);
               _tracking.erase(it++);
            }
            else
            {
               ++it;
            }
         }
      }
   }

   void view::manage_on_tracking(element& e, tracking state)
   {
      // Simulate a begin_tracking if needed
      if (_tracking.find(&e) == _tracking.end() && state == tracking::while_tracking)
         on_tracking(e, tracking::begin_tracking);

      _tracking[&e] = std::chrono::steady_clock::now();
      on_tracking(e, state);

      if (state == tracking::end_tracking)
         _tracking.erase(&e);
   }

   void view::in_context_do(element& e, context_function f)
   {
      if (_content.empty())
         return;

      with_context_do(
         [&e, &f](auto const& ctx, auto& _main_element)
         {
            _main_element.in_context_do(ctx, e, f);
         },
         *this, _current_bounds
      );
   }
}

/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/view.hpp>
#include <elements/window.hpp>
#include <elements/element/composite.hpp>
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
   view::view(extent size_)
    : base_view(size_)
    , _main_element(make_scaled_content())
    , _work(asio::make_work_guard(_io))
   {}

   view::view(host_view_handle h)
    : base_view(h)
    , _main_element(make_scaled_content())
    , _work(asio::make_work_guard(_io))
   {}

   view::view(window& win)
    : base_view(win.host())
    , _main_element(make_scaled_content())
    , _work(asio::make_work_guard(_io))
   {
      on_change_limits = [&win](view_limits limits_)
      {
         win.limits(limits_);
      };
      win.limits(_current_limits);
   }

   view::~view()
   {
      _io.stop();
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
      asio::post(_io,
         [this]()
         {
            base_view::refresh();
         }
      );
   }

   void view::refresh(rect area)
   {
      // Allow refresh to be called from another thread
      asio::post(_io,
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

      asio::post(_io,
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
         refresh({tl.x, tl.y, br.x, br.y});
      }
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
   }

   void view::arrow_focus_navigation(bool on)
   {
      _arrow_focus_nav = on;
   }

   bool view::arrow_focus_navigation() const
   {
      return _arrow_focus_nav;
   }

   void view::focus(element_ptr e)
   {
      if (!e)
         return;
      asio::post(_io,
         [this, e]()
         {
            // Bail out cleanly if the view already shut down (io_context
            // stopped during ~view).
            if (_content.empty())
               return;

            _main_element.end_focus();
            if (descend_set_focus(_main_element, e.get()))
            {
               _main_element.begin_focus(element::focus_request::restore_previous);
               base_view::refresh();
               _is_focus = _main_element.focus();
            }
         }
      );
   }

   bool view::key(key_info const& k)
   {
      if (_content.empty())
         return false;

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
            with_context_do(
               [d, &handled](auto const& ctx, auto& _main_element)
               {
                  std::vector<focusable_entry> list;
                  collect_focusables(ctx, _main_element, list);
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
                     // No matching focus yet: take the first focusable.
                     target = list.front().el;
                  }
                  else
                  {
                     target = pick_directional(list, cur_collected, cur_bounds, d);
                  }

                  if (!target || target == cur_collected)
                     return;

                  _main_element.end_focus();
                  if (descend_set_focus(_main_element, target))
                  {
                     _main_element.begin_focus(
                        element::focus_request::restore_previous);
                     handled = true;
                     ctx.view.refresh(ctx);
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
               _main_element.end_focus();
               _main_element.begin_focus(
                  reverse
                     ? element::focus_request::from_bottom
                     : element::focus_request::from_top
               );
               handled = _main_element.focus() != nullptr;
               if (handled)
                  ctx.view.refresh(ctx);
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
      _io.poll();
      if (!_tracking.empty())
      {
         for (auto it = _tracking.cbegin(); it != _tracking.cend(); /**/)
         {
            using namespace std::chrono_literals;
            auto now = std::chrono::steady_clock::now();
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

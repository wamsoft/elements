/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/atlas_stepper.hpp>
#include <elements/view.hpp>
#include <algorithm>

namespace cycfi::elements
{
   atlas_stepper_base::atlas_stepper_base(pixmap_ptr atlas, config cfg)
    : _atlas(std::move(atlas))
    , _cfg(std::move(cfg))
   {}

   //---------------------------------------------------------------------
   // 幾何
   //---------------------------------------------------------------------
   rect atlas_stepper_base::arrow_bounds(arrow const& a, rect bounds) const
   {
      // at は widget bounds 左上原点の相対 px
      return a.at.move(bounds.left, bounds.top);
   }

   rect atlas_stepper_base::subject_bounds(rect bounds) const
   {
      if (_cfg.subject_at.width() > 0 && _cfg.subject_at.height() > 0)
         return _cfg.subject_at.move(bounds.left, bounds.top);

      // 省略時は «矢印の外側» を削って内側を subject の領域にする。 dec / inc の
      // 意味ではなく `at` の実際の位置で判断するので、 縦横どちらでも、 また
      // 増える側がどちら端にあっても正しく効く。
      rect r = bounds;
      for (int dir : {-1, +1})
      {
         auto const& a = arrow_of(dir);
         if (!a.valid())
            continue;
         auto ab = arrow_bounds(a, bounds);
         if (_cfg.vertical)
         {
            bool near_top = (ab.top + ab.bottom) < (bounds.top + bounds.bottom);
            if (near_top)
               r.top = std::max(r.top, ab.bottom);
            else
               r.bottom = std::min(r.bottom, ab.top);
         }
         else
         {
            bool near_left = (ab.left + ab.right) < (bounds.left + bounds.right);
            if (near_left)
               r.left = std::max(r.left, ab.right);
            else
               r.right = std::min(r.right, ab.left);
         }
      }
      return r;
   }

   int atlas_stepper_base::arrow_hit(rect bounds, point p) const
   {
      for (int dir : {-1, +1})
      {
         auto const& a = arrow_of(dir);
         if (a.valid() && arrow_bounds(a, bounds).includes(p))
            return dir;
      }
      return 0;
   }

   //---------------------------------------------------------------------
   // 表示
   //---------------------------------------------------------------------
   view_limits atlas_stepper_base::limits(basic_context const& ctx) const
   {
      auto sl = subject().limits(ctx);

      // 最小 = 宣言された矩形を包む extent。 最大は両軸 full_extent にして
      // canvas / floating の "at" 矩形をそのまま受ける (パーツ配置は左上原点の
      // 絶対 px なので bounds が大きくても描画位置は変わらない)。
      float w = 0.0f, h = 0.0f;
      auto cover =
         [&w, &h](rect r)
         {
            w = std::max(w, r.right);
            h = std::max(h, r.bottom);
         };
      if (_cfg.dec.valid()) cover(_cfg.dec.at);
      if (_cfg.inc.valid()) cover(_cfg.inc.at);

      if (_cfg.subject_at.width() > 0 && _cfg.subject_at.height() > 0)
      {
         cover(_cfg.subject_at);
      }
      else if (_cfg.vertical)
      {
         h += sl.min.y;      // 矢印の並びに subject のぶんを足す
      }
      else
      {
         w += sl.min.x;
      }

      // 直交軸は subject の最小も確保する
      if (_cfg.vertical)
         w = std::max(w, sl.min.x);
      else
         h = std::max(h, sl.min.y);

      return {{w, h}, {full_extent, full_extent}};
   }

   void atlas_stepper_base::prepare_subject(context& ctx)
   {
      ctx.bounds = subject_bounds(ctx.bounds);
   }

   void atlas_stepper_base::draw_arrow(context const& ctx, int dir)
   {
      auto const& a = arrow_of(dir);
      if (!a.valid())
         return;

      auto pick =
         [](rect first, rect second, rect fallback)
         {
            if (first.width() > 0 && first.height() > 0)   return first;
            if (second.width() > 0 && second.height() > 0) return second;
            return fallback;
         };

      rect src = a.normal;
      if (!ctx.enabled || !is_enabled())
         src = pick(a.disabled, a.normal, a.normal);
      else if ((_held_dir == dir && _held_inside) || flashing(dir))
         src = pick(a.pressed, a.hilite, a.normal);
      else if (_hover_dir == dir)
         src = pick(a.hilite, a.normal, a.normal);

      ctx.canvas.draw(*_atlas, src, arrow_bounds(a, ctx.bounds));
   }

   void atlas_stepper_base::draw(context const& ctx)
   {
      proxy_base::draw(ctx);   // subject (bounds は prepare_subject で内側へ)

      // フォーカス中の値変化を矢印の点灯へ落とす。 キー / パッド操作はここへ
      // 届かない (パッドはフォーカス末端へ直接配送される) ので、 «値が動いた
      // 向き» から拾うのが確実。 つまみドラッグ中と矢印押下中は、 そちらで
      // 見えているので点灯させない。
      if (value_of && _cfg.flash_ms > 0)
      {
         double v = value_of();
         if (_has_last_value && v != _last_value
             && _focused && _held_dir == 0 && !_subject_pressed)
            flash(ctx, v > _last_value ? +1 : -1);
         _last_value = v;
         _has_last_value = true;
      }

      draw_arrow(ctx, -1);
      draw_arrow(ctx, +1);
   }

   element* atlas_stepper_base::hit_test(context const& ctx, point p, bool leaf, bool control)
   {
      if (arrow_hit(ctx.bounds, p) != 0)
         return this;
      return proxy_base::hit_test(ctx, p, leaf, control);
   }

   //---------------------------------------------------------------------
   // 点灯 (入力方向のフィードバック)
   //---------------------------------------------------------------------
   bool atlas_stepper_base::flashing(int dir) const
   {
      return _flash_dir == dir
          && std::chrono::steady_clock::now() < _flash_until;
   }

   void atlas_stepper_base::flash(context const& ctx, int dir)
   {
      if (_cfg.flash_ms <= 0 || dir == 0)
         return;
      _flash_dir   = dir;
      _flash_until = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(_cfg.flash_ms);
      ctx.view.refresh(ctx);
      auto& view_ = ctx.view;
      _flash_timer = view_.post(
         std::chrono::milliseconds(_cfg.flash_ms + 16),
         [&view_]() { view_.refresh(); });
   }

   //---------------------------------------------------------------------
   // ステップと自動リピート
   //---------------------------------------------------------------------
   void atlas_stepper_base::step(context const& ctx, int dir)
   {
      if (!on_step || dir == 0)
         return;
      on_step(dir);
      ctx.view.refresh(ctx);
   }

   void atlas_stepper_base::start_repeat(context const& ctx, int dir)
   {
      stop_repeat();
      if (!_cfg.repeat)
         return;

      auto& view_ = ctx.view;
      std::weak_ptr<element> self = shared_from_this();
      const int rate = _cfg.repeat_rate_ms > 0 ? _cfg.repeat_rate_ms : 60;

      _repeat_tick = std::make_shared<std::function<void()>>();
      std::weak_ptr<std::function<void()>> tick = _repeat_tick;
      *_repeat_tick =
         [this, self, tick, &view_, dir, rate]()
         {
            auto keep = self.lock();
            if (!keep)
               return;                  // 要素が消えている
            if (_held_dir != dir)
               return;                  // 離した / 別の矢印へ移った
            if (!is_enabled())
               return;                  // 途中で無効化された
            if (_held_inside && on_step)
            {
               on_step(dir);
               view_.refresh();
            }
            if (auto t = tick.lock())
               _repeat_timer = view_.post(std::chrono::milliseconds(rate), *t);
         };

      const int delay = _cfg.repeat_delay_ms > 0 ? _cfg.repeat_delay_ms : 400;
      _repeat_timer = view_.post(std::chrono::milliseconds(delay), *_repeat_tick);
   }

   void atlas_stepper_base::stop_repeat()
   {
      _repeat_timer.reset();
      _repeat_tick.reset();
   }

   //---------------------------------------------------------------------
   // 入力
   //---------------------------------------------------------------------
   bool atlas_stepper_base::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled || !is_enabled())
         return false;
      if (btn.state != mouse_button::left)
         return proxy_base::click(ctx, btn);

      if (btn.down)
      {
         if (int dir = arrow_hit(ctx.bounds, btn.pos); dir != 0)
         {
            _held_dir = dir;
            _held_inside = true;
            // 矢印はフォーカスを取らない。 本体へ渡して、 そのまま方向キー /
            // パッドで操作を続けられるようにする (arrow_button と同じ)。
            if (subject().wants_focus())
               ctx.view.focus(shared_from_this());
            step(ctx, dir);
            start_repeat(ctx, dir);
            return true;
         }
         // 矢印でも subject でもない余白は無反応 (値が飛ばないように)
         if (!subject_bounds(ctx.bounds).includes(btn.pos))
            return false;
         _subject_pressed = true;
         return proxy_base::click(ctx, btn);
      }

      if (_held_dir != 0)
      {
         stop_repeat();
         _held_dir = 0;
         ctx.view.refresh(ctx);
         return true;
      }
      if (_subject_pressed)
      {
         _subject_pressed = false;
         return proxy_base::click(ctx, btn);
      }
      return false;
   }

   void atlas_stepper_base::drag(context const& ctx, mouse_button btn)
   {
      if (_held_dir != 0)
      {
         // 押したまま矢印の外へ出たらリピートを止め、 戻れば再開する
         bool inside = arrow_bounds(arrow_of(_held_dir), ctx.bounds).includes(btn.pos);
         if (inside != _held_inside)
         {
            _held_inside = inside;
            ctx.view.refresh(ctx);
         }
         return;
      }
      proxy_base::drag(ctx, btn);
   }

   bool atlas_stepper_base::cursor(context const& ctx, point p, cursor_tracking status)
   {
      int hover = (status == cursor_tracking::leaving) ? 0 : arrow_hit(ctx.bounds, p);
      if (hover != _hover_dir)
      {
         _hover_dir = hover;
         ctx.view.refresh(ctx);
      }
      // 矢印の上でも subject へは流す (hover_focus が効いたままになる)
      return proxy_base::cursor(ctx, p, status);
   }

   void atlas_stepper_base::begin_focus(focus_request req)
   {
      _focused = true;
      proxy_base::begin_focus(req);
   }

   bool atlas_stepper_base::end_focus()
   {
      _focused = false;
      return proxy_base::end_focus();
   }
}

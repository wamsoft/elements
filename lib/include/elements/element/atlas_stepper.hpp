/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ATLAS_STEPPER_SEPTEMBER_2_2026)
#define ELEMENTS_ATLAS_STEPPER_SEPTEMBER_2_2026

#include <elements/element/proxy.hpp>
#include <elements/element/image.hpp>
#include <chrono>
#include <functional>
#include <memory>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // atlas_stepper — 被制御要素 (subject) の両脇にアトラス素材の «増減矢印» を
   // 足すプロキシ。
   //
   //   [dec の絵]  ← subject (スライダ / スクロールバー) →  [inc の絵]
   //
   // 矢印は «値を減らす» (dec) / «値を増やす» (inc) の 2 個で、 **幾何的な向き
   // (左右 / 上下) では区別しない**。 縦にしたとき「上下どちらが増える側か」は
   // スライダ (value 1.0 = 上) とスクロールバー (offset 0 = 上) で逆になるので、
   // 名前は意味だけを持たせ、 **絵をどこに置くかは `at` (widget bounds 左上原点の
   // 相対 px) が決める**。 subject の領域を省略したときも、 dec/inc の意味では
   // なく `at` の実際の位置から «内側» を割り出す。
   //
   // ステップの実体は `on_step(dir)` (dir: -1 = dec / +1 = inc) でホストが与える。
   // 0..1 のスライダなら ±step、 index スクロールバーなら ∓行数、 と subject の
   // 都合に合わせられるので、 このプロキシは値の意味を一切知らない。
   //
   // 操作:
   //   - 矢印をクリック → `on_step` 1 回 + 押し続けで自動リピート
   //     (repeat_delay_ms 待って repeat_rate_ms 間隔。 押したまま矢印の外へ
   //      出るとリピートは止まり、 戻ると再開する)
   //   - 矢印は**フォーカスを取らない** (Tab / パッド巡回の停留点にならない)。
   //     クリックしたときはフォーカスを subject へ渡すので、 そのまま方向キーや
   //     パッドで操作を続けられる (arrow_button と同じ扱い)
   //   - 矢印に当たらないクリックは subject へ流す。 ただし **subject の領域内に
   //     限る**ので、 widget の余白を押しても値は飛ばない
   //
   // 見た目:
   //   frames は normal / hilite (カーソルが乗っている) / pressed (押している) /
   //   disabled の 4 状態で、 無い状態は 1 段ずつ縮退する
   //   (disabled → normal、 pressed → hilite → normal)。
   //
   //   `value_of` を与えると「フォーカス中に値が変わったら、 その向きの矢印を
   //   flash_ms だけ光らせる」フィードバックが付く (キー / パッド操作でも
   //   «どちらへ動いたか» が見える。 atlas_cycle_picker の矢印点灯と同じ考え方)。
   //   つまみのドラッグ中と矢印の押下中は点灯しない (そちらは pressed で見える)。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_stepper_base : public proxy_base
   {
   public:

      // 矢印 1 個ぶんの素材と配置。 normal と at が無いものは «矢印なし» 扱い
      // (片端だけ矢印がある素材も組める)。
      struct arrow
      {
         rect                 normal{};
         rect                 hilite{};
         rect                 pressed{};
         rect                 disabled{};
         rect                 at{};      // widget bounds 左上原点の相対 px

         bool                 valid() const
         {
            return at.width() > 0 && at.height() > 0
                && normal.width() > 0 && normal.height() > 0;
         }
      };

      struct config
      {
         arrow                dec{};           // 値を減らす側
         arrow                inc{};           // 値を増やす側
         rect                 subject_at{};    // 空 = 矢印の外側から自動算出
         bool                 vertical = false;
         bool                 repeat = true;
         int                  repeat_delay_ms = 400;
         int                  repeat_rate_ms  = 60;
         int                  flash_ms        = 140;
      };

      using on_step_function = std::function<void(int dir)>;
      using value_function   = std::function<double()>;

                              atlas_stepper_base(pixmap_ptr atlas, config cfg);

   // Display

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;
      void                    prepare_subject(context& ctx) override;
      element*                hit_test(context const& ctx, point p, bool leaf, bool control) override;

   // Control

      bool                    wants_control() const override { return true; }
      bool                    click(context const& ctx, mouse_button btn) override;
      void                    drag(context const& ctx, mouse_button btn) override;
      bool                    cursor(context const& ctx, point p, cursor_tracking status) override;
      void                    begin_focus(focus_request req) override;
      bool                    end_focus() override;

      // dir: -1 = dec (減) / +1 = inc (増)
      on_step_function        on_step;
      // 任意。 値の取得子を与えると «フォーカス中の値変化» で矢印が点灯する。
      value_function          value_of;

      config const&           stepper_config() const { return _cfg; }
      void                    flash(context const& ctx, int dir);

   private:

      arrow const&            arrow_of(int dir) const
                              { return dir < 0 ? _cfg.dec : _cfg.inc; }
      rect                    arrow_bounds(arrow const& a, rect bounds) const;
      rect                    subject_bounds(rect bounds) const;
      int                     arrow_hit(rect bounds, point p) const;
      void                    draw_arrow(context const& ctx, int dir);
      bool                    flashing(int dir) const;
      void                    step(context const& ctx, int dir);
      void                    start_repeat(context const& ctx, int dir);
      void                    stop_repeat();

      pixmap_ptr              _atlas;
      config                  _cfg;

      int                     _held_dir = 0;        // 押下中の矢印 (0 = 無し)
      bool                    _held_inside = true;  // その矢印の上にまだ乗っているか
      int                     _hover_dir = 0;
      bool                    _focused = false;
      bool                    _subject_pressed = false;

      double                  _last_value = 0.0;
      bool                    _has_last_value = false;

      int                     _flash_dir = 0;
      std::chrono::steady_clock::time_point _flash_until{};
      std::shared_ptr<void>   _flash_timer;   // 消灯用の再描画予約を保持
      std::shared_ptr<void>   _repeat_timer;  // 次のリピート発火の予約
      // リピートは «自分を再予約する» 関数なので、 自己参照で循環しないよう
      // 本体はここで持ち、 予約側は weak で見る。
      std::shared_ptr<std::function<void()>> _repeat_tick;
   };

   ////////////////////////////////////////////////////////////////////////////
   // atlas_stepper — 矢印付きにしたい要素を包む。
   //
   //   auto sl = share(atlas_stepper(atlas, cfg, hold(slider)));
   //   (*sl).on_step = [w = std::weak_ptr{slider}](int dir) { ... };
   ////////////////////////////////////////////////////////////////////////////
   template <concepts::Element Subject>
   inline proxy<remove_cvref_t<Subject>, atlas_stepper_base>
   atlas_stepper(pixmap_ptr atlas, atlas_stepper_base::config cfg, Subject&& subject)
   {
      return {std::forward<Subject>(subject), std::move(atlas), std::move(cfg)};
   }
}

#endif

/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/atlas.hpp>
#include <elements/support/context.hpp>
#include <elements/support/theme.hpp>
#include <elements/view.hpp>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace cycfi::elements
{
   //---------------------------------------------------------------------
   // atlas_image
   //---------------------------------------------------------------------
   atlas_image::atlas_image(pixmap_ptr atlas, rect src,
                            bool stretch_h, bool stretch_v)
    : image(std::move(atlas))
    , _src(src)
    , _stretch_h(stretch_h)
    , _stretch_v(stretch_v)
   {}

   view_limits atlas_image::limits(basic_context const& /*ctx*/) const
   {
      auto w = _src.width();
      auto h = _src.height();
      point min_{w, h};
      point max_{_stretch_h ? full_extent : w,
                 _stretch_v ? full_extent : h};
      return {min_, max_};
   }

   point atlas_image::size() const
   {
      return {_src.width(), _src.height()};
   }

   rect atlas_image::source_rect(context const& /*ctx*/) const
   {
      return _src;
   }

   //---------------------------------------------------------------------
   // atlas_sprite
   //---------------------------------------------------------------------
   atlas_sprite::atlas_sprite(pixmap_ptr atlas, std::vector<rect> frames, bool native)
    : basic_sprite(std::move(atlas))
    , _frames(std::move(frames))
    , _native(native)
   {
      if (_frames.empty())
         throw std::runtime_error{
            "atlas_sprite requires at least one frame"};
   }

   point atlas_sprite::max_extent() const
   {
      float w = 0, h = 0;
      for (auto const& f : _frames)
      {
         w = std::max<float>(w, f.width());
         h = std::max<float>(h, f.height());
      }
      return {w, h};
   }

   view_limits atlas_sprite::limits(basic_context const& /*ctx*/) const
   {
      // 既定は frame 0 (全 frame 同寸法前提)。 native モードでは frame 間で寸法が
      // 違うので、 最大の frame に合わせて box を確保する (小さい frame は中央)。
      point sz = _native ? max_extent()
                         : point{_frames[0].width(), _frames[0].height()};
      return {sz, sz};
   }

   point atlas_sprite::size() const
   {
      if (_native)
         return max_extent();
      auto const& f = _frames[_index];
      return {f.width(), f.height()};
   }

   rect atlas_sprite::source_rect(context const& /*ctx*/) const
   {
      return _frames[_index];
   }

   void atlas_sprite::draw(context const& ctx)
   {
      if (!_native)
      {
         image::draw(ctx);   // 従来: 現 frame を bounds へ伸縮
         return;
      }
      // native: 現 frame を実寸のまま bounds 中央へ (伸縮しない)。
      auto src = source_rect(ctx);
      rect dest{0, 0, src.width(), src.height()};
      ctx.canvas.draw(pixmap(), src, center(dest, ctx.bounds));
   }

   void atlas_sprite::index(std::size_t i)
   {
      if (i < _frames.size())
         _index = i;
   }

   //---------------------------------------------------------------------
   // animated_sprite
   //---------------------------------------------------------------------
   animated_sprite::animated_sprite(pixmap_ptr atlas, std::vector<rect> frames,
                                    float fps, bool loop, bool native)
    : atlas_sprite(std::move(atlas), std::move(frames), native)
    , _fps(fps)
    , _loop(loop)
   {}

   void animated_sprite::draw(context const& ctx)
   {
      // 経過時間からフレーム index を算出 (最初の draw を基点にする)。
      if (!_started)
      {
         _t0 = std::chrono::steady_clock::now();
         _started = true;
      }
      std::size_t n = num_frames();
      if (n > 0 && _fps > 0.0f)
      {
         auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - _t0).count();
         double f = static_cast<double>(ms) * (_fps / 1000.0);
         std::size_t idx = _loop
            ? static_cast<std::size_t>(std::fmod(f, static_cast<double>(n)))
            : std::min<std::size_t>(static_cast<std::size_t>(f), n - 1);
         index(idx);
      }
      atlas_sprite::draw(ctx);
   }

   //---------------------------------------------------------------------
   // atlas_progress
   //---------------------------------------------------------------------
   atlas_progress::atlas_progress(pixmap_ptr atlas, rect track, rect fill,
                                  double value, bool vertical, rect fill_at)
    : _atlas(std::move(atlas))
    , _track(track)
    , _fill(fill)
    , _fill_at(fill_at)
    , _value(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value))
    , _vertical(vertical)
   {}

   view_limits atlas_progress::limits(basic_context const& /*ctx*/) const
   {
      point sz{_track.width(), _track.height()};
      return {sz, sz};
   }

   void atlas_progress::draw(context const& ctx)
   {
      // 背景 (track) を bounds 全域に
      ctx.canvas.draw(*_atlas, _track, ctx.bounds);

      if (_value <= 0.0) return;

      // 前景 (fill) を value 分だけ。 src 矩形と dest 矩形を value で切詰める
      // (= 左/下端固定、 残り 1-value 分を捨てる)。
      auto sf = _fill;
      auto df = ctx.bounds;
      if (_fill_at.width() > 0 && _fill_at.height() > 0 &&
          _track.width() > 0 && _track.height() > 0)
      {
         // fill_at (track ソース矩形の左上原点 px) を bounds 空間へ写像
         // (fill が track にインセットされた素材向け)。
         float sx = ctx.bounds.width()  / _track.width();
         float sy = ctx.bounds.height() / _track.height();
         df = rect{
            ctx.bounds.left + _fill_at.left   * sx,
            ctx.bounds.top  + _fill_at.top    * sy,
            ctx.bounds.left + _fill_at.right  * sx,
            ctx.bounds.top  + _fill_at.bottom * sy
         };
      }
      if (_vertical)
      {
         float h_src = sf.height() * static_cast<float>(_value);
         float h_dst = df.height() * static_cast<float>(_value);
         sf.top = sf.bottom - h_src;
         df.top = df.bottom - h_dst;
      }
      else
      {
         float w_src = sf.width()  * static_cast<float>(_value);
         float w_dst = df.width()  * static_cast<float>(_value);
         sf.right = sf.left + w_src;
         df.right = df.left + w_dst;
      }
      ctx.canvas.draw(*_atlas, sf, df);
   }

   void atlas_progress::set_value(double v)
   {
      if (v < 0.0) v = 0.0;
      if (v > 1.0) v = 1.0;
      _value = v;
   }

   //---------------------------------------------------------------------
   // atlas_cycle_picker
   //---------------------------------------------------------------------
   namespace
   {
      // bounds 左上原点の相対矩形 → 画面座標
      rect offset_rect(rect at, rect bounds)
      {
         return rect{
            bounds.left + at.left,
            bounds.top  + at.top,
            bounds.left + at.right,
            bounds.top  + at.bottom
         };
      }
   }

   atlas_cycle_picker::atlas_cycle_picker(
      pixmap_ptr atlas,
      std::vector<std::string> options, std::size_t initial,
      arrow_frames left, arrow_frames right,
      rect left_at, rect right_at, rect text_at)
    : cycle_picker(std::move(options), initial)
    , _atlas(std::move(atlas))
    , _left(left)
    , _right(right)
    , _left_at(left_at)
    , _right_at(right_at)
    , _text_at(text_at)
   {}

   view_limits atlas_cycle_picker::limits(basic_context const& /*ctx*/) const
   {
      // 最小 = 3 パーツの相対矩形を包む extent。 max は full_extent にして
      // canvas floating の "at" 矩形サイズをそのまま受け入れる (パーツ配置は
      // 左上原点の絶対 px なので bounds が大きくても描画位置は不変)。
      float w = std::max({_left_at.right, _right_at.right, _text_at.right});
      float h = std::max({_left_at.bottom, _right_at.bottom, _text_at.bottom});
      return {{w, h}, {full_extent, full_extent}};
   }

   void atlas_cycle_picker::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  st = cnv.new_state();

      // 既定は「入力方向の矢印だけを短時間 hilite」。 _flash_ms = 0 のときは
      // 従来どおり「フォーカス中は両矢印 hilite」。
      const bool hi_left  = (_flash_ms > 0) ? flashing(-1) : focused();
      const bool hi_right = (_flash_ms > 0) ? flashing(+1) : focused();
      cnv.draw(*_atlas, hi_left ? _left.hilite : _left.normal,
               offset_rect(_left_at, ctx.bounds));
      cnv.draw(*_atlas, hi_right ? _right.hilite : _right.normal,
               offset_rect(_right_at, ctx.bounds));

      if (num_options())
      {
         auto font = get_theme().label_font;
         font = font.size(font._size * font_size());
         cnv.font(font);
         cnv.fill_style(_color);
         cnv.text_align(cnv.center | cnv.middle);
         auto tr = offset_rect(_text_at, ctx.bounds);
         cnv.fill_text(
            option_text(index()),
            {tr.left + tr.width() / 2.0f, tr.top + tr.height() / 2.0f});
      }
   }

   //---------------------------------------------------------------------------
   // 左右入力のフィードバック
   //
   // フォーカスしただけで矢印が光っていると「今その向きへ入力した」のか
   // 「単に選択中」なのか区別が付かないので、 入力があった向きだけを
   // 短時間光らせる。 消灯のための再描画は view へ予約しておく (入力が
   // 続かなくても通常表示へ戻す)。
   //---------------------------------------------------------------------------
   bool atlas_cycle_picker::flashing(int dir) const
   {
      return _flash_dir == dir
          && std::chrono::steady_clock::now() < _flash_until;
   }

   void atlas_cycle_picker::flash(context const& ctx, int dir)
   {
      if (_flash_ms <= 0 || dir == 0)
         return;
      _flash_dir   = dir;
      _flash_until = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(_flash_ms);
      ctx.view.refresh(ctx);
      auto& view_ = ctx.view;
      _flash_timer = view_.post(
         std::chrono::milliseconds(_flash_ms + 16),
         [&view_]() { view_.refresh(); });
   }

   bool atlas_cycle_picker::key(context const& ctx, key_info k)
   {
      const std::size_t before = index();
      if (!cycle_picker::key(ctx, k))
         return false;
      if (index() != before)
         flash(ctx, (k.key == key_code::left) ? -1 : +1);
      return true;
   }

   bool atlas_cycle_picker::pad_axis(context const& ctx, pad_axis_info info)
   {
      const std::size_t before = index();
      if (!cycle_picker::pad_axis(ctx, info))
         return false;
      if (index() != before)
         flash(ctx, (info.value < 0.0f) ? -1 : +1);
      return true;
   }

   bool atlas_cycle_picker::click(context const& ctx, mouse_button btn)
   {
      if (!ctx.enabled)
         return false;
      if (btn.state != mouse_button::left || !btn.down)
         return false;
      ctx.view.focus(shared_from_this());

      int delta = 0;
      if (offset_rect(_left_at, ctx.bounds).includes(btn.pos))
         delta = -1;
      else if (offset_rect(_right_at, ctx.bounds).includes(btn.pos))
         delta = +1;
      if (delta != 0 && step(delta))
      {
         ctx.view.refresh(ctx);
         flash(ctx, delta);
      }
      return true;
   }
}

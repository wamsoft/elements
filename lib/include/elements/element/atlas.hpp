/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ATLAS_JUNE_13_2026)
#define ELEMENTS_ATLAS_JUNE_13_2026

#include <elements/element/image.hpp>
#include <vector>
#include <chrono>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // atlas_image — 1 枚画像 (= pixmap) の任意の sub-rect を 1 要素として
   // 表示する。 pixmap_ptr は外部所有 / 他要素と共有可能 (= テクスチャ
   // アトラスのシェア)。
   //
   //   stretch_h: 横方向にストレッチを許可 (max.x = full_extent)。
   //              スライダ track / 9-patch 風背景用途。
   //   stretch_v: 縦方向にストレッチを許可。
   //   両方 false (既定): 固定サイズ。 飾り画像、 スライダ thumb 用。
   //
   // PSD ベース UI では canvas + floating で絶対座標配置するのが普通なので、
   // stretch は false で十分。 stretch_h=true 等は flex layout (htile) 等に
   // 入れる場合のオプション。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_image : public image
   {
   public:
                              atlas_image(pixmap_ptr atlas, rect src,
                                          bool stretch_h = false,
                                          bool stretch_v = false);

      view_limits             limits(basic_context const& ctx) const override;
      point                   size() const override;
      rect                    source_rect(context const& ctx) const override;

      rect const&             sub_rect() const { return _src; }

   private:

      rect                    _src;
      bool                    _stretch_h;
      bool                    _stretch_v;
   };

   ////////////////////////////////////////////////////////////////////////////
   // atlas_sprite — テクスチャアトラスから複数 sub-rect を状態別 frame と
   // して持つ sprite。 basic_sprite を継承して sprite_button_styler の
   // find_subject<sprite*> が dynamic_cast で拾えるようにしている。
   //
   // basic_sprite は 1 枚画像を縦に等高さスライスする前提だが、 atlas_sprite
   // は任意の sub-rect を frame 配列で受ける → アトラス共有 + 状態別矩形を
   // 自由配置できる。 関連メソッド (num_frames / index / source_rect / size /
   // limits) を override してアトラスベースの計算で置き換える。
   //
   //   frame index と sprite_button_styler のマッピング (lib 既存仕様):
   //     0 = normal (value=false, hilite=false)
   //     1 = hilite (value=false, hilite=true)
   //     2 = pressed (value=true, hilite=false)
   //     3 = pressed_hilite (value=true, hilite=true)
   //     4 = disabled (任意、 num_frames>4 のときだけ使う)
   //
   // limits / size は frame 0 の矩形を採用する。 全 frame で同寸法を前提と
   // する (ボタンの状態切替で見た目サイズは普通変えない)。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_sprite : public basic_sprite
   {
   public:
                              atlas_sprite(pixmap_ptr atlas,
                                           std::vector<rect> frames,
                                           bool native = false);

      view_limits             limits(basic_context const& ctx) const override;
      point                   size() const override;
      rect                    source_rect(context const& ctx) const override;
      // native モード時は各 frame を実寸のまま bounds 中央に描く (frame 間で
      // サイズが違っても伸縮しない)。 既定 (false) は従来どおり bounds へ伸縮。
      void                    draw(context const& ctx) override;

      std::size_t             num_frames() const override { return _frames.size(); }
      std::size_t             index() const override      { return _index; }
      void                    index(std::size_t i) override;

   private:

      point                   max_extent() const;

      std::vector<rect>       _frames;
      bool                    _native = false;
   };

   ////////////////////////////////////////////////////////////////////////////
   // animated_sprite — atlas_sprite のフレームを一定 fps で自動送りするスプライト
   // アニメ (パラパラ / スプライトシート再生)。 アニメカーソルアイコン、 スピナー、
   // 待機ループ演出など。 経過時間 (steady_clock) からフレーム index を算出し、
   // atlas_sprite::draw に委譲して描く。 loop=false は最終フレームで停止。
   // elements_modal (console) は毎フレーム再描画するので滑らかに動く。
   ////////////////////////////////////////////////////////////////////////////
   class animated_sprite : public atlas_sprite
   {
   public:
                              animated_sprite(pixmap_ptr atlas,
                                              std::vector<rect> frames,
                                              float fps = 12.0f,
                                              bool loop = true,
                                              bool native = false);

      void                    draw(context const& ctx) override;

      float                   fps() const { return _fps; }
      void                    set_fps(float f) { _fps = f; }
      bool                    loop() const { return _loop; }
      void                    set_loop(bool l) { _loop = l; }

   private:

      float                                 _fps;
      bool                                  _loop;
      std::chrono::steady_clock::time_point _t0;
      bool                                  _started = false;
   };

   ////////////////////////////////////////////////////////////////////////////
   // atlas_progress — ゲージ / HP バー用、 非インタラクティブ。
   // アトラスの track 矩形を背景として全幅に、 fill 矩形を value (0..1) 分の
   // 長さだけ前景として描画する。
   //
   //   vertical=false (水平、 既定): fill は左から右に value 分伸びる
   //   vertical=true  (垂直):        fill は下から上に value 分伸びる
   //
   // limits / size は track の矩形を採用する (= 描画サイズは track と同じ)。
   // set_value() で値を 0..1 にクランプして更新。 次フレームで反映される。
   //
   // fill_at: fill の配置先を track ソース矩形の左上原点の px で指定する
   // (fill が track に対してインセットされている画像素材向け。 例: ゲージ枠
   // の内側にバーが入るデザイン)。 空 (既定) なら従来どおり bounds 全域。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_progress : public element
   {
   public:
                              atlas_progress(pixmap_ptr atlas, rect track,
                                             rect fill, double value = 0.0,
                                             bool vertical = false,
                                             rect fill_at = {});

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      double                  value() const { return _value; }
      void                    set_value(double v);

   private:

      pixmap_ptr              _atlas;
      rect                    _track;
      rect                    _fill;
      rect                    _fill_at;
      double                  _value;
      bool                    _vertical;
   };
}

#endif

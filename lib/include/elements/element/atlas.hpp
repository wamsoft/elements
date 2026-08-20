/*=============================================================================
   Copyright (c) 2026 Cycfi Research
   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ATLAS_JUNE_13_2026)
#define ELEMENTS_ATLAS_JUNE_13_2026

#include <elements/element/image.hpp>
#include <elements/element/picker.hpp>
#include <elements/element/text.hpp>   // atlas_number の text_writer
#include <map>
#include <string>
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
      void                    sub_rect(rect r) { _src = r; }

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

      // Play again from the first frame on the next draw. Needed for
      // loop=false animations that are shown more than once: elapsed time
      // keeps running while the sprite is hidden, so without this the
      // second showing would start (and stay) at the last frame.
      void                    restart() { _started = false; }

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

   ////////////////////////////////////////////////////////////////////////////
   // atlas_number — 数字素材 (0-9 の sub-rect) で «文字列» を描く非インタラクティブ
   // 要素。 スコア / 残数 / 音量のような「フォントではなく絵の数字」表示用。
   //
   //   digits: 0,1,...,9 の順に 10 個の sub-rect。
   //   glyphs: 数字以外のグリフ (UTF-8 1 文字 → sub-rect)。 "-" "." "%" "/" 等。
   //           指定の無い文字は幅 0 として読み飛ばす (' ' は space_width で送る)。
   //
   // set_text() は text_writer 経由でも呼べるので、 VariableStore の subscriber
   // (label の text_var と同じ仕掛け) をそのまま流用できる。
   //
   //   align:   bounds 内の水平寄せ (left / center / right)。 縦は常に中央。
   //   spacing: 字間 px (負値で詰める)。
   //   scale:   素材の拡大率 (既定 1.0 = 実寸)。
   //
   // limits は「digits の最大サイズ × 1 文字」を最小、 横は full_extent まで
   // 伸びられる形にしてある (canvas + floating の絶対配置が主用途)。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_number : public element, public text_writer
   {
   public:

      enum class align_x { left, center, right };

                              atlas_number(pixmap_ptr atlas,
                                           std::vector<rect> digits,
                                           std::map<std::string, rect> glyphs = {},
                                           float spacing = 0.0f,
                                           float scale = 1.0f,
                                           align_x align = align_x::left);

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;

      void                    set_text(string_view text) override;
      std::string const&      text() const { return _text; }

      void                    space_width(float w) { _space_width = w; }
      float                   space_width() const { return _space_width; }

   private:

      // 1 文字ぶんの描画元矩形を引く (見つからなければ nullptr)。
      rect const*             glyph_rect(std::string const& ch) const;
      // 現在のテキストの描画幅 (scale / spacing 込み)。
      float                   text_width() const;

      pixmap_ptr              _atlas;
      std::vector<rect>       _digits;
      std::map<std::string, rect> _glyphs;
      std::string             _text;
      float                   _spacing;
      float                   _scale;
      float                   _space_width = 0.0f;
      align_x                 _align;
   };

   ////////////////////////////////////////////////////////////////////////////
   // atlas_cycle_picker — cycle_picker の選択モデル (step / select / key /
   // pad_axis / wrap-around) をそのまま継承し、 描画をアトラス素材に置き換えた
   // 画像ボタン式ピッカー。
   //
   //   [左矢印絵]  現在の選択テキスト  [右矢印絵]
   //
   // 左右の矢印はそれぞれ normal / hilite の 2 フレームを持つ。 既定では
   // 通常表示で、 **左右入力があったときだけ入力方向の矢印が短時間 hilite に
   // なる** (押した向きが分かるフィードバック)。 フォーカス中に出しっぱなしに
   // はしない。 点灯時間は arrow_flash_ms() で変更でき、 0 で従来どおり
   // 「フォーカス中は両矢印 hilite」に戻る。 各パーツの配置は
   // widget bounds 左上原点の相対 px 矩形 (left_at / right_at / text_at) で
   // 指定する (PSD 由来の絶対配置向け)。 クリックは left_at / right_at の
   // ヒットで ∓1 ステップ、 それ以外はフォーカス取得のみ。
   ////////////////////////////////////////////////////////////////////////////
   class atlas_cycle_picker : public cycle_picker
   {
   public:

      struct arrow_frames { rect normal, hilite; };

                              atlas_cycle_picker(
                                 pixmap_ptr atlas,
                                 std::vector<std::string> options,
                                 std::size_t initial,
                                 arrow_frames left, arrow_frames right,
                                 rect left_at, rect right_at, rect text_at);

      view_limits             limits(basic_context const& ctx) const override;
      void                    draw(context const& ctx) override;
      bool                    click(context const& ctx, mouse_button btn) override;
      bool                    key(context const& ctx, key_info k) override;
      bool                    pad_axis(context const& ctx, pad_axis_info info) override;

      void                    text_color(color c) { _color = c; }
      color                   text_color() const  { return _color; }

      // 入力方向の矢印を光らせる時間 (ms)。 0 = 無効 (フォーカス中は両矢印 hilite)
      void                    arrow_flash_ms(int ms) { _flash_ms = ms; }
      int                     arrow_flash_ms() const { return _flash_ms; }

   private:

      // 左右入力の直後だけ、 その向きの矢印を hilite にする。
      void                    flash(context const& ctx, int dir);
      bool                    flashing(int dir) const;

      pixmap_ptr              _atlas;
      arrow_frames            _left;
      arrow_frames            _right;
      rect                    _left_at;
      rect                    _right_at;
      rect                    _text_at;
      color                   _color = colors::white;

      int                     _flash_ms = 140;   // 0 で従来動作
      int                     _flash_dir = 0;    // -1 = 左 / +1 = 右 / 0 = 無し
      std::chrono::steady_clock::time_point _flash_until{};
      std::shared_ptr<void>   _flash_timer;      // 消灯用の再描画予約を保持
   };
}

#endif

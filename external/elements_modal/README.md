# elements_modal

純粋 SDL3 + [Elements](https://github.com/wamsoft/elements) + [ThorVG](https://www.thorvg.org/) ベースの JSON モーダル UI ライブラリ。

SDL3 を使う任意のアプリから単体で利用できる。

## 特徴

- JSON / JSONC (行コメント・ブロックコメント・末尾カンマ) でレイアウト定義。
  bool フィールドは `true` / `false` に加えて **number の 0 / 非 0 も真偽値として受け付ける**
  (吉里吉里 TJS2 など bool 型のないホスト言語が辞書を JSON 化して渡すケース向け。
  `json_layout.cpp` の `bool_field` / `truthy_field`)
- 独立 SDL_Window でモーダル表示 (`run_modal`) — 内容に合わせた window サイズで生成 + 閉じるまでブロック
- 既存サーフェスへのオーバーレイ (`overlay_session`) — ホスト側がイベント / 描画ループを駆動
- 複数画面の JSON 駆動遷移 (`navigator` + マニフェスト + `"transitions"`) — push / pop / replace / fade をホストにロジックを書かずに
- パーツ演出 (`"animate"`) — 要素の移動 / 拡縮 / 回転 / フェードを一般イージング・台形プロファイル・ループ付きで JSON 指定 (周囲を reflow しない見た目だけの変換)。 発火トリガ `"on"` で登場 (enter) / focus / 決定 (select) / 退場 (exit) を出し分け
- ボタン押下 / state widget の値変化を結果構造で返却 + 任意 callback でも通知
- 親 SDL_Window を渡せば OS レベルでモーダル化 (`SDL_WINDOW_MODAL`)
- 多言語フォントレンダリング (Elements の FreeType + HarfBuzz ローダ経由)
- キーボード (Tab / 矢印 / Enter / Esc / Space / PageUp/Down / Home/End / 任意 key shortcut) + ゲームパッド (D-Pad / A=Enter / B=Esc / X=Shift+Tab / Y=Tab / stick / 任意 pad shortcut) ナビゲーション

## 使い方 (run_modal)

```cpp
#include <elements_modal/modal.h>

int main()
{
    SDL_Init(SDL_INIT_VIDEO);

    elements_modal::config cfg;
    cfg.title_utf8 = "Settings";
    cfg.width  = 600;
    cfg.height = 400;
    cfg.pixel_scale = 2.0f;          // HiDPI レンダ密度 (logical = pixel / scale)
    // cfg.parent = my_main_window;  // optional: OS モーダル
    // cfg.on_event = [](const std::string& id, bool is_btn_click,
    //                   const elements_modal::value_t& v) { ... };

    std::string layout = R"json({
        "size": [600, 400],
        "background": [40, 40, 50, 255],
        "input": {
            "arrow_focus_nav": true
        },
        "content": {
            "type": "margin", "padding": 20,
            "child": {
                "type": "vtile",
                "children": [
                    { "type": "label", "text": "Hello!" },
                    { "type": "align_right",
                      "child": { "type": "hsize", "width": 100,
                                 "child": { "type": "button",
                                            "id": "ok", "text": "OK",
                                            "initial_focus": true,
                                            "close_on_click": true } } }
                ]
            }
        }
    })json";

    elements_modal::result r;
    if (elements_modal::run_modal(layout, cfg, r)) {
        printf("closed by: %s\n", r.action.c_str());
        // r.values に state widget の最終値マップ
    }

    elements_modal::shutdown();
    SDL_Quit();
    return 0;
}
```

## JSON レイアウト仕様

### トップレベル

| キー | 型 | 説明 |
|---|---|---|
| `size` | `[w, h]` | 推奨論理サイズ (run_modal は上限としてのみ使用、 fit-to-content で実サイズが決まる) |
| `background` | `[r, g, b, a]` | content 描画前の塗りつぶし色 (省略時は透明 / 縁取りなし) |
| `locale` | `"ja-JP"` 等 | label の `"locale"` 未指定時に使う既定ロケール (CJK 同形漢字の出し分け用) |
| `pad_theme` | `"xbox"` / `"ps"` / `"switch"` / `"keyboard"` / `"none"` | content build 前に global pad theme を切り替える (任意)。 未指定なら呼出側がセットした既存値を維持。 `pad_icon` の name 解決に効く |
| `content` | element | ルート要素 |
| `input` | object | キー / パッドナビゲーション設定 (後述) |
| `vars` | `{name: string, ...}` | 変数 store 初期値。 `label.text_var` の読み手、 focusable の `vars_on_focus` の書き手が共通参照する (後述) |
| `strings` | `{id: {lang: string}}` | i18n 文字列テーブル (StringStore)。 `text_id` / `options_id` の解決元 (後述「i18n」節) |
| `lang` | `"ja"` 等 | i18n の初期表示言語。 実行中の切替はホストの `set_language()` |
| `transitions` | `{action: target}` | JSON 駆動ランナ向けの画面遷移定義。 マニフェスト駆動の `app.jsonc` と組み合わせて使う (後述) |
| `atlases` | `{name: spec, ...}` | テクスチャアトラス事前ロード。 `atlas_image` / `atlas_button` / `atlas_slider` 等が名前で参照する pixmap_ptr を content build 前に解決 (後述「アトラス共有」節) |
| `font_scale` | number | 明示 `size` を持たない button / toggle / radio / check_box / label の既定フォント倍率 (既定 1.0 = 従来一致) |
| `style` | object | 未指定値の既定をまとめて与えるテーマ入口 (下記「style ブロック」)。 いずれも省略で従来一致 |

#### `style` ブロック

```jsonc
"style": { "font_scale": 1.25, "row_height": 44, "tile_gap": 8, "padding": 24 }
```

| キー | 意味 |
|---|---|
| `font_scale` | top-level `font_scale` と同じ (両方あれば style 側が優先) |
| `tile_gap` | `"gap"` 未指定の `vtile` / `htile` の子要素間隙間 px (spacer 自動挿入と等価) |
| `row_height` | `button` / `toggle_button` / `check_box` / `slide_switch` / `input_box` / `selection_menu` の既定最小高 px。 固定高ウィジェット (button 等) にも効く専用 proxy で行高を確保する (自然サイズがこれより大きければ影響しない)。 button は行いっぱいの body を描くので「高さのあるボタン」になる |
| `padding` | `content` 全体を包む外側余白 px (`background` の内側) |

既定で「詰まった」見た目になるのを避けたいとき、 spacer を並べずにこのブロック 1 つで
余白 / 行間 / 行高 / フォントの密度をまとめて指定できる。 将来のテーマ一括指定
(9patch / atlas スキン等) はこのブロックを拡張して受ける想定。

### 要素タイプ (`"type"`)

#### レイアウト系
- `vtile` / `htile` — 縦・横タイル。 `"children"`: 子要素配列。 `"gap"`: 子要素間の隙間 px (省略時は `style.tile_gap`、 それも無ければ 0 = 密着。 spacer 自動挿入と等価)。
- `margin` — 内側マージン。 `"padding"`: `[l, t, r, b]` / `[h, v]` / `n`、 `"child"`: 子要素。
- `top_margin` / `left_margin` / `right_margin` / `bottom_margin` — 単方向マージン。 `"value"` + `"child"`。
- `hsize` / `vsize` — 固定サイズ。 `"width"` / `"height"` + `"child"`。
- `hmin_size` / `vmin_size` — 最小サイズ制約。 `"width"` / `"height"` + `"child"`。
- `hspacer` / `vspacer` — 固定スペーサ。 `"width"` / `"height"`。
- `spacer` — 2D 固定スペーサ。 `"size": [w, h]` または `"width"` / `"height"`。
- `align_center` / `align_left` / `align_right` / `align_top` / `align_middle` / `align_bottom` / `align_center_middle` — 子要素の整列。 `"child"`。 X 軸 = `left/center/right`、 Y 軸 = `top/middle/bottom`、 両軸中央 = `align_center_middle`。
- `box` — 単色塗り。 `"color": [r, g, b, a]`。
- `band` — 単色背景帯 + child の重ね合わせ (= `layer` のショートカット)。 `"color": [r, g, b, a]` + 任意 `"child"`。 child 省略時は `box` 相当。 footer 帯やタイトルバーなど、 背景を引きつつ中身を上に乗せたい局所要素に使う。 将来 `gradient` / `image` フィールドを追加予定。
- `layer` — 重ね順。 `"children"`: 先頭が最前面。
- `group` — タイトル付きフレーム。 `"title"` + `"label_size"` + `"child"`。
- `scroller` — 縦スクロール領域。 `"child"`。
- `filler` — 親 tile の余り領域を埋める素の (透明 + 完全 stretchy) スペーサ。 引数なし。
- `floating` — `"at": [x, y, w, h]` + `"child"`。 親 bounds に関係なく child を指定矩形に固定配置 (lib の `floating_element` 薄ラッパ)。 PSD でデザインされたレイアウトをそのまま絶対座標で組む用。
- `canvas` — `"width"` / `"height"` (任意、 省略時は親 view extent) + `"children": [...]`。 子要素は通常の dispatch object に `"at": [x, y, w, h]` を加えるだけで、 内部の composite が **親 bounds の origin に rect をオフセット** して子を配置する (= 親 bounds の左上を基準とする相対座標)。 root に置けば bounds origin = (0, 0) なので絶対座標に見えるが、 別 canvas にネストすると外側 canvas が割り当てた領域の中で相対配置になる (排他グループの分離等で nested canvas を使う場面で重要)。 PSD ベース UI の主役。 追加オプション:
  - `"choice_nav": true` — この canvas の selectable な直接子 (atlas_choice / radio_button) をまとめて **1 フォーカスの左右トグルグループ**にする (後述「choice_nav」節)。
  - 子要素の `"at_var": "varname"` — 配置 rect を変数 store で駆動 (後述「変数 store」節)。

#### 入力 / state widget
- `label` — `"text"` + `"size"` (フォントサイズ、 **px 絶対**) + `"locale"` + `"color": [r,g,b,a]` (任意) + `"text_var": "varname"` (任意、 後述の **変数 store** から動的に text を取る、 指定時は `text` は初期値の fallback)。 倍率で指定したい場合は `"size_scale"` を使用 (テーマ既定 `label_font._size` ≒ 14px に対する比)。 両方指定時は `size` 優先。 追加バリエーション:
  - `"text_id": "id"` — i18n。 StringStore の textID で現在言語の訳文を表示、 言語切替に追従 (後述「i18n」節。 優先順位 text_id > text_var > 静的 text)。
  - `"text_list": [s0, s1, ...]` + `"index_var": "varname"` — **指定番号表示ラベル**。 変数 store の値 (10 進 index 文字列) で text_list の 1 エントリを選んで表示し、 変数変更に追従する。 picker の `index_var` と同名にすると選択連動 (機種別 SPEC 表示等)。 範囲外 index は無視 (現状維持)。
- `text_box` — 複数行・自動折返しの静的テキスト (cycfi `static_text_box`)。 `"text"` + `"size"` (px 絶対) / `"size_scale"` + `"color"` + `"mono"` (真で等幅フォント) + `"text_var"` (label と同じ変数 store 購読。 setVar で本文を丸ごと差替え)。 幅は親 (`hsize` 等) が決め、 高さは折返し結果に追従。 長文は親に `scroller` を置く (ライセンス表示等の長文ビューア向け、 行 label を大量に並べるより軽い)。
- `button` — `"text"` + `"id"` (任意)。 後述の **focusable / interactive 属性** をサポート。
- `checkbox` / `check_box` — `"text"` + `"id"` + `"value"` (初期 bool)。
- `toggle_button` — `"text"` + `"id"` + `"value"`。
- `slide_switch` — `"id"` + `"value"`。
- `input_box` — `"placeholder"` + `"id"` + `"size"` (相対サイズ)。
- `selection_menu` — `"id"` + `"options": [...]` + `"selected"` (初期 index)。

#### console / pad 系 widget

- `invert_button` — focus すると地色と文字色が反転する momentary button。 `"text"` + `"id"` + `"size"` (**px 絶対**) または `"size_scale"` (倍率)。 button と同じく `initial_focus` / `close_on_click` をサポート。
- `ring_button` — focus すると外周にリング装飾が出る momentary button。 `"text"` + `"id"` + `"outline": [r,g,b,a]` (default white) + `"size"` (px) / `"size_scale"` (倍率)。 同じく `initial_focus` / `close_on_click` をサポート。
- `cycle_picker` — `< value >` 形式。 ←→ で循環 (端で wrap)。 `"options": [...]` + `"initial": int` (index、 default 0) + `"id"` + `"initial_focus"` + `"font_size": double` (**px 絶対**、 内部テキスト) または `"font_size_scale"` (倍率)。 値変化で `value_t{int64_t index}` を発火。 picker 系共通の追加フィールド:
  - `"options_id": [...]` — i18n。 各要素を StringStore の textID として現在言語で解決 (`options` より優先)。 言語切替で選択 index を維持したまま表示文字列だけ差し替わる (後述「i18n」節)。
  - `"index_var": "varname"` — 選択 index を変数 store と**双方向**連動。 build 時に初期 index を書き込み (text_list ラベルや rect_list 画像と初期表示を揃える)、 選択変更のたびに set する。 変数に既に値があれば initial として採用。 さらに**変数→picker の追従** (ホストの set_var 一発で表示と依存 widget が揃って切り替わる。 quiet = on_change 非発火なのでエコーバックしない)。 範囲外/パース不能な値は無視。
  - `"enabled_var": "varname"` (cycle_picker / atlas_cycle_picker のみ) — 選択肢の有効/無効 mask を変数連動にする。 値は index 順の `'0'`/`'1'` 文字列 (例 `"10111011"` = index 1 と 4 を無効)。 mask より後ろの index は有効扱い。 step / click / pad は無効 index をスキップ (wrap 継続)、 現在選択が無効化されたら最寄りの有効 index へ進めて on_change 発火 (依存 widget が追従)。 隠し要素 (未開放の機種など) の動的出し分けに使う。
- `framed_cycle_picker` — `[<] [ value ] [>]` の 3 ボックス框付き。 フィールドは `cycle_picker` と同じ (`font_size` / `options_id` / `index_var` も対応)。
- `segmented_picker` — `[ A | B | C ]` 形式 (選択 segment 反転)。 端で **clamp** (wrap しない)。 フィールドは `cycle_picker` と同じ (`font_size` / `options_id` / `index_var` も対応)。
- `atlas_cycle_picker` — 画像矢印ボタン式の cycle_picker (アトラス素材、 「アトラス共有」節参照)。
- `slider` — 0..1 範囲の素のスライダ。 `"id"` + `"initial": double` (default 0.5)。 値変化で `value_t{double pos}` を発火。 thumb / track はホワイト固定。
- `slider_with_range` — `[min] [track] [max]` のラベル付きスライダ。 `"id"` + `"min": int` + `"max": int` + `"initial": double` (min..max スケール、 default 中央) + `"font_size": double` (**px 絶対**、 min/max ラベル) または `"font_size_scale"` (倍率)。 値変化で `value_t{double (min + (max-min)*pos)}` を発火。
- `labeled_row` — 左カラム固定幅ラベル + 残り child の 1 行コンテナ。 `"label": string` + `"label_width": int` (default 180) + `"font_size": double` (**px 絶対**) または `"font_size_scale"` (倍率) + `"child"`。 child の最初の focusable を click-focus target にする。
- `tab_view` — タブ + ページの組合せ (1 画面内で複数 pane を切替)。 `"tabs": [{ "label": string, "child": element, "id"?: string }, ...]` + `"initial": int` (初期 index、 default 0) + `"tab_size": double` (タブ文字 px、 任意) または `"tab_size_scale"` (倍率)。 lib の `deck_composite` + `tab` (basic_choice ベース) を組み合わせて生成。 タブクリックで該当 pane に瞬時切替、 兄弟タブは自動 deselect (basic_choice の choice 機構)。 タブの見た目は **lib 既定の button_styler (角丸 / アクティブ色)**。 各タブの `id` を付ければ shortcut / vars_on_focus 等で参照可能。 状態 (focus 位置 / 入力途中の値 / 変数 store) は session が同じなので保持される。 さらに **PageUp/PageDown** キーが前/次タブ切替に bind される (force shortcut、 テキスト入力中もスキップ)。 **LB/RB** パッドボタンは組込デフォルトの `page_prev/next` アクション (PageUp/Down 合成) 経由で同じ切替に届く (画面 JSON の `"bindings"` で差替可)。
- `pad_icon` — Kenney input-prompts のコントローラアイコン。 `"name": logical_name` (例 `"face_south"` / `"a"` / `"dpad_up"` 等、 下記参照) + 以下のいずれか:
  - **SVG モード (default)**: `"height": logical_pixels` (default 48) + `"colored": bool` (default false、 true で `_color_` バリアントを優先 — Xbox / PS の face button のみ対応、 無ければ通常版にフォールバック) + `"outline": bool` (default false、 true で `*_outline.svg` バリアントを優先、 colored と併用時は `_color_*_outline.svg` → `_color_*.svg` → `*_outline.svg` → `*.svg` の順でフォールバック)
  - **font モード**: `"use_font": true` + `"size": px` (**px 絶対**) または `"size_scale"` (倍率) + `"color": [r,g,b,a]` (任意、 default 白) — Kenney 同梱 TTF + codepoint で label として描画。 ベクター + フォントの両対応 (画面用途は SVG、 本文インラインは font 推奨)。

  `"color"` は **font モード時のみ反映**。 SVG モードでは元 SVG の色がそのまま出るため指定は無視され警告が出る (SVG の tint は canvas API 拡張が必要で現状未対応)。 アイコンに任意色を当てたい場合は `"use_font": true` を選ぶ。

  theme は top-level `"pad_theme"` で切り替え。 名前 / theme で解決できない場合、 SVG モードでは灰色プレースホルダ、 font モードでは `[name]` フォールバック label を描画して layout は維持する。

  `name` に使える論理名 (theme 横断):
  - **Steam Input style**: `face_south` / `face_east` / `face_west` / `face_north` / `dpad_up` / `dpad_down` / `dpad_left` / `dpad_right` / `lb` / `rb` / `lt` / `rt` / `lstick` / `rstick` / `lstick_press` / `rstick_press` / `start` / `back` (= `select`) / `home` / `share`
  - **theme-native** (xbox): `a` / `b` / `x` / `y` / `view` / `menu` / `share` / `guide`
  - **theme-native** (ps): `cross` / `circle` / `square` / `triangle` / `options` / `touchpad` / `playstation` / `l1` / `r1` / `l2` / `r2` / `l3` / `r3`
  - **theme-native** (switch): `a` / `b` / `x` / `y` / `l` / `r` / `zl` / `zr` / `plus` / `minus` / `capture` / `home` / `sl` / `sr`
  - **theme-native** (keyboard): `keyboard_enter` / `keyboard_space` / `keyboard_escape` / `keyboard_arrow_{up,down,left,right}` / `keyboard_a`…`keyboard_z` / `keyboard_0`…`keyboard_9` 等

#### 画像 / sprite / 9-patch 系

PSD でデザインされた固定サイズ / 固定位置のビットマップ UI を JSON 化する用途向け。 通常は `canvas` + `at` で配置する (canvas 内では `at` は canvas 自身の bounds 左上を原点とする相対座標)。 ファイル単位で 1 枚画像を読む素直な版と、 1 枚のアトラス画像を共有する版があり、 後者は別セクション (「アトラス共有」) で扱う。

- `sprite_button` — 縦 strip スプライト (4 〜 5 frame: normal / hilite / pressed / pressed_hilite / disabled) で状態切替する momentary button。 `"image": "path"` + `"frame_height": px` + `"scale": float` (任意) + `"id"`。 frame は上から順に縦並びを仮定。 frame が 4 未満のときは欠けた状態をフォールバックする (pressed+hilite→pressed→normal) ので、 **3 frame (normal/hilite/pressed)** だけでも可。 その場合マウス押下 (hilite 中) もキーボード押下も同じ pressed frame を表示する。 `initial_focus` / `close_on_click` / `vars_on_focus` 対応。
- `gizmo_image` — 9-patch / 3-patch 画像。 `"image": "path"` + `"axis": "9" | "h" | "v"` (既定 `"9"`) + `"scale": float`。 親 layout が与える bounds に合わせて中央部分が伸縮する (lib の `gizmo` / `hgizmo` / `vgizmo`)。 frame / background 等の伸縮素材向け。

#### アトラス共有 (`atlases` / `atlas_image` / `atlas_button` / `atlas_slider`)

1 枚のテクスチャアトラス画像を pixmap として共有し、 sub-rect 切り出しで複数の要素を構築する。 同じ画面で何度も使う部品はアトラスにまとめると I/O とメモリが減り、 PSD/Photoshop で 1 シートに描いた UI をそのまま読み込めるようになる。

top-level の `"atlases"` でアトラスを名前付きで事前ロードしておき、 個々の要素で `"atlas": "<name>"` で参照する:

```jsonc
{
    "atlases": {
        "ui": { "path": "resources/ui_atlas.png", "scale": 1.0 },
        // 短縮形: "ui": "resources/ui_atlas.png"  (= scale 1.0)
    },
    "content": {
        "type": "canvas", "width": 1920, "height": 1080,
        "children": [
            { "at": [80, 160, 200, 80],
              "type": "atlas_button", "atlas": "ui", "id": "ok",
              "frames": {
                "normal":         [0,   0, 200, 80],
                "hilite":         [0,  80, 200, 80],
                "pressed":        [0, 160, 200, 80],
                "pressed_hilite": [0, 240, 200, 80],
                "disabled":       [0, 320, 200, 80]
              } },
            { "at": [580, 140, 128, 128],
              "type": "atlas_image", "atlas": "ui", "rect": [220, 0, 128, 128] },
            { "at": [80, 360, 400, 32],
              "type": "atlas_slider", "atlas": "ui", "id": "vol",
              "track": [220, 140, 256, 16],
              "thumb": [220, 170,  32, 32], "initial": 0.5 }
        ]
    }
}
```

- `atlases` の値は string (= path 短縮形) または `{path, scale}` object。 path は `resource_base` 起点で解決 (絶対パスはそのまま)。
- `atlases` は content build より**先に**解決されるので、 同じ JSON 内のどこから参照しても OK。
- 名前未登録のアトラスを参照すると build エラーログを出して該当要素はスキップ。

各要素タイプ:

- `atlas_image` — `"atlas": name` + `"rect": [x, y, w, h]` (アトラス内座標) + `"stretch_h"` / `"stretch_v"` (任意、 既定 false)。 既定は固定サイズ (= 飾り)。 stretch_h/v: true で当該軸を stretchable に (= 親 floating の bounds に合わせて伸縮)。 追加バリエーション:
  - `"rect_list": [[x,y,w,h], ...]` + `"index_var": "varname"` — ソース矩形リストを**変数 store の index で切替**える (rect より優先)。 picker の `index_var` と同名にすると選択連動 (機種別スクリーンショット等)。 範囲外/パース不能な値は無視 (現状維持)。 limits は現在矩形基準なので全 rect 同寸法を推奨 (canvas の `at` 固定配置で使う)。
- `image` — **単一画像ファイルをパス指定で読み込み**、 与えられた bounds にアスペクト比維持で fit 描画する (atlas 非依存)。 `"image": "path"` (resource_loader 経由で解決、 JPEG/PNG/WEBP 対応 = ThorVG。 BMP は非対応) + `"scale": float` (任意、 指定時は fit でなく native×scale 固定サイズ)。 パスが **`"mem://<name>"`** ならファイル VFS でなく**ホスト注入画像ストア** (実行時に登録した名前→画像バイト) から読む。 読込失敗 (未登録 / ファイル無し / デコード失敗) は空要素になる (レイアウト維持・無描画)。 pixmap は build 時に一度読むので、 実行時差し替えは画像を再登録して画面を開き直す。 セーブサムネイル等の動的画像に使う (ホスト側は自前で PNG 等にエンコードして注入する)。
- `atlas_button` — `"atlas": name` + `"frames": ...` + `"id"`。 frames は **object** (`{normal, hilite, pressed, pressed_hilite, disabled}` の順で値があるところまで使う) または **array** (`[[x,y,w,h], ...]` 順番固定) のどちらか。 sprite_button_styler で frame 自動切替 (4 frame で normal/hilite/pressed/pressed_hilite を仮定、 disabled を含めれば 5)。 frame が 4 未満なら欠けた状態をフォールバック (pressed+hilite→pressed→normal) するので **3 frame (normal/hilite/pressed)** でも可。 PSD 由来の「通常 / オーバー / 押し下げ」 3 状態ボタンはこの形になり、 マウス押下とキーボード押下が同じ pressed frame を表示する。 `initial_focus` / `close_on_click` / `vars_on_focus` 対応。
- `atlas_toggle` (別名 `atlas_check`) — 2 値保持型。 frames は object `{off_normal, off_hilite, on_normal, on_hilite, disabled}` または array (順番固定)。 `"initial": bool` で初期値、 `"id"` + 値変化で `value_t{bool}` を発火。 `"value_var": "name"` (任意) で変数 store と連動: 変数に既存値があれば初期状態を上書き (`"0"`/`"false"`/空 = off)、 クリックで `"0"`/`"1"` を書き戻し、 変数変更は状態へ反映のみ (イベント非発火)。
- `atlas_choice` (別名 `atlas_radio`) — 排他選択型 (ラジオボタン)。 frames は atlas_toggle と同じ。 同じ親 composite (`canvas` の layer など) に並べた複数の atlas_choice 群は、 1 個 ON になると他を自動 OFF (lib の `basic_choice` の `find_composite` + 兄弟スキャン)。 `"selected": bool` で初期状態 (1 グループ内で 1 つだけ true 推奨)。 値変化で `value_t{bool true}` を発火 (新しく選ばれた側のみ。 deselect 側は `on_click` 走らず)。 `"selected_var"` / `"selected_value"` (任意) で変数 store と双方向連動 (下記「変数連動ファミリ一覧」参照)。 **排他スコープは「直近の親 composite」単位** — 複数の独立グループを 1 画面で使うときは、 各グループを別々の composite (= ネストした `canvas` や `layer` / `htile` 等) に入れて分離する (下記「排他グループの分離」参照)。
- `atlas_slider` — 0..1 スライダ。 `"atlas": name` + `"track": [x,y,w,h]` + `"initial": double` (0..1、 既定 0.5) + `"vertical": bool` (既定 false) + `"id"`。 見た目は 2 形式:
  - **thumb 形式**: `"thumb": [x,y,w,h]`。 track はスライダ軸方向に stretchable / 直交軸固定、 thumb は完全固定。 **`track` は省略可** — 溝 (バー) が背景画像側に描いてある素材向けで、 見えない stretchable 要素が敷かれて thumb だけ描画される (可動域は widget の bounds いっぱい)。
  - **fill 形式 (ゲージ型)**: thumb の代わりに `"fill": [x,y,w,h]` を指定すると、 atlas_progress と同じ track+fill 描画のまま**操作可能** (クリック/ドラッグ/矢印キー/パッド) なスライダになる。 `"fill_at": [dx,dy,w,h]` (任意) で fill の配置先を track ソース矩形の左上原点 px で指定 (枠の内側にバーが入るインセット素材向け)。
  - どちらも値変化で `value_t{double pos}` を発火。 `"value_var": "name"` (任意) で変数 store と連動: 変数変更で値が追従 (通知のみ、 イベント非発火)、 ユーザ操作の on_change は通常どおり発火。 値は `"0.75"` 形式の 10 進文字列。
- `atlas_progress` — 非インタラクティブのゲージ。 `"atlas": name` + `"track": [x,y,w,h]` + `"fill": [x,y,w,h]` + `"fill_at": [dx,dy,w,h]` (任意、 fill の配置インセット。 atlas_slider と同義) + `"value": double` (0..1 静的) + `"value_var": "name"` (任意、 変数 store キー、 string→double で reactive) + `"vertical": bool`。
- `atlas_cycle_picker` — **画像矢印ボタン式ピッカー**。 選択モデル (step / wrap / ←→ キー / パッド横軸) は `cycle_picker` と同一で、 描画をアトラス素材に置き換えたもの。 **フォーカス中は左右矢印が hilite フレームになる (= フォーカス表示を兼ねる)**。 クリックは left_at / right_at のヒットで ∓1 ステップ、 それ以外はフォーカス取得のみ。

  ```jsonc
  { "type": "atlas_cycle_picker", "atlas": "ui", "id": "machine",
    "left":  { "normal": [x,y,w,h], "hilite": [x,y,w,h] },   // 左矢印 (hilite 省略時 normal と同じ)
    "right": { "normal": [x,y,w,h], "hilite": [x,y,w,h] },   // 右矢印
    "left_at":  [dx,dy,w,h],   // 矢印/テキストの配置。 widget bounds 左上原点の相対 px
    "right_at": [dx,dy,w,h],
    "text_at":  [dx,dy,w,h],   // 選択テキスト表示領域 (中央寄せ描画)
    "options": ["A", "B"],     // または "options_id" (i18n、 options より優先)
    "initial": 0, "font_size": 30, "color": [r,g,b,a],
    "index_var": "machine" }   // 任意: 選択 index を変数 store と連動
  ```

  値変化で `value_t{int64_t index}` を発火。 `options_id` / `index_var` / `initial_focus` / `vars_on_focus` の意味は cycle_picker と同じ。

##### atlas_button / atlas_toggle / atlas_choice の text overlay

`"text"` (+ 任意 `"text_size"` / `"text_color"` / `"text_offset": [dx, dy]` / `"locale"`) を指定すると button の上にラベルを重ねる。 内部実装は **非 composite な proxy_base 派生ラッパ** (`label_decoration`) で button (subject) + label (overlay) を保持し、 draw/layout で両方に同じ bounds を流す。

非 composite ラッパであることが atlas_choice の排他動作を維持する鍵:
- `basic_choice::activate/click` は `find_composite` で**親 composite** を取得して兄弟全部の selectable を deselect する
- もし button + label を `layer_composite` 等の composite で wrap すると、 find_composite はそこで止まってしまい、 兄弟 (他の atlas_choice) が見えなくなって排他が壊れる
- proxy_base 派生は composite_base ではないので find_composite が素通りし、 canvas layer まで届く

#### テキスト版 radio_button

lib 既定の塗りつぶし円 + テキスト styler を使ったテキスト版排他ボタン。 atlas を使わない普通の UI 用:

- `radio_button` — `"text": "Easy"` + `"id"` + `"selected": bool` (既定 false)。 同じ親 composite に並べた radio_button 群は atlas_choice と同じ仕組みで自動排他。 値変化で `value_t{bool true}` を発火。 `"selected_var"` / `"selected_value"` (任意) も atlas_choice と同様に対応。

#### 排他グループの分離 (atlas_choice / radio_button)

`basic_choice` の排他は **「自分の直近の親 composite に並ぶ兄弟 selectable 全部」** を対象にする。 つまり 1 つの `canvas` (= 1 つの layer_composite) に radio_button 群と atlas_choice 群を直接並べると、 両者が**同じグループ**として相互排他になる (片方を click すると他方も全部 OFF)。

複数の独立した排他グループを 1 画面に置きたい場合、 **各グループを別々の composite に入れて分離**する。 一番素直なのはネストした `canvas`:

```jsonc
{ "type": "canvas",
  "children": [
    // 難易度グループ (= 内側 canvas の layer_composite が排他スコープ)
    { "at": [80, 700, 600, 32],
      "type": "canvas", "width": 600, "height": 32,
      "children": [
        { "at": [  0, 0, 150, 32], "type": "radio_button", "text": "EASY",   "id": "diff_easy",   "selected": true },
        { "at": [160, 0, 150, 32], "type": "radio_button", "text": "NORMAL", "id": "diff_normal" },
        { "at": [320, 0, 150, 32], "type": "radio_button", "text": "HARD",   "id": "diff_hard"   }
      ] },
    // 言語グループ (= 別の内側 canvas、 排他は内側のみ)
    { "at": [80, 790, 600, 40],
      "type": "canvas", "width": 600, "height": 40,
      "children": [
        { "at": [  0, 0, 160, 40], "type": "atlas_choice", "atlas": "ui", "id": "lang_ja", "selected": true, "text": "JA", "frames": { ... } },
        { "at": [170, 0, 160, 40], "type": "atlas_choice", "atlas": "ui", "id": "lang_en",                   "text": "EN", "frames": { ... } },
        { "at": [340, 0, 160, 40], "type": "atlas_choice", "atlas": "ui", "id": "lang_ko",                   "text": "KO", "frames": { ... } }
      ] }
  ] }
```

`canvas` 以外の composite (`layer` / `htile` / `vtile` 等) でも同じ効果になる。 atlas/text を混在させる場合は **composite を分けないと両方が同一グループ**になる点に注意。

##### 排他スコープの実装メモ (なぜ「直近の親 composite」?)

lib の `basic_choice::activate / click` は次の処理をする:
1. 自分を `value(true)` にして `on_click(true)` 発火
2. `find_composite(ctx)` で親 context 鎖を辿り、 最初に出てくる `composite_base` を取得
3. その composite の全 children に対し `find_element<selectable*>` を試し、 自分以外の selected な selectable を `select(false)` (= deselect)
4. composite の context を refresh

つまり「**find_composite が最初に当たる composite**」が排他スコープ。 atlas_button / atlas_toggle の text overlay は `label_decoration` (proxy_base 派生、 composite ではない) を使うので find_composite を素通りする — find_composite はあくまで **canvas / layer / htile / vtile / 内側 canvas** などの composite_base 派生で止まる。

#### choice_nav (排他グループの 1 フォーカス左右トグル化)

排他グループの内側 canvas に `"choice_nav": true` を付けると、 グループ全体が**方向フォーカスナビゲーションの 1 focusable** になり、 メンバー個別のフォーカスは無くなる:

```jsonc
{ "at": [822, 218, 790, 46],
  "type": "canvas", "width": 790, "height": 46,
  "id": "grp_display_mode", "choice_nav": true,
  "vars_on_focus": { "help": "表示モードを切り替えます。" },
  "children": [
    { "at": [  0, 0, 389, 46], "type": "atlas_choice", "id": "mode_full", "selected": true, "frames": { ... } },
    { "at": [401, 0, 389, 46], "type": "atlas_choice", "id": "mode_dot",  "frames": { ... } }
  ] }
```

- **←→ キー / パッド横軸で選択メンバーが移動** (端で止まる = wrap しない。 端からさらに押した左右は view のフォーカスナビへ素通し — segmented_picker と同じ)。
- 選択変更は**新しく選ばれたメンバーの `on_click(true)`** でイベント発火 (= マウスクリック時と同じ semantics。 event_callback には**メンバーの id** が届く)。
- **フォーカス表示 = 選択中メンバーの hilite 兼用**: グループが focus を持つ間、 選択中メンバーが hilite フレーム (`on_hilite`) で描かれる。 マウス hover の hilite とは独立。
- マウスクリックは従来どおり子へ素通し (排他動作も従来のまま)。
- canvas の `"id"` / `"vars_on_focus"` / `"initial_focus"` は**グループ自体**に配線される (focused-id polling / help 連動 / 初期フォーカスがグループ単位になる)。
- 対象は canvas の **selectable な直接子** (atlas_choice / radio_button)。 ネストした canvas の中までは辿らない。
- 実装: lib の `focus_unit_element` (focus.hpp — サブツリーを方向ナビの 1 focusable として扱う proxy 基底) の派生 `choice_nav_group`。 各行が 1 フォーカス (picker / choice_nav グループ / slider) になり、 上下ナビが行単位で自然になる。

#### resource_base (相対パスの解決起点)

`sprite_button` / `gizmo_image` / `atlases` などが受け取る相対パスは、 `overlay_session::start(..., resource_base)` (または `run_modal` のホスト側設定) で渡された **resource_base** ディレクトリを起点に resolve される。 絶対パスはそのまま使用。 マニフェスト駆動の elements_console ランナは「その画面 JSON が見つかった検索ルート」を resource_base として渡す。

ホストが `set_resource_resolver()` (modal.h 公開 API) でリゾルバを注入すると、 相対パスの解決はすべてそこを通る (`resolve(rel, origin_base)`。 origin_base = その画面の resource_base)。 elements_console はここでマルチルート検索 (origin → 他ルートの登録順) を実装しており、 吉里吉里ホスト等はアーカイブ内検索 (Storages) を差し込める。 未設定なら従来どおり resource_base 前置。 絶対パスと `mem://` はリゾルバを通さない。 `input_defaults.jsonc` のロードも同じ規則。

### フォントサイズ指定の方針

ウィジェットの `size` / `font_size` は **ピクセル絶対値**。 width / height がピクセルである JSON 文化に合わせて、 文字サイズも同じ単位系。 同じウィジェットが代わりに受ける `size_scale` / `font_size_scale` はテーマ既定 (`theme.label_font._size`、 通常 14px) に対する**倍率**。

- `"size": 28` → 28px
- `"size_scale": 2.0` → テーマ既定 × 2.0 (= 28px、 既定が 14 のとき)
- 両方指定された場合は `size` 優先 (px が勝つ)
- 未指定はテーマ既定 (= 14px / scale 1.0)

倍率を残してあるのは、 テーマを差替えたときに連動して大きくしたいケース、 もしくは「基準の何倍」と書きたいケース用。 新規 JSON は基本 px を使う。

### Focusable / interactive 属性

button / checkbox / toggle_button / slide_switch / input_box / selection_menu (= focusable widget) で共通:

| キー | 型 | 説明 |
|---|---|---|
| `id` | string | event_callback / result.values のキー、 shortcut の `target` 参照先 |
| `initial_focus` | bool | true なら起動時にこの要素にフォーカス (複数あれば build 順で先勝ち) |
| `close_on_click` | bool | (button のみ) true で click 時に modal を閉じて `result.action = id` とする。 **デフォルト false** で、 click は外部 callback (= `on_event` / `Dialog.onAction`) を発火するだけ |
| `vars_on_focus` | `{name: string, ...}` | この要素が focus を得たときに変数 store に書き込む値の dict。 同じ変数を `text_var` で見ている label に自動反映 (focus 連動ヘルプテキスト等) |

### 変数 store (`vars` / `text_var` / `vars_on_focus`)

軽量な「変数設定 + 参照 + 更新通知」機構。 lib に侵襲しない方式で、 runtime が focus 変化を毎フレーム poll し、 focused 要素の `vars_on_focus` を変数 store に書き込み、 同名の `text_var` を持つ label に自動で `set_text` する。

```jsonc
{
    "vars": { "current_help": "セーブします。" },   // 初期値

    "content": {
        "type": "vtile",
        "children": [
            // writer: focus されたら "current_help" を書き換える
            { "type": "invert_button", "id": "save",
              "text": "SAVE", "initial_focus": true,
              "vars_on_focus": { "current_help": "ゲームデータをセーブします。" } },
            { "type": "invert_button", "id": "load",
              "text": "LOAD",
              "vars_on_focus": { "current_help": "セーブデータをロードします。" } },

            // reader: 現在の "current_help" を表示
            { "type": "label", "text_var": "current_help",
              "size": 1.6, "color": [255, 255, 255, 255] }
        ]
    }
}
```

- 値型は string のみ (将来拡張余地あり)。
- 同じ変数に複数 label が subscribe してもよい。
- `vars_on_focus` は dict なので 1 widget で複数変数を一度に書ける。
- ホストからも `overlay_session::set_var(name, value)` で書ける (focus poll 以外の書き手。 ソフトウェアキーボードの入力文字列表示のような「ホスト状態 → label」の動的反映に使う)。 反映は次フレームの `render_to_buffer`。
- 初期 focus の widget の `vars_on_focus` は次回 render 前に poll される (= `vars` の初期値は最初の poll までだけ表示される。 通常は初期 focus の値と同じにしておく)。

#### 変数連動ファミリ一覧

変数 store を読む / 書くフィールドの早見。 いずれも同名変数で連動する:

| フィールド | 対象 widget | 向き | 値の形式 |
|---|---|---|---|
| `text_var` | label | 読み | 表示文字列 |
| `vars_on_focus` | focusable 全般 / choice_nav グループ | 書き (focus 時) | 任意 string |
| `text_list` + `index_var` | label | 読み | 10 進 index (`"2"`) |
| `rect_list` + `index_var` | atlas_image | 読み | 10 進 index |
| `index_var` | picker 系 (cycle / framed / segmented / atlas_cycle_picker) | **双方向** (選択変更で書き + 変数変更で quiet 追従 / 既値があれば initial 採用) | 10 進 index |
| `enabled_var` | cycle_picker / atlas_cycle_picker | 読み (選択肢の有効/無効 mask) | `'0'`/`'1'` 文字列 (`"10111011"`) |
| `selected_var` (+`selected_value`) | atlas_choice / radio_button | **双方向** (var == selected_value で選択 / クリックで var へ selected_value を書き戻し) | 任意 string (`selected_value` 既定 `"1"`) |
| `value_var` | atlas_slider / atlas_progress | slider: 読み (通知のみ、 イベント非発火) / progress: 読み | 10 進小数 (`"0.75"`) |
| `at_var` | canvas の任意の子 | 読み | `"x,y"` または `"x,y,w,h"` (10 進 px) |

- `at_var` — canvas 子要素の配置 rect を変数で駆動する (キャラ位置マーカー等の座標アニメ用機構)。 `"x,y"` はサイズ維持で位置のみ、 `"x,y,w,h"` は矩形ごと差し替え。 初期値は `"at"`。 build 時に変数へ既値があれば即適用。 パース不能 / 要素不足の値は無視 (現状維持)。

  ```jsonc
  { "at": [80, 470, 128, 128],
    "type": "atlas_image", "atlas": "ui", "rect": [220, 0, 128, 128],
    "at_var": "chara_pos" }   // set_var("chara_pos", "600,400") で移動
  ```

- `selected_var` — choice をラジオグループ変数パターンで連動させる。 グループ全員に**同じ** `selected_var` と**異なる** `selected_value` を付けると、 「var の現在値 == 自分の selected_value」の 1 個だけが選択状態になる。 ホストの set_var 一発でグループの選択が入れ替わり (quiet、 イベント非発火)、 ユーザクリックでは選ばれた側の `selected_value` が var に書き戻される。 build 時に var へ既値があれば静的 `selected` 指定より優先。 config 画面の初期値注入 (ON/OFF・言語選択等) 向け。

  ```jsonc
  { "type": "atlas_choice", "id": "CHOICE_ON",  "selected_var": "se_mode", "selected_value": "1", ... },
  { "type": "atlas_choice", "id": "CHOICE_OFF", "selected_var": "se_mode", "selected_value": "0", ... }
  // set_var("se_mode", "0") で OFF 側が選択される
  ```

- **picker → text_list / rect_list の選択連動**: picker に `index_var` を付け、 表示側 (label の text_list / atlas_image の rect_list) に同名の `index_var` を付けるだけで、 選択変更が表示に即時反映される (build 時に picker が初期 index を書き込むので初期表示も揃う)。 機種選択 → スクリーンショット / SPEC 表示のような連動 UI が JSON だけで組める。

### i18n (`strings` / `lang` / `text_id` / `options_id`)

textID → 言語別文字列の対応表 (StringStore) による実行時多言語化。 言語切替は再 build なしで全 widget に反映される:

```jsonc
{
    "lang": "ja",                                  // 初期言語
    "strings": {
        "menu.save":  { "ja": "セーブ", "en": "Save" },
        "opt.speed.slow":   { "ja": "遅い",   "en": "Slow" },
        "opt.speed.normal": { "ja": "普通",   "en": "Normal" }
    },
    "content": { ... }
}
```

- label / button 系の `"text_id": "menu.save"` — 現在言語の訳文を表示。 `"text"` は i18n 非対応ランタイム / 未知 id 向けの静的 fallback。
- picker 系の `"options_id": ["opt.speed.slow", ...]` — options を textID で与える (`options` より優先)。 言語切替時は**選択 index を維持**したまま表示文字列だけ `set_options` で差し替わる。
- 未知 id は id 文字列をそのまま表示。 現在言語にエントリが無ければ先頭言語へフォールバック。
- 実行中の言語切替はホスト API (`overlay_session::set_language(lang)` / navigator 経由)。 subscribe 済みの全 label / picker が再解決される。

### 画面遷移 (`transitions` + マニフェスト)

複数画面を JSON だけで切り替えたいケース向けの軽量ランナ仕様。 ホスト (例: `elements_console.exe`) は **マニフェスト JSON** (= entry 画面 + 画面名 → ファイルマップ) を読み、 各画面 JSON の top-level `"transitions"` を見て次画面を決める。 ホストには C++ で遷移を書く必要がない。 遷移の解釈とスタック駆動は `navigator` が担う (下記「ホスト連携」)。

#### マニフェスト JSON (例: `app.jsonc`)

```jsonc
{
    "entry": "sysmenu",
    "screens": {
        "sysmenu": "sysmenu.jsonc",
        "command": "command.jsonc",
        "config1": "config1.jsonc"
    }
}
```

- `entry`: 起点画面名 (= `screens` のキー)
- `screens`: 画面名 → JSON ファイルの相対パス。 マニフェストファイル自身のあるディレクトリ起点で解決する。

公開 API: `elements_modal::parse_app_manifest(json_utf8) -> app_manifest`。 ok=true なら entry / screens が埋まる。

#### `"transitions"` ブロック (各画面 JSON 内)

```jsonc
{
    "background": [20, 18, 24, 255],
    "vars": { ... },
    "transitions": {
        "command": { "target": "command", "effect": "fade", "duration": 220 },
        "config":  "config1",
        "quit":    "<exit>",
        "":        "<back>"
    },
    "content": { ... }
}
```

- key = action id (= `id` を持つ button や picker の id)。 空文字 `""` は Esc / B / 右クリック / `close()` 等の空 action を捕まえる。
- value は **string** か **object**:
  - string: target のみ短縮形 (例 `"command"` / `"<back>"` / `"<exit>"` / `"<replace:foo>"` / `"<stay>"`)
  - object: `{"target": "...", "effect": "fade", "duration": 220}` — effect / duration を併記

target syntax:
| 値 | 動き |
|---|---|
| `"foo"` | マニフェストの screens に登録された `foo` を **push** |
| `"<back>"` | 現画面を **pop** (= 1 つ戻る) |
| `"<exit>"` | アプリ **終了** |
| `"<replace:foo>"` | 現画面を `foo` に**すげ替え** (stack 不変) |
| `"<stay>"` | 現画面を**再 enter** (stack 不変) |

未定義 action のフォールバックは `resolve_transition()` / `navigator` (下記「ホスト連携」) が担う。 既定は「空 action: entry なら exit / 子画面なら pop」「非空の未定義 action: entry なら stay / 子画面なら pop」。

#### エフェクト

| `effect` | 動き |
|---|---|
| `""` / 省略 | 即切替 (デフォルト) |
| `"fade"` | クロスフェード (旧画面の最終フレーム ↔ 新画面を時間で lerp 混色) |
| `"universal"` | rule 画像によるユニバーサルトランジション。 追加キー `"rule"` (画像パス、 解決・ロードはホスト責務) / `"vague"` (境界ぼかし幅、 rule 値スケール 0-255、 既定 64) |

`duration`: ms。 省略 / 0 でホスト既定 (= 200ms 程度)。 未対応 effect は警告 + 即切替フォールバック。

`"fade"` の混色そのものは `<elements_modal/effects.h>` の `blend_argb8888(from, to, t, out, count)` (ヘッダオンリー) を使う。 旧画面の最終フレームを `from`、 新画面を `to`、 `t = elapsed / duration` (0→1) で channel 別 lerp する。 `out` は `to` と同一バッファでよい (in-place)。

`"universal"` の混色は同ヘッダの `blend_universal_argb8888(from, to, rule, phase01, vague, out, count)`。 `rule` は from/to と同画素数の 8bit グレースケールバッファ (ロード・スケーリングはホスト責務) で、 値が小さい画素ほど早く `to` へ切り替わる。 `phase01` (0→1) が内部で `0 → 255+vague` の閾値スイープに展開される。 rule バッファを用意しないホストは fade へフォールバックすればよい。

#### ホスト連携: `navigator` (画面遷移ドライバ)

`<elements_modal/navigator.h>` は、 上記の transitions / マニフェスト仕様に従って **画面名スタックを駆動するドライバ**を提供する。 ホストが `get_result().action` の lookup・`<back>`/`<replace:…>` 等の解釈・スタック操作・focus / 言語メモリを自前で実装する必要をなくす (SDL / `overlay_session` 非依存・単体テスト可能)。 `overlay_session` の生成・描画・入力転送・ファイル読込は引き続きホストの責務。

| API | 役割 |
|---|---|
| `resolve_transition(action, transitions, is_entry) -> nav_step` | 純関数。 action を transitions で解決し `nav_step{action, name, effect, duration_ms, rule, vague}` を返す。 `nav_action` は `push` / `pop` / `replace` / `stay` / `exit` |
| `navigator(app_manifest)` | スタック + manifest + 画面ごと focus 記憶 + 表示言語の保持 |
| `navigator::reset_to(entry)` | スタックを `{entry}` に初期化 (entry 空ならマニフェストの entry) |
| `navigator::advance(action, transitions) -> nav_step` | session 完了時に呼ぶ。 `is_entry` を深度から自動判定して解決 + スタック更新。 戻り値で effect 演出を処理 |
| `navigator::current()` / `empty()` | 現画面名 / 終了状態 |
| `navigator::remember_focus(screen, id)` / `focus_to_restore(screen)` | 再入時 focus 復元用 (id 空は無視) |
| `navigator::set_language(lang)` / `language()` | 画面遷移をまたぐ表示言語の保持。 start 後に `overlay_session::set_language()` で再適用する |
| `navigator::screen_file(name)` | manifest 登録の相対パス (未登録は空文字列 → ホストのフォールバックに委ねる) |

典型的なホストループ (session 完了 → 次画面ロード):

```cpp
// session が finished() になったら:
nav.remember_focus(nav.current(), sess.focused_id());
auto step = nav.advance(sess.get_result().action, sess.transitions());
if (step.effect == "fade" && !nav.empty()) { /* 旧フレームを snapshot して blend_argb8888 */ }
sess.reset();
if (nav.empty()) { /* 終了 */ }
else {
   // nav.current() の JSON を nav.screen_file()+manifest_dir で解決してロード、 start
   if (!nav.language().empty())               sess.set_language(nav.language());
   if (auto& id = nav.focus_to_restore(nav.current()); !id.empty()) sess.focus_by_id(id);
}
```

standalone な最小実例は **`examples/navigator_screens.cpp`** (`-DELEMENTS_MODAL_BUILD_EXAMPLES=ON` で `elements_modal_navigator_example` を生成。 inline JSON 3 画面を menu→settings/about で push/pop/fade 遷移)。 マニフェスト (ファイル) 駆動・i18n・pad 連携まで含む実アプリ例は elements_console の `main.cpp`。

### パーツ演出 (`"animate"`) — Phase A

任意の要素に `"animate"` を付けると、 その要素を **見た目だけ動かす変換**でスライド/ポップ/回転させられる (周囲のレイアウトは動かさない非 reflow オーバーレイ)。 既定では画面表示時 (enter) に再生され、 `overlay_session` が毎フレーム駆動する。 `"on"` で発火タイミングを focus / 決定 (select) に変えられる (下記)。 ホストに演出コードは不要。

```jsonc
{ "type": "label", "id": "title", "text": "START",
  "animate": {
    "type": "move",          // move | scale | rotate | fade
    "from": [-220, 0],       // move/scale は [x,y] or スカラ、 rotate は度、 fade は %
    "to":   [0, 0],
    "frames": 18,            // 再生フレーム数 (60fps 換算)。 代わりに "duration_ms" 可
    "easing": "out_cubic"    // 一般イージング (下記)。 台形指定があればそちら優先
  }
}
```

| フィールド | 意味 |
| --- | --- |
| `"type"` | `move`(平行移動 px) / `scale`(拡縮率) / `rotate`(度) / `fade`(透過%) — 既定 `move` |
| `"from"` / `"to"` | 開始・終了値。 move/scale は `[x,y]` か単一スカラ、 rotate は度、 fade は % (0..100) |
| `"frames"` / `"duration_ms"` | 再生時間。 `frames` は 60fps 換算 (要望のフレーム数指定)。 既定 300ms |
| `"easing"` | `linear` / `in_out` / `out_cubic` / `in_back` 等の一般イージング |
| `"accel"` / `"decel"` | 台形速度プロファイルの加速・減速割合 (0..1)。 指定すると easing より優先。 加速→等速→減速を別々に制御する要望仕様 |
| `"loops"` | ループ/明滅回数 (`0`=ループ無し / `N`=N 回 / `-1`=無限)。 `"yoyo": true` で往復 (明滅 1 回 = 2 pass) |
| `"pivot"` | 拡縮/回転の起点 `[ox,oy]` (0..1, 既定中央 `[0.5,0.5]`)。 左上起点は `[0,0]` |
| `"delay"` | 発火から再生開始までの待ち (フレーム数。 ms は `"delay_ms"`)。 待機中は `from` で固定。 スタッガー/シーケンスに使う |
| `"on"` | 発火トリガ `enter`(既定) / `focus` / `select` / `exit` / `hover` / `change`。 下表参照 |

`"animate"` は配列でも書ける。 各エントリは同じ変換状態を共有するので、 **移動 + 拡縮 + 回転の同時掛け**が自然に合成される (例: スライドしながら拡大)。

#### 発火トリガ (`"on"`)

| 値 | タイミング | 用途 |
| --- | --- | --- |
| `enter` (既定) | 画面表示時に 1 回 | 登場演出 (スライドイン / ポップ / フェードイン) |
| `focus` | 要素が focus を得た瞬間に前進、 失った瞬間に逆再生で復帰 | カーソル移動による選択強調 / 選択・非選択の切替 |
| `select` | 要素が決定 (button click / Enter) された瞬間に 1 回 | 押下フィードバックのポップ |
| `hover` | 要素にマウスが乗った瞬間に前進、 外れた瞬間に逆再生で復帰 (focus と対称) | マウスオーバー強調 |
| `change` | 要素の値が変わった瞬間に 1 回 (checkbox/toggle/slider 等) | 値変更フィードバック |
| `exit` | 画面を閉じる操作 (Esc / B / 閉じるボタン / `close()`) の瞬間に再生し、 **完了してから実際に終了**する | 退場演出 (スライドアウト / フェードアウト) |

- `focus` / `hover` / `select` / `change` は要素の `"id"` に紐付き、 その id への発火だけに反応する (要素に `"id"` が必須。 plain `button` 含む focusable は自動追跡。 `hover` は button 系のみ)。
- **focus と hover の併用**: `"input":{"hover_focus":true}` (hover で自動 focus) を使うと hover が focus も誘発し focus トリガと多重発火しうる。 `"input":{"focus_anim":false}` で focus トリガ演出を止められる (hover 演出だけ使う運用)。
- `focus` の `"from"` は **静止状態 (= 非選択時の見た目)** と一致させること。 発火前は `from` 側で静止し、 focus 取得で `to` へ、 喪失で `from` へ戻る。 ループ指定 (`loops≠1`) の focus 演出は喪失時に即 `from` へ戻す (逆再生しない)。
- **exit×遷移の協調**: 終了要求 (`close()` / Esc / `close_on_click` button) があると、 exit 束縛があれば即終了せず exit 演出を再生し、 完了後に `finished()` が true になる (この間は入力を受け付けない)。 これにより退場演出を見せてから画面遷移できる。 **exit 演出は有限長にすること** (無限ループ `loops:-1` は完了しないので終了がハングする)。 exit 束縛が無ければ従来どおり即終了。
- enter 以外のトリガはホストの自動駆動 (enter = 表示時 / focus = 変化時 / select = button click / exit = 終了要求時) のほか、 `overlay_session::play_animation(trigger, id)` で手動発火もできる。

内部構成 (個別利用も可能):

- `<elements_modal/tween.h>` — 一般イージング (`ease`) と台形プロファイル (`trapezoid`)、 ループ/往復つき `tween` 再生器。 SDL/Elements 非依存でヘッドレス検証可。
- `<elements_modal/transform.h>` — `xform_state` (移動/拡縮/回転/ピボット/透過) を共有して掛ける非 reflow 変換 proxy `xform()`。
- `<elements_modal/animator.h>` — `anim_binding` (進捗 tween → チャンネル) を束ねて毎フレーム tick する `animator`。

透過 (`fade`) は canvas のグループ不透明度 (`canvas::global_alpha`) で実装 (Phase B)。 `xform_state.opacity` を fill/stroke/text/image の alpha に乗算する方式で、 オフスクリーン合成は行わない (重なり部分の厳密合成ではなく各シェイプ独立の alpha 乗算)。 `"loops": -1` + `"yoyo": true` で明滅 (BLINK) になる。

### `"input"` ブロック

view 全体のナビゲーション設定。 全フィールドが任意:

```jsonc
"input": {
    // 矢印キー / dpad / 左 stick (focus モード) で 2D 方向移動を有効化
    "arrow_focus_nav": true,

    // マウスが乗った focusable にキーボードフォーカスも移す (既定 true)。
    // false でマウス hover とフォーカスを独立させる。
    "hover_focus": true,

    // 軸モード: disabled / focus / value / both
    // デフォルト: dpad=both / left_stick=focus / right_stick=value / trigger=disabled
    "dpad_mode":        "both",
    "left_stick_mode":  "focus",
    "right_stick_mode": "value",
    "trigger_mode":     "disabled",

    // アナログ感度 (任意)
    "stick_deadzone":    0.15,
    "stick_value_speed": 1.0,

    // pad button → key 合成の上書き (デフォルト A=Enter / B=Esc / X=Shift+Tab / Y=Tab / D-Pad=矢印)
    "pad_bindings": [
        { "pad": "a", "key": "enter" },
        { "pad": "x", "key": "tab", "mods": ["shift"] }
    ],

    // 任意 key / pad → 要素 id のショートカット
    // force: true で text-editing widget 占有中でもバイパス
    "shortcuts": [
        { "key": "f",  "mods": ["ctrl"], "target": "search_btn" },
        { "pad": "lb",                    "target": "cancel" },
        { "pad": "rb",                    "target": "ok", "force": true }
    ],

    // ---- named-action バインド (overlay_session) ----

    // 2D 方向移動の端で反対端へ回り込む (要 arrow_focus_nav、 既定 false)
    "focus_wrap": true,

    // 2D 方向移動が disabled (enabled_var="0" 等) の要素を飛ばす (既定 false)
    "skip_disabled": true,

    // focus モード軸 (dpad / stick) の長押しリピート。 rate 0 = 倒し量で
    // 60〜250ms 可変 (既定)。 delay 既定 400ms
    "repeat_delay_ms": 400,
    "repeat_rate_ms": 80,

    // 画面を開いた時の初期フォーカス (id 指定)。 要素側 "initial_focus": true
    // と併存した場合はこちらが勝つ
    "initial_focus": "BTN_START",

    // 入力 → named action のバインド (組込デフォルトへの差分)。
    // 同一入力の再宣言で上書き、 "action": "none" で無効化。
    // mouse は "right"/"middle" (左クリックは widget 直接操作)、 wheel は "up"/"down"
    "bindings": [
        { "key": "escape", "action": "cancel" },
        { "pad": "b",      "action": "cancel" },
        { "mouse": "right","action": "cancel" },
        { "pad": "start",  "action": "open_menu" },   // 未知 action → ホスト通知
        { "wheel": "up",   "action": "scroll_up" }
    ],

    // action 発火時の SE 名 (ホスト通知のみ、 再生はホスト責務)。
    // キーはカテゴリ (nav / accept / cancel / page / scroll) または
    // 個別 action 名・button id (そちらが優先)
    "se": { "nav": "cursor.ogg", "accept": "ok.ogg", "cancel": "cancel.ogg" },

    // cursor-warp ナビ: キー/パッドでフォーカスが動いたとき、 ホストが実マウス
    // カーソルをフォーカス先の hot point へ warp する運用を有効化 (既定 false)。
    // session は take_key_focus_move() でワンショット通知するだけで、 実際の
    // warp / カーソル非表示はホスト責務。 hover 由来 (hover_focus) の移動では
    // 発火しない。 warp によりカーソルがフォーカス widget に乗るので、 hover の
    // 見た目 (hilite フレーム / hover 演出) がフォーカスに自然追従する
    "cursor_warp": true
}
```

widget 側の `"focus_point": [ax, ay]` (0..1 アンカー比、 既定 [0.5,0.5]=中心)
で warp の飛び先を個別調整できる。 widget 既定: slider = thumb 中心 (トラック
クリックの値ジャンプ防止)、 choice_nav グループ = 選択中メンバー中心、 他 =
bounds 中心。

#### named-action と組込デフォルト標準バインド (overlay_session)

「閉じる / 決定 / フォーカス移動 / ページ送り / スクロール」は **名前付き
アクション**への 3 層バインドで決まる (後勝ち):
①組込デフォルト → ②`input_defaults.jsonc` (resource_base 直下、 起動後の
初回 start() で 1 回ロード・キャッシュ。 top-level が `"input"` ブロックと
同形) → ③画面別 `"input"."bindings"`。

| action | 意味 | 既定バインド (key / pad / mouse) |
|---|---|---|
| `accept` | 決定 (focus widget を起動) | Enter† / A / 左click (widget 直) |
| `cancel` | 戻る / 閉じる (`begin_finish("")`) | Esc / B / **右click** |
| `nav_up/down/left/right` | focus 方向移動 | 矢印キー† / dpad† / 左stick† |
| `focus_prev/next` | tab 順移動 | Shift+Tab† / Tab† / X, Y |
| `page_prev/next` | ページ / タブ送り | PageUp/Down† / LB, RB |
| `scroll_up/down` | スクロール | — / 右stick(value)† / ホイール |
| `scroll_page_up/down` | ページ単位スクロール | (既定バインド無し) |
| (その他任意名) | ホスト通知 (`onAction("<action>", 名前)`) | — |

† = ネイティブ経路 (view の focus dispatch / arrow nav / axis 機構 /
tab_view の PageUp/Down shortcut) がそのまま実装。 これらのキーを同じ意味の
action に再宣言しても登録はスキップされる (キー合成の自己再帰防止)。

- `cancel` / `none` の force 既定は true (text input focus 中も効く)。
  他 action は false (text 編集優先)。 `"force"` で個別上書き可。
- Esc の旧 hard-code (`on_key_down` 直 `begin_finish`) は撤廃済み。 バインド
  差し替え / 無効化で画面ごとに戻る挙動を制御できる。
- SE はアクション発火時に `event_callback("<se>", false, SE名)` で通知
  (`nav` は focus 変化検出、 `accept` は button click で一元発火)。

#### 名前リファレンス

- `key`: `"enter"` / `"escape"` (`"esc"`) / `"tab"` / `"space"` / `"backspace"` / `"delete"` / `"insert"` / `"left"` / `"right"` / `"up"` / `"down"` / `"page_up"` (`"pgup"`) / `"page_down"` (`"pgdn"`) / `"home"` / `"end"` / `"a"`〜`"z"` / `"0"`〜`"9"` / `"f1"`〜`"f12"`
- `mods` 配列要素: `"shift"` / `"ctrl"` (`"control"`) / `"alt"` / `"super"` (`"cmd"` / `"command"`) / `"action"` (= Ctrl on Win/Linux, Cmd on Mac)
- `pad`: `"a"` / `"b"` / `"x"` / `"y"` / `"dpad_up"` / `"dpad_down"` / `"dpad_left"` / `"dpad_right"` / `"lb"` (`"l1"`) / `"rb"` (`"r1"`) / `"lt"` (`"l2"` / `"lt_click"`) / `"rt"` (`"r2"` / `"rt_click"`) / `"l3"` / `"r3"` / `"back"` / `"start"` / `"guide"`
- `pad_axis_mode`: `"disabled"` / `"focus"` / `"value"` / `"both"`

## イベント callback

`config.on_event` (run_modal) と `overlay_session::start(..., external_cb)` は同一シグネチャ:

```cpp
using event_callback = std::function<void(
    const std::string& id,
    bool is_button_click,
    const value_t& payload)>;
```

- `is_button_click=true`: button click。 `payload` は空 (`bool false` 相当)。 `close_on_click=true` の button だけがその後 modal を終了させる。
- `is_button_click=false`: state widget の値変化。 `payload` は実値:
  - `checkbox` / `toggle_button` / `slide_switch` → `bool`
  - `input_box` → `std::string`
  - `selection_menu` → 選択された option 文字列 (`std::string`)

## 使い方 (overlay_session)

ホスト側のサーフェスに描画 + ホストがイベント転送する低レベル API:

```cpp
elements_modal::overlay_session sess;
sess.start(json_utf8, /*view_w*/ 600, /*view_h*/ 400, /*pixel_scale*/ 2.0f,
           [](auto const& id, bool is_btn, auto const& v) {
               // 全イベント (state + 全 button click) がここに来る
           });

while (!sess.finished()) {
    // SDL イベントを host が掬って on_xxx に流す
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            sess.on_mouse_down(ev.button.x, ev.button.y,
                               ev.button.button, SDL_GetModState());
            break;
        case SDL_EVENT_KEY_DOWN:
            sess.on_key_down(ev.key.key, ev.key.mod);
            break;
        case SDL_EVENT_TEXT_INPUT:
            sess.on_text_input(ev.text.text);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            sess.on_pad_button(ev.gbutton.button, true);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            sess.on_pad_axis(ev.gaxis.axis, ev.gaxis.value);
            break;
        // ... etc
        }
    }
    // 自前で確保したピクセルバッファに描画 + 表示
    overlay_session::render_rect rect;
    sess.render_to_buffer(my_buffer, buf_w_px, buf_h_px,
                          surface_w, surface_h, rect);
    // rect の位置にバッファを表示
}

auto const& result = sess.get_result();
// result.action / result.values
```

`render_to_buffer` の出力 `render_rect` は surface 論理座標での描画矩形 (中央配置済み)。 ホスト側はこの位置にテクスチャを貼る。

`close_on_click=true` な button click で session が自動 finish するので、 host は `sess.finished()` でループ脱出を判定する。

### 入力転送の戻り値 (handled / pass-through)

`on_key_down` / `on_key_up` / `on_pad_button` は **`bool` (このダイアログが入力を消費したか)** を返す。 `true` = Esc / focus 中 widget が処理 / 既知パッドボタン、 `false` = 未処理。 ホストが**複数 UI を重ねる / ゲームと共存させる**場合、 この戻り値を見て「ダイアログが使わなかったキーはゲーム側へ素通しする」といったキーボードフォーカスの pass-through を実装できる (戻り値を無視すれば従来どおり)。 `on_text_input` / `on_mouse_*` / `on_pad_axis` は `void` のまま。

## pad_icon の前提セットアップ

`pad_icon` を使う JSON を読む前に、 アセット配置とフォント登録をホスト側で
済ませる必要がある (`elements_modal::init()` 自体は ThorVG init しかしない
ので、 アセット系は呼出元の責務)。 最低限の流れ:

```cpp
#include <elements/element/pad_icon.hpp>

namespace ce = cycfi::elements;

// 1. Kenney Input Prompts pack を <theme>/{vector,*.ttf} の構成で配置した
//    ベースディレクトリを設定 (default は空)
ce::set_pad_icon_base_dir("resources/kenny_input_prompts");

// 2. font モードを使う場合は TTF を elements の font system に登録
ce::load_pad_icon_fonts();

// 3. JSON 内に "pad_theme" が無い場合のデフォルトを (任意で) セット
ce::set_pad_theme(ce::pad_theme::xbox);
```

### Kenney Input Prompts pack について

`pad_icon` が描画する SVG / TTF は [Kenney.nl](https://kenney.nl/) が CC0
ライセンスで配布している **Input Prompts** pack
([https://kenney.nl/assets/input-prompts](https://kenney.nl/assets/input-prompts))
のアセットを前提にしている。 CC0 なので商用 / 改変 / 再配布いずれも自由だが、
本リポには含めていないので利用側で取得 → 配置する必要がある。

pack は zip 1 ファイルで配布されており、 展開すると `Xbox Series` /
`PlayStation Series` / `Nintendo Switch` / `Keyboard & Mouse` のテーマ別
ディレクトリの下に `Vector/` (SVG) と `Fonts/` (TTF + map.txt) が並ぶ構成
になっている。 これを `pad_icon` が読みに行く以下のレイアウトに変換する:

```
<base_dir>/
├── xbox/
│   ├── vector/*.svg
│   ├── kenney_input_xbox_series.ttf
│   └── kenney_input_xbox_series_map.txt
├── ps/
│   ├── vector/*.svg
│   └── kenney_input_playstation_series.ttf + _map.txt
├── switch/
│   ├── vector/*.svg
│   └── kenney_input_nintendo_switch.ttf + _map.txt
└── keyboard/
    ├── vector/*.svg
    └── kenney_input_keyboard_&_mouse.ttf + _map.txt
```

### 配置スクリプト (`scripts/copy_kenney_assets.sh`)

zip 展開後のディレクトリから上記レイアウトへの変換は本リポ同梱の
`scripts/copy_kenney_assets.sh` で自動化できる (bash + msys2 / WSL 等)。

```bash
# 1. https://kenney.nl/assets/input-prompts から zip をダウンロード

# 2. consuming repo の root に展開 (default src = ./kenney_input-prompts/)
unzip kenney_input-prompts.zip -d kenney_input-prompts

# 3. スクリプトを consuming repo の root から実行
./external/elements_modal/scripts/copy_kenney_assets.sh
# → ./resources/kenny_input_prompts/ 配下に配置される

# 4. 任意の src / dst を渡したい場合
./external/elements_modal/scripts/copy_kenney_assets.sh \
    /path/to/kenney_input-prompts /path/to/dst
```

スクリプトは CWD を基準にデフォルトパスを組み立てるので、 consuming repo の
root から実行するのが想定動作。 ホスト側コードでは
`set_pad_icon_base_dir("resources/kenny_input_prompts")` のようにそのまま
dst を指定する。 元 zip ディレクトリを `.gitignore` しておくと安全。

## ビルド

C++20 が要る。 依存:

- SDL3 (`SDL3::SDL3` or `SDL3-shared` ターゲット)
- cycfi::elements (`add_subdirectory(external/elements)` 等で提供)
- thorvg (Elements の transitive、 静的リンク推奨)
- picojson (header-only)

```cmake
add_subdirectory(external/elements_modal)
target_link_libraries(myapp PRIVATE elements_modal::elements_modal)
```

standalone サンプル (`-DELEMENTS_MODAL_BUILD_EXAMPLES=ON` で生成):
- `examples/hello_modal.cpp` — 単一モーダル (`run_modal`)。 ターゲット `elements_modal_hello_example`。
- `examples/navigator_screens.cpp` — 複数画面遷移 (`overlay_session` + `navigator`)。 ターゲット `elements_modal_navigator_example`。

## 設計メモ

### run_modal の window fit-to-content

`run_modal` は 2 パスで内容に合わせた window サイズを決める:

1. JSON parse 後、 view を `cfg.width × cfg.height` (要求サイズ) の extent で作成 → `view.content()` の中で `set_limits()` 内部発火 → `view.limits()` が有効に。
2. `vlim.min` を自然サイズとして取り、 要求サイズを上限にクランプ。 `view.size()` で縮め、 SDL_Window を fit ピクセルサイズで生成。

ループ内の中央配置ロジックは残置 — ユーザがリサイズして大きくしたら自動センタリング、 小さくしたら content はみ出し / クランプ。 `cfg.width / cfg.height` は「上限として機能」する。

### Elements 新 API への接続

Elements 本体 (2026-06-06 以降) で `view::pad_button_event` / `view::pad_axis_event` / `view::arrow_focus_navigation` / `view::bind_shortcut` 等が入った。 elements_modal は SDL イベントをそのまま `view` の対応関数に流すだけ。 旧来の `key_intercept` wrap や `focus_pending` Tab 注入は撤去済み。

### close_on_click の意味論

button click は外部 callback (`on_event` / `external_cb`) を**常に**呼んでから `close_on_click` で modal 終了を判定する。 これにより:

- `close_on_click=false` (デフォルト) → callback だけ発火、 modal は継続
- `close_on_click=true` → callback 発火後、 modal が `result.action = id` で確定して終了

非閉じボタンは「テキスト編集中の Apply / Reset / Preview 系」、 閉じるボタンは「OK / Cancel 系」に対応する。

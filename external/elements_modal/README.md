# elements_modal

純粋 SDL3 + [Elements](https://github.com/wamsoft/elements) + [ThorVG](https://www.thorvg.org/) ベースの JSON モーダル UI ライブラリ。

SDL3 を使う任意のアプリから単体で利用できる。

## 特徴

- JSON / JSONC (行コメント・ブロックコメント・末尾カンマ) でレイアウト定義
- 独立 SDL_Window でモーダル表示 (`run_modal`) — 内容に合わせた window サイズで生成 + 閉じるまでブロック
- 既存サーフェスへのオーバーレイ (`overlay_session`) — ホスト側がイベント / 描画ループを駆動
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

### 要素タイプ (`"type"`)

#### レイアウト系
- `vtile` / `htile` — 縦・横タイル。 `"children"`: 子要素配列。
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

#### 入力 / state widget
- `label` — `"text"` + `"size"` (フォントサイズ比) + `"locale"` + `"color": [r,g,b,a]` (任意) + `"text_var": "varname"` (任意、 後述の **変数 store** から動的に text を取る、 指定時は `text` は初期値の fallback)。
- `button` — `"text"` + `"id"` (任意)。 後述の **focusable / interactive 属性** をサポート。
- `checkbox` / `check_box` — `"text"` + `"id"` + `"value"` (初期 bool)。
- `toggle_button` — `"text"` + `"id"` + `"value"`。
- `slide_switch` — `"id"` + `"value"`。
- `input_box` — `"placeholder"` + `"id"` + `"size"` (相対サイズ)。
- `selection_menu` — `"id"` + `"options": [...]` + `"selected"` (初期 index)。

#### console / pad 系 widget

- `invert_button` — focus すると地色と文字色が反転する momentary button。 `"text"` + `"id"` + `"size"` (相対フォントサイズ、 default 1.0)。 button と同じく `initial_focus` / `close_on_click` をサポート。
- `ring_button` — focus すると外周にリング装飾が出る momentary button。 `"text"` + `"id"` + `"outline": [r,g,b,a]` (default white) + `"size"`。 同じく `initial_focus` / `close_on_click` をサポート。
- `cycle_picker` — `< value >` 形式。 ←→ で循環 (端で wrap)。 `"options": [...]` + `"initial": int` (index、 default 0) + `"id"` + `"initial_focus"` + `"font_size": double` (内部テキストの相対サイズ、 default 1.0)。 値変化で `value_t{int64_t index}` を発火。
- `framed_cycle_picker` — `[<] [ value ] [>]` の 3 ボックス框付き。 フィールドは `cycle_picker` と同じ (`font_size` も対応)。
- `segmented_picker` — `[ A | B | C ]` 形式 (選択 segment 反転)。 端で **clamp** (wrap しない)。 フィールドは `cycle_picker` と同じ (`font_size` も対応)。
- `slider` — 0..1 範囲の素のスライダ。 `"id"` + `"initial": double` (default 0.5)。 値変化で `value_t{double pos}` を発火。 thumb / track はホワイト固定。
- `slider_with_range` — `[min] [track] [max]` のラベル付きスライダ。 `"id"` + `"min": int` + `"max": int` + `"initial": double` (min..max スケール、 default 中央) + `"font_size": double` (min/max ラベルの相対サイズ、 default 1.0)。 値変化で `value_t{double (min + (max-min)*pos)}` を発火。
- `labeled_row` — 左カラム固定幅ラベル + 残り child の 1 行コンテナ。 `"label": string` + `"label_width": int` (default 180) + `"font_size": double` (相対、 default 1.0) + `"child"`。 child の最初の focusable を click-focus target にする。
- `pad_icon` — Kenney input-prompts のコントローラアイコン。 `"name": logical_name` (例 `"face_south"` / `"a"` / `"dpad_up"` 等、 下記参照) + 以下のいずれか:
  - **SVG モード (default)**: `"height": logical_pixels` (default 48) + `"colored": bool` (default false、 true で `_color_` バリアントを優先 — Xbox / PS の face button のみ対応、 無ければ通常版にフォールバック)
  - **font モード**: `"use_font": true` + `"size": relative_size` (default 1.0) — Kenney 同梱 TTF + codepoint で label として描画。 ベクター + フォントの両対応 (画面用途は SVG、 本文インラインは font 推奨)

  theme は top-level `"pad_theme"` で切り替え。 名前 / theme で解決できない場合、 SVG モードでは灰色プレースホルダ、 font モードでは `[name]` フォールバック label を描画して layout は維持する。

  `name` に使える論理名 (theme 横断):
  - **Steam Input style**: `face_south` / `face_east` / `face_west` / `face_north` / `dpad_up` / `dpad_down` / `dpad_left` / `dpad_right` / `lb` / `rb` / `lt` / `rt` / `lstick` / `rstick` / `lstick_press` / `rstick_press` / `start` / `back` (= `select`) / `home` / `share`
  - **theme-native** (xbox): `a` / `b` / `x` / `y` / `view` / `menu` / `share` / `guide`
  - **theme-native** (ps): `cross` / `circle` / `square` / `triangle` / `options` / `touchpad` / `playstation` / `l1` / `r1` / `l2` / `r2` / `l3` / `r3`
  - **theme-native** (switch): `a` / `b` / `x` / `y` / `l` / `r` / `zl` / `zr` / `plus` / `minus` / `capture` / `home` / `sl` / `sr`
  - **theme-native** (keyboard): `keyboard_enter` / `keyboard_space` / `keyboard_escape` / `keyboard_arrow_{up,down,left,right}` / `keyboard_a`…`keyboard_z` / `keyboard_0`…`keyboard_9` 等

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
- 初期 focus の widget の `vars_on_focus` は次回 render 前に poll される (= `vars` の初期値は最初の poll までだけ表示される。 通常は初期 focus の値と同じにしておく)。

### `"input"` ブロック

view 全体のナビゲーション設定。 全フィールドが任意:

```jsonc
"input": {
    // 矢印キー / dpad / 左 stick (focus モード) で 2D 方向移動を有効化
    "arrow_focus_nav": true,

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
    ]
}
```

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

`examples/hello_modal.cpp` に standalone サンプルあり (`elements_modal_hello_example` ターゲット)。

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

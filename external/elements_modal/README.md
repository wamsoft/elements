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
| `content` | element | ルート要素 |
| `input` | object | キー / パッドナビゲーション設定 (後述) |

### 要素タイプ (`"type"`)

#### レイアウト系
- `vtile` / `htile` — 縦・横タイル。 `"children"`: 子要素配列。
- `margin` — 内側マージン。 `"padding"`: `[l, t, r, b]` / `[h, v]` / `n`、 `"child"`: 子要素。
- `top_margin` / `left_margin` / `right_margin` / `bottom_margin` — 単方向マージン。 `"value"` + `"child"`。
- `hsize` / `vsize` — 固定サイズ。 `"width"` / `"height"` + `"child"`。
- `hmin_size` / `vmin_size` — 最小サイズ制約。 `"width"` / `"height"` + `"child"`。
- `hspacer` / `vspacer` — 固定スペーサ。 `"width"` / `"height"`。
- `spacer` — 2D 固定スペーサ。 `"size": [w, h]` または `"width"` / `"height"`。
- `align_center` / `align_left` / `align_right` — 子要素の整列。 `"child"`。
- `box` — 単色塗り。 `"color": [r, g, b, a]`。
- `layer` — 重ね順。 `"children"`: 先頭が最前面。
- `group` — タイトル付きフレーム。 `"title"` + `"label_size"` + `"child"`。
- `scroller` — 縦スクロール領域。 `"child"`。

#### 入力 / state widget
- `label` — `"text"` + `"size"` (フォントサイズ比) + `"locale"`。
- `button` — `"text"` + `"id"` (任意)。 後述の **focusable / interactive 属性** をサポート。
- `checkbox` / `check_box` — `"text"` + `"id"` + `"value"` (初期 bool)。
- `toggle_button` — `"text"` + `"id"` + `"value"`。
- `slide_switch` — `"id"` + `"value"`。
- `input_box` — `"placeholder"` + `"id"` + `"size"` (相対サイズ)。
- `selection_menu` — `"id"` + `"options": [...]` + `"selected"` (初期 index)。

### Focusable / interactive 属性

button / checkbox / toggle_button / slide_switch / input_box / selection_menu (= focusable widget) で共通:

| キー | 型 | 説明 |
|---|---|---|
| `id` | string | event_callback / result.values のキー、 shortcut の `target` 参照先 |
| `initial_focus` | bool | true なら起動時にこの要素にフォーカス (複数あれば build 順で先勝ち) |
| `close_on_click` | bool | (button のみ) true で click 時に modal を閉じて `result.action = id` とする。 **デフォルト false** で、 click は外部 callback (= `on_event` / `Dialog.onAction`) を発火するだけ |

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

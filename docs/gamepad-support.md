# ゲームパッド対応

Elements (ThorVG ポート版) はキーボード操作と同じプラミング上に **ゲームパッド**
対応を載せている。離散ボタンはキー入力として合成し、アナログスティック / トリガは
別経路でアナログ値として届ける。SDL3 ホスト (`ELEMENTS_HOST_UI_LIBRARY=sdl`) でのみ
動作する (Win32 ホストは現時点で未対応)。

## 既定マッピング

### 離散ボタン → キー合成

| pad | デフォルト | 動作 |
|---|---|---|
| A | Enter | 選択 / ボタン発火 |
| B | Esc | キャンセル / メニュー閉じる |
| X | Shift+Tab | フォーカス戻し |
| Y | Tab | フォーカス進め |

D-Pad は **キー合成しない** (アナログ軸機構に直結する)。LB / RB / 三角キー / L3 /
R3 / Start / Back / Guide は既定で何も bind されていない (shortcut で利用する想定)。

### 軸モード既定

| 入力 | mode | 動作 |
|---|---|---|
| D-Pad | `both` | 値変更を試み、消費されなければフォーカス移動 (=現在の矢印キーと同じ) |
| 左スティック | `focus` | フォーカス移動のみ (閾値超え+オートリピート) |
| 右スティック | `value` | フォーカス中の値系 widget の値を傾き量に応じて連続調整 |
| LT / RT | `disabled` | 何もしない |

## API

### 設定 (`view`)

```cpp
// ボタン → キー合成 (デフォルトを書き換えたいときに使う)
view.bind_pad_button(pad_button::lb, key_code::page_down);
view.unbind_pad_button(pad_button::start);

// ショートカット (キーでもパッドでも同じ仕組み)
view.bind_shortcut(pad_button::lb, prev_btn);       // 直接 element 起動
view.bind_shortcut(pad_button::rb, next_btn);
view.bind_shortcut(
   key_info{key_code::f1, mod_control}, help_btn);
view.bind_shortcut(pad_button::guide,
   [&]{ show_overlay = !show_overlay; });           // コールバック直接
view.unbind_shortcut(pad_button::lb);

// 軸モード
view.dpad_mode(pad_axis_mode::focus);
view.left_stick_mode(pad_axis_mode::both);
view.right_stick_mode(pad_axis_mode::value);
view.trigger_mode(pad_axis_mode::disabled);

// アナログ感度
view.stick_deadzone(0.18f);          // 既定 0.15
view.stick_value_speed(1.5f);        // 既定 1.0 (= max tilt で 1 秒に 0→1)
```

### widget 側 (任意のアナログ受け口)

```cpp
class element {
   virtual bool pad_axis(context const& ctx, pad_axis_info info);
};
```

戻り値 `true` で消費。`both` モード時に `false` を返した場合のみフォーカス移動へ
フォールスルーする。

実装パターン (slider/dial/thumbwheel 内部より):

```cpp
double step = info.value
            * ctx.view.stick_value_speed()
            * ctx.view.frame_dt();
edit_value(clamp(value() + step, 0.0, 1.0));
```

### `consumes_text` でショートカットを抑制

`basic_text_box::consumes_text()` が `is_focus() && editable()` を返すように
なっている。input_box にフォーカスがある間は、`force=false` で登録された
shortcut は発火しない。明示的に `Ctrl+S` のような修飾付きを `force=true` で
登録すれば編集中も発火する。

```cpp
view.bind_shortcut(
   key_info{key_code::s, mod_action},
   save_btn,
   /*force=*/true);
```

## 軸モードの内部動作

`view::poll()` (Host の event loop から毎フレーム呼ばれる) で:

1. `frame_dt` を更新 (前回 poll からの経過秒)。デバッガ停止などの飛びを吸収するため 0.1s でクランプ
2. 8 軸 (D-Pad x/y, L-stick x/y, R-stick x/y, LT, RT) を順に処理
3. mode に応じて:
   - **value**: フォーカス先 widget の `pad_axis(info)` を呼ぶ。`info.value` は -1〜+1 (トリガは 0〜1)
   - **focus**: |tilt| > 0.5 を初めて越えたフレームで矢印キーを 1 回 synthesize、その後 400ms 待って オートリピート (60〜250ms tilt 連動)
   - **both**: value 試行 → 戻り値 false なら focus へフォールスルー
4. D-Pad は SDL 上では離散ボタン (UP/DOWN/LEFT/RIGHT) として届くが、view が dpad_x/y 軸状態に変換するので mode 機構を同じく使える

### スティック → フォーカス移動の挙動

| 操作 | 結果 |
|---|---|
| しきい値 (0.5) 越え | 即座に矢印キーを 1 回 synthesize |
| そのまま保持 | 400ms 後にオートリピート開始 |
| リピート間隔 | `60 + (1-tilt) * 190` ms (60〜250ms, tilt 大ほど高速) |
| しきい値以下に戻す | 状態リセット (再度越えたとき again エッジトリガ) |

### スティック → 値変更の挙動 (slider など)

```
delta_per_frame = stick_value_speed * frame_dt * info.value
new_value       = clamp(value + delta_per_frame, 0, 1)
```

スティックを max tilt にすると 1 秒で 0→1 (デフォルト)、半分なら 2 秒で 0→1。

## widget の軸割り当て

| widget | 値変更 ON 時に消費する軸 |
|---|---|
| 横長 slider | 横軸 (dpad_x / left_x / right_x) |
| 縦長 slider | 縦軸 (dpad_y / left_y / right_y) |
| dial | 横軸 |
| thumbwheel | 縦軸 |

軸が合わないとき `pad_axis()` は `false` を返すので、`both` モードの場合
フォーカス移動側で消費される。

## SDL3 ホスト統合

`app.cpp` で `SDL_INIT_GAMEPAD` を初期化。`base_view.cpp` のイベントループで:

- `SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`: `SDL_OpenGamepad` / `SDL_CloseGamepad`
- `SDL_EVENT_GAMEPAD_BUTTON_DOWN` / `UP`: `view::pad_button_event({btn, down})`
- `SDL_EVENT_GAMEPAD_AXIS_MOTION`: `view::pad_axis_event({axis, value/32767})`

メソッド名に `_event` サフィックスを付けているのは、MSVC で view クラススコープ
内の名前検索が enum 型 `pad_button` / `pad_axis` を同名メンバ関数で隠してしまう
ためのワークアラウンドである。

ゲームパッドイベントはウィンドウに紐付かないため、現在 SDL 入力フォーカスを
持つビュー (なければ最初に登録されたビュー) に届ける。

## サンプル

`examples/key_driven/` を参照。同じ画面でキーボードとゲームパッドの両方が動作する。
ゲームパッドが接続されているとき:
- LB / RB を「Move focus to slider」「Reset Latch」相当の shortcut にバインド
- 左スティックで widget 間をフォーカス移動 (focus mode)
- 右スティックで focused slider/dial/thumbwheel の値を傾き量に応じて変更 (value mode)
- D-Pad は both モード (キー入力 ↑↓←→ と同等)

# キーボード操作と 2D フォーカスナビ

Elements (ThorVG ポート版) は **テキスト編集以外の全 widget でキー操作可能**
になっている。テキストボックスだけが `wants_focus` を返していた上流の状態から、
ボタン / スライダー / ダイヤル / サムホイール / メニューにフォーカスとキー
ハンドラを追加し、加えて view レイヤで以下の機能を提供する。

- Tab / Shift+Tab による先頭⇄末尾ループ
- 矢印キーによる **2D 方向フォーカスナビ** (有効/無効を切替可能)
- 初期フォーカスを宣言的または手続き的に指定

## 一覧

| キー | 動作 |
| --- | --- |
| `Tab` | 次のフォーカス対象へ。末尾で先頭にループ |
| `Shift+Tab` | 前のフォーカス対象へ。先頭で末尾にループ |
| `Space` / `Enter` / `Kp_Enter` | フォーカス中のボタン系を発火 (momentary / toggle / latching / choice / button_menu のポップアップ展開) |
| `← →` | 横長 slider と dial で値変更。それ以外は (オプション ON 時) 横方向の widget へフォーカス移動 |
| `↑ ↓` | 縦長 slider と thumbwheel で値変更。それ以外は (オプション ON 時) 縦方向の widget へフォーカス移動 |
| `Home` / `End` | フォーカス中の値系 widget で min / max |
| `PageUp` / `PageDown` | フォーカス中の値系 widget で ±0.1 |
| `Esc` | メニューポップアップを閉じる / ドラッグキャンセル (既存挙動) |

矢印キーは **値操作優先** で、フォーカス中の widget が消費しなかった (= 軸が合わない or
そもそも値系でない) ときだけフォーカスナビが発動する。

## widget ごとの軸割り当て

| widget | ←→ | ↑↓ |
| --- | --- | --- |
| **横長 slider** (`_is_horiz=true`) | 値 ±0.05 | ナビへ通す |
| **縦長 slider** (`_is_horiz=false`) | ナビへ通す | 値 ±0.05 |
| **dial** (`basic_dial`) | 値 ±0.05 | ナビへ通す |
| **thumbwheel** (`thumbwheel_base`) | ナビへ通す | y 値 ±0.05 |
| **button / check / radio / button_menu** | 全方向ナビへ通す | 全方向ナビへ通す |

`_is_horiz` は `slider_base::limits()` 内で「track の最大幅 > 最大高さ」かどうかで自動判定される。
明示指定の API は無いが、レイアウト (`hsize`/`vsize`) で形状を決めれば従う。

## API

### view レベル

```cpp
class view {
   // 既存のキー入力に加えて 2D 方向フォーカスナビを有効化する。
   // OFF (デフォルト) では従来の挙動 (矢印は値系 widget でのみ反応)。
   void arrow_focus_navigation(bool on);
   bool arrow_focus_navigation() const;

   // 2D ナビが端に到達したとき反対端へ回り込む (コンソール UI のループ
   // メニュー)。要 arrow_focus_navigation。OFF (デフォルト) では端で停止。
   void arrow_focus_wrap(bool on);
   bool arrow_focus_wrap() const;

   // 2D ナビが disabled (is_enabled()==false) の要素を飛ばす。
   // OFF (デフォルト) では従来どおり disabled もフォーカス対象。
   // ※Tab 巡回は composite 側の機構のため対象外 (2D ナビのみ)。
   void focus_skip_disabled(bool on);
   bool focus_skip_disabled() const;

   // focus モード軸 (dpad / stick) の長押しリピート間隔。delay_ms は初回
   // リピートまでの待ち (既定 400)、rate_ms は固定間隔。rate_ms=0 (既定)
   // は従来の倒し量スケール (60〜250ms 可変) を維持する。
   void axis_repeat(int delay_ms, int rate_ms);
   int  axis_repeat_delay() const;
   int  axis_repeat_rate() const;

   // 任意の要素へプログラムでフォーカスを移動する。
   // 指定要素は view のツリーに既に含まれている必要がある。
   // 呼び出しは次の idle で実行 (event 配送中でも安全)。
   void focus(element_ptr e);
};
```

### 宣言的初期フォーカス

`<elements/element/focus.hpp>` で提供される `initial_focus(...)` ラッパー。
初回 layout 時に view.focus(this) を呼んで自分自身にフォーカスを設定する。

```cpp
#include <elements/element.hpp>   // focus.hpp も透過的に取り込まれる

view_.content(
   initial_focus(button("OK")),   // 起動直後はこのボタンが focus 状態
   background
);
```

## 2D フォーカスナビのアルゴリズム

1. フォーカス中の widget の `key()` にまず配送 → slider / dial / thumbwheel が
   軸の合う矢印を消費したら `handled=true` で終了 (= 値操作優先)
2. 消費されなかった場合、`view::key()` は `_arrow_focus_nav` が true なら
   2D ナビへフォールバック
3. 現在表示中の focusable を全て列挙し、それぞれの中心座標と現在フォーカス
   widget の中心座標を比較
4. スコア = `主軸方向距離 + 直交軸ズレ × 4` が最小の候補へフォーカス移動
5. 直交軸ズレに重み (×4) を掛けているため、Right を押した時は「同じ行で右隣」を
   強く優先し、無ければ近い行の右側を選ぶ

候補が無ければ (= 端到達)、フォーカスは現状のままで何もしない。Tab ナビとは
別系統なのでデフォルトではループしない (端は端でナビ終了)。
`arrow_focus_wrap(true)` にすると端到達時に**反対端の候補** (Down なら最上段、
Right なら最左) へ回り込む。回り込み先の選定も直交軸ズレ ×4 の重み付きで
「同じ列/行」を優先する。`focus_skip_disabled(true)` なら候補列挙の段階で
disabled 要素を除外する (通常ナビ・回り込みの両方に効く)。

## フォーカスリング描画

`theme::focus_ring_color` (デフォルト `rgba(0, 190, 255, 220)`) と
`theme::focus_ring_width` (デフォルト 1.5) を使い、各 widget の draw 関数内で
フォーカス中なら矩形/円のリングを上から描画する。

- `basic_button` 系: `ctx.bounds` の周りに `button_corner_radius` で角丸ストローク
- `slider`: thumb の bounds をやや膨らませた円形ストローク
- `dial` / `thumbwheel`: 全体 bounds を膨らませた角丸ストローク

styler 側で独自に focus ring を描きたい場合は `basic_button::focused()` 等の
ゲッターで状態取得できる。

## 内部実装メモ

- `composite_base::key()` の Tab 巡回ロジックは元から存在。末端到達で `false`
  を返してくるので、`view::key()` 側でそれを検知して
  `_main_element.begin_focus(from_top/from_bottom)` で先頭/末尾を選び直し、
  Tab ループを成立させている
- `view::focus(element_ptr)` および 2D ナビは、ツリー走査時に `composite_base` /
  `proxy_base` だけでなく `indirect_base` も辿る必要がある (view の主要素は
  `scale_element<indirect<reference<layer_composite>>>` であり、`hold(ptr)` も
  `indirect<shared_element<X>>` を返すため)
- `composite_base::focus()` は **直下の子要素しか返さない** ため、フォーカス
  チェーン全体を取得するには各 composite の `focus_index()` を辿る独自の
  walk_focus_path が必要 (view.cpp の anonymous namespace に実装)

## サンプル

`examples/key_driven/` を参照。一画面に以下を配置している:

- 4 種類のボタン (momentary / toggle / latching / その reset)
- 3 個の check_box と 3 個の radio_button
- 2 個の横長 slider、1 個の dial、1 個の vthumbwheel
- button_menu (Space/Enter で開いて ↑↓+Enter で項目選択)
- 2 個の input_box (テキスト編集)
- `view::focus(element_ptr)` を呼ぶボタン
- arrow_focus_navigation を実行時切替する check_box

起動直後は initial_focus で 1 つ目のボタンにフォーカスが当たる。

# アクセシビリティ (スクリーンリーダー) 対応 設計

状態: **設計案 (未実装)**。対象 OS は Windows / macOS / Linux のデスクトップ 3 系統。

この文書は 2 部構成になっている。

- 第 1 部: ライブラリ本体 (`lib/`) と `elements_modal` の設計
- 第 2 部: 組み込み先ホスト (独自ウィンドウを持たず、ゲームのフレームバッファへ描くホスト) がつなぐための指針

個々のアプリ側の接続設計は、それぞれのリポジトリの文書に置く。

---

## 0. 調査結果の要約

### 0.1 現状の Elements

| 項目 | 現状 | 設計への影響 |
|---|---|---|
| アクセシビリティ関連コード | なし。`WM_GETOBJECT` も未処理 (`lib/host/windows/base_view.cpp` の WndProc は DefWindowProc へ流す) | ゼロから作る |
| 要素の識別子 | なし。親ポインタもバウンズも保持しない。`hold()` / `link()` / `reference` で 1 要素が複数箇所に現れうる | ノード ID は「ツリー上の経路」から作る (§2.3) |
| 親・バウンズ | 走査中の `context` (`parent`, `bounds`) でのみ得られる | 専用の walker を作る (§2.4) |
| フォーカス | composite ごとの `_focus` 連鎖。末端を返す公開 API はない (`walk_focus_path` は view.cpp 内の static) | `view::focused_element()` を公開する |
| 変更通知 | イベントバスはない。近いものは `view::on_tracking`、各ウィジェットの `on_change`、`refresh()` | dirty フラグ + 再構築 + 差分で行く (§2.5) |
| 名前の取り出し | `text_reader` (`label`, `button` styler, `static_text_box`)。前例は `find_subject<text_reader*>` (style/menu.hpp) | 自動導出に使う。`toggle_selector` は `text_reader` を継承していないので足す |
| テキスト編集 | `basic_text_box` の選択範囲は UTF-8 バイトオフセット。IME の変換中文字列は扱わない (確定文字だけ届く) | テキストパターンは後段の Phase。当面は value と選択範囲のみ |
| ホスト | Win32 (`ElementsView` 子 HWND)、SDL3 (`SDL_Window*`)。ネイティブ Cocoa / GTK ホストはない。SDL で mac / Linux 用のプリセットはあるが、動作は未確認 | mac / Linux は SDL3 経由で対応する |
| 組み込みモード | `view(extent)` はネイティブウィンドウを持たない (`elements_modal::overlay_session` がこれを使う) | アダプタはホスト側が持つ設計にする (第 2 部) |
| スレッド | `detail::task_queue::post()` はどのスレッドから呼んでもよい | AT からのアクション要求は `post()` で UI スレッドへ渡せる |

### 0.2 OS API と実装方式の比較

| 方式 | 3 OS 対応 | 工数感 | 備考 |
|---|---|---|---|
| **AccessKit (accesskit-c)** | Win (UIA) / mac (NSAccessibility) / Unix (AT-SPI over D-Bus)。ほかに Android / iOS | 小〜中。ツリーを渡す処理とアクション処理だけ書けばよい | MIT / Apache-2.0。core 0.25.x、accesskit-c 0.23.x (2026-09)。prebuilt を `find_package(ACCESSKIT)` で取り込める。採用例: Godot 4.5 (C API 経由)、egui、Slint、Bevy |
| OS ネイティブ直書き | OS ごとに別実装 | 大。UIA の TextPattern だけで AccessKit 統合全体より重い | COM の寿命管理、macOS の左下原点、AT-SPI の D-Bus を全部自前で書くことになる |
| 読み上げ専用 (Prism / Tolk / SAPI / speech-dispatcher) | ライブラリによる | 極小 | ツリーがないので、SR のレビュー機能・点字・探索が効かない。補助として使う |

AccessKit の要点 (設計に効くところ):

- **push 型**。最初に全体を渡し、以後は `TreeUpdate{nodes, tree?, focus}` で差分を渡す。ノードは丸ごと置き換わる。**`focus` は毎回必須**。
- **遅延起動**。`update_if_active(cb)` は AT が接続するまで何もしない。AT がいなければコストはほぼゼロ。
- **座標**は「ツリーのコンテナ (ウィンドウ) 原点からの物理ピクセル、y 下向き」。スクリーン座標への変換はアダプタが行う。
- **接続方法**:
  - Win32: `WM_GETOBJECT` を `handle_wm_getobject` に渡す。または subclassing adapter を使う。
  - macOS: NSWindow を subclassing adapter に渡す。
  - Unix: ウィンドウハンドル不要。外接矩形とフォーカスはホストから通知する。
- **アクション要求**は UI スレッド以外から届きうる。
- 「一言読ませる」専用 API はない。**live region ノードの文字列を書き換える**のが定石。

**結論: OS バックエンドは AccessKit を採用する。ただしライブラリ本体は AccessKit に依存させない。** 本体は独自の意味モデル (a11y ツリー) と差分までを持ち、AccessKit への変換は任意ビルドの別ターゲットに分ける。理由は次のとおり。

1. Rust 製の依存を、NX などコンソール向けのビルドに持ち込まない。
2. ヘッドレスでツリーを JSON にダンプでき、CI や REPL で検証できる。
3. 組み込みホストは複数のセッションを 1 本のツリーに合成する必要があり、合成は意味モデルの段階で行うほうが素直。

---

# 第 1 部: ライブラリ本体

## 1. レイヤ構成

```
 L0  element::accessible()           各ウィジェットが自分の役割・名前・値・状態を申告
       │  (a11y/accessible.hpp — 依存なし)
 L1  a11y::walker / snapshot / diff  view を走査して平坦なノード表を作り、前回分との差分を出す
       │  view::a11y_*  (dirty 管理・アクション受付・announce)
 L2  a11y::source                    「スナップショットを出せて、アクションを受けられるもの」の抽象
       │                              view と overlay_session (elements_modal) が実装する
 L3  a11y::accesskit_host            複数の source を 1 ウィンドウのツリーへ合成し、AccessKit に流す
       │  (lib/a11y/accesskit/ — ELEMENTS_A11Y_ACCESSKIT=ON のときだけビルド)
 OS  UIA / NSAccessibility / AT-SPI
```

- L0 から L2 は常にビルドする。sink が接続されていなければ、コストは dirty フラグの更新だけになる。
- L3 はオプション。組み込みホストは L3 を自分のウィンドウに付けて使う。L3 を使わず、L2 から独自の出力先 (読み上げ専用など) へつないでもよい。

## 2. 意味モデル (L0 / L1)

### 2.1 型 (`lib/include/elements/support/a11y.hpp`)

```cpp
namespace cycfi::elements::a11y
{
   enum class role : std::uint8_t
   {
      none,            // 透過: 自分はノードにならず、子を親へ繰り上げる
      generic,         // まとまりだけ表す (group)
      window, dialog, alert_dialog,
      label, heading, image, separator,
      button, toggle_button, check_box, radio_button, radio_group,
      tab_list, tab, tab_panel,
      slider, spin_button,          // cycle_picker 系は spin_button 扱い
      combo_box, menu, menu_item, menu_bar,
      list, list_item,
      text_input, multiline_text_input,
      progress_indicator, scroll_view, status, // status = live region
   };

   enum class live : std::uint8_t { off, polite, assertive };

   enum state : std::uint32_t
   {
      focusable = 1u<<0, focused  = 1u<<1, disabled  = 1u<<2,
      checked   = 1u<<3, mixed    = 1u<<4, selected  = 1u<<5,
      expanded  = 1u<<6, collapsed= 1u<<7, read_only = 1u<<8,
      modal     = 1u<<9, offscreen= 1u<<10, required = 1u<<11,
   };

   enum class action : std::uint8_t
   {
      focus, click, increment, decrement, set_value,
      scroll_into_view, expand, collapse, set_text_selection,
   };

   using node_id = std::uint64_t;

   struct node
   {
      node_id              id = 0;
      role                 role = role::none;
      std::string          name;          // アクセシブル名
      std::string          description;   // 補足説明 (ヘルプ行)
      std::string          value;         // 表示値 ("50%", 入力中の文字列)
      std::optional<double> num_value, num_min, num_max, num_step;
      std::uint32_t        states = 0;
      std::uint32_t        actions = 0;   // 1u << action
      live                 live = live::off;
      rect                 bounds;        // view 座標 (DIP, view::scale 適用後)
      std::vector<node_id> children;
      std::optional<std::pair<std::size_t,std::size_t>> text_selection; // UTF-8 byte
      std::string          locale;        // BCP47。canvas_state::text_locale を流用
      std::string          debug_id;      // elements_modal の "id" など (ダンプ用)

      bool operator==(node const&) const = default;
   };

   struct info;      // element::accessible() に渡す書き込み口 (node のサブセット + 制御フラグ)
   struct snapshot;  // root, focus, ノード表 (id → node, id → element*)
   struct update;    // changed nodes, removed ids, focus, full
}
```

### 2.2 要素側のフック

`element` に仮想関数を 1 つ足す。

```cpp
// element.hpp
virtual void accessible(a11y::info& out, context const& ctx) const;  // 既定: role::none
```

`a11y::info` の制御フラグは次のとおり。

- `out.leaf = true`: 子を走査しない。ボタンの内部ラベルのような装飾的な子は読ませたくないため、これを使う。
- `out.name_from_content = true`: 名前が空なら、子孫の `text_reader` から連結して名前にする。前例の `find_subject<text_reader*>` を一般化したもの。

主要ウィジェットの実装方針:

| 要素 | role | name | value / states | actions |
|---|---|---|---|---|
| `basic_button` | button | 子孫の text_reader | enabled, focus | click → `activate(ctx)` |
| `basic_toggle_button` (check_box) | check_box / toggle_button | 同上 (`toggle_selector` に `text_reader` を足す) | checked | click |
| `basic_choice` (radio, tab) | radio_button / tab | 同上 | selected | click |
| `basic_label_styler_base` / `static_text_box` | label | `get_text()` | — | — |
| `slider_base` / `basic_dial` | slider | — | num_value (0..1)、表示用の value は wrapper で上書きする | increment / decrement / set_value / focus |
| `cycle_picker` 系 | spin_button | — | value = `option_text(index())` | increment / decrement |
| `basic_input_box` / `basic_text_box` | text_input / multiline_text_input | placeholder (getter を足す) | value = text、text_selection、read_only | focus / set_value / set_text_selection |
| `status_bar_base` | progress_indicator | — | num_value | — |
| `basic_button_menu` / `basic_menu_item_element` | combo_box / menu_item | 同上 | expanded / selected | click / expand |
| `modal_element` | dialog | — | modal | — |
| `port_base` / scroller | scroll_view | — | — | scroll_into_view |
| `list` | list (子は list_item) | — | — | — |
| `deck_element` | none (選択中のページのみ走査) | — | — | — |
| `hidable_element` (`is_hidden`) | — (部分木ごと除外) | — | — | — |

ウィジェット側で書き換えずに、宣言的に上書きできるラッパー (proxy) も用意する。

```cpp
// <elements/element/accessible.hpp>
a11y_label("音量", slider)                 // 名前の上書き
a11y_description("BGM の大きさ", e)
a11y_role(a11y::role::heading, label)
a11y_value_fn([&]{ return fmt(vol); }, e)  // 表示値 ("75%")
a11y_hidden(decoration)                    // 部分木を除外
a11y_live(a11y::live::polite, msg_label)   // 文字列が変わったら読み上げる
a11y_props({.name=..., .role=..., ...}, e) // まとめて指定
```

proxy 側の `accessible()` は、まず subject の `accessible()` を呼び、その結果を上書きする。

### 2.3 ノード ID

要素は共有・再利用されるので、ポインタは ID に使えない。ID は次の規則で決める。

1. **明示 ID がある場合**: `id = hash(namespace, "id 文字列")`。elements_modal の `"id"` がこれに当たり、最も安定する。
2. **ない場合**: `id = hash(親ノード id, 親の中の子 index, typeid)`。構造が変わらないかぎり安定し、遅延生成される list のセルも index で安定する。
3. 衝突した場合: walker が検出し、後から来た方に連番を混ぜる。デバッグビルドでは警告を出す。

`snapshot` は `id → element*` の対応も持つ。これはアクションを受けたときの逆引きに使う。この対応は次のスナップショットまで有効。

### 2.4 walker

`view::main_element()` から `context` を組み立てながら降りる。`collect_focusables` (view.cpp) と同じ型分岐 (composite / proxy / indirect) を使い、相違点は次のとおり。

- `composite_base`: 全ての子を `bounds_of(ctx, ix)` で走査する。可視の子だけでなく、スクロール外の子も含める (`offscreen` 状態を付ける)。
  - 例外は仮想化された `list`。可視セルだけを出し、list ノードに `size` (行数) を持たせる。
- `deck_element`: `selected()` のページだけを辿る。
- `hidable_element::is_hidden`: 部分木ごと除外する。
- view の layers: 最前面に `modal_element` があれば、それより下の layer は出さない。モーダルとして扱う。popup や menu の layer は root 直下の兄弟として出す。
- `role::none`: ノードを作らず、子を親の `children` へ繰り上げる。
- バウンズは `canvas::user_to_device` で view 座標 (DIP) に変換する。走査には `with_context_do` (scratch_context) を使う。描画前 (`_current_bounds` が未確定) なら空ツリーを返す。

### 2.5 変更検出と更新の頻度

`view` に次を足す。

```cpp
void           a11y_sink(std::shared_ptr<a11y::sink>);   // 出力先 (L2/L3)
bool           a11y_active() const;                       // sink があり、AT が接続中
a11y::snapshot a11y_snapshot();                           // 同期・UI スレッド
void           a11y_invalidate();                         // dirty 化
void           a11y_perform(a11y::node_id, a11y::action, a11y::action_arg = {}); // 任意スレッド可 (post)
void           announce(std::string_view text, a11y::live = a11y::live::polite);
element*       focused_element() const;                   // walk_focus_path の末端
```

dirty 化のきっかけは次の 3 つで、既存の流れにフックするだけでよい。

- `refresh()` 系の全て。描画が変わるなら意味も変わりうる、という粗い近似。
- `layout()`、`content()`、`add()` / `remove()`、フォーカスの変更 (`focus()`、`key()`、`click()` の中)。
- `manage_on_tracking` (値の編集)。

`view::poll()` の中の処理:

- dirty で、かつ `a11y_active()` のとき、`sink->tree_changed()` を呼ぶ。sink は AccessKit の `update_if_active` コールバックの中で `a11y_snapshot()` を取り、前回分と差分を取って送る。
- **フォーカス変更は即時**に送る。体感応答性に直結するため。
- それ以外は最短 33ms 間隔に間引く (トゥイーンのアニメーション中に毎フレーム bounds を送らないため)。
- ツリー全体の再走査は O(n)。UI の要素数は数百程度を想定しているので許容する。問題が出たら部分木単位の dirty に切り替える。

### 2.6 アクションの処理

`a11y_perform` は `post()` で UI スレッドへ移してから、snapshot の逆引きで要素を得る。

| action | 処理 |
|---|---|
| focus | `view::focus(element&)` |
| click | `basic_button` なら `activate(ctx)`。それ以外は、フォーカスしてから Enter キーを合成する (キーボード操作と同じ意味に揃える) |
| increment / decrement | フォーカスしてから、主軸方向の矢印キーを合成する。slider や picker の既存のキー処理をそのまま使うので、ステップ幅や on_change の発火も一致する |
| set_value | `receiver<double>` なら `value()` を設定し、`on_change` を発火する。`text_writer` なら `set_text` |
| set_text_selection | `basic_text_box::select_start/end` (UTF-16 から UTF-8 への変換はアダプタ側で行う) |
| scroll_into_view | フォーカス可能ならフォーカスする (既存の scroll-into-view が働く)。それ以外は親 port の `valign` を調整する |
| expand / collapse | menu なら open / close |

キーを合成する方式にしておくと、ゲームパッド・キーボード・AT の 3 経路で挙動が一致する。

### 2.7 読み上げ (announce)

`view::announce(text, live)` は `sink->announce()` に渡すだけ。AccessKit アダプタは root 直下に非表示の `status` ノード (live region) を 1 つ持ち、その name を書き換える。同じ文字列が連続しても読ませたいので、末尾に不可視の交互サフィックスを付けて差分を作る。

`a11y_live(...)` ラッパーが付いたノードは、name / value が変わると AT が自動で読み上げる (UIA LiveRegionChanged 相当)。

## 3. ネイティブホスト (L3 の接続)

### 3.1 CMake

```
option(ELEMENTS_A11Y_ACCESSKIT "Build AccessKit-based OS accessibility adapter" OFF)
set(ELEMENTS_A11Y_ACCESSKIT_DIR "" CACHE PATH "accesskit-c prebuilt root")
```

- `if(NOT TARGET accesskit)` のときだけ `find_package(ACCESSKIT)` する (`CMAKE_PREFIX_PATH` / `ELEMENTS_A11Y_ACCESSKIT_DIR`)。親プロジェクトがターゲットを供給できるようにするためで、SDL3 / ThorVG と同じ流儀。
- 見つからなければ FetchContent で accesskit-c の **prebuilt リリース**アーカイブを取得する。Rust ツールチェーンは要求しない。
- ターゲット `elements_a11y_accesskit` (STATIC) を作り、`ELEMENTS_A11Y_ACCESSKIT=1` を PUBLIC 定義する。
- Windows では追加で `UIAutomationCore` へのリンクは不要 (accesskit 側が持つ)。

### 3.2 `a11y::accesskit_host` (L3 本体)

```cpp
class accesskit_host
{
public:
   // native: HWND / NSWindow* / (Unix は nullptr)
   static std::unique_ptr<accesskit_host> attach_win32(HWND, bool handle_wm_getobject_manually);
   static std::unique_ptr<accesskit_host> attach_sdl(SDL_Window*);  // 内部で SDL_GetWindowProperties

   // 合成: 1 ウィンドウに複数の source (view / overlay_session) を z 順に載せる
   void add_source(int slot, a11y::source*, affine_transform view_to_window_px, int z);
   void set_transform(int slot, affine_transform);
   void remove_source(int slot);
   void set_window_label(std::string);         // root (window) ノードの名前
   void set_modal(int slot, bool);             // それより下の slot を隠す

   void invalidate();                          // source の dirty 通知から呼ぶ
   void announce(std::string_view, a11y::live);
   bool is_active() const;

   // Win32 手動接続用
   std::optional<LRESULT> on_wm_getobject(WPARAM, LPARAM);
   void on_window_focus(bool focused);
   void on_window_bounds(rect screen_px);      // Unix 用
};
```

- **ID の名前空間**: AccessKit の node id は `(slot << 48) | (id & 0xFFFF'FFFF'FFFF)` とする。AccessKit 0.25 の `tree_id` / graft (部分木の接ぎ木) は、単一ウィンドウ内の合成には不要なので当面は使わない。使えるかどうかは Phase 0 で確認する。
- **座標**: source の bounds は view 座標 (DIP) で出てくる。ホストが渡す `view_to_window_px` (スケール、レターボックスのオフセット、オーバーレイの位置) で物理ピクセルに変換し、各ノードの `bounds` に入れる。
- **フォーカス**: z 最上位の「アクティブな source」の focus を採用する。どの source にもフォーカスがなければ root にする。
- **アクション**: AccessKit のコールバックは、Windows では UIA のスレッド、Unix では別スレッドから来る。slot で source を引いて `source->perform()` に渡す。Windows ではウィンドウのスレッドへ投げ直してから呼ぶ (§6.4)。view の source は `post()` で UI スレッドへ移すので、ここでは同期を取らない。

### 3.2.1 Phase 0 の結果 (Windows、2026-10-04)

試作 `examples/a11y_spike` (`-DELEMENTS_A11Y_SPIKE=ON`) で次を確認した。

- 確認の範囲:
  - SDL3 ホスト、Win32 ホストの両方
  - accesskit-c 0.23.1 の prebuilt (static、MSVC x64、Release)
  - 検証は UI Automation クライアント (外部プロセス) で行った。スクリーンリーダーの音声は未確認。
- 結果:
  - ツリー (Window → Button / CheckBox / Slider / live region) が見える。名前、ロール、RangeValue (0..100、"50%")、Toggle、矩形 (DPI 200% で物理ピクセル)、フォーカス追従が正しい。
  - AT からの Invoke / Toggle / RangeValue.SetValue / SetFocus は、どれも UI スレッドへ渡り、Elements のキー操作と同じ経路で反映される。live region の更新も届く。

設計に効く事実:

- **subclassing adapter は使えない**。`accesskit_windows_subclassing_adapter_new` は「ウィンドウが既に可視」だと panic する。elements の両ホストも、ゲームエンジンのメインウィンドウも、アダプタを付ける時点で既に表示されている。
  - そこで、通常の `accesskit_windows_adapter_new(hwnd, is_focused, …)` を使い、こちらで `SetWindowSubclass` (comctl32) して `WM_GETOBJECT` を `handle_wm_getobject` に、`WM_SETFOCUS` / `WM_KILLFOCUS` を `update_window_focus_state` に渡す。この方式なら、どのホストにも後付けできる。
  - Win32 ホストではキーボードフォーカスを持つ `ElementsView` 子 HWND に付ける。SDL3 ホストではトップレベル HWND に付ける。
- prebuilt には全 OS 分が入っている (Windows x86 / x64 / arm64 の MSVC と MinGW、macOS arm64 / x86_64、Linux x86 / x86_64、Android、iOS)。
  - 同梱のソース用 CMakeLists は cargo を要求するので、`FetchContent_Declare(... SOURCE_SUBDIR <存在しない名前>)` で展開だけを行い、`accesskit-config.cmake` を include する。
  - Windows の static ライブラリは約 9MB で、`uiautomationcore` などのシステムライブラリへの依存は config 側が付ける。
- 空の live region は、矩形のない `Text ''` として見えてしまう。announce のときだけ children に入れるか、bounds を付ける。
- 未確認: Debug 構成 (/MDd と Rust の /MD の混在)、NVDA / ナレーターでの実際の読み上げ。

### 3.2.2 Phase 0 の結果 (macOS / Linux、2026-10-04)

試作 `examples/a11y_spike/sdl_minimal` を使った。Elements を使わない SDL3 + AccessKit だけの構成で、ウィンドウを**表示してから**アダプタを付けている。

| | macOS 15.7 (Intel、Retina) | Ubuntu 26.04 + GNOME |
|---|---|---|
| 接続 | `accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window")` + `subclassing_adapter_for_window(NSWindow)`。表示後に付けても問題ない (Windows と違って panic しない。条件は content view があることだけ) | `accesskit_unix_adapter_new` (ハンドル不要) |
| ツリー | AXGroup (content view) の下に AXButton ×2 と AXStaticText (live) | frame の下に push button ×2 と label (live) |
| 座標 | 物理ピクセル (density 2.0) で渡すと、AccessKit がポイントに直して正しい位置に出る | X11: ピクセル = 論理 (density 1.0)。外接矩形を `set_root_window_bounds` で通知すれば、スクリーン座標も正しい。Wayland: 下記 |
| 操作 | AXPress → Click、AXFocused=true → Focus。どちらも届く | AT-SPI の click / grab_focus → Click / Focus。どちらも届く |
| 読み上げ | VoiceOver で確認済み | Orca で確認 (ユーザーによる目視) |
| 起動条件 | AT 接続時に activation | GNOME の `org.a11y.Status.IsEnabled` が true のときだけ登録される。**アプリ起動後に Orca を起動しても、その時点で activation される** |

設計に効く事実:

- **Unix ではすべてのハンドラ (activation を含む) が別スレッドから呼ばれる**。そのため L3 は「UI スレッドで作った最新スナップショット」を mutex 付きで保持し、activation では待たずにそれを返す。UI スレッドへ post して結果を待つ方式は、ロックの順序が絡むので採らない。Windows / macOS では activation は UI スレッドで来るが、実装は同じでよい。
- **Wayland の座標単位**: frame の外接はウィンドウ座標 (論理) で出るのに、ノードにピクセル (density 2.67 を掛けた値) を渡すと単位が混在した。分数スケーリングの Wayland では、AT-SPI は論理座標で扱うのが通例なので、**Unix ではノードの bounds を SDL のウィンドウ座標 (論理) で渡す**のを既定にする。Orca のマウスレビューや拡大鏡での確認は未実施。Wayland ではウィンドウの絶対位置が取れないので、スクリーン座標はウィンドウ原点が (0,0) のままになる (AccessKit が記載している制約)。
- **SDL 3.4.0 は、この環境の Wayland で `SDL_Init` 中に segfault する** (`Wayland_ShowCursor` → `wl_proxy_get_version`)。3.4.18 では直っている。elements は `release-3.4.0` に固定しているので、上げる必要がある (a11y とは独立した問題)。
- macOS: SDL3 の NSWindow サブクラス名は `SDL3Window` (`src/video/cocoa/SDL_cocoawindow.m`)。SDL を上げてこの名前が変わると、フォーカスの転送が効かなくなる。
- ssh から AX を検査するには、macOS のアクセシビリティ権限を ssh 側のプロセスに付ける必要がある。Linux は、ログイン中のセッションバス (`DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus`) を指定すれば、Python の Atspi で外から検査も操作もできる。

### 3.3 Win32 ホスト (`lib/host/windows/`)

- `view_info` に `std::unique_ptr<a11y::accesskit_host>` を持たせる。`ElementsView` 子 HWND の WndProc に次を足す。
  - `WM_GETOBJECT`: `on_wm_getobject()`。値を返したらそれを返す。
  - `WM_SETFOCUS` / `WM_KILLFOCUS`: 既存の begin / end_focus に加えて `on_window_focus()`。
- 遅延起動: `WM_GETOBJECT` が来るまでアダプタは何もしない (AccessKit の遅延初期化に任せる)。
- 座標: view DIP × `get_scale_for_window(hwnd)` = クライアントピクセル。スクリーン座標への変換は AccessKit が行う。

### 3.4 SDL3 ホスト (`lib/host/sdl/`)

- ウィンドウ作成直後に `attach_sdl(window)` を呼ぶ。
  - Windows: `SDL_PROP_WINDOW_WIN32_HWND_POINTER` の HWND に、§3.2.1 の方式 (通常の adapter + `SetWindowSubclass`) で付ける。SDL の WndProc より先に `WM_GETOBJECT` を受けられる。`SDL_SetWindowsMessageHook` は LRESULT を返せないので使わない。
  - macOS: `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER` から subclassing adapter を作る。ウィンドウの表示前に付けておく (要確認)。
  - Linux: unix adapter を作る (ハンドル不要)。`SDL_EVENT_WINDOW_MOVED` / `RESIZED` / `FOCUS_GAINED` / `FOCUS_LOST` で外接矩形とフォーカスを通知する。Wayland ではウィンドウの絶対位置が取れないので、bounds は相対座標までに留まる (既知の制約)。
- `SDL_WINDOW_HIGH_PIXEL_DENSITY` を考慮し、座標は `SDL_GetWindowPixelDensity` を掛けて物理ピクセルにする。

## 4. elements_modal

### 4.1 JSON スキーマの追加

全ウィジェット共通のキーとして `"a11y"` を足す。文字列なら label の短縮形、オブジェクトなら詳細指定。

```jsonc
{ "type": "sprite_button", "id": "btn_save", "a11y": "セーブ" }
{ "type": "slider", "id": "vol_bgm",
  "a11y": { "label_id": "cfg.bgm", "description_id": "cfg.bgm.help",
            "value_var": "vol_bgm_disp" } }
{ "type": "label", "text_var": "msg_body", "a11y": { "live": "polite" } }
{ "type": "image", "a11y": { "hidden": true } }
```

| キー | 意味 |
|---|---|
| `label` / `label_id` | アクセシブル名 (`_id` は StringStore で引くので、言語切替に追従する) |
| `description` / `description_id` | 補足説明 |
| `role` | role の上書き (`"heading"`, `"status"` など) |
| `value_var` | 表示値を持つ変数名 |
| `live` | `"polite"` / `"assertive"`。text_var が変わると読み上げる |
| `hidden` | 部分木を除外する |

画面のトップレベルには `"a11y": { "title" / "title_id", "announce": "..."/"announce_id" }` を置ける。画面に入ったとき、ダイアログ名として読ませる。

### 4.2 自動導出 (何も書かなくてもそれなりに読む)

| 情報 | 導出元 (優先順) |
|---|---|
| name | `a11y.label*`、`text` / `text_id` / `text_var`、`labeled_row.label` (行の中の子に名前として付与)、`group.title`、`pad_icon` の論理ボタン名 ("A ボタン" などの StringStore キー) |
| description | `a11y.description*`、`strings_on_focus` / `vars_on_focus` (既存の「ヘルプ行」の慣用) |
| value | `a11y.value_var`、`display_var`、picker の `option_text` |
| role | `type` からの対応表 (`button` / `sprite_button` / `atlas_button` は button、`atlas_toggle` は check_box、`cycle_picker` 系は spin_button、`atlas_slider` は slider、`atlas_progress` は progress_indicator、レイアウト系は none) |
| 安定 ID | `"id"` があれば §2.3 の規則 1 |

実装は `LayoutBuilder::build()` の後段のラッパー連鎖 (animate → … → visible_var) に `a11y_props` proxy を 1 段足す形にする。`visible_var` の内側に置くので、非表示の要素は自動で除外される。`register_id` の時点で `(id, type, name 導出元)` を記録しておく。

### 4.3 overlay_session の API

overlay_session は `a11y::source` を実装する。

```cpp
a11y::snapshot a11y_snapshot();                     // view::a11y_snapshot + 画面 title ノードを root に
void           a11y_perform(a11y::node_id, a11y::action, a11y::action_arg = {});
void           set_a11y_dirty_callback(std::function<void()>);   // ホストが invalidate を受ける
void           announce(std::string_view, a11y::live);
std::string    a11y_dump_json();                    // REPL / panel / テスト用 (§5)
```

- dirty の通知元は次の 3 つ。いずれも既存の経路を使う。
  - `update()` 内のフォーカス変更検出ブロック (focus_poll)
  - 変数変更の通知 (var watcher)
  - 画面の enter / leave
- navigator で画面を移ると session が作り直される。そのとき新しい session の title と初期フォーカスを最初の更新で送り、移った先を読ませる。
- 将来の「メニュー読み上げボイス」(キャラクターボイスでメニューを読む機能) も同じ「フォーカスと名前」の情報で駆動できる。a11y ツリーをその入力として流用する想定。

### 4.4 ツール類

- `.eui`: `.a11y("セーブ")`、`.a11yHint(...)`、`.a11yLive()` 修飾子を足す (transpile.py)。
- verify: 画像だけのボタン (`sprite_button` / `atlas_*`) に `a11y` も `text` もなければ警告する。
- contract.py: `a11y.value_var` を、ホストが用意すべき変数として抽出する。

## 5. ダンプ形式 (検証用・共通)

REPL / panel / テストで共通の JSON 形式。名前の付け方は AT に近づけてある。

```jsonc
{ "focus": "btn_save",
  "nodes": [
    { "id": "screen", "role": "dialog", "name": "システム", "children": ["btn_save", "vol_bgm"] },
    { "id": "btn_save", "role": "button", "name": "セーブ", "states": ["focusable","focused"],
      "rect": [120, 340, 220, 48], "actions": ["click","focus"] },
    { "id": "vol_bgm", "role": "slider", "name": "BGM", "value": "75%",
      "num": [0.75, 0, 1], "description": "BGM の大きさ" } ] }
```

ID がない要素は `#<hex>` で表す。併せて、「SR がおおよそ何と読むか」を 1 行にした**読み上げログ**も出す。例えば `"セーブ, ボタン"`、`"BGM, スライダー, 75%"`、`[polite] ...` のような形。実機の SR がなくても回帰テストできるようにするため。

## 6. 段階計画

| Phase | 内容 | 完了条件 |
|---|---|---|
| 0 スパイク | accesskit-c prebuilt を Win32 サンプルに直結し、ボタン 2 個を NVDA / ナレーターで読む。SDL3 + subclassing (Win / mac)、Linux + Orca の疎通。graft の要否を確認 | 3 OS で「名前・ロール・フォーカス追従」が読める |
| 1 本体 L0〜L2 | `a11y.hpp`、walker、diff、`view::a11y_*`、`focused_element()`、主要ウィジェットの `accessible()`、ラッパー、ダンプ JSON、ヘッドレスのテスト (`test/a11y_tree`) | 全例題のツリーダンプのスナップショットテスト |
| 2 L3 + ネイティブホスト | `elements_a11y_accesskit`、Win32 / SDL ホストへの接続、`examples/key_driven` で 3 OS を手動確認 | Accessibility Insights / Accessibility Inspector / Accerciser でツリーが正しい |
| 3 elements_modal | `a11y` キー、自動導出、`overlay_session` の source 化、verify の警告、.eui | elements_console で全 advgame 画面のダンプが妥当 |
| 4 組み込みホスト | 各アプリ側の設計書を参照 | — |
| 5 テキスト | `text_input` の TextRun (文字位置、単語境界)、IME の変換中文字列を value に反映 | NVDA で文字単位のカーソル読み |

### 6.1 Phase 1 の実装 (2026-10-04)

| 場所 | 中身 |
|---|---|
| `support/a11y.hpp` / `src/support/a11y.cpp` | 型 (`role` / `state` / `action` / `node` / `info` / `snapshot` / `update` / `sink`)、`diff`、`to_json`、`describe` (読み上げ 1 行)、`id_string` |
| `support/detail/a11y_tree.hpp` / `src/support/a11y_tree.cpp` | walker。走り方は `collect_focusables` と同じ型分岐で、proxy は `prepare_subject` を通す (margin / align の効いた矩形になる)。deck は選択中のページだけ、layer は最前面から見て modal が出たらそれより下を出さない |
| `element::accessible()` / `element::a11y_perform()` | 既定は `text_reader` をラベルとして出す (label、ボタンのキャプション、static text)。それ以外は透過 |
| `view` | `a11y_snapshot()`、`a11y_sink()`、`a11y_perform()` (どのスレッドからでも可、`post` で UI スレッドへ)、`announce()`、`a11y_name()`、`a11y_invalidate()`、`focused_element()`。`refresh` 系で dirty になり、`poll()` が sink へ差分を送る (フォーカス変更・announce・AT 操作の直後は即時、それ以外は 33ms 間隔) |
| `element/accessible.hpp` | `a11y_label` / `a11y_description` / `a11y_role` / `a11y_id` / `a11y_live` / `a11y_hidden` / `a11y_value_fn` / `a11y_props` |
| ウィジェット | `basic_button` (button / toggle_button / check_box / radio_button / tab の判別。クリックは `activate(ctx)`、つまり Space/Enter と同じ経路)、`slider_base` (0..100、増減幅はキーと同じ 5)、3 種の picker (spin_button)、`basic_text_box` / `basic_input_box`、`status_bar_base`、`basic_menu_item_element`、`modal_element` (dialog + modal)、`hidable_element` / `vcollapsable_element` (隠れていれば除外)、`toggle_selector` に `text_reader` を追加 |
| `test/a11y_tree_test.cpp` | ヘッドレス (`view(extent)`) のテスト 27 項目。`-DELEMENTS_BUILD_TESTS=ON`、`ctest` で回す。SDL / Win32 の両ホストで通過 |

設計 (§2) との差:

- `role` は今あるウィジェットで使う分だけにした。`radio_group` / `tab_list` / `tab_panel` / `combo_box` / `menu` / `list` / `scroll_view` / `separator` はまだない。`action` も `scroll_into_view` / `expand` / `collapse` / `set_text_selection` はまだない。
- スクロール外の子は出していない (`for_each_visible` が間引く)。`offscreen` 状態も未実装。
- `text_selection`、`locale` はまだ持たない (Phase 5)。
- `basic_dial` / `thumbwheel_base` / `range_slider` / `basic_button_menu` の開閉状態は未対応。
- ノード ID は「構造上の位置 + 型」の hash (§2.3 の規則 2) で、`typeid().hash_code()` を使うため**プロセスをまたいでは安定しない**。プロセスをまたいで安定させたいときは `a11y_id` を付ける。
- ヘッドレスで view を使うときは、ホストがやっている初期化と後始末 (`tvg::Initializer::init` → … → `detail::release_shared_scratch()` → `tvg::Initializer::term`) を自分で行う必要がある (テスト参照)。

### 6.2 Phase 2 の実装 (2026-10-04)

| 場所 | 中身 |
|---|---|
| `a11y/accesskit_host.hpp` / `src/a11y/accesskit_host.cpp` | `accesskit_host` (L3)。1 ウィンドウに 1 つ。`add_source(slot, perform, snapshot_now, get_transform)` が slot 用の `a11y::sink` を返す。slot は z 順に root の下へ並べ、modal の slot より下は出さない。AT 用 id は上位 8bit に slot を入れる |
| `attach_accesskit(view&)` | ネイティブの Elements ウィンドウ用。Win32 ホストは `ElementsView` 子 HWND、SDL ホストは `SDL_GetWindowProperties` から HWND / NSWindow を取る。Unix は SDL のイベントを `SDL_AddEventWatch` で見て、フォーカスと外接矩形を通知する |
| CMake | `-DELEMENTS_A11Y_ACCESSKIT=ON` で `elements_a11y_accesskit` を作る (accesskit-c 0.23.1 の prebuilt を FetchContent。親が `accesskit` ターゲットを持っていればそれを使う) |
| `examples/accessibility` | 実際のウィジェットで組んだ設定画面。`attach_accesskit(view_)` の 1 行で OS に出る |

動作確認:

- **Windows (SDL3 / Win32 ホスト)**: UI Automation の外部クライアントで確認した。全ノードのロール・名前・値・矩形、初期フォーカス、Invoke / Toggle / SelectionItem.Select / RangeValue.SetValue / Value.SetValue / SetFocus、live region (`announce`) が正しく動く。
- **Linux (GNOME、X11)**: AT-SPI (Python Atspi) で確認した。ツリー、座標 (倍率 1.5 のウィンドウ座標)、click / grab_focus、checked 状態が正しい。elements が固定している SDL 3.4.0 は Wayland で落ちるので、X11 で確認している。
- **macOS 15 (Intel、Retina)**: System Events (AX) で確認した。ロール (Heading / AXButton / AXCheckBox / AXRadioButton / AXSlider / AXIncrementor / AXTextField / AXStaticText)、値、AXPress / AXIncrement / 値の設定、live region が正しい。
- ヘッドレステストは 3 OS とも通る (Linux はリポジトリのルートを絶対パスで渡すこと。相対パスだとフォントが読めない)。

Mac で確かめる中で、ホストの既存の不具合を 3 つ直した (a11y とは独立):

- **終了時の abort (macOS)**: `app::~app()` が測定用の scratch canvas を残したまま `tvg::Initializer::term()` を呼んでいた。canvas が生きていると term がフォントローダを畳まずに戻り、atexit で破棄済みのフォントマネージャの mutex を触って abort していた。elements_modal の `shutdown()` と同じく、先に `detail::release_shared_scratch()` を呼ぶようにした (Win32 / SDL ホストとも)。

- **論理座標の換算**: ウィンドウ座標を表示倍率で割って論理座標にしていたが、macOS / Wayland のウィンドウ座標はすでにポイントなので、二重に割って半分になっていた。「ウィンドウ座標 / 論理単位 = 表示倍率 ÷ ピクセル密度」に直した (Windows 150% → 1.5、macOS Retina → 1、X11 → 表示倍率、Wayland → 1)。描画の倍率 (canvas) は従来どおり表示倍率。
- **macOS のフォント**: .app では `SDL_GetBasePath()` が `Contents/Resources/` を返すため、その下の `resources/` を探してフォントが 1 つも読めていなかった。`Contents/Resources/` 自体も探すようにした。

実装で決めたこと:

- **座標**: Windows / macOS には物理ピクセル (`SDL_GetWindowSizeInPixels` / クライアント矩形 ÷ view 幅)、Unix にはウィンドウ座標 (`SDL_GetWindowSize` ÷ view 幅) で渡す。倍率が変わったら全体を送り直す。
- **ノードの矩形は、祖先の矩形との共通部分にする** (walker)。横スクロールする入力欄の中身などは、表示より広く (幅 200 万 px など) レイアウトされているため。
- **AccessKit の LABEL / STATUS は、テキストを value に入れる** (UIA の Name になる)。HEADING は label に入れる。入力欄は空でも value を付ける (UIA は value がないと Value パターンを出さない)。
- **起動は遅延**: AT が接続するまで view はツリーを作らない (`sink::is_active`)。Windows / macOS は接続時 (UI スレッド) に `snapshot_now` でその場のツリーを返す。Unix は別スレッドなので、それまでに受け取ったツリーを返し、あとから view が全体を送る。
- **CRT**: prebuilt の static lib は static CRT (`/MT`) の構成にそのままリンクでき、警告も出ない (実行ファイルは vcruntime に依存しない)。

### 6.3 Phase 3 の実装 (2026-10-04)

| 場所 | 中身 |
|---|---|
| `elements_modal/src/json_layout.cpp` | 部品ごとの `"a11y"` (§4.1) と自動導出 (§4.2) を `a11y_json_element` proxy で受ける。名前・説明・値は関数で持ち、ツリーを作るたびに StringStore / 変数を引き直す (言語切替・変数変化に追従)。`visible_var` / `opacity` 0 は読み上げからも消す。`labeled_row` は行ラベルを中の部品の名前にし、ラベル自体は読み上げから外す |
| トップレベル `"a11y"` | `title(_id)` が root (window) の名前、`announce(_id)` が画面に入ったときの live region |
| `overlay_session` | `a11y_sink()`、`a11y_snapshot()`、`a11y_perform()` (id 版と、ダンプの id 文字列版)、`announce()`、`a11y_dump_json()` |
| `a11y::speech_lines()` | 2 つのツリーの差を «SR がおおよそ何と読むか» の行にする (`[focus]` / `[value]` / `[state]` / `[polite]`)。REPL / パネルの読み上げログに使う |
| `accesskit_host::attach_sdl(SDL_Window*)` | Elements の view ではない SDL ウィンドウ (elements_console、ゲームエンジン) にも 1 行で付ける |
| `test/modal_a11y_test.cpp` | JSON 画面のヘッドレステスト |

設計からの差・実装で決めたこと:

- **上書きの行き先**: ラッパーの部分木に操作部品 (actions を持つノード) があれば、最初の操作部品に付ける。無ければ最初のノード。範囲つきスライダー (`[0] ── [100]`) で、行ラベルと id が最小値ラベルに付いてしまう問題への対処。
- **id はウィジェット型のものだけ**を使う (`register_id` を通ったもの)。レイアウト型の id を付けると中の最初の部品へ乗り移ってしまうため。
- **補足説明の自動導出は `strings_on_focus` だけ**。`vars_on_focus` は画像番号など文言でない値も書くので使わない。ヘルプ帯を読ませたいときは、帯のラベルに `"a11y": {"live": "polite"}` を付ける。
- **不具合修正 (view)**: sink を最初の描画より前に付けると (オーバーレイのホストはこの順になる)、空のツリーを送ったあと、レイアウトで dirty にならず二度と送らなかった。レイアウトが走ったら dirty にする。
- **不具合修正 (accesskit_host)**: AT がいない間もソースが送ってくる場合 (読み上げログのため)、差分が溜まり続けていた。非アクティブの間は最新のツリーだけを持ち、アクティブ化で全体を送る。

### 6.4 埋め込みホストの自前ノード向けの追加 (2026-10-04)

埋め込みホスト (ゲームエンジン) が、Elements で作っていない自前の UI (ゲーム画面に描いた選択肢やメニュー) を、ノードの表から snapshot に組んで slot に載せる用途のための追加。

| 場所 | 中身 |
|---|---|
| `a11y::role` | `list` / `list_item` を追加 (選択肢の並び)。AccessKit では List / ListItem、`list_item` の `selected` は選択状態として出る |
| `a11y::role_from_name()` / `state_from_name()` | スクリプトやレイアウトファイルに書いた名前 (`"check_box"` / `"checkbox"` / `"group"` / `"text"` / `"progress"` … と `role_name()` の綴り) からロールと状態ビットを引く。elements_modal の JSON `"a11y".role` もこれを使う |
| `accesskit_host` (Windows) | AT の操作を UIA のスレッドで受けたら、ウィンドウへメッセージで投げ直し、ウィンドウのスレッドで `perform` を呼ぶ。メッセージループで待機しているホスト (描画が止まっている静止画面) もこれで起きる。それまでは次の描画まで操作が届かなかった |

## 7. リスクと未決事項

- **AccessKit の C API の追従**: 0.x 系で破壊的変更がある。prebuilt のバージョンは CMake で固定し、L3 に閉じ込める (L0〜L2 の公開 API は AccessKit 型を出さない)。
- **ゲームの全画面表示**: 排他フルスクリーンでも UIA は動くが、SR の音声とゲーム音声がぶつかる。音量のダッキングはアプリ側の設定に委ねる。
- **SR を使わない利用者への自前読み上げ (self-voicing)**: 本設計の範囲外。必要になったら、L2 の sink 実装の 1 つとして Prism (MPL-2.0、vcpkg あり) などを差し込める。
- **ゲーム本体 (Elements 以外) の UI**: ホスト側が独自の source を実装して `add_source` すれば同じツリーに載る (第 2 部)。
- **モバイル (Android / iOS)**: AccessKit にアダプタはあるが、今回の範囲外。L0〜L2 はそのまま使える。

---

# 第 2 部: 組み込みホストの接続指針

ゲームエンジンのように、Elements を `view(extent)` / `overlay_session` で**オフスクリーン描画**し、自前のウィンドウに合成するホスト向けの指針。

1. **アダプタはホストが 1 ウィンドウに 1 つ持つ**。`accesskit_host::attach_sdl()` / `attach_win32()` を、メインウィンドウの作成直後に呼ぶ。自前の WndProc を持つホストは `on_wm_getobject()` を手で呼ぶ。
2. **各オーバーレイ (session) を source として登録する**。z 順・モーダル・表示位置 (`render_rect` → ウィンドウピクセル) を `add_source` / `set_transform` / `set_modal` で同期する。ホストの描画パスが合成位置を決めた直後に更新するのが確実。
3. **ゲーム本体の UI** (Elements 以外のボタンなど) は、ホスト独自の `a11y::source` 実装で slot を 1 つ取る。最低限は root 直下の `label` ノード (現在の場面名) だけでよい。
4. **物語テキストなどの読み上げ**は、次の 2 通りから選ぶ。
   - スクリプトから `announce()` を呼ぶ (話者名と本文を組み立てられる、ボイス再生と連動できる)
   - メッセージ窓のラベルに `"a11y": {"live":"polite"}` を付ける (スクリプト不要)

   前者を推奨する。
5. **REPL / Agent 系の検証チャネル**には `a11y_dump_json()` と読み上げログをそのまま流す。AT が接続していなくても `a11y_snapshot()` は取れる (active でなくても同期で計算できる)。
6. AT が接続していないときは、`update_if_active` が何もしないので負荷はない。ホストは `is_active()` を見て、スクリプト側の読み上げ処理を省略してよい。

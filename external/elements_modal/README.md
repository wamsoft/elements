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
- **テーマ (`"theme"` / `"skins"` / `"widget_skins"`)** — 配色・寸法・フォントの
  一括差し替え、 名前付きフレーム表、 ベクタ型 → 画像実装の差し替え。
  **画面 JSON を変えずに**見た目だけ入れ替えられる (下記「テーマ」)
- **長文ビューア** — `scroller` + `text_box` で、 本文をファイルから読み
  (`"text_file"`)、 読んでいる位置と表示量を変数へ出せる
  (`"pos_var"` / `"fraction_var"` / `"display_var"`)。 `"pos_var"` は**双方向**で、
  スライダと同じ変数を挿すだけで «つまみを掴んで本文を送る» が組める。 日本語は
  行頭行末禁則つきで文字単位に折り返す
- **一覧の «窓»** — N 行の `label` に固定の `"index"` を振り、 全行で
  `"index_offset_var"` (先頭位置) を共有すると、 **変数 1 個を書き換えるだけで
  一覧が送れる**。 一覧データ自体も `"text_list_var"` で差し替えられる
  (改行区切り / JSON 配列)。 行ごとの変数も `visible_var` の回し込みも要らない
- **掴んで動かす (`"drag_at_var"` / `"drag_bounds"` / `"drag_events"`)** — 型を
  問わず書ける。 ドラッグ位置を書いた変数を `at_var` に挿せば **ホスト実装なしで
  絵がついてくる** (C++ 内で完結)。 判断が要るときだけ `drag_callback` を受ける
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
| `background_opacity_var` | string | **背景板だけ**の不透明度を変数連動にする (0..1 の 10 進小数)。 中身 (文字やボタン) はそのままなので、 下のゲーム画面を透かしても可読性が落ちない。 全体に掛けると重なった要素が二重にブレンドされて文字が浮くので、 背景 box にだけ掛けるのが要点 |
| `align` | `"bottom"` / `"top_right"` 等 | **サーフェス内の配置アンカー** (既定 = 中央)。 文字列に `top`/`bottom` が含まれれば縦、 `left`/`right` が含まれれば横をその端へ寄せる (含まれない軸は中央のまま)。 `"center"` は中央 |
| `margin` | number | `align` が非中央のときのサーフェス端からの余白 px (既定 0) |
| `base` | `"window"` / `"content"` | **overlay 提示時の配置 / 拡縮の基準** (既定 `"window"` = ホストのウィンドウ全面)。 `"content"` はホストが定義するコンテンツ矩形 (メイン画像の表示領域等) 基準。 ホストは `overlay_session::placement_base()` で参照する。 `run_modal` (独立ウィンドウ) では無視。 widget 内の `base` (text_area の文字方向) とは別物 |
| `locale` | `"ja-JP"` 等 | label の `"locale"` 未指定時に使う既定ロケール (CJK 同形漢字の出し分け用) |
| `pad_theme` | `"xbox"` / `"ps"` / `"switch"` / `"keyboard"` / `"none"` | content build 前に global pad theme を切り替える (任意)。 未指定なら呼出側がセットした既存値を維持。 `pad_icon` の name 解決に効く |
| `content` | element | ルート要素 |
| `input` | object | キー / パッドナビゲーション設定 (後述) |
| `vars` | `{name: string, ...}` | 変数 store 初期値。 `label.text_var` の読み手、 focusable の `vars_on_focus` の書き手が共通参照する (後述) |
| `strings` | `{id: {lang: string}}` | i18n 文字列テーブル (StringStore)。 `text_id` / `options_id` の解決元 (後述「i18n」節) |
| `lang` | `"ja"` 等 | i18n の初期表示言語。 実行中の切替はホストの `set_language()` |
| `font_languages` | `{lang: {map, fallback}}` | **言語連動フォント置換表** (任意)。 `map` = family (または registerFont の別名) → 置換先 family。 widget の `"font"` 指定と theme 既定チェーンの各 family トークンへ適用され、 `"#tag=val"` 軸サフィックスは温存される (JP/SC/TC の同軸 VF ならウェイトが揃う)。 `fallback` = その言語のときに theme 既定 families チェーンを置き換える並び (任意、 エントリの無い言語では swap 前の並びへ戻る)。 適用言語は widget 明示 `"locale"` > `set_language()` の現在言語。 表はプロセスグローバルに言語単位でマージ登録され、 画面 JSON と `app.jsonc` (マニフェスト) の両方で宣言できる (後読みが言語単位で上書き。 全画面同一表の運用を想定 — 異なる表の画面の同時表示は非対応)。 ⚠ `text_area` はビルド時にフォントを固定するため、 表示中の言語切替には追従しない (開き直しで反映) |
| `transitions` | `{action: target}` | JSON 駆動ランナ向けの画面遷移定義。 マニフェスト駆動の `app.jsonc` と組み合わせて使う (後述) |
| `atlases` | `{name: spec, ...}` | テクスチャアトラス事前ロード。 `atlas_image` / `atlas_button` / `atlas_slider` 等が名前で参照する pixmap_ptr を content build 前に解決 (後述「アトラス共有」節) |
| `font_scale` | number | 明示 `size` を持たない button / toggle / radio / check_box / label の既定フォント倍率 (既定 1.0 = 従来一致) |
| `style` | object | 未指定値の既定をまとめて与えるテーマ入口 (下記「style ブロック」)。 いずれも省略で従来一致 |

> `align` / `margin` は、 画面がサーフェスに**収まる**場合は
> `render_to_buffer` が内部で適用する。 画面がサーフェスより**大きく**
> ホストが縮小して提示する場合は、 ホストが `overlay_session::placement()` を
> 見て同じ式で配置する責任を持つ (吉里吉里Z は対応済み)。 ホストが中央固定に
> していると、 縮小提示のときだけ `align` が効かない、 という症状になる。

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
- `list` (別名 `row_list`) — **行テンプレートを行数ぶん複製する一覧**。 `"rows": N` + `"row": { …ウィジェット木… }` + `"row_size": [w,h]` / `"pitch": [dx,dy]`。 «窓» (`index` + `index_offset_var`) が «文字» までしか面倒を見ないのに対し、 こちらは**位置・当たり判定・hover・選択・件数不足行の後始末**まで持つ (下記「行テンプレートの一覧」)。
- `group` — タイトル付きフレーム。 `"title"` + `"label_size"` + `"child"`。
- `scroller` — スクロール領域。 `"child"` + `"id"` + `"horizontal"` (真で横) +
  `"no_scrollbars"` + `"focusable"`。 スクロール状態を変数へ出せる:
  `"pos_var"` (先頭位置 0..1) / `"fraction_var"` (見えている割合 0..1) /
  `"display_var"` + `"display"` (整形済み文字列。 slider と同じ書式指定)。
  **`"pos_var"` は双方向** — 変数へ書けば本文がその位置へ送られる。 `slider` /
  `atlas_slider` の `"value_var"` と同じ変数名にするだけで «つまみを掴んで本文を
  送る» が成立する (ホスト実装は要らない)。 外から動かしたときも `"fraction_var"` /
  `"display_var"` は追従する。 現在位置と同値なら何もしないので往復しない。
  `"focusable": true` にすると自分でフォーカスを取り、 Home / End / PageUp /
  PageDown と上下キーで送れる (パッド駆動の長文ビューア向け)。 既定は
  フォーカスを取らない — ふつうのコントロールを包むスクローラが余計な
  フォーカス停止点になるのを避けるため。
- `filler` — 親 tile の余り領域を埋める素の (透明 + 完全 stretchy) スペーサ。 引数なし。
- `floating` — `"at": [x, y, w, h]` + `"child"`。 親 bounds に関係なく child を指定矩形に固定配置 (lib の `floating_element` 薄ラッパ)。 PSD でデザインされたレイアウトをそのまま絶対座標で組む用。
- `locale_variant` — 現在言語に一致する子だけを表示するデッキ (`"children"` の各要素に `"lang"` を付ける)。 詳細は「i18n」節。
- `canvas` — `"width"` / `"height"` (任意、 省略時は親 view extent) + `"children": [...]`。 子要素は通常の dispatch object に `"at": [x, y, w, h]` を加えるだけで、 内部の composite が **親 bounds の origin に rect をオフセット** して子を配置する (= 親 bounds の左上を基準とする相対座標)。 root に置けば bounds origin = (0, 0) なので絶対座標に見えるが、 別 canvas にネストすると外側 canvas が割り当てた領域の中で相対配置になる (排他グループの分離等で nested canvas を使う場面で重要)。 PSD ベース UI の主役。 追加オプション:
  - `"choice_nav": true` — この canvas の selectable な直接子 (atlas_choice / radio_button) をまとめて **1 フォーカスの左右トグルグループ**にする (後述「choice_nav」節)。
  - 子要素の `"at_var": "varname"` — 配置 rect を変数 store で駆動 (後述「変数 store」節)。

#### 入力 / state widget
- `label` — `"text"` + `"size"` (フォントサイズ、 **px 絶対**) + `"locale"` + `"color": [r,g,b,a]` (任意) + `"text_var": "varname"` (任意、 後述の **変数 store** から動的に text を取る、 指定時は `text` は初期値の fallback)。 倍率で指定したい場合は `"size_scale"` を使用 (テーマ既定 `label_font._size` ≒ 14px に対する比)。 両方指定時は `size` 優先。 追加バリエーション:
  - `"text_id": "id"` — i18n。 StringStore の textID で現在言語の訳文を表示、 言語切替に追従 (後述「i18n」節。 優先順位 text_id > text_var > 静的 text)。
  - `"fit": true` — **枠幅に合わせてフォントを自動縮小**。 実測幅が `at` 矩形の幅を超えるときだけ、 収まる最大サイズまで縮めて描く。 多言語化 (EN/TC/SC) で訳文が枠に入らないのを、 訳を詰めずに表示側で吸収するための指定。 下限倍率は `"fit_min_scale"` (既定 0.5)。 下限でも入らない場合は下限で描く (= 従来どおりはみ出す。 読めなくなるより崩れて見える方を選べるようにしてある)。 描画のたびに測り直すので言語切替にも追従する。 **`wrap` / `runs` (rich text) との併用は無効**。
  - `"color_var": "varname"` — **文字色を変数で差し替える**。 値の書き方は `"@名前"` (テーマの色トークン) / `"#rrggbb"` / `"r,g,b"` / `"r,g,b,a"` (後 2 者は 0-255)。 `"color"` 未指定でも使える (テーマ既定色から始まる)。 `text_anchor` のある label (anchored_text 経路) と通常の label のどちらでも効く。 一覧の行で «選択中だけ色を変える» のに、 色違いの label を重ねて `visible_var` で出し分ける必要がなくなる。
    - 実装メモ: 色スタイラを持たない label は後から色を変えられないので、 `color_var` があるときはテーマ既定色で «色を持った» label として組んでいる。
  - `"text_list": [s0, s1, ...]` + `"index_var": "varname"` — **指定番号表示ラベル**。 変数 store の値 (10 進 index 文字列) で text_list の 1 エントリを選んで表示し、 変数変更に追従する。 picker の `index_var` と同名にすると選択連動 (機種別 SPEC 表示等)。 範囲外 index は clamp。
  - `"text_list_id": [id0, id1, ...]` — i18n。 `text_list` の各エントリを StringStore の textID で与える版 (`text_list` より優先)。 index で引いてから現在言語で解決するので、 **言語切替でも表示中の 1 本がその場で差し替わる** (picker の `options_id` と同じ考え方)。 `text_list` を併記した場合は「i18n 非対応ランタイム / 未知 id」用の静的 fallback として同 index が使われる。
  - **改行 (`\n`) を含む text は複数行として扱う**。 行ごとに描画し、 高さも行数ぶん確保するので、 `vtile` の子に置いても後続ウィジェットと重ならない。 幅は最長行。 縦アライン (`top`/`middle`/`bottom`) はブロック全体に効く。
    - ただし `label` は**自動折返しをしない** (明示した `\n` でのみ改行する)。 与えられた幅で折り返したいなら `text_area` (禁則付き・ホストのテキストエンジンと改行位置が一致) か `text_box` (cycfi 内蔵 wrap) を使う。
    - `text_var` / `text_id` で**行数が変わる**差し替えをすると、 高さは次に親がレイアウトし直すまで更新されない。 行数が動くものは `text_area` を使うか、 行ごとの label に分けるのが確実。
- `text_box` — 複数行・自動折返しの静的テキスト (cycfi `static_text_box`)。 `"text"` + `"size"` (px 絶対) / `"size_scale"` + `"color"` + `"font"` (comma 区切り families) + `"mono"` (真で等幅フォント) + `"text_var"` (label と同じ変数 store 購読。 setVar で本文を丸ごと差替え)。 幅は親 (`hsize` 等) が決め、 高さは折返し結果に追従。 長文は親に `scroller` を置く (ライセンス表示等の長文ビューア向け、 行 label を大量に並べるより軽い)。
  - `"text_file": "text/credits.txt"` — 本文をリソースフォルダのテキスト
    ファイルから読む (UTF-8。 BOM と CRLF は落とす)。 クレジットやライセンス
    表記のように画面 JSON へ直接書きたくない長文向け。 出どころの優先順位は
    **`text_var` > `text_file` > `text`**。
  - 折返しは**空白の無い言語 (CJK) では文字単位**で、 行頭行末禁則が効く
    (閉じ括弧や句読点が行頭に来ない / 開き括弧が行末に残らない)。
  - 折返しの計算は**幅か本文が変わったときだけ**走る。 `scroller` は毎描画で
    子を再レイアウトするので、 この判定が無いと長文で毎フレーム全文を
    走査することになる。
- `text_area` — 矩形に流し込む静的テキスト (lib の `block_text_box`)。 `"text_file"` は `text_box` と同じ (`text_var` > `text_file` > `text`)。 折返しを**ホストが差し込んだ block text バックエンド**に任せるのが `text_box` との違いで、 ホスト側のテキストエンジンと**改行位置が一致する** (吉里吉里Z なら `Layer.drawShapedTextArea` と同一。 行頭行末禁則が効く)。 バックエンド未注入なら lib 内蔵の幅貪欲 wrap にフォールバックする。 字幕 / セリフ窓向け。
  - `"text"` / `"text_id"` / `"text_var"` / `"text_list"` + `"text_list_id"` + `"index_var"` — label と同規約 (優先順位 index_var > text_id > text_var > 静的 text)。 指定番号表示も i18n の言語切替追従も label と同じ挙動。
  - `"size"` (px 絶対) / `"size_scale"` + `"color": [r,g,b,a]` + `"font"` (comma 区切り families、 省略時は theme の text_box_font)。
  - `"align"`: `"left"` (既定) / `"center"` / `"right"`、 `"line_spacing"`: 行間追加 px (負値可)、 `"base"`: `"auto"` (既定) / `"ltr"` / `"rtl"`。
  - `"count_var": "varname"` — **文字送り**。 変数 store の整数値だけ先頭からクラスタ単位で表示する (-1 = 全部)。 ホストが `setVar("sub_count", "12")` するだけで進む。 **折返しは全文で確定してから count を適用する**ので送ってもリフローしない。 静的指定は `"count"`。
  - 幅は親 (`hsize` / `floating` / `canvas` の `at` 等) が決める。 fit-to-content の親に置くと最小幅 200px を要求する点は `text_box` と同じ。 `floating` の絶対座標で置くなら top-level `"size"` を明示しないとダイアログが内容サイズまで縮んで**何も見えなくなる**点に注意。
  - 仕組みとホスト側バックエンドの実装契約は [`docs/block-text.md`](../../docs/block-text.md) を参照。
- `button` — `"text"` + `"id"` (任意)。 後述の **focusable / interactive 属性** をサポート。
- `checkbox` / `check_box` — `"text"` + `"id"` + `"value"` (初期 bool)。
- `toggle_button` — `"text"` + `"id"` + `"value"`。
- `slide_switch` — `"id"` + `"value"`。
- `input_box` — `"placeholder"` + `"id"` + `"size"` (相対サイズ) + `"text"`/`"value"` (初期値。 全選択で入るので initial_focus からそのまま打つと置き換え) + `"max_chars"` (別名 `"maxlength"`。 最大文字数、 Unicode codepoint 単位、 0/省略 = 無制限。 満杯の打鍵は無視、 paste は収まる分だけ)。
- `selection_menu` — `"id"` + `"options": [...]` + `"selected"` (初期 index)。

#### console / pad 系 widget

- `invert_button` — focus すると地色と文字色が反転する momentary button。 `"text"` + `"id"` + `"size"` (**px 絶対**) または `"size_scale"` (倍率)。 button と同じく `initial_focus` / `close_on_click` をサポート。
- `ring_button` — focus すると外周にリング装飾が出る momentary button。 `"text"` + `"id"` + `"outline": [r,g,b,a]` (default white) + `"size"` (px) / `"size_scale"` (倍率)。 同じく `initial_focus` / `close_on_click` をサポート。
- `cycle_picker` — `< value >` 形式。 ←→ で循環 (端で wrap)。 `"options": [...]` + `"initial": int` (index、 default 0) + `"id"` + `"initial_focus"` + `"font_size": double` (**px 絶対**、 内部テキスト) または `"font_size_scale"` (倍率)。 値変化で `value_t{int64_t index}` を発火。 picker 系共通の追加フィールド:
  - `"options_id": [...]` — i18n。 各要素を StringStore の textID として現在言語で解決 (`options` より優先)。 言語切替で選択 index を維持したまま表示文字列だけ差し替わる (後述「i18n」節)。
  - `"font": "Family[#axes]"` — 表示テキストのフォント指定 (`label` と同じ書式。 可変フォントの軸指定込み)。 省略時・未登録 family のときはテーマ既定へフォールバック。 `cycle_picker` / `framed_cycle_picker` / `segmented_picker` / `atlas_cycle_picker` 共通で、 PSD 由来のウェイト指定を持つ UI でピッカーの選択テキストだけ既定フォントに残るのを防ぐ。
  - `"index_var": "varname"` — 選択 index を変数 store と**双方向**連動。 build 時に初期 index を書き込み (text_list ラベルや rect_list 画像と初期表示を揃える)、 選択変更のたびに set する。 変数に既に値があれば initial として採用。 さらに**変数→picker の追従** (ホストの set_var 一発で表示と依存 widget が揃って切り替わる。 quiet = on_change 非発火なのでエコーバックしない)。 範囲外/パース不能な値は無視。
  - `"enabled_var": "varname"` (cycle_picker / atlas_cycle_picker のみ) — 選択肢の有効/無効 mask を変数連動にする。 値は index 順の `'0'`/`'1'` 文字列 (例 `"10111011"` = index 1 と 4 を無効)。 mask より後ろの index は有効扱い。 step / click / pad は無効 index をスキップ (wrap 継続)、 現在選択が無効化されたら最寄りの有効 index へ進めて on_change 発火 (依存 widget が追従)。 隠し要素 (未開放の機種など) の動的出し分けに使う。
  - `"options_var": "varname"` — **選択肢リストそのもの**を変数連動にする (4 種の picker 共通)。 値の書式は label の `text_list_var` と同じ (改行区切り、 先頭が `[` なら JSON 配列)。 インストール済みフォント名や接続中デバイス名のように、 画面を作った時点では中身が決まらない一覧をホストが実行時に流し込む用途 (`enabled_var` は「一覧は固定で出し分けだけ」、 こちらは一覧の実体を差し替える)。 静的な `"options"` は変数が空のあいだの fallback。 `"options_id"` (i18n) との併用は不可 (言語切替が動的一覧を上書きするため、 options_var があるときはそちらが勝つ)。
- `framed_cycle_picker` — `[<] [ value ] [>]` の 3 ボックス框付き。 フィールドは `cycle_picker` と同じ (`font_size` / `options_id` / `index_var` も対応)。
- `segmented_picker` — `[ A | B | C ]` 形式 (選択 segment 反転)。 端で **clamp** (wrap しない)。 フィールドは `cycle_picker` と同じ (`font_size` / `options_id` / `index_var` も対応)。
- `atlas_cycle_picker` — 画像矢印ボタン式の cycle_picker (アトラス素材、 「アトラス共有」節参照)。
- `slider` — 0..1 範囲の素のスライダ。 `"id"` + `"initial": double` (default 0.5)。 値変化で `value_t{double pos}` を発火。 `"vertical": true` で縦向き。 つまみと溝の色は `"thumb_color"` / `"track_color"` (既定はテーマの予約色 `@slider_thumb` = 白 / `@slider_track` = 黒。 溝の上に乗る細いハイライトは 色指定の影響を受けず白のまま)。 `"value_var"` / `"display_var"` / `"display"` は下記「スライダの数値表示」。
- `slider_with_range` — `[min] [track] [max]` のラベル付きスライダ。 `"id"` + `"min": int` + `"max": int` + `"initial": double` (min..max スケール、 default 中央) + `"font_size": double` (**px 絶対**、 min/max ラベル) または `"font_size_scale"` (倍率)。 値変化で `value_t{double (min + (max-min)*pos)}` を発火。 `"display_var"` を付けると `"display"` 省略時の整形は自動的にこの `min`..`max` スケールになる (下記)。

#### スライダの数値表示 (`display_var` / `display`)

`slider` / `slider_with_range` / `atlas_slider` に共通。 「つまみを動かすと横の数字が変わる」を**ホスト実装なし**で作るための仕組み。

```jsonc
{ "type": "atlas_slider", "atlas": "ui", "id": "vol",
  "track": [220, 140, 256, 16], "thumb": [220, 170, 32, 32], "initial": 0.5,
  "value_var":   "vol",            // 生値 (0..1) を書く変数。 双方向
  "display_var": "vol_text",       // 整形済み «表示用文字列» を書く変数
  "display": { "min": 0, "max": 100, "step": 1, "digits": 0,
               "pad": 0, "prefix": "", "suffix": "%" } }
// 表示側は普通の label:
{ "type": "label", "text_var": "vol_text", "size": 28, "at": [500, 396, 120, 40] }
```

- 値が変わるたびに `display_var` へ整形済み文字列が書かれ、 `text_var` で読んでいる label が自動で更新される (`text` / `text_area` も同じ)。
- 整形は `pos` (0..1) → `min + pos * (max - min)` → `step` があればその倍数へ丸め → `digits` 桁の 10 進 → `pad` で整数部を 0 埋め → `prefix` / `suffix` を連結。 `display` 自体を省略すると `0..100` の整数 (`slider_with_range` はその `min`..`max`)。
- 初期値は build 時に両変数へ書かれるので、 最初のフレームから正しい数値が出る。
- ホストが `set_var(value_var, "0.8")` で外から動かした場合も `display_var` は追従する。
- 生値が要るとき (保存など) は `value_var` を読む。 従来の `event_callback` も変わらず発火する。
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
  - **theme-native** (ps): `cross` / `circle` / `square` / `triangle` / `options` / `touchpad` / `playstation` / `l1` / `r1` / `l2` / `r2` / `l3` / `r3`。 刻印基準の別名 `a` (= cross) / `b` (= circle) / `x` (= square) / `y` (= triangle) も可 (刻印は無いが役割が対応: A=決定=cross、 B=cancel=circle)
  - **theme-native** (switch): `a` / `b` / `x` / `y` / `l` / `r` / `zl` / `zr` / `plus` / `minus` / `capture` / `home` / `sl` / `sr`

  フェイスボタンの名前は**位置基準** (`face_south` 等 = 「その位置にあるボタン」の絵) と**刻印基準** (`a`/`b`/`x`/`y` = 「その刻印のボタン」の絵) の 2 系統が全 theme で使える。 入力側 (bindings の `pad`) も同じ 2 系統を持つので、 割り当てと表示は同じ基準どうしで組にすること (任天堂系は A が右・B が下なので、 基準が食い違うと絵と実際のボタンがずれる)。
  - **theme-native** (keyboard): `keyboard_enter` / `keyboard_space` / `keyboard_escape` / `keyboard_arrow_{up,down,left,right}` / `keyboard_a`…`keyboard_z` / `keyboard_0`…`keyboard_9` 等

#### 画像 / sprite / 9-patch 系

PSD でデザインされた固定サイズ / 固定位置のビットマップ UI を JSON 化する用途向け。 通常は `canvas` + `at` で配置する (canvas 内では `at` は canvas 自身の bounds 左上を原点とする相対座標)。 ファイル単位で 1 枚画像を読む素直な版と、 1 枚のアトラス画像を共有する版があり、 後者は別セクション (「アトラス共有」) で扱う。

- `sprite_button` — 縦 strip スプライト (4 〜 5 frame: normal / hilite / pressed / pressed_hilite / disabled) で状態切替する momentary button。 `"image": "path"` + `"frame_height": px` + `"scale": float` (任意) + `"id"`。 frame は上から順に縦並びを仮定。 frame が 4 未満のときは欠けた状態をフォールバックする (pressed+hilite→pressed→normal) ので、 **3 frame (normal/hilite/pressed)** だけでも可。 その場合マウス押下 (hilite 中) もキーボード押下も同じ pressed frame を表示する。 `initial_focus` / `close_on_click` / `vars_on_focus` 対応。
- `gizmo_image` — 9-patch / 3-patch 画像。 `"image": "path"` + `"axis": "9" | "h" | "v"` (既定 `"9"`) + `"scale": float`。 親 layout が与える bounds に合わせて中央部分が伸縮する (lib の `gizmo` / `hgizmo` / `vgizmo`)。 frame / background 等の伸縮素材向け。
- `image` — **単一画像ファイルをパス指定で読み込み**、 与えられた bounds にアスペクト比維持で fit 描画する (atlas 非依存)。 `"image": "path"` (resource_loader 経由で解決、 JPEG/PNG/WEBP 対応 = ThorVG。 BMP は非対応) + `"scale": float` (任意、 指定時は fit でなく native×scale 固定サイズ)。 ロゴや一枚絵をレイアウトへそのまま置く用途、 およびセーブサムネイル等の動的画像に使う。 パスが **`"mem://<name>"`** ならファイル VFS でなく**ホスト注入画像ストア** (実行時に登録した名前→画像バイト) から読む。 読込失敗 (未登録 / ファイル無し / デコード失敗) は空要素になる (レイアウト維持・無描画)。 mem:// のバイトを差し替えたら、 ホストが `refresh_mem_image(name)` を呼ぶと**表示中の構築済み widget も再デコードされて即時反映**される (krkrz は `ElementsDialog.registerImage` が自動で呼ぶ)。 登録前に build した widget は空表示のままなので、 初回は画面を開く前に登録する。

- `atlas_nine` (別名 `atlas_gizmo`) — アトラスの矩形を **9-patch** で伸縮して描く
  (ウィンドウ枠 / パネル)。 `"atlas"` + `"rect"` + `"insets": [l,t,r,b]`、
  またはテーマのスキン名 `"skin"`。 `insets` は伸ばさずに保つ四辺の幅で、
  角の丸みや枠線の太さを保ったまま `at` いっぱいに広がる。 素材の原寸に
  縛られないので、 ベクタ用に組んだレイアウトへ画像テーマを流し込める。
  上流の `gizmo_image` (ce::gizmo) は画像 1 枚まるごとを 1/2.4 の固定比で
  割るので、 角の大きさが素材ごとに違う UI パックには合わない。

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
  - `"native": true` — **矩形を bounds へ引き伸ばさず、 実寸のまま bounds 中央へ**描く。 大きさの違う絵を `rect_list` で切り替えても歪まない (長さの違うメッセージ画像など)。 単一 `rect` でも効く。 幅か高さが 0 の矩形は「何も描かない」。 テーマの `skins` が `"native": true` を書くと `"native_frames"` に落ちるので、 **`"native_frames"` も同じ意味で受ける** (スキン経由で指定できるように)。 `atlas_sprite` (`atlas_button` 等) の `native_frames` と同じ挙動。
  - `"rect_list": [[x,y,w,h], ...]` + `"index_var": "varname"` — ソース矩形リストを**変数 store の index で切替**える (rect より優先)。 picker の `index_var` と同名にすると選択連動 (機種別スクリーンショット等)。 範囲外/パース不能な値は無視 (現状維持)。 limits は現在矩形基準なので全 rect 同寸法を推奨 (canvas の `at` 固定配置で使う)。 寸法の違う絵を並べたいときは `"native": true` を併記する (伸縮せず中央に置かれるので歪まない)。
  - `"focus_link": "id"` / `["id", ...]` + `"frames": { "normal": [x,y,w,h], "hilite": [x,y,w,h] }` — **自分はフォーカスを取らない飾り**が、 リンク先 id のフォーカス状態で frame を切り替える。 行の下地や「当たっている項目にぽっちを出す」インジケータ用。 配列で複数 id を書くと、 そのいずれかがフォーカス中なら hilite。 `hilite` は必須で、 **`normal` は省略可** (省略時は非フォーカス中に何も描かない = 素材 1 枚でフォーカスインジケータが組める)。
  - `"hover_focus_link": false` (既定 true) — 上の飾りは既定で「飾りの上へ hover するとリンク先へフォーカスを移す」プロキシに包まれる (行のどこへマウスを乗せても選択が動く)。 プロキシはクリックも先に受け取るので、 **飾りがコントロールに重なる配置** (項目の角に載せるインジケータ等) では下のボタンが押せない死角になる。 その場合は `false` を指定して「クリックを一切取らない飾り」へ戻す (重なりの無い行の下地は既定のままでよい)。
- `atlas_button` — `"atlas": name` + `"frames": ...` + `"id"`。 frames は **object** (`{normal, hilite, pressed, pressed_hilite, disabled}` の順で値があるところまで使う) または **array** (`[[x,y,w,h], ...]` 順番固定) のどちらか。 sprite_button_styler で frame 自動切替 (4 frame で normal/hilite/pressed/pressed_hilite を仮定、 disabled を含めれば 5)。 frame が 4 未満なら欠けた状態をフォールバック (pressed+hilite→pressed→normal) するので **3 frame (normal/hilite/pressed)** でも可。 PSD 由来の「通常 / オーバー / 押し下げ」 3 状態ボタンはこの形になり、 マウス押下とキーボード押下が同じ pressed frame を表示する。 `initial_focus` / `close_on_click` / `vars_on_focus` 対応。
  - `"enabled_var": "varname"` — ボタンの有効/無効を変数 store と連動 (`button` / `invert_button` / `ring_button` も同様)。 値 `"0"` で無効、 それ以外 (既定) で有効。 無効中はクリック/キー決定が効かず、 描画は `disabled` frame があればそれ、 無ければ半透明フォールバック。 ホストの `set_var` 一発で切り替わるので、 進行状況で開放されるメニュー項目 (未クリアなら「おまけ」を灰色にする等) に使う。
- `atlas_toggle` (別名 `atlas_check`) — 2 値保持型。 frames は object `{off_normal, off_hilite, on_normal, on_hilite, disabled}` または array (順番固定)。 `"initial": bool` で初期値、 `"id"` + 値変化で `value_t{bool}` を発火。 `"value_var": "name"` (任意) で変数 store と連動: 変数に既存値があれば初期状態を上書き (`"0"`/`"false"`/空 = off)、 クリックで `"0"`/`"1"` を書き戻し、 変数変更は状態へ反映のみ (イベント非発火)。
- `atlas_choice` (別名 `atlas_radio`) — 排他選択型 (ラジオボタン)。 frames は atlas_toggle と同じ。 同じ親 composite (`canvas` の layer など) に並べた複数の atlas_choice 群は、 1 個 ON になると他を自動 OFF (lib の `basic_choice` の `find_composite` + 兄弟スキャン)。 `"selected": bool` で初期状態 (1 グループ内で 1 つだけ true 推奨)。 値変化で `value_t{bool true}` を発火 (新しく選ばれた側のみ。 deselect 側は `on_click` 走らず)。 `"selected_var"` / `"selected_value"` (任意) で変数 store と双方向連動 (下記「変数連動ファミリ一覧」参照)。 **排他スコープは「直近の親 composite」単位** — 複数の独立グループを 1 画面で使うときは、 各グループを別々の composite (= ネストした `canvas` や `layer` / `htile` 等) に入れて分離する (下記「排他グループの分離」参照)。
- `atlas_slider` — 0..1 スライダ。 `"atlas": name` + `"track": [x,y,w,h]` + `"initial": double` (0..1、 既定 0.5) + `"vertical": bool` (既定 false) + `"id"`。 見た目は 2 形式:
  - **thumb 形式**: `"thumb": [x,y,w,h]`。 track はスライダ軸方向に stretchable / 直交軸固定、 thumb は完全固定。 track 画像は **widget の全域** (= canvas 配置なら `at` の矩形そのまま) に描かれる — 角丸ボーダー込みで書き出した枠素材の端が切り詰められないようにするため、 「thumb 半分だけ内側へインセット」という一般 slider の既定は使わない。 thumb は直交軸も素材本来のサイズでレール中央に置かれる (canvas 配置でつまみがレール高さに押し潰されない)。 **`track` は省略可** — 溝 (バー) が背景画像側に描いてある素材向けで、 見えない stretchable 要素が敷かれて thumb だけ描画される (可動域は widget の bounds いっぱい)。
  - **thumb の 9-slice (キャップ付きつまみ)**: `"thumb"` はオブジェクトでも書ける — `{ "rect": [x,y,w,h], "insets": [l,t,r,b], "size": [w,h] }`。 `insets` の四辺を潰さずに `size` のつまみへ伸ばす (`size` 省略時は `rect` 原寸)。 上下 (左右) にキャップがあり胴だけ伸びる資材を、 素材原寸に縛られずレールの長さに合わせて使える。 `insets` が全て 0 なら単一矩形と同じなので配列形式でよい。
  - **fill 形式 (ゲージ型)**: thumb の代わりに `"fill": [x,y,w,h]` を指定すると、 atlas_progress と同じ track+fill 描画のまま**操作可能** (クリック/ドラッグ/矢印キー/パッド) なスライダになる。 `"fill_at": [dx,dy,w,h]` (任意) で fill の配置先を track ソース矩形の左上原点 px で指定 (枠の内側にバーが入るインセット素材向け)。
  - どちらも値変化で `value_t{double pos}` を発火。 `"value_var": "name"` (任意) で変数 store と**双方向**連動: 変数変更で値が追従 (通知のみ、 イベント非発火)、 ユーザ操作では on_change 発火に加えて変数側も更新される。 値は `"0.75"` 形式の 10 進文字列 (常に 0..1)。
  - **数値表示** (`"display_var"` + `"display"`): 下記「スライダの数値表示」を参照。 `slider` / `slider_with_range` / `atlas_slider` 共通。
  - **両端の増減矢印** (`"dec"` / `"inc"`): アトラス素材の矢印ボタンを両脇に置いて **矢印 + スライダを 1 パーツ**にできる。 `"dec"` / `"inc"` はフレーム指定 (`{ "normal": [x,y,w,h], "hilite": …, "pressed": …, "disabled": … }`、 配列 1 本なら normal のみ)、 置き場所は `"dec_at"` / `"inc_at"` (widget `at` 左上原点の相対 px、 必須)。 本体の領域は `"track_at"` (省略時は矢印の外側から自動算出)。 1 クリックの増減は `"step"` (0..1。 省略時は `display` の 1 目盛、 それも無ければ 5%)、 押し続けの自動リピートは `"repeat"` / `"repeat_delay_ms"` / `"repeat_rate_ms"`、 キー / パッドで値が動いたときは向きの矢印が `"flash_ms"` だけ光る。 **名前は左右上下ではなく «減 (dec) / 増 (inc)»** — 縦にしたときの増える側が widget の種類で逆になるため。 幾何名 (`left`/`right`、 縦なら `down`/`up`) もエイリアスで受ける。 矢印はフォーカスを取らず、 クリック時はフォーカスを本体へ渡す。 送りの実体は値編集そのものなので既存の `value_var` / `display_var` / `onAction` 配線にそのまま乗る。
- `atlas_number` — **数字素材 (0-9 の sub-rect) で数値を描く**表示専用パーツ。 フォントではなく «絵の数字» を出したいスコア / 残数 / 音量表示用。

  ```jsonc
  { "type": "atlas_number", "atlas": "ui",
    "digits": [[x,y,w,h] × 10],          // 0,1,...,9 の順。 等間隔の 1 枚素材なら
    "digits_rect": [x, y, w, h], "count": 10, "digits_vertical": false,  // 分割指定でも可
    "glyphs": { "%": [x,y,w,h], "-": [x,y,w,h] },   // 数字以外 (任意)
    "text": "50",                        // 静的初期値
    "text_var": "vol_text",              // 文字列変数を購読 (スライダの display_var を直接指せる)
    "value_var": "vol",                  // 生値 (0..1) を購読して
    "display": { "min": 0, "max": 100, "suffix": "%" },   // 自前で整形
    "align": "left" | "center" | "right",
    "spacing": 0, "scale": 1.0, "space_width": 0 }
  ```

  - `text_var` / `value_var` は変数変更で即再描画 (label の `text_var` と同じ VariableStore)。 併用時は後から来た更新が反映される。
  - 未定義の文字は幅 0 で読み飛ばす (`" "` は `space_width` があればその幅だけ送る)。
  - 各グリフは縦中央に、 左から `spacing` 間隔で並ぶ。 `align` は widget bounds 内の水平寄せ。
  - `set_text()` を持つ (`text_writer`) ので、 ホストから直接書き換えることもできる。

- `atlas_scrollbar` — **溝 + つまみのスクロールバー**。 `"atlas": name` + `"thumb"` (atlas_slider と同じ 2 形式。 9-slice でキャップ付き資材も可) + `"track": [x,y,w,h]` (省略可 = 溝が背景側に描いてある素材) + `"vertical": bool` (既定 true) + `"id"`。 **行を自前で並べる一覧** (下記「一覧の «窓»」) に、 溝・つまみ・ページ送り・ホイール・ドラッグをホスト実装なしで足すためのもの (本文が `scroller` に載っているなら `scroller` の `pos_var` で足りる)。 つなぎ方は 2 通り:
  - **index モード** (`"index_offset_var": "top"`): つまみが «一覧の先頭 index» を指す。 総件数 `"count"` / `"count_var"`、 見えている行数 `"visible"` / `"visible_count_var"` を渡すと、 **つまみの長さが «見えている行数 ÷ 総件数» に比例**する (件数が変われば長さも追従)。 «窓» の行と同じ `index_offset_var` を挿すだけで一覧が動く。 (行数の変数キーが `visible_var` でないのは、 `visible_var` が全 widget 共通の «表示 / 非表示» キーとして先に使われているため — 行数 0 でスクロールバーごと消える衝突を避けて `visible_count_var` へ改名した。)
  - **value モード** (`"value_var": "pos"`): 0..1 の位置。 `scroller` の `pos_var` と同じ変数を挿せば本文と連動する。 count / visible があればつまみは可変長、 無ければ素材の原寸で固定長。
  - 操作: つまみドラッグ / **溝クリックでページ送り** (`"page"` 行、 既定 = visible) / **ホイール** (`"wheel_step"` 行、 既定 1)。 `"thumb_min"` (既定 16) でつまみの最小長 px。
  - 値が変わると変数へ書かれ、 `id` があれば `onAction` にも流れる (index モードは行 index の整数、 value モードは 0..1 の double)。
  - **両端の増減ボタン**: atlas_slider と同じ `"dec"` / `"inc"` (+ `"dec_at"` / `"inc_at"` / `"track_at"` / `"repeat*"` / `"flash_ms"`) を書くと «両端にボタンのあるスクロールバー» になる。 送り量はホイールと同じ (`"wheel_step"` 行、 value モードは 5%)。 送りの実体は既存のスクロール処理に委ねるので、 矢印・ホイール・溝クリックが同じ規則で動く。
- `atlas_progress` — 非インタラクティブのゲージ。 `"atlas": name` + `"track": [x,y,w,h]` + `"fill": [x,y,w,h]` + `"fill_at": [dx,dy,w,h]` (任意、 fill の配置インセット。 atlas_slider と同義) + `"value": double` (0..1 静的) + `"value_var": "name"` (任意、 変数 store キー、 string→double で reactive) + `"vertical": bool`。
- `animated_sprite` — **アトラスのフレーム列を fps で自動送りするスプライトアニメ** (パラパラ / スプライトシート再生)。 `"atlas": name` + `"frames": [[u,v,w,h], ...]` (配列順 = 再生順) + `"fps": double` (既定 12) + `"loop": bool` (既定 true。 false は最終フレームで停止) + `"native_frames": bool` (実寸のまま中央へ)。 アニメアイコン、 スピナー、 待機ループ等の表示専用パーツ。
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
    "font": "Noto Sans JP#wght=500",   // 任意: 表示テキストの family[#axes]
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

- **←→ キー / パッド横軸で選択メンバーが移動**。 **端では反対側へ折り返す** (既定)。 `"choice_wrap": false` を付けると従来どおり端で素通しし、 view のフォーカスナビへ渡る (segmented_picker と同じ)。
  - 既定を折り返しにしてあるのは、 素通しだと「ON/OFF の OFF でさらに右」「言語の最後でさらに右」が**別の行へフォーカスを飛ばしてしまい、 選択操作のつもりが縦移動になる**ため。
  - メンバーが 1 つしか無いグループは折り返す意味が無いので、 `choice_wrap` の指定によらず素通しする。
- **左右の並び順は children の記載順ではなく `at` の配置座標** (画面上の左→右、 同じ x なら上→下) で決まる。 PSD レイヤ順に由来する children の並びは視覚順と一致するとは限らず、 そのままだと左右操作が逆になるグループが出るため。
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
| `initial_focus` | bool / number | 起動時フォーカスの候補。 **複数の要素に指定でき**、 先頭候補が無効 (`enabled_var` で disabled) なら次の有効な候補へ落ちる。 数値を書くと明示優先度 (小さいほど優先。 `true` = 0)、 同値は build 順。 候補の確定は表示直後の idle まで遅延するので、 ホストが `set_var` で有効/無効を流し込んでから判定される |
| `close_on_click` | bool | (button のみ) true で click 時に modal を閉じて `result.action = id` とする。 **デフォルト false** で、 click は外部 callback (= `on_event` / krkrz の `ElementsDialog.onAction`) を発火するだけ |
| `vars_on_focus` | `{name: string, ...}` | この要素が focus を得たときに変数 store に書き込む値の dict。 同じ変数を `text_var` で見ている label に自動反映 (focus 連動ヘルプテキスト等) |

### フォーカスリング表示 (アプリ全体設定)

フォーカス中の要素には lib が汎用の枠 (theme の `focus_ring_color` /
`focus_ring_width`) を描く。 状態別の絵を自前で持つ画像 UI (PSD 由来の
`atlas_button` / `atlas_toggle` 等) では枠が素材に重なって邪魔になるので、
**アプリ全体で切れる**:

```cpp
elements_modal::set_focus_ring_enabled(false);   // 既定 true
bool on = elements_modal::focus_ring_enabled();
```

画面単位ではなくグローバルテーマ (`cycfi::elements::theme::focus_ring_enabled`)
のフラグ。 button / slider / dial / thumbwheel の枠がまとめて消える。
**フォーカス自体は生きている**ので、 キー/パッドのナビゲーションと
`hilite` frame の切替は従来どおり動く。

### テーマ (`"theme"` / `"skins"` / `"widget_skins"`)

見た目を一括で差し替える仕組み。 **画面 JSON はそのまま**で、 配色・寸法・
フォント (ベクタ widget) と絵 (画像 widget) を入れ替えられる。

```jsonc
// アプリ既定 (マニフェスト) — 画面側の "theme" がこの上にマージされる
{ "entry": "title", "theme": "kenney_blue", "screens": { … } }
```

値はテーマ名 (文字列) かテーマオブジェクト。 名前のときは
`<runtime>/json/theme/<名前>.jsonc` (または `<runtime>/theme/<名前>.jsonc`) を
リソースリゾルバ経由で読む。 ホストからは `set_ui_theme(name)` /
`get_ui_theme()` (`modal.h`)。 **テーマは画面を組むときに当たる**ので、
表示中の画面へ反映するには組み直すこと (elements のテーマはプロセス全体で
1 つで、 一部の色はスタイラ構築時に焼き込まれる)。

テーマオブジェクトの中身:

| キー | 内容 |
|---|---|
| `colors` | 名前付き色。 以後どこでも `"@名前"` で参照できる。 色を書けるところは `[r,g,b,a]` / `"@名前"` / `"#rrggbb"` / `"#rrggbbaa"` / `"#rgb"` を受ける |
| (elements テーマの各フィールド) | `panel_color` / `frame_color` / `frame_corner_radius` / `default_button_color` / `label_font` / `text_box_font` / `picker_bg_color` / `picker_fg_color` / `scrollbar_color` / `focus_ring_enabled` … 約 60 個 |
| `font_variations` | `{ファミリ名: "wght=500"}` — 可変フォントの既定軸。 UI 全体の太さを 1 行で変えるためのもの |
| `font_languages` | 言語連動フォント置換表 (画面 / マニフェストと同じ書式) |
| `atlases` | 画面の `"atlases"` と同じ書式。 テーマ側で絵の出どころを宣言できる |
| `skins` | **名前付きフレーム表**。 `{名前: {atlas, rect, frames, insets, native}}` |
| `widget_skins` | **ベクタ型 → 画像実装**。 `{型名: {impl, skin, track, thumb, fill}}` |

`skins` の `insets` [l,t,r,b] を書くと 9-patch (`atlas_nine`) として描かれ、
四隅と辺を原寸のまま保ったまま `at` いっぱいに伸びる。 素材の原寸に縛られない
ので、 ベクタ用に組んだレイアウトへ画像テーマを流し込める。

画像 widget 側では `"frames"` / `"rect"` の代わりに `"skin": "名前"` と書ける
(スキンが無ければ従来どおり `frames` / `rect` を見るので既存画面は無改変で動く)。

テーマを当てていなくても引ける**予約色名**: `@ink` / `@ink_dim` / `@accent` /
`@accent_hi` / `@accent_dim` / `@panel` / `@panel_line` / `@disabled` / `@bg`
(現在の elements テーマから自動で埋まる)。 加えて `@slider_thumb` /
`@slider_track` / `@ring_outline` を定義すると、 スタイラ内部の色を差し替える。

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
- 外から一覧・観測したいときは `list_vars()` / `get_var()` / `set_var_watcher()`
  (→「外から覗く・触る」)。 「この画面がどの変数を何に使っているか」も取れる。
- ホストからも `overlay_session::set_var(name, value)` で書ける (focus poll 以外の書き手。 ソフトウェアキーボードの入力文字列表示のような「ホスト状態 → label」の動的反映に使う)。 反映は次フレームの `render_to_buffer`。
- 初期 focus の widget の `vars_on_focus` は次回 render 前に poll される (= `vars` の初期値は最初の poll までだけ表示される。 通常は初期 focus の値と同じにしておく)。

#### hover 連動 (`vars_on_hover`)

`vars_on_focus` の hover 版。 **要素の型を問わず**書ける (label / 画像 /
コントロールいずれも)。 `{名前: 値}` を**カーソルが乗っている間だけ**書き、
**離れたら乗る直前の値へ戻す**。 キー / パッドのフォーカスとは独立なので、
「一覧の行をマウスでなぞって説明を出す / 色を変える」はこちらで書く。

```jsonc
{ "type": "label", "id": "row1", "text": "SAVE",
  "color_var": "row1_color",
  "vars_on_hover": { "row1_color": "@accent_hi", "help": "セーブします。" } }
```

⚠ **当たりを持たない要素 (label / 画像) は hover を受け取れない**ので、 この
指定を書くと proxy が当たりを引き受ける。 そのため **下に重なっているコントロールが
押せなくなる** (`hover_focus_link` と同じ事情)。 コントロール自身に付けた場合は
中身の当たりが優先されるので影響しない。

#### 変数連動ファミリ一覧

変数 store を読む / 書くフィールドの早見。 いずれも同名変数で連動する:

| フィールド | 対象 widget | 向き | 値の形式 |
|---|---|---|---|
| `text_var` | label / text_box / text_area | 読み | 表示文字列 |
| `color_var` | label | 読み (文字色) | `"@名前"` / `"#rrggbb"` / `"r,g,b[,a]"` (0-255) |
| `count_var` | text_area | 読み | 表示クラスタ数 (10 進、 `"-1"` = 全部) |
| `vars_on_focus` | focusable 全般 / choice_nav グループ | 書き (focus 時) | 任意 string |
| `vars_on_hover` | 全 widget 共通 | 書き (hover 中。 離れたら直前の値へ復帰) | 任意 string |
| `text_list` / `text_list_id` + `index_var` | label / text_area | 読み | 10 進 index (`"2"`) |
| `text_list_var` | label / text_area | 読み (一覧データそのもの) | **改行区切り** (`"A\nB\nC"`) か、 先頭が `[` なら **JSON 配列** (`["A","B","C"]`) |
| `index` / `index_offset_var` | label / text_area | 読み (引く位置 = `index` + offset) | 10 進 index。 `index` は行ごとの固定値、 `index_offset_var` は**行で共有する先頭位置** |
| `rect_list` + `index_var` | atlas_image | 読み | 10 進 index |
| `index_var` | picker 系 (cycle / framed / segmented / atlas_cycle_picker) | **双方向** (選択変更で書き + 変数変更で quiet 追従 / 既値があれば initial 採用) | 10 進 index |
| `enabled_var` | cycle_picker / atlas_cycle_picker | 読み (選択肢の有効/無効 mask) | `'0'`/`'1'` 文字列 (`"10111011"`) |
| `options_var` | picker 系 4 種 | 読み (選択肢リストそのもの) | 改行区切り、 先頭 `[` なら JSON 配列 (`text_list_var` と同書式) |
| `enabled_var` | button 系 (`button` / `atlas_button` / `invert_button` / `ring_button`) | 読み (要素そのものの有効/無効) | `"0"` = 無効 / それ以外 = 有効 |
| `selected_var` (+`selected_value`) | atlas_choice / radio_button | **双方向** (var == selected_value で選択 / クリックで var へ selected_value を書き戻し) | 任意 string (`selected_value` 既定 `"1"`) |
| `value_var` | atlas_slider / atlas_progress | slider: 読み (通知のみ、 イベント非発火) / progress: 読み | 10 進小数 (`"0.75"`) |
| `pos_var` | scroller | **双方向** (スクロールで書き + 変数変更で本文をその位置へ送る) | 10 進小数 (`"0.37"`) |
| `fraction_var` / `display_var` | scroller | 書き (見えている割合 / 整形済み文字列) | 10 進小数 / 表示文字列 |
| `at_var` | canvas の任意の子 | 読み | `"x,y"` または `"x,y,w,h"` (10 進 px) |
| `at_var_offset` | canvas の子 | (指定値) | `[dx, dy, dw, dh]` — `at_var` の値への差分 |
| `index_offset_var` | atlas_scrollbar | **双方向** (操作で書き / 変化で追従) | 10 進整数 (先頭 index) |
| `count_var` / `visible_count_var` | atlas_scrollbar | 読み | 10 進整数 (総件数 / 見えている行数) |
| `hover_var` / `select_var` | list | 書き (行に乗った / 行を選んだ) | 10 進整数のデータ index (hover 無しは `"-1"`) |
| `row_hover_var` / `row_select_var` | list | 書き (行ごと) | `"1"` / `""` (`#index` で行番号へ展開) |
| `drag_at_var` | 全 widget 共通 | 書き (ドラッグ中) | `"x,y"` (10 進 px) |
| `opacity_var` | 全 widget 共通 | 読み | 不透明度 0..1 の 10 進小数 (`"0.6"`) |
| `visible_var` | 全 widget 共通 | 読み | `"0"` / `"false"` / 空文字 = 非表示、 それ以外 = 表示 |
| `background_opacity_var` | トップレベル (`background` の板) | 読み | 不透明度 0..1 の 10 進小数 |

- `"opacity"` (0..1、 既定 1.0) / `"opacity_var"` — **要素単位の不透明度**。 canvas の global_alpha を子の描画中だけ掛ける (オフスクリーン合成ではなく、 その要素が描く fill / stroke / text / image の alpha に乗算するので軽い)。 字幕窓の下地だけ薄くする、 といった用途向け。 `opacity_var` で変数連動でき、 ホストが `setVar("sub_bg_alpha", "0.6")` するだけで変わる。 0 で描画自体を省く。
- `"visible_var"` — **要素の表示 / 非表示を変数で切り替える**。 非表示のときは描かない・フォーカスを受け取らない (方向ナビの飛び先にならない)・当たり判定に出ない (クリックが下の要素へ抜ける)・入力イベントを一切受けない。 機種によって不要な設定項目を消す用途 (PC 専用のフルスクリーン切替をコンソール版で出さない等) を想定。

    ⚠ **場所は空けたまま**になる。 canvas は子を絶対座標で配置するので、 非表示にしてもそこに空白が残り、 下の項目は詰まらない。 詰めたいならレイアウト自体を機種別に用意する必要がある。

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

#### 一覧の «窓» (`index` / `index_offset_var` / `text_list_var`)

`label` / `text_area` の「一覧から 1 個引いて表示する」機構 (`text_list` + `index_var`) の拡張。 **新しい型は増えていない**。 引く位置は

    引く位置 = base + offset
      base   … `"index"` (行ごとの固定値) または `"index_var"` (変数で動かす)
      offset … `"index_offset_var"` (**行で共有**する先頭位置)

で決まる。 行 N 個に `"index": 0..N-1` を書き、 全行へ同じ `"index_offset_var"` を挿すと **«N 行の窓»** になる。 ホストは**先頭位置の変数を 1 個書き換えるだけ**で一覧を送れる (行ごとに変数を用意して `text_var` / `visible_var` を回す必要がない)。

```jsonc
// 6 行の窓。 行ごとに違うのは "index" だけ
{ "at": [148, 192, 676, 40], "type": "label",
  "index": 0,                     // 窓の中の位置 (0..5)
  "index_offset_var": "list_top", // 先頭位置 (6 行で共有)
  "text_list_var":    "list_items" },
...
// ホスト側
sess.set_var("list_items", "A\nB\nC\nD\nE\nF\nG\nH");
sess.set_var("list_top",   "3");   // → 窓が D..H + 空行 1 行になる
```

- `"text_list_var"` — 一覧データ自体を変数から取る。 **2 形式**を受ける:
  - **改行区切り** (`"A\nB\nC"`) — ホストが素直に組み立てられる形。 末尾の CR は落とす
  - **JSON 配列** (`["A","B","C"]`) — **先頭の非空白文字が `[`** ならこちら。 項目に改行を含めたい / ホストが既に配列を持っている場合向け。 文字列以外の要素は文字列化して入れる。 JSON として壊れていたら改行区切りとして扱う (黙って空にはしない)
- 静的な `"text_list"` / `"text_list_id"` にも `"index_offset_var"` は効く。 多言語の一覧を窓で送るなら `text_list_id` + `index_offset_var` の組合せになる (言語切替でも現在位置を引き直す)。
- `"text_list_var"` を書いた場合、 静的な `text_list` / `text_list_id` は無くてよい。

⚠ **範囲外 (0 未満 / データ末尾より後ろ) の扱いが 2 通りある**。 `"index_offset_var"` の有無で切り替わる:

| モード | 範囲外 | 理由 |
|---|---|---|
| 従来 (`index_offset_var` **無し**) | **clamp** (端の項目で止まる) | picker や `index_var` で «番号を選ぶ» 用途は端で止まるのが自然。 **既存の挙動は変えていない** |
| 窓モード (`index_offset_var` **あり**) | **空文字** (何も表示しない) | データ末尾より後ろの行は «何も出ない» のが正しい。 clamp すると最後の項目が並んで見えてしまう |

行 N 個そのものは uitool の `copies` が生成できるので、 画面 JSON を手で N 回コピペする必要はない。

**スクロールバーを付ける**なら `atlas_scrollbar` に同じ `index_offset_var` を挿す。
総件数 (`count_var`) と行数 (`visible`) を渡せば、 つまみの長さと位置・ホイール・
ページ送り・ドラッグまで含めてホスト実装なしで動く。 ホスト側は一覧データ
(`text_list_var`) と件数 (`count_var`) を書くだけになる。

```jsonc
{ "at": [836, 192, 16, 240], "type": "atlas_scrollbar", "atlas": "ui", "id": "sb",
  "track": [16, 0, 8, 64],
  "thumb": { "rect": [0, 0, 16, 32], "insets": [0, 6, 0, 6] },
  "index_offset_var": "list_top",   // 窓の行と同じ変数
  "count_var": "list_n", "visible": 6 }
```

#### 行テンプレートの一覧 (`list`)

«窓» は «文字» までは面倒を見るが、 行の当たり判定・hover・選択の色は行ごとに手で
組むことになる (行あたり label 1 + 当たり用ボタン 1、 変数は文字・色・当たりの位置の
3 本、 さらに «件数が足りない行を画面外へ逃がす» 後始末)。 `list` は **1 行分の
テンプレートを行数ぶん複製**して、 そこまでまとめて持つ。 ホストは «データを流し込む
だけ» になる。

```jsonc
{ "at": [148, 192, 676, 240], "type": "list", "id": "files",
  "rows": 6,                       // 窓の行数
  "row_size": [676, 36],           // 行の矩形 (当たり判定もこれ)
  "pitch": [0, 40],                // 行ごとの送り (既定 = row_size の高さ)
  "index_offset_var": "top",       // 先頭 index (スクロールバーと共有できる)
  "count_var": "n",                // 総件数 (足りない行は消える)
  "hover_var": "hov",              // いま乗っている «データ index» ("-1" = 無し)
  "select_var": "sel",             // 選ばれている «データ index»
  "row_hover_var":  "rhov#index",  // 行ごとのフラグ ("1" / "")
  "row_select_var": "rsel#index",
  "row": {
    "type": "layer",               // 先頭が最前面
    "children": [
      { "type": "label", "id": "row#index", "text_list_var": "items" },
      { "type": "atlas_image", "atlas": "ui", "rect": [0, 96, 676, 36],
        "visible_var": "rsel#index" },      // 選択の下地
      { "type": "atlas_image", "atlas": "ui", "rect": [0, 60, 676, 36],
        "visible_var": "rhov#index" }       // hover の下地
    ]
  } }
```

テンプレートの展開規則:

- 文字列値の中の **`#index` が行番号 (0..rows-1) へ置換**される。
  `"id": "row#index"` → `"row0"` / `"row1"` … (`Agent.dialogClick` で行を指せる)、
  `"visible_var": "rhov#index"` → 行ごとのフラグ変数。
- 一覧を引く指定 (`text_list_var` / `text_list` / `text_list_id` / `rect_list` /
  `rect_list_var`) を持つ要素には **`"index"` (行番号) と `"index_offset_var"` が
  自動で挿される** (明示してあればそのまま)。 行テンプレートに
  `"text_list_var": "items"` と書くだけで «窓» になる。

行の当たり:

- 行の中の widget (button 等) が**先に**クリックを受ける。 誰も受けなければ
  **行そのもののクリック**として `onAction(id, 行の «データ index»)` が発火する
  (透明な当たり判定ボタンを敷かなくてよい)。 モーダルなら `result.values[id]` にも
  選択された index が載る。
- `count` / `count_var` を渡すと、 **データが無い行は描画も当たりも消える**
  (画面外へ逃がす後始末が要らない)。

hover / 選択の色は 2 通りの受け方がある。 画面 JSON の中で閉じるなら
`row_hover_var` / `row_select_var` (行ごとのフラグを `visible_var` /
`color_var` で受ける)、 **絵がホスト側のレイヤ**にあるなら `hover_var` /
`select_var` を `ElementsDialog.onVar` で拾ってホストが差し替える。

スクロールは `atlas_scrollbar` に同じ `index_offset_var` / `count_var` を挿すだけ
(上記)。 一覧側にホイールを付けたい場合もスクロールバーが受ける。

### 掴んで動かす (`drag_at_var` / `drag_events` / `drag_bounds`)

マウスで要素を掴んで動かす指定。 `vars_on_hover` と同じく build の最後に proxy で
包むので、 **要素の型を問わず**書ける。 出口が 2 つあり、 両方同時に書ける:

| キー | 型 | 説明 |
|---|---|---|
| `drag_at_var` | string | ドラッグ中の位置を `"x,y"` (10 進 px) で変数へ書く |
| `drag_bounds` | `[x, y, w, h]` | 可動域。 **絵全体**が中に収まるよう左上位置を丸める |
| `drag_events` | bool | `begin` / `move` / `end` をホストへ通知する (下記「ドラッグ通知」) |

```jsonc
{ "at": [200, 120, 96, 96],
  "type": "atlas_image", "atlas": "ui", "rect": [0, 0, 96, 96], "id": "pin",
  "drag_at_var": "pin_at",             // ドラッグ位置をここへ書き
  "at_var":      "pin_at",             // 同じ変数で配置を駆動 = 絵がついてくる
  "drag_bounds": [0, 0, 1920, 1080],
  "drag_events": true }
```

- `drag_at_var` を canvas 子の `at_var` に挿すと、 **ホスト実装なしで絵がついてくる**
  (C++ 内で完結するのでフレーム同期。 描画とずれない)。
- 2 回目以降のドラッグは `at_var` に残っている位置を起点にするので、 元位置へ
  戻らない。
- 下地は lib の `tracker`。 `tracker::click` / `drag` は先に中身へ転送してから追跡を
  始めるので、 **ボタンに付けてもボタンのクリックは効く**。
- **ドラッグ中に外から `at_var` を書き換えても «掴み位置» は維持される**。 追跡は
  掴んだ瞬間の `at_var` 値を起点として控え、 以後は «掴んでからの移動量» を足した
  値を毎フレーム書き直すため、 途中のホスト書込は次の move (と離したときの end) で
  上書きされる。 ホスト側の書込が位置として効くのは «掴んでいない間» で、 次に
  掴んだときの起点として読み直される。
- **1 本の変数で複数枚を動かす**には、 挿す側 (canvas の子) に
  `"at_var_offset": [dx, dy, dw, dh]` を書く。 変数の値へ加える差分で、
  dx/dy は位置、 dw/dh はサイズへの加算。 つまみを «上キャップ + 伸びる胴 +
  下キャップ» の 3 枚で描くような資材でも、 `drag_at_var` が書く 1 本の変数で
  まとめて動かせる (offset が無いと 3 枚のうち 1 枚しか追従しない)。

```jsonc
// つまみ 3 枚を 1 変数 "thumb_at" で動かす (胴は上下キャップぶん詰める)
{ "at": [40,  92, 16,  8], "type": "atlas_image", "atlas": "ui",
  "rect": [0, 0, 16, 8], "at_var": "thumb_at", "at_var_offset": [0, -8, 0, 0] },
{ "at": [40, 100, 16, 40], "type": "atlas_image", "atlas": "ui",
  "rect": [0, 8, 16, 8], "stretch_v": true, "at_var": "thumb_at" },
{ "at": [40, 140, 16,  8], "type": "atlas_image", "atlas": "ui",
  "rect": [0, 16, 16, 8], "at_var": "thumb_at", "at_var_offset": [0, 40, 0, 0] }
```

- **使い分け**: 見た目の追従は `drag_at_var` (フレーム同期)、 «どこで離したか» の
  ような判断は `drag_events` (通知は非同期になり得るので、 見た目を任せない)。

### i18n (`strings` / `lang` / `text_id` / `text_list_id` / `options_id`)

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

- label / text_area / button 系の `"text_id": "menu.save"` — 現在言語の訳文を表示。 `"text"` は i18n 非対応ランタイム / 未知 id 向けの静的 fallback。
- picker 系の `"options_id": ["opt.speed.slow", ...]` — options を textID で与える (`options` より優先)。 言語切替時は**選択 index を維持**したまま表示文字列だけ `set_options` で差し替わる。
- label / text_area の `"text_list_id": ["help.save", ...]` (+ `index_var`) — 指定番号表示の textID 版 (`text_list` より優先)。 言語切替時は**表示中の index を維持**したまま引き直す。 メニューの説明文のように「focus 連動 + 多言語」な 1 本のラベルはこれで賄える。
- 未知 id は id 文字列をそのまま表示。 現在言語にエントリが無ければ先頭言語へフォールバック。
- 実行中の言語切替はホスト API (`overlay_session::set_language(lang)` / navigator 経由)。 subscribe 済みの全 label / picker が再解決される。
- その画面が**どの言語を出せるか**は `overlay_session::languages()` (`strings` の lang キーの和集合)。 ホストが言語切替 UI を組むのに使う。
- **widget 丸ごとの言語別出し分けは `locale_variant`** — 現在言語に一致する子だけを表示するデッキ:
  ```jsonc
  { "type": "locale_variant", "at": [x,y,w,h], "default": "en",
    "children": [ { "lang": "ja", <widget> }, { "lang": "en", <widget> } ] }
  ```
  各 child は `"lang"` 付きの通常 widget オブジェクト。 言語切替で deck の表示が切り替わる (全 child は同一 box を占める)。 一致する lang が無ければ `"default"`、 それも無ければ先頭。 タイトルロゴ画像の言語別差し替えなど、 文字列テーブルでは表せない出し分けに使う。

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

**同じ種類 (チャンネル) を並べるとシーケンス (折れ線) になる**。 `move` を 2 本以上並べるような場合は合成できないので、 **`"delay"` を過ぎて再生を始めたエントリのうち最後のもの**が値を決める (まだ待っているエントリは先行の動きを邪魔しない)。 したがって **`"delay"` の昇順に並べるだけで多段の演出が書ける**:

```jsonc
// 右から入って、いったん止まり、少し戻る (3 段)
"animate": [
    { "type": "move", "from": [220, 0], "to": [0,   0], "frames": 12, "delay": 0  },
    { "type": "move", "from": [0,   0], "to": [-16, 0], "frames": 6,  "delay": 12 },
    { "type": "move", "from": [-16, 0], "to": [0,   0], "frames": 6,  "delay": 18 }
]
```

- 各段の `"from"` は**前段の `"to"` に合わせる** (繋ぎ目が飛ばないように)
- 1 本しか無いチャンネルの挙動は従来どおり (`"delay"` 中は `"from"` で待つ)
- チャンネルが違うエントリ (`move` と `scale` 等) は今までどおり同時に掛かる

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
    // false でマウス hover とフォーカスを独立させる。 画面を開いた直後だけは
    // ポインタが実際に動くまでこの追従を止める (前の操作でたまたま項目の上に
    // 残っていただけのポインタが、 宣言した initial_focus を即座に上書き
    // してしまうのを防ぐ。 hover の見た目は切らない)。
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
    // と併存した場合はこちらが勝つ (要素側は複数候補を優先度順に持て、
    // 無効な候補は飛ばして次の候補が選ばれる)
    "initial_focus": "BTN_START",

    // 入力 → named action のバインド (組込デフォルトへの差分)。
    // 同一入力の再宣言で上書き、 "action": "none" で無効化、
    // "action": "passthrough" で未処理化 (消費せずホストへ素通し。 後述)。
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

warp が起きるのは **直近の操作がキー / パッドだったとき**だけで、 実マウスが動く
(カーソル位置が実際に変わる) か、 マウスボタンが押された時点でナビ種別は mouse へ
戻る。 ここが戻らないと、 一度キー操作した後はマウス hover でフォーカスが動くたびに
warp が走り、 warp の合成 move がまた hover を動かして、 隣接する 2 項目の間で
フォーカスと実カーソルが振動し続ける (グリッド状 UI = ソフトウェアキーボード等が
操作不能になる)。 パッドの斜め入力でフォーカスが上下に振動するのも同じ経路。

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
- `"action": "passthrough"` は何も bind しない = その入力を**未処理のまま
  ホストへ素通し**する (組込デフォルトも無効化した上で)。 常駐する非モーダル
  オーバレイが「この入力は下のアプリのもの」と宣言するのに使う。 `"none"` は
  消費して何もしない (下へは届かない) のに対し、 こちらは消費自体をやめる。
- Esc の旧 hard-code (`on_key_down` 直 `begin_finish`) は撤廃済み。 バインド
  差し替え / 無効化で画面ごとに戻る挙動を制御できる。
- SE はアクション発火時に `event_callback("<se>", false, SE名)` で通知
  (`nav` は focus 変化検出、 `accept` は button click で一元発火)。

#### 名前リファレンス

- `key`: `"enter"` / `"escape"` (`"esc"`) / `"tab"` / `"space"` / `"backspace"` / `"delete"` / `"insert"` / `"left"` / `"right"` / `"up"` / `"down"` / `"page_up"` (`"pgup"`) / `"page_down"` (`"pgdn"`) / `"home"` / `"end"` / `"a"`〜`"z"` / `"0"`〜`"9"` / `"f1"`〜`"f12"`
- `mods` 配列要素: `"shift"` / `"ctrl"` (`"control"`) / `"alt"` / `"super"` (`"cmd"` / `"command"`) / `"action"` (= Ctrl on Win/Linux, Cmd on Mac)
- `pad`: `"a"` / `"b"` / `"x"` / `"y"` / `"dpad_up"` / `"dpad_down"` / `"dpad_left"` / `"dpad_right"` / `"lb"` (`"l1"`) / `"rb"` (`"r1"`) / `"lt"` (`"l2"` / `"lt_click"`) / `"rt"` (`"r2"` / `"rt_click"`) / `"l3"` / `"r3"` / `"back"` / `"start"` / `"guide"` / `"face_south"` / `"face_east"` / `"face_west"` / `"face_north"`
  - フェイスボタンは**刻印基準** (`"a"`/`"b"`/`"x"`/`"y"`) と**位置基準** (`"face_*"`) の 2 系統。 1 回の押下で両方が届くので、 ボタンごとにどちらで縛るかを選ぶ (任天堂系と Xbox で X/Y の位置が入れ替わるため)。 説明表示の `pad_icon` も同じ 2 系統の名前を持つので、 割り当てと表示は同じ基準どうしで組にする (例: `"pad": "a"` の説明は `name: "a"`、 `"pad": "face_north"` の説明は `name: "face_north"`)
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

### ドラッグ通知 (`drag_callback`)

`"drag_events": true` を書いた要素の押下 → 移動 → 離すが届く。 受け口は
`event_callback` とは**別**で、 **`event_callback` の署名は変えていない**:

```cpp
sess.set_drag_callback([](elements_modal::drag_event const& ev) {
    // ev.id                  … widget の "id"
    // ev.ph                  … drag_event::phase::begin / move / end
    // ev.x, ev.y             … 現在位置 (view 論理座標)
    // ev.dx, ev.dy           … 前回の通知からの差分 (begin では 0)
    // ev.start_x, ev.start_y … 掴んだ位置
    // ev.modifiers           … 押されている修飾キー
});
```

receiver は shared_ptr のスロット越しに持つので、 **`start()` の前後どちらでも
設定・差し替えできる** (空を渡せば解除)。 見た目の追従は `drag_at_var` に任せ、
こちらは «どこで離したか» の判断に使う (→「掴んで動かす」)。

receiver は 1 個で、 `"drag_events": true` を書いた要素すべての通知がここに来る。
どの要素かは `ev.id` で見分けるので、 通知が要る要素には `"id"` を付けておくこと。

`end` の `x` / `y` は **離した位置**、 `dx` / `dy` は直前の通知からの差分。
(下地の `tracker` は離した時点で追跡位置を更新しないので、 proxy 側が `up` の
座標を控えて反映している。 実マウスは離す直前に必ず移動イベントが来るため
普段は差が出ないが、 入力を合成して検証するときはここが効く。)

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

### 再ラスタライズの抑止 (update / needs_render)

`render_to_buffer` は毎フレーム全ウィジェットを ThorVG (CPU) で再ラスタライズする。 静止した UI では無駄なので、 ホストは **update と描画を分離**して変化のあるフレームだけ描画できる:

```cpp
// 毎フレーム: 状態更新 (変数 poll / 演出 tick / キャレット点滅 / 退場演出の完了検出)。
// 戻り値 = 見た目に影響する変化があり再描画が必要か。
bool dirty = sess.update();

if (dirty) {
    overlay_session::render_rect rect;
    sess.render_to_buffer(my_buffer, buf_w_px, buf_h_px, surface_w, surface_h, rect);
    // → テクスチャへアップロードして rect の位置に表示
} else {
    // 前回アップロード済みのテクスチャを同じ位置にそのまま表示するだけでよい
}
```

- **`update()` は描画をスキップするフレームでも毎フレーム呼ぶこと** (止めるとキャレット点滅・遅延 focus 適用・パーツ演出・退場演出の完了検出が止まる)。
- ダーティになる契機: 入力イベントの転送 / focus・hover の変化 / パーツ演出の再生中 / `set_var`・`set_language` の実変化 (同値書込は無視) / view 内部の refresh 要求 (キャレット点滅等) / `notify_view_resize`。
- `needs_render()` で現在のダーティ状態だけ読める。 `invalidate()` はセッションから観測できない外部変化 (`refresh_mem_image` による mem:// 画像差替等) 用の明示的な再描画要求。
- `update()` を呼ばず従来どおり `render_to_buffer` だけ呼ぶホストは無変更で動く (内部で update 相当を自動実行する後方互換)。

### 外から覗く・触る (検証ツール / デバッグパネル向け)

「実行中の画面を外から観測して操作する」ためのホスト API 一式。 elements_console
の操作パネル (ブラウザ + REPL) はこれだけで組んである。 ゲーム本体では使わなくて
よいが、 画面の作り込み中に「今この変数はいくつか」「この言語は出せるのか」を
見たいときの入口になる。

```cpp
// 画面が使っている変数 — JSON の参照表 + ストアの現在値をマージしたもの。
// 参照だけあって一度も書かれていない変数も、 逆にホストが set_var で作った
// だけの変数も載る。
for (auto const& v : sess.list_vars()) {
    // v.name / v.value / v.used_by = [{要素 id, 参照の種類}]
    // 種類は JSON のキーそのもの ("text_var" / "visible_var" / "vars_on_focus" …)
}
std::string cur;
sess.get_var("hp", cur);                    // 現在値だけ引く

// 変数が変わったら教えてもらう。 ホストの set_var だけでなく vars_on_focus の
// 書込やスライダの value_var / display_var 連動など、 全ての書込経路で発火する
// (同値書込では発火しない)。 画面ごとに session を作り直すので毎回設定する。
sess.set_var_watcher([](std::string const& name, std::string const& value) {
    // 注: レンダリング中に呼ばれうる。 記録に留め、 ここで画面を作り直さない
});

// この画面が出せる言語 ("strings" の lang キーの和集合、 辞書順)
for (auto const& lang : sess.languages()) { /* 言語切替 UI を組む */ }

// 名前で入力を注入する。 語彙は画面 JSON の "input"."bindings" と同じ表
// (native → enum の変換はホストアダプタ側の仕事なので混同しないこと)。
sess.on_key_down(elements_modal::parse_key_code("enter"),
                 elements_modal::parse_modifier("shift"));
sess.on_pad_button(elements_modal::parse_pad_button("dpad_up"), true);
```

`list_widgets()` (id + type の列挙)、 `focus_by_id()` / `activate_by_id()`
(要素を名指しで動かす)、 `play_animation()` (演出の手動発火) も同じ用途で使える。
**要素を名指しで動かす API と、 実際の入力を流す API は別物**で、 当たり判定・
フォーカスナビ・ドラッグの確認は後者でないと意味がない。

画面スタックを直接動かす `navigator::push()` / `pop()` / `replace()` /
`stack()` も検証ツール向け (通常の遷移は `advance()`)。 いずれもスタックを
書き換えるだけなので、 画面の作り直しはホストの責務。

### 入力転送の戻り値 (handled / pass-through)

`on_key_down` / `on_key_up` / `on_pad_button` は **`bool` (このダイアログが入力を消費したか)** を返す。 `true` = Esc / focus 中 widget が処理 / 既知パッドボタン、 `false` = 未処理。 ホストが**複数 UI を重ねる / ゲームと共存させる**場合、 この戻り値を見て「ダイアログが使わなかったキーはゲーム側へ素通しする」といったキーボードフォーカスの pass-through を実装できる (戻り値を無視すれば従来どおり)。 `on_text_input` / `on_mouse_*` / `on_pad_axis` は `void` のまま。

## 登録済みフォントの列挙 (`font_families`)

フォント選択画面をホスト側で組むための **elements 本体 API**
(`lib/include/elements/support/font.hpp`)。 いま描画に使える family 名を昇順で
返す:

```cpp
#include <elements/support/font.hpp>

for (auto const& family : cycfi::elements::font_families())
    ;   // picker の "options" に積む / 一覧に並べる
```

- 返るのは fonts ディレクトリ / `register_font()` で**登録済み**の family だけ。
  OS にインストールされているだけのフォントは含まれない (その列挙はホストの仕事)。
- ディレクトリから読んだフォントは **«整形名» と «ファイル stem» の 2 つの名前で
  登録される** (`Noto Sans JP` と `NotoSansJP-VF`) ので、 同じ face が 2 行出る。
  どちらの名前でも描けるが、 1 face 1 行にしたいなら呼出側で絞る。
- ある family が使えるかだけ知りたいときは `font_family_available(name)`。

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

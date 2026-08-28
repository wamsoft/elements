//---------------------------------------------------------------------------
//!@file 内部: JSON → Elements 要素ツリー変換層
//
// 純粋 C++ + SDL_Log + std::function ベースの JSON → Elements ツリー変換。
// 公開 API ではない (実装内部用)。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_JSON_LAYOUT_H
#define ELEMENTS_MODAL_JSON_LAYOUT_H

#include "elements_modal/modal.h"   // value_t
#include "elements_modal/animator.h" // anim_binding (Phase A: パーツ演出)

#include <elements.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elements_modal {

// transition_spec / app_manifest は modal.h (public) に定義済。

// event_callback は modal.h で公開定義済 (std::string id ベース)。
// 内部 layout builder もこれと同じ型を使う。

//! @brief named UI action への 1 バインド宣言 ("input"."bindings" の 1 要素、
//! および input_defaults.jsonc のもの)。 src で key / pad / mouse / wheel の
//! どれか 1 入力を指し、 action に解決先アクション名を持つ。
//! action "none" は該当入力の無効化 (入力は消費するが何もしない)。
struct action_binding
{
	enum class source { key, pad, mouse, wheel };
	source src = source::key;

	cycfi::elements::key_code   key = cycfi::elements::key_code::unknown;   // src==key
	int                         mods = 0;                                    // src==key
	cycfi::elements::pad_button pad = cycfi::elements::pad_button::unknown;  // src==pad
	cycfi::elements::mouse_button::what
	                            mbtn = cycfi::elements::mouse_button::left;  // src==mouse
	int                         wheel_dir = 0;                               // src==wheel (+1=up / -1=down)

	std::string action;        //!< named action ("cancel"/"accept"/"nav_*"/... 未知名はホスト通知)
	bool        force = false; //!< text input focus 中も発火するか
	bool        force_set = false; //!< JSON で force 明示があったか (無ければ action 既定)
	std::string target;        //!< 予約: page/scroll 対象 widget id (現状未使用)
};

//! @brief "input" ブロックのうち action バインド関連の解析結果。
//! (arrow_focus_nav / axis mode 等の view settings は apply_input closure が担当)
struct input_action_config
{
	std::vector<action_binding> bindings;
	std::map<std::string, std::string> se;   //!< action 名 or カテゴリ → SE 名
	std::string initial_focus_id;            //!< "initial_focus": "<id>" (画面別のみ有効)

	//! "cursor_warp": bool — キー/パッド由来のフォーカス変化をホストへ通知し、
	//! ホストがマウスカーソルを focus hot point へ warp する運用を有効化。
	//! -1=未指定 (下層の値を継承) / 0=off / 1=on。 組込既定は off。
	int cursor_warp = -1;
};

//! @brief 変数参照表: 変数名 → [{要素 id, 参照の種類}]。
//! 種類は JSON のキーそのもの ("text_var" / "visible_var" / "vars_on_focus" 等)。
//! id は「いちばん近い祖先の id」なので、 id 無しの子要素での参照も辿れる。
using var_ref_map =
	std::map<std::string, std::vector<std::pair<std::string, std::string>>>;

//! @brief JSON 1 件のパース結果。
struct parsed_layout
{
	std::shared_ptr<cycfi::elements::element> root;
	int  width  = 400;  //!< top-level "size":[w,h] から取得 (run_modal は無視)
	int  height = 300;

	//! overlay の配置 / 拡縮基準 (JSON top-level "base":
	//! "window" (既定) | "content")。 modal.h の overlay_base 参照。
	overlay_base placement_base = overlay_base::window;

	//! 初期キーボードフォーカスを当てる要素 (任意)。
	//! JSON で要素に "initial_focus": true を付けたものから単一選択 (先勝ち)。
	//! ホストは view.content() の後に view.focus(pick_initial_focus()) を呼ぶ。
	std::shared_ptr<cycfi::elements::element> initial_focus;

	//! "initial_focus": true を付けた要素の一覧 (build 順)。 先頭要素が
	//! enabled_var 等で無効なとき、 次の候補へフォールバックするために持つ
	//! (例: SAVE が無効な場面ではその次に印を付けた MAP へ初期フォーカス)。
	std::vector<std::shared_ptr<cycfi::elements::element>> initial_focus_list;

	//! initial_focus_list から最初の有効 (is_enabled) な要素を返す。
	//! すべて無効 / リストが空のときは従来どおり initial_focus (先勝ち要素)。
	std::shared_ptr<cycfi::elements::element> pick_initial_focus() const;

	//! view へ input 設定 / バインド / ショートカットを適用するクロージャ。
	//! 中で view.arrow_focus_navigation / dpad_mode / bind_pad_button /
	//! bind_shortcut 等を呼ぶ。 ホストは view.content() の後に呼ぶ。
	//! 何も設定がなければ空。
	std::function<void(cycfi::elements::view&)> apply_input;

	//! "閉じるボタン" として登録された button の id 集合。 JSON で
	//! "close_on_click": true を付けたものが入る。 デフォルト (省略) は閉じない。
	//! ホスト collector はこの set に id が含まれる button click でのみ
	//! 終了フラグを立てる。 含まれない button click は外部 callback だけ発火。
	std::set<std::string> close_button_ids;

	//! 毎フレーム呼ぶと現在 focus されている要素の "vars_on_focus" を JSON 内
	//! VariableStore に反映し、 同じ変数を "text_var" で見ている label に
	//! set_text が走る。 ホストは render 前 or input 処理後に呼ぶ。 null 可。
	std::function<void()> focus_poll;

	//! 画面遷移定義。 key = action id (button id / picker id 等。 "" は Esc /
	//! B / 右クリック等の空 action 用) → value = 遷移仕様。 マニフェスト駆動
	//! ランナがこれを見て次画面を決める。 空なら遷移定義なし (ホストの既定
	//! 挙動 = entry なら exit / 子画面なら back)。
	std::map<std::string, transition_spec> transitions;

	//! id 付き要素 → element_ptr のマップ。 shortcut の "target" 解決と、
	//! ホスト側の focus_by_id() で参照。
	std::map<std::string, std::shared_ptr<cycfi::elements::element>> id_map;

	//! id 付き要素を「登録順」に id+type で列挙 (UI ツリー dump 用)。
	std::vector<std::pair<std::string, std::string>> id_types;

	//! focus_poll が更新する「現在 focus されている id」スロット。 ホストは
	//! このポインタの中身を読めば現在の focused id を取れる。 focus poll を
	//! 一度も呼んでない / 何も focus されてない場合は空文字列。
	std::shared_ptr<std::string> focused_id_slot;

	//! 毎フレーム呼ぶと現在 hover (マウスオーバー) されている button 系の id を
	//! hovered_id_slot に書く poll。 hover トリガ演出の駆動にホストが使う。 null 可。
	std::function<void()> hover_poll;

	//! hover_poll が更新する「現在 hover されている id」スロット。
	std::shared_ptr<std::string> hovered_id_slot;

	//! focus_link 装飾 (別 id のフォーカスに追従して絵を変える atlas_image 等)。
	//! フォーカス変化で hilite⇔normal が切り替わるが、 フォーカス要素本体とは
	//! 別の要素なので、 部分再描画のホストはフォーカス変化時にこれらの矩形も
	//! ダーティにする必要がある (でないと装飾の切替が消え残る)。
	std::vector<std::weak_ptr<cycfi::elements::element>> focus_link_elements;

	//! focus トリガ演出を有効にするか (JSON "input":{"focus_anim":false} で無効化)。
	//! hover_focus 併用時に focus と hover の多重発火を避けるための逃がし。 既定 true。
	bool focus_anim = true;

	//! i18n: 実行中の言語切替。 呼ぶと StringStore の現在言語を変え、 "text_id"
	//! を持つ全 label に set_text が走る (= EUI Phase 2 の動的更新)。 内部で
	//! StringStore を shared_ptr 捕捉しているので、 この closure を保持する限り
	//! 対応表 + subscribers は生存する。 "strings" 未定義でも非 null (no-op 相当)。
	//! 戻り値 = 言語が実際に変わったか (ホストの再描画要否判定用)。
	std::function<bool(const std::string&)> set_language;

	//! i18n: JSON top-level "lang" の初期言語 (無ければ空)。 ホストが
	//! overlay_session::language() で読む初期値。
	std::string lang;

	//! ホスト主導の変数書込。 VariableStore に書き、 同名を "text_var" で
	//! subscribe している label に set_text が走る (次 render で反映)。
	//! 内部で VariableStore を shared_ptr 捕捉しているので、 この closure を
	//! 保持する限り store + subscribers は生存する。 "vars" 未定義でも非 null。
	//! 戻り値 = 値が実際に変わったか (同値書込は false。 再描画要否判定用)。
	std::function<bool(const std::string&, const std::string&)> set_var;

	//! 部分再描画用: 変数変化で**見た目が変わった要素**の通知先を仕掛ける。
	//! ホストが `view::refresh(element)` を呼べば、 その要素の bounds だけを
	//! ダーティにできる (全面再描画を避けられる)。 通知は set_var だけでなく
	//! vars_on_focus の書込など全ての変数書込経路で発火する。
	//! 要素を特定できない subscriber (位置が変わる at_var、 複数要素が変わる
	//! もの) は通知されないので、 ホストは「1 つも通知が来なければ全面」と
	//! フォールバックすること。
	std::function<void(std::function<void(cycfi::elements::element&)>)>
		set_var_change_notifier;

	//! 変数ストアの現在値スナップショット (検証ツールの変数一覧用)。
	std::function<std::map<std::string, std::string>()> var_snapshot;

	//! 変数の変化 (名前, 新しい値) をホストへ流すフックの設置口。
	//! set_var だけでなく vars_on_focus 等あらゆる書込経路で発火する。
	std::function<void(std::function<void(const std::string&,
	                                      const std::string&)>)> set_var_watcher;

	//! 画面 JSON が参照している変数の一覧 (JSON から直接収集したもの)。
	//! ストアの現在値と違い、 一度も書かれていない変数もここには載る。
	var_ref_map var_refs;

	//! i18n: 画面が持つ言語コードの一覧 ("strings" の lang キーの和集合)。
	//! "strings" 未定義でも非 null (空リストを返す)。
	std::function<std::vector<std::string>()> languages;

	//! 配置アンカー (JSON top-level "align")。 0=左/上、 0.5=中央、 1=右/下。
	//! render_to_buffer がサーフェス内での描画矩形位置の決定に使う。 既定は
	//! 中央 (0.5, 0.5)。 "align": "top_left"/"top"/"left"/"center"/... で設定。
	float anchor_x = 0.5f;
	float anchor_y = 0.5f;

	//! 非中央アンカー時のサーフェス端からの余白 px (JSON top-level "margin")。
	int margin = 0;

	//! パーツ演出 (Phase A): "animate" 指定から生成した変換アニメ束縛。
	//! 各 binding は xform_state を proxy と共有し、 進捗 tween で移動/拡縮/回転を
	//! 駆動する。 ホスト (overlay_session) が animator に積んで毎フレーム tick する。
	//! Phase A は全て画面表示時 (enter) 発火。
	std::vector<anim_binding> animations;

	//! "input" ブロックの action バインド関連 ("bindings" / "se" /
	//! "initial_focus")。 overlay_session が組込デフォルト + input_defaults.jsonc
	//! とマージして view / セッションに適用する。
	input_action_config actions;
};

//! @brief input_defaults.jsonc (top-level が画面別 "input" ブロックと同形) の
//! 解析結果。 apply_settings は arrow_focus_nav / axis mode 等の view settings
//! 適用クロージャ (画面別 apply_input より先に呼ぶ = 画面別が勝つ)。
struct input_defaults_data
{
	bool ok = false;
	std::function<void(cycfi::elements::view&)> apply_settings;
	input_action_config actions;
};

//! @brief input_defaults.jsonc テキストの解析。 JSONC 可。 失敗時 ok=false。
input_defaults_data parse_input_defaults(const std::string& json_utf8);

//! @brief JSON 文字列を Elements ツリーに変換する。
//!        失敗時 root=nullptr。 詳細は SDL_Log に出力。
//! @param resource_base 画像など外部リソースの相対パスを解決するベース
//!                       ディレクトリ (末尾 '/' 推奨)。 空のときは CWD 解決。
//! @param drag_slot ドラッグ通知の receiver スロット。 中身は後から差し替えて
//!                   よい (overlay_session::set_drag_callback がそうする) ので、
//!                   build 済みの widget も最新の receiver を見る。
parsed_layout parse_from_string(const std::string& json_utf8,
                                event_callback cb,
                                const std::string& resource_base = {},
                                std::shared_ptr<drag_callback> drag_slot = {});

// app_manifest / parse_app_manifest() は modal.h (public) に定義済。

} // namespace elements_modal

#endif

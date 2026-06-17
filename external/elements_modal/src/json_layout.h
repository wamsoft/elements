//---------------------------------------------------------------------------
//!@file 内部: JSON → Elements 要素ツリー変換層
//
// 純粋 C++ + SDL_Log + std::function ベースの JSON → Elements ツリー変換。
// 公開 API ではない (実装内部用)。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_JSON_LAYOUT_H
#define ELEMENTS_MODAL_JSON_LAYOUT_H

#include "elements_modal/modal.h"   // value_t

#include <elements.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

namespace elements_modal {

// transition_spec / app_manifest は modal.h (public) に定義済。

// event_callback は modal.h で公開定義済 (std::string id ベース)。
// 内部 layout builder もこれと同じ型を使う。

//! @brief JSON 1 件のパース結果。
struct parsed_layout
{
	std::shared_ptr<cycfi::elements::element> root;
	int  width  = 400;  //!< top-level "size":[w,h] から取得 (run_modal は無視)
	int  height = 300;

	//! 初期キーボードフォーカスを当てる要素 (任意)。
	//! JSON で要素に "initial_focus": true を付けたものから単一選択。
	//! ホストは view.content() の後に view.focus(initial_focus) を呼ぶ。
	std::shared_ptr<cycfi::elements::element> initial_focus;

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

	//! i18n: 実行中の言語切替。 呼ぶと StringStore の現在言語を変え、 "text_id"
	//! を持つ全 label に set_text が走る (= EUI Phase 2 の動的更新)。 内部で
	//! StringStore を shared_ptr 捕捉しているので、 この closure を保持する限り
	//! 対応表 + subscribers は生存する。 "strings" 未定義でも非 null (no-op 相当)。
	std::function<void(const std::string&)> set_language;

	//! i18n: JSON top-level "lang" の初期言語 (無ければ空)。 ホストが
	//! overlay_session::language() で読む初期値。
	std::string lang;

	//! 配置アンカー (JSON top-level "align")。 0=左/上、 0.5=中央、 1=右/下。
	//! render_to_buffer がサーフェス内での描画矩形位置の決定に使う。 既定は
	//! 中央 (0.5, 0.5)。 "align": "top_left"/"top"/"left"/"center"/... で設定。
	float anchor_x = 0.5f;
	float anchor_y = 0.5f;

	//! 非中央アンカー時のサーフェス端からの余白 px (JSON top-level "margin")。
	int margin = 0;
};

//! @brief JSON 文字列を Elements ツリーに変換する。
//!        失敗時 root=nullptr。 詳細は SDL_Log に出力。
//! @param resource_base 画像など外部リソースの相対パスを解決するベース
//!                       ディレクトリ (末尾 '/' 推奨)。 空のときは CWD 解決。
parsed_layout parse_from_string(const std::string& json_utf8,
                                event_callback cb,
                                const std::string& resource_base = {});

// app_manifest / parse_app_manifest() は modal.h (public) に定義済。

} // namespace elements_modal

#endif

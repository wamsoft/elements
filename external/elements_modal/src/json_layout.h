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

	//! focus_poll が更新する「現在 focus されている id」スロット。 ホストは
	//! このポインタの中身を読めば現在の focused id を取れる。 focus poll を
	//! 一度も呼んでない / 何も focus されてない場合は空文字列。
	std::shared_ptr<std::string> focused_id_slot;
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

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
#include <memory>
#include <set>
#include <string>
#include <string_view>

namespace elements_modal {

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
};

//! @brief JSON 文字列を Elements ツリーに変換する。
//!        失敗時 root=nullptr。 詳細は SDL_Log に出力。
parsed_layout parse_from_string(const std::string& json_utf8,
                                event_callback cb);

} // namespace elements_modal

#endif

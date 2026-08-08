//---------------------------------------------------------------------------
//!@file navigator: 画面遷移スタックのドライバ (JSON 駆動ランナ用)
//
// overlay_session 1 つ = 1 画面。 複数画面を transitions / app_manifest に
// 従って push / pop / replace していく「遷移ロジック」は、 これまで各ホスト
// (SDL ランナ等) が自前で実装していた。 その純粋部分をライブラリへ集約する。
//
// このヘッダは SDL にも overlay_session にも依存しない。 navigator は
//   - 画面名スタックの管理
//   - transitions からの次手 (push/pop/replace/stay/exit) 解決
//   - 画面ごとの focus id 記憶
//   - 画面遷移をまたぐ表示言語の保持
// だけを担い、 overlay_session の生成 / ファイル読込 / 描画 / 入力転送は
// 引き続きホストの責務。 ホストは「session 完了 → advance() → 次画面名を
// ロード」という最小の連携でランナを組める。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_NAVIGATOR_H
#define ELEMENTS_MODAL_NAVIGATOR_H

#include <elements_modal/modal.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace elements_modal {

//---------------------------------------------------------------------------
// 純粋リゾルバ (層1) — transitions + 現在の文脈から次手を決める。
//---------------------------------------------------------------------------

//! @brief 遷移の種別。 transition_spec::target の語彙に対応する。
//!   push    — name の画面を積む
//!   pop     — 現画面を降ろす ("<back>")
//!   replace — 現画面を name にすげ替える ("<replace:name>")
//!   stay    — スタック不変で現画面を再 enter ("<stay>")
//!   exit    — アプリ終了 (空 target / "<exit>")
enum class nav_action { push, pop, replace, stay, exit };

//! @brief resolve_transition() / navigator::advance() の戻り値。
struct nav_step
{
	//! 解決された遷移種別。
	nav_action action = nav_action::stay;

	//! push / replace のときの遷移先画面名 (それ以外は空)。
	std::string name;

	//! 遷移エフェクト ("" = なし / "fade" / "universal")。 transition_spec から引き継ぐ。
	std::string effect;

	//! エフェクト所要時間 ms (0 = ホスト既定)。
	int duration_ms = 0;

	//! effect=="universal" の rule 画像パス / ぼかし幅。 transition_spec から引き継ぐ。
	std::string rule;
	int vague = 64;
};

//! @brief action を transitions で lookup し、 次手を返す純関数。
//!
//! transitions にキーがあればその target を種別へ正規化し、 effect / duration
//! も引き継ぐ。 未定義のときの既定挙動 (旧ランナ互換):
//!   - 空 action … entry なら exit / 子画面なら pop
//!   - 非空 action … entry なら stay (遷移先不明なので据置) / 子画面なら pop
//!
//! @param action      閉じトリガの action id (空文字 = Esc / B / 右クリック等)
//! @param transitions overlay_session::transitions() で得た辞書
//! @param is_entry    現画面がスタック起点 (深度 1) か
nav_step resolve_transition(
	const std::string& action,
	const std::map<std::string, transition_spec>& transitions,
	bool is_entry);

//---------------------------------------------------------------------------
// navigator (層2) — スタック + manifest + focus/言語メモリ。
//---------------------------------------------------------------------------

//! @brief 画面遷移スタックのドライバ。 overlay_session のライフサイクルや
//! 描画には関与しない。 名前管理・次手解決・focus 記憶・言語永続化のみ。
class navigator
{
public:
	//! @param manifest entry + screens レジストリ。 ok=false でも使えるが、
	//! その場合 screen_file() は常に空を返す (ホスト側フォールバックに委ねる)。
	explicit navigator(app_manifest manifest = {});

	// --- スタック ---

	//! @brief スタックを {entry} に初期化する。 起動時に 1 度呼ぶ。
	//! entry が空ならマニフェストの entry を使う (それも空なら何も積まない)。
	void reset_to(const std::string& entry = {});

	//! @brief 現画面名。 スタックが空なら空文字列。
	const std::string& current() const;

	//! @brief スタックが空 (= 終了状態) か。
	bool empty() const { return _stack.empty(); }

	//! @brief スタック深度。
	std::size_t depth() const { return _stack.size(); }

	//! @brief 現画面がスタック起点 (深度 1) か。
	bool at_entry() const { return _stack.size() == 1; }

	// --- 遷移適用 ---

	//! @brief session 完了時に呼ぶ。 action を transitions で解決し、 スタックを
	//! 更新して、 決まった次手を返す。 is_entry は内部 (深度) から判定するので
	//! ホストが渡す必要はない。
	//!
	//! 戻り値 nav_step:
	//!   - effect / duration_ms はホストが fade 等の演出に使える。
	//!   - action==exit のとき呼出後 empty()==true。
	//!   - スタック更新後の現画面は current() で取得する。
	nav_step advance(const std::string& action,
	                 const std::map<std::string, transition_spec>& transitions);

	// --- focus メモリ ---

	//! @brief 画面 close 時の focused id を記録する。 id が空なら記録しない。
	//! 同じ画面へ再入したとき focus_to_restore() で取り出せる。
	void remember_focus(const std::string& screen, const std::string& id);

	//! @brief 画面に記録された focus id を返す。 無ければ空文字列。
	//! ホストは start 後に空でなければ overlay_session::focus_by_id() で復元する。
	const std::string& focus_to_restore(const std::string& screen) const;

	// --- 表示言語 (i18n) ---

	//! @brief 画面遷移をまたいで保持する表示言語を設定する。 "lang:<code>"
	//! アクション等を拾ったホストが呼ぶ。
	void set_language(const std::string& lang) { _language = lang; }

	//! @brief 保持中の表示言語。 未設定なら空文字列 (= 各画面 JSON の "lang" 既定)。
	//! ホストは start 後に空でなければ overlay_session::set_language() で再適用する。
	const std::string& language() const { return _language; }

	// --- マニフェスト ---

	//! @brief 画面名 → JSON 相対パス。 manifest.ok かつ screens に登録があれば
	//! その相対パスを、 無ければ空文字列を返す (ホスト側フォールバックに委ねる)。
	//! 返すパスは app.jsonc 自身のディレクトリ起点 (ホストが結合する)。
	std::string screen_file(const std::string& name) const;

	//! @brief 保持しているマニフェスト。
	const app_manifest& manifest() const { return _manifest; }

private:
	app_manifest             _manifest;
	std::vector<std::string> _stack;
	std::map<std::string, std::string> _last_focus_per_screen;
	std::string              _language;
};

} // namespace elements_modal

#endif

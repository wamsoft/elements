//---------------------------------------------------------------------------
//!@file animator: tween で xform_state を毎フレーム駆動する小さなドライバ。
//
// JSON の "animate" 指定 1 件を anim_binding に落とし、 animator がまとめて
// tick する。 各 binding は進捗 tween (0→1) を 1 本持ち、 その進捗を対象
// チャンネル (移動/拡縮/回転/フェード) の値に線形マップして、 共有の
// xform_state に書き込む。 複数 binding が同じ xform_state を共有すれば、
// 移動 + 拡縮の同時掛け等が自然に合成される (互いに別フィールドを書くため)。
//
// 発火トリガ (anim_binding::trigger):
//   - enter  : 画面表示時に 1 回再生 (既定)。 start() で発火。
//   - focus  : 要素が focus を得た瞬間に前進再生、 失った瞬間に逆再生で復帰
//              (カーソル移動による選択強調 / 選択・非選択の切替向け)。
//   - select : 要素が決定 (button click 等) された瞬間に 1 回前進再生。
//   - exit   : 画面退場時に再生。 ホストが play_animation("exit") で明示発火する
//              (遷移との協調はホスト責務)。
// focus/select 束縛は対象要素の id を持ち、 その id への発火だけに反応する。
//
// レンダリングや時間取得には依存しない: ホスト (overlay_session) が経過 ms を
// 渡して tick する。 transform.h / tween.h にのみ依存。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_ANIMATOR_H
#define ELEMENTS_MODAL_ANIMATOR_H

#include "elements_modal/transform.h"
#include "elements_modal/tween.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace elements_modal {

//! @brief 1 つの演出 (進捗 tween → xform_state の 1 チャンネル) の束縛。
struct anim_binding
{
	//! 駆動対象のチャンネル。
	enum class channel { move, scale, rotate, fade };

	//! 発火タイミング。 hover はマウスオーバー、 change は値変化 (checkbox/slider 等)。
	enum class trigger { enter, focus, select, exit, hover, change };

	std::shared_ptr<xform_state> st;   //!< 書き込み先 (proxy と共有)。
	tween   prog;                      //!< 進捗ドライバ (from=0, to=1)。
	channel ch = channel::move;
	trigger trig = trigger::enter;     //!< 発火タイミング。
	std::string id;                    //!< 対象要素 id (focus/select の照合用)。

	// from → to。 move/scale は (x,y)、 rotate は (a→b 度) で ax/bx のみ使用、
	// fade は (a→b 透明度[0,1]) で ax/bx のみ使用。
	float ax = 0.0f, ay = 0.0f;
	float bx = 0.0f, by = 0.0f;

	// --- 再生状態 (animator が制御) ---
	bool active   = false;  //!< 現在 tick 対象か (focus/select は発火まで false)。
	bool reversed = false;  //!< true なら to→from へ逆再生 (focus 解除時)。

	//! @brief 進捗を読んで対象チャンネルへ反映する。
	void apply() const
	{
		if (!st) return;
		const float p = prog.value();           // 既に easing/台形で整形済の 0..1
		// 逆再生時は from/to を入れ替えて lerp (to から from へ戻る)。
		const float fx = reversed ? bx : ax;
		const float fy = reversed ? by : ay;
		const float tx = reversed ? ax : bx;
		const float ty = reversed ? ay : by;
		auto& s = *st;
		switch (ch) {
			case channel::move:
				s.tx = fx + (tx - fx) * p;
				s.ty = fy + (ty - fy) * p;
				break;
			case channel::scale:
				s.sx = fx + (tx - fx) * p;
				s.sy = fy + (ty - fy) * p;
				break;
			case channel::rotate:
				s.rot = deg_to_rad(fx + (tx - fx) * p);
				break;
			case channel::fade:
				s.opacity = fx + (tx - fx) * p;  // xform_base が global_alpha に乗算
				break;
		}
	}

	//! @brief 全 pass を終えたか (無限ループは常に false)。
	bool done() const { return prog.done(); }

	//! @brief ループ指定があるか (無限 or 複数 pass)。 解除時の扱い分けに使う。
	bool looping() const { return prog.iterations <= 0 || prog.iterations > 1; }
};

//! @brief "enter"/"focus"/"select"/"exit"/"hover"/"change" を trigger に (未知は enter)。
inline anim_binding::trigger trigger_from_string(std::string_view s)
{
	using t = anim_binding::trigger;
	if (s == "focus")  return t::focus;
	if (s == "select") return t::select;
	if (s == "exit")   return t::exit;
	if (s == "hover")  return t::hover;
	if (s == "change") return t::change;
	return t::enter;
}

//! @brief anim_binding をまとめて駆動する。
//!
//! enter 束縛は start() で一斉に前進再生。 focus/select/exit 束縛は対応する
//! 発火 (notify_focus / fire) があるまで idle (xform_state は既定の静止状態の
//! まま)。 ループ指定 (iterations<=0 や明滅) は tween 側が面倒を見る。
class animator
{
public:

	using trigger = anim_binding::trigger;

	void add(anim_binding b) { _bindings.push_back(std::move(b)); }

	bool empty() const { return _bindings.empty(); }
	std::size_t size() const { return _bindings.size(); }

	//! @brief enter 束縛を先頭へ巻き戻して前進再生開始。 他トリガは idle。
	//!
	//! focus/select/exit 束縛には触れない → xform_state は既定の静止値のまま
	//! なので、 各束縛の "from" は静止状態と一致させて書くこと (= 発火前の見た目)。
	void start()
	{
		for (auto& b : _bindings) {
			if (b.trig == trigger::enter) {
				b.reversed = false;
				b.prog.reset();
				b.active = true;
				b.apply();
			} else {
				b.active = false;
			}
		}
		_focus_id.clear();
		_hover_id.clear();
		_all_done = false;
	}

	//! @brief 経過 ms を進めて active な束縛を反映する。
	//! @return active な束縛を 1 つ以上進めた (= xform_state を書き換えた) か。
	//!         ホストの再描画要否 (ダーティ) 判定に使える。
	bool tick(float dt_ms)
	{
		if (_bindings.empty()) return false;
		bool all = true;
		bool ticked = false;
		for (auto& b : _bindings) {
			if (!b.active) continue;
			ticked = true;
			b.prog.tick(dt_ms);
			b.apply();
			if (b.prog.done()) b.active = false;  // 無限ループは done() にならない
			else               all = false;
		}
		_all_done = all;
		return ticked;
	}

	//! @brief 指定トリガ群を前進再生する。 id 非空なら一致 (または束縛 id 空) のみ。
	//!        ホスト主導の発火 (select / exit / 手動) に使う。
	void fire(trigger t, const std::string& id = {})
	{
		for (auto& b : _bindings) {
			if (b.trig != t) continue;
			if (!id.empty() && !b.id.empty() && b.id != id) continue;
			b.reversed = false;
			b.prog.reset();
			b.active = true;
			b.apply();
		}
	}

	//! @brief 現在 focus されている id をホストから受け取り、 focus 束縛を駆動する。
	//!        前回と変化したときだけ、 旧 id を逆再生 (復帰)、 新 id を前進再生する。
	void notify_focus(const std::string& id) { notify_inout(trigger::focus, _focus_id, id); }

	//! @brief 現在 hover されている id を受け取り、 hover 束縛を駆動する (focus と対称)。
	void notify_hover(const std::string& id) { notify_inout(trigger::hover, _hover_id, id); }

	//! @brief 全 active 束縛が完了したか (idle 含む。 無限ループ active なら false)。
	bool all_done() const { return _all_done; }

	//! @brief 指定トリガの束縛数。 0 ならそのトリガの演出は無い。
	std::size_t count(trigger t) const
	{
		std::size_t n = 0;
		for (auto& b : _bindings) if (b.trig == t) ++n;
		return n;
	}

	//! @brief 指定トリガの束縛で、 まだ再生中 (active) のものがあるか。
	//!        exit 演出の完了待ち (退場×遷移の協調) に使う。 enter の無限ループ等
	//!        他トリガには影響されない。
	bool active_any(trigger t) const
	{
		for (auto& b : _bindings) if (b.trig == t && b.active) return true;
		return false;
	}

private:

	// focus/hover の in/out 駆動 (共通)。 last は前回 id を保持するスロット。
	void notify_inout(trigger t, std::string& last, const std::string& id)
	{
		if (id == last) return;
		const std::string old = last;
		last = id;
		for (auto& b : _bindings) {
			if (b.trig != t) continue;
			if (!old.empty() && b.id == old) release_inout(b);
			if (!id.empty()  && b.id == id)  start_inout(b);
		}
	}

	// 取得: 前進再生を開始。
	static void start_inout(anim_binding& b)
	{
		b.reversed = false;
		b.prog.reset();
		b.active = true;
		b.apply();
	}

	// 喪失: 単発なら逆再生で静止へ、 ループ束縛は即座に静止 (from) へ戻す。
	static void release_inout(anim_binding& b)
	{
		b.prog.reset();
		if (b.looping()) {
			b.reversed = false;
			b.active = false;
			b.apply();          // p=0 → from (静止) を書いて停止
		} else {
			b.reversed = true;  // to→from へ逆再生
			b.active = true;
			b.apply();
		}
	}

	std::vector<anim_binding> _bindings;
	std::string _focus_id;   //!< 直近 notify_focus で受け取った focused id。
	std::string _hover_id;   //!< 直近 notify_hover で受け取った hovered id。
	bool _all_done = false;
};

} // namespace elements_modal

#endif

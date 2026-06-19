//---------------------------------------------------------------------------
//!@file animator: tween で xform_state を毎フレーム駆動する小さなドライバ。
//
// JSON の "animate" 指定 1 件を anim_binding に落とし、 animator がまとめて
// tick する。 各 binding は進捗 tween (0→1) を 1 本持ち、 その進捗を対象
// チャンネル (移動/拡縮/回転/フェード) の値に線形マップして、 共有の
// xform_state に書き込む。 複数 binding が同じ xform_state を共有すれば、
// 移動 + 拡縮の同時掛け等が自然に合成される (互いに別フィールドを書くため)。
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
#include <vector>

namespace elements_modal {

//! @brief 1 つの演出 (進捗 tween → xform_state の 1 チャンネル) の束縛。
struct anim_binding
{
	//! 駆動対象のチャンネル。
	enum class channel { move, scale, rotate, fade };

	std::shared_ptr<xform_state> st;   //!< 書き込み先 (proxy と共有)。
	tween   prog;                      //!< 進捗ドライバ (from=0, to=1)。
	channel ch = channel::move;

	// from → to。 move/scale は (x,y)、 rotate は (a→b 度) で ax/bx のみ使用、
	// fade は (a→b 透明度[0,1]) で ax/bx のみ使用。
	float ax = 0.0f, ay = 0.0f;
	float bx = 0.0f, by = 0.0f;

	//! @brief 進捗を読んで対象チャンネルへ反映する。
	void apply() const
	{
		if (!st) return;
		const float p = prog.value();           // 既に easing/台形で整形済の 0..1
		auto& s = *st;
		switch (ch) {
			case channel::move:
				s.tx = ax + (bx - ax) * p;
				s.ty = ay + (by - ay) * p;
				break;
			case channel::scale:
				s.sx = ax + (bx - ax) * p;
				s.sy = ay + (by - ay) * p;
				break;
			case channel::rotate:
				s.rot = deg_to_rad(ax + (bx - ax) * p);
				break;
			case channel::fade:
				s.opacity = ax + (bx - ax) * p;  // xform_base が global_alpha に乗算
				break;
		}
	}

	//! @brief 全 pass を終えたか (無限ループは常に false)。
	bool done() const { return prog.done(); }
};

//! @brief anim_binding をまとめて駆動する。
//!
//! Phase A の発火タイミングは「画面表示時 (enter)」固定: start() で全 binding を
//! 先頭へ巻き戻して初期値を適用し、 以後 tick(dt) で進める。 ループ指定
//! (iterations<=0 や明滅) は tween 側が面倒を見る。
class animator
{
public:

	void add(anim_binding b) { _bindings.push_back(std::move(b)); }

	bool empty() const { return _bindings.empty(); }
	std::size_t size() const { return _bindings.size(); }

	//! @brief 全 binding を先頭へ巻き戻し、 初期値 (進捗 0) を適用する。
	void start()
	{
		for (auto& b : _bindings) {
			b.prog.reset();
			b.apply();
		}
		_all_done = false;
	}

	//! @brief 経過 ms を進めて全 binding を反映する。
	void tick(float dt_ms)
	{
		if (_bindings.empty()) return;
		bool all = true;
		for (auto& b : _bindings) {
			b.prog.tick(dt_ms);
			b.apply();
			if (!b.done()) all = false;
		}
		_all_done = all;
	}

	//! @brief 全 binding が完了したか (無限ループが 1 つでもあれば false)。
	bool all_done() const { return _all_done; }

private:
	std::vector<anim_binding> _bindings;
	bool _all_done = false;
};

} // namespace elements_modal

#endif

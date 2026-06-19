//---------------------------------------------------------------------------
//!@file transform: パーツ単位の移動/拡縮/回転を「見た目だけ」適用する proxy。
//
// 対象要素 (subject) を包み、 描画時の canvas 行列にのみ平行移動・拡縮・回転を
// 掛ける proxy 要素。 レイアウト (limits) は subject の自然サイズのまま返すので、
// 周囲の要素は一切 reflow しない (= ゲーム的なスライド/ポップ/回転演出向けの
// オーバーレイ変換)。 ctx.bounds を変えない点が、 レイアウト自体を拡縮する
// lib の scale_element とは異なる。
//
// 変換量は xform_state を shared_ptr で外から共有して持つ。 アニメータ
// (tween 駆動) が毎フレーム xform_state を書き換え、 次の draw に反映される。
// レイアウトは常に自然座標で確定するので、 アニメ中に再レイアウトは起きない。
//
// 入力は prepare_subject(ctx, p) で逆変換 (device_to_user) するので、 変換後の
// 見た目位置をクリック/ホバーしても subject の自然座標のヒット領域に当たる。
//
// 透明度 (opacity) は canvas::global_alpha (グループ不透明度) に乗算して適用する
// (fill/stroke/text/image の alpha に効く非オフスクリーン方式)。
//
// 依存: cycfi::elements (proxy/canvas/context)。 SDL 非依存。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_TRANSFORM_H
#define ELEMENTS_MODAL_TRANSFORM_H

#include <elements/element/proxy.hpp>
#include <elements/support/canvas.hpp>
#include <elements/support/context.hpp>
#include <infra/support.hpp>

#include <cmath>
#include <memory>
#include <type_traits>
#include <utility>

namespace elements_modal {

//! @brief 変換量。 アニメータと proxy が shared_ptr で共有する可変状態。
//!
//! 適用順は「平行移動 → (ピボット基準で) 回転 → 拡縮」。 ピボットは subject の
//! 描画矩形に対する割合で、 (0,0)=左上, (0.5,0.5)=中央, (1,1)=右下。 拡縮/回転の
//! 起点切替 (左上 or 中心) はこの ox/oy で指定する。
struct xform_state
{
	float tx = 0.0f;    //!< 平行移動 X (px)
	float ty = 0.0f;    //!< 平行移動 Y (px)
	float sx = 1.0f;    //!< 拡縮 X
	float sy = 1.0f;    //!< 拡縮 Y
	float rot = 0.0f;   //!< 回転 (ラジアン)
	float ox = 0.5f;    //!< ピボット X (subject 矩形に対する割合)
	float oy = 0.5f;    //!< ピボット Y
	float opacity = 1.0f; //!< 透明度 [0,1] (canvas::global_alpha に乗算)

	//! @brief 変換なし (恒等) か。 true なら proxy は素通しでよい。
	bool identity() const
	{
		return tx == 0.0f && ty == 0.0f &&
		       sx == 1.0f && sy == 1.0f && rot == 0.0f;
	}
};

namespace ce = cycfi::elements;

//! @brief xform_state を canvas 変換として適用する proxy ベース。
class xform_base : public ce::proxy_base
{
public:

	explicit xform_base(std::shared_ptr<xform_state> st)
	 : _state(std::move(st))
	{
		if (!_state) _state = std::make_shared<xform_state>();
	}

	// limits は **オーバーライドしない** → proxy_base 既定 = subject の limits を
	// そのまま返す。 これにより周囲は reflow しない (非 reflow オーバーレイ)。

	void prepare_subject(ce::context& ctx) override
	{
		auto& st = *_state;
		auto& cnv = ctx.canvas;
		cnv.save();
		if (!st.identity()) {
			// ピボットを描画矩形から算出 (device 座標)。
			const float px = ctx.bounds.left + st.ox * (ctx.bounds.right  - ctx.bounds.left);
			const float py = ctx.bounds.top  + st.oy * (ctx.bounds.bottom - ctx.bounds.top);
			// 平行移動 → ピボット基準で回転 → 拡縮。
			// ctx.bounds は変えない: subject は自然座標で描き、 canvas 行列だけが
			// 見た目を動かす (レイアウト非依存のオーバーレイ変換)。
			if (st.tx != 0.0f || st.ty != 0.0f)
				cnv.translate({st.tx, st.ty});
			if (st.rot != 0.0f || st.sx != 1.0f || st.sy != 1.0f) {
				cnv.translate({px, py});
				if (st.rot != 0.0f) cnv.rotate(st.rot);
				if (st.sx != 1.0f || st.sy != 1.0f) cnv.scale({st.sx, st.sy});
				cnv.translate({-px, -py});
			}
		}
		// 透明度 (フェード): 親の global_alpha に乗算して合成 (Phase B)。
		// save() 済なので restore() で元に戻る。
		if (st.opacity < 1.0f)
			cnv.global_alpha(cnv.global_alpha() * st.opacity);
	}

	void prepare_subject(ce::context& ctx, ce::point& p) override
	{
		prepare_subject(ctx);
		// 入力点は現在の CTM の逆変換で subject の自然座標へ戻す。
		if (!_state->identity())
			p = ctx.canvas.device_to_user(p);
	}

	void restore_subject(ce::context& ctx) override
	{
		ctx.canvas.restore();
	}

	//! @brief 共有変換状態へのハンドル (アニメータが毎フレーム書き換える)。
	std::shared_ptr<xform_state> const& state() const { return _state; }

private:

	std::shared_ptr<xform_state> _state;
};

//! @brief subject を変換 proxy で包む。 変換量は共有 st 経由で外から駆動する。
//!
//! 使い方:
//! @code
//!   auto st = std::make_shared<elements_modal::xform_state>();
//!   auto el = cycfi::elements::share(elements_modal::xform(st, std::move(inner)));
//!   // st を保持しておき、 毎フレーム st->tx 等を書き換える。
//! @endcode
template <ce::concepts::Element Subject>
inline ce::proxy<std::remove_cvref_t<Subject>, xform_base>
xform(std::shared_ptr<xform_state> st, Subject&& subject)
{
	return {std::forward<Subject>(subject), std::move(st)};
}

//! @brief 度 → ラジアン (JSON は度指定が直感的なので変換ヘルパを置く)。
inline float deg_to_rad(float deg)
{
	return deg * (3.14159265358979f / 180.0f);
}

} // namespace elements_modal

#endif

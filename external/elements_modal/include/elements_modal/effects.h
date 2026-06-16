//---------------------------------------------------------------------------
//!@file effects: 画面遷移エフェクト用の小さなピクセルヘルパ。
//
// transition_spec::effect / nav_step::effect が "fade" のとき、 ホストは旧画面
// の最終フレームと新画面を t (0→1) で混色する。 その ARGB8888 クロスブレンドを
// ライブラリ側に集約しておく (各ホストでの再実装と微妙な channel 取り違いを防ぐ)。
//
// ヘッダオンリー (inline)。 SDL/Elements 非依存。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_EFFECTS_H
#define ELEMENTS_MODAL_EFFECTS_H

#include <cstddef>
#include <cstdint>

namespace elements_modal {

//! @brief ARGB8888 バッファ同士のチャンネル別線形補間 (クロスフェード)。
//!        out[i] = from[i] + (to[i] - from[i]) * t を 4 チャンネル独立に適用。
//!
//! fade 演出の典型的な使い方は、 from = 旧画面の最終フレーム、 to = 新画面の
//! 現フレームで、 t = elapsed / duration を 0→1 に進める。 t=0 で from、 t=1 で
//! to。 `out` は `to` と同一バッファでよい (in-place 更新)。
//!
//! @param from   開始フレーム (ARGB8888 / LE = BGRA byte order)
//! @param to     終了フレーム (同フォーマット、 同じ画素数)
//! @param t      補間係数。 [0,1] にクランプされる。
//! @param out    書き込み先 (`to` とエイリアス可)。 from/to と同じ画素数。
//! @param count  画素数 (= width * height)。
inline void blend_argb8888(const std::uint32_t* from,
                           const std::uint32_t* to,
                           float t,
                           std::uint32_t* out,
                           std::size_t count)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	for (std::size_t i = 0; i < count; ++i) {
		const std::uint32_t a = from[i];
		const std::uint32_t b = to[i];
		const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF;
		const int ab =  a        & 0xFF, aa = (a >> 24) & 0xFF;
		const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF;
		const int bb =  b        & 0xFF, ba = (b >> 24) & 0xFF;
		const int r  = ar + int((br - ar) * t);
		const int g  = ag + int((bg - ag) * t);
		const int bl = ab + int((bb - ab) * t);
		const int al = aa + int((ba - aa) * t);
		out[i] = (std::uint32_t(al) << 24) | (std::uint32_t(r) << 16) |
		         (std::uint32_t(g)  <<  8) |  std::uint32_t(bl);
	}
}

} // namespace elements_modal

#endif

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

//! @brief rule 画像によるユニバーサルトランジション (ARGB8888、 4ch)。
//!
//! 各画素の切替タイミングを rule (8bit グレースケール、 from/to と同画素数) が
//! 決める: rule 値が小さい画素ほど早く to 側へ切り替わる。 vague が境界の
//! ぼかし幅 (rule 値スケール、 1-255)。 phase01 は進行度 0→1 で、 内部で
//! 0 → 255+vague の閾値スイープに展開する (旧来のユニバーサルトランジションと
//! 同じ意味論)。 phase01=0 で全画素 from、 phase01=1 で全画素 to。
//!
//! `out` は `to` と同一バッファでよい (in-place 更新)。
//!
//! @param from    開始フレーム (ARGB8888 / LE = BGRA byte order)
//! @param to      終了フレーム (同フォーマット、 同じ画素数)
//! @param rule    8bit rule バッファ (同じ画素数)
//! @param phase01 進行度。 [0,1] にクランプされる。
//! @param vague   境界ぼかし幅 (rule 値スケール)。 [1,255] にクランプされる。
//! @param out     書き込み先 (`to` とエイリアス可)。
//! @param count   画素数 (= width * height)。
inline void blend_universal_argb8888(const std::uint32_t* from,
                                     const std::uint32_t* to,
                                     const std::uint8_t* rule,
                                     float phase01,
                                     int vague,
                                     std::uint32_t* out,
                                     std::size_t count)
{
	if (phase01 < 0.0f) phase01 = 0.0f;
	if (phase01 > 1.0f) phase01 = 1.0f;
	if (vague < 1)   vague = 1;
	if (vague > 255) vague = 255;

	// rule 値 → to 側の混合率 (0-256) の 256 段テーブル。 phasemax が
	// 0 → 255+vague へスイープし、 rule < phasemax-vague は to 確定 (256)、
	// rule >= phasemax は from 確定 (0)、 間は線形にぼかす。
	const int phasemax = static_cast<int>(phase01 * (255 + vague) + 0.5f);
	const int phasemin = phasemax - vague;
	std::uint32_t table[256];
	for (int i = 0; i < 256; ++i) {
		if (i < phasemin)        table[i] = 256;
		else if (i >= phasemax)  table[i] = 0;
		else {
			int tmp = (phasemax - i) * 256 / vague;
			if (tmp < 0)   tmp = 0;
			if (tmp > 256) tmp = 256;
			table[i] = static_cast<std::uint32_t>(tmp);
		}
	}

	// 2 チャンネルずつの固定小数点 lerp (A,G / R,B ペア)。 t は 0-256 なので
	// t=256 で正確に to へ到達する。
	for (std::size_t i = 0; i < count; ++i) {
		const std::uint32_t t = table[rule[i]];
		if (t == 0)        { out[i] = from[i]; continue; }
		if (t == 256)      { out[i] = to[i];   continue; }
		const std::uint32_t a = from[i];
		const std::uint32_t b = to[i];
		const std::uint32_t a_rb = a & 0x00ff00ffu;
		const std::uint32_t b_rb = b & 0x00ff00ffu;
		const std::uint32_t a_ag = (a >> 8) & 0x00ff00ffu;
		const std::uint32_t b_ag = (b >> 8) & 0x00ff00ffu;
		const std::uint32_t rb = (a_rb + (((b_rb - a_rb) * t) >> 8)) & 0x00ff00ffu;
		const std::uint32_t ag = (a_ag + (((b_ag - a_ag) * t) >> 8)) & 0x00ff00ffu;
		out[i] = rb | (ag << 8);
	}
}

} // namespace elements_modal

#endif

//---------------------------------------------------------------------------
//!@file tween: 時間ベースの補間 (イージング/台形速度プロファイル) ユーティリティ。
//
// パーツ移動・拡縮・回転・フェード等の演出を駆動するための「値を時間で
// 補間する」小さな計算層。 SDL / Elements / canvas には一切依存しない純粋な
// 数値計算なので、 ヘッドレスで数値検証できる (effects.h / navigator.h と同じ
// 非依存ポリシ)。
//
// 2 つの整形 (shaping) をサポートする:
//   1. 一般的なイージング関数 (easing enum) — linear / quad / cubic / sine /
//      quart / expo / back の in/out/in-out。
//   2. 台形速度プロファイル (trapezoid) — 「最高速度に達するまでのフレーム数
//      (加速)」「最高速度から停止するまでのフレーム数 (減速)」を別々に指定する
//      要望仕様向け。 加速・巡航・減速の 3 区間からなる等速台形。
//
// ループ (iterations) と往復 (yoyo) も持つので、 フェードの明滅/点滅にも使える
// (yoyo + iterations=明滅回数*2)。
//
// ヘッダオンリー (inline)。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_TWEEN_H
#define ELEMENTS_MODAL_TWEEN_H

#include <cmath>
#include <string>
#include <string_view>

namespace elements_modal {

//---------------------------------------------------------------------------
// イージング関数
//---------------------------------------------------------------------------

//! @brief 一般的なイージングカーブの種類。 in=加速のみ / out=減速のみ /
//!        in_out=両方。 入力 t は [0,1]、 出力も [0,1]。
enum class easing {
	linear,
	in_quad,   out_quad,   in_out_quad,
	in_cubic,  out_cubic,  in_out_cubic,
	in_sine,   out_sine,   in_out_sine,
	in_quart,  out_quart,  in_out_quart,
	in_expo,   out_expo,   in_out_expo,
	in_back,   out_back,   in_out_back,
};

namespace detail {

inline float clamp01(float t)
{
	if (t < 0.0f) return 0.0f;
	if (t > 1.0f) return 1.0f;
	return t;
}

} // namespace detail

//! @brief イージング関数を 1 つ評価する。 t は内部で [0,1] にクランプ。
//! @return 整形後の進捗 [0,1] (linear なら t そのまま)。
inline float ease(easing e, float t)
{
	t = detail::clamp01(t);
	const float t2 = t * t;
	const float t3 = t2 * t;
	const float t4 = t2 * t2;
	switch (e) {
		case easing::linear:        return t;

		case easing::in_quad:       return t2;
		case easing::out_quad:      return 1.0f - (1.0f - t) * (1.0f - t);
		case easing::in_out_quad:   return t < 0.5f
		                                 ? 2.0f * t2
		                                 : 1.0f - 0.5f * (2.0f - 2.0f * t) * (2.0f - 2.0f * t);

		case easing::in_cubic:      return t3;
		case easing::out_cubic: {
			const float u = 1.0f - t;
			return 1.0f - u * u * u;
		}
		case easing::in_out_cubic:  return t < 0.5f
		                                 ? 4.0f * t3
		                                 : 1.0f - 0.5f * std::pow(-2.0f * t + 2.0f, 3.0f);

		case easing::in_sine:       return 1.0f - std::cos((t * 3.14159265358979f) * 0.5f);
		case easing::out_sine:      return std::sin((t * 3.14159265358979f) * 0.5f);
		case easing::in_out_sine:   return -0.5f * (std::cos(3.14159265358979f * t) - 1.0f);

		case easing::in_quart:      return t4;
		case easing::out_quart: {
			const float u = 1.0f - t;
			return 1.0f - u * u * u * u;
		}
		case easing::in_out_quart:  return t < 0.5f
		                                 ? 8.0f * t4
		                                 : 1.0f - 0.5f * std::pow(-2.0f * t + 2.0f, 4.0f);

		case easing::in_expo:       return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
		case easing::out_expo:      return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
		case easing::in_out_expo:
			if (t <= 0.0f) return 0.0f;
			if (t >= 1.0f) return 1.0f;
			return t < 0.5f
			     ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
			     : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);

		case easing::in_back: {
			const float c1 = 1.70158f, c3 = c1 + 1.0f;
			return c3 * t3 - c1 * t2;
		}
		case easing::out_back: {
			const float c1 = 1.70158f, c3 = c1 + 1.0f;
			const float u = t - 1.0f;
			return 1.0f + c3 * u * u * u + c1 * u * u;
		}
		case easing::in_out_back: {
			const float c2 = 1.70158f * 1.525f;
			if (t < 0.5f) {
				const float u = 2.0f * t;
				return 0.5f * (u * u * ((c2 + 1.0f) * u - c2));
			} else {
				const float u = 2.0f * t - 2.0f;
				return 0.5f * (u * u * ((c2 + 1.0f) * u + c2) + 2.0f);
			}
		}
	}
	return t;
}

//! @brief JSON 等の文字列からイージング種別を引く。 未知なら fallback。
//!        受理例: "linear", "ease_in", "ease_out", "ease_in_out" (= quad 既定),
//!        "in_cubic", "out_sine", "in_out_back" ...
inline easing easing_from_string(std::string_view s, easing fallback = easing::linear)
{
	auto eq = [&](const char* lit) { return s == lit; };
	if (eq("linear"))                                   return easing::linear;
	// 省略形 (種別省略時は quad)
	if (eq("ease_in")  || eq("in"))                     return easing::in_quad;
	if (eq("ease_out") || eq("out"))                    return easing::out_quad;
	if (eq("ease_in_out") || eq("in_out") || eq("ease")) return easing::in_out_quad;

	if (eq("in_quad"))      return easing::in_quad;
	if (eq("out_quad"))     return easing::out_quad;
	if (eq("in_out_quad"))  return easing::in_out_quad;
	if (eq("in_cubic"))     return easing::in_cubic;
	if (eq("out_cubic"))    return easing::out_cubic;
	if (eq("in_out_cubic")) return easing::in_out_cubic;
	if (eq("in_sine"))      return easing::in_sine;
	if (eq("out_sine"))     return easing::out_sine;
	if (eq("in_out_sine"))  return easing::in_out_sine;
	if (eq("in_quart"))     return easing::in_quart;
	if (eq("out_quart"))    return easing::out_quart;
	if (eq("in_out_quart")) return easing::in_out_quart;
	if (eq("in_expo"))      return easing::in_expo;
	if (eq("out_expo"))     return easing::out_expo;
	if (eq("in_out_expo"))  return easing::in_out_expo;
	if (eq("in_back"))      return easing::in_back;
	if (eq("out_back"))     return easing::out_back;
	if (eq("in_out_back"))  return easing::in_out_back;
	return fallback;
}

//---------------------------------------------------------------------------
// 台形速度プロファイル
//---------------------------------------------------------------------------

//! @brief 台形 (加速→等速→減速) の速度プロファイルで進捗を整形する。
//!
//! 速度 v(t) が [0, accel] で 0→vmax へ線形に立ち上がり、 [accel, 1-decel] で
//! vmax 一定、 [1-decel, 1] で vmax→0 へ線形に落ちる、 という台形の面積 (=移動量)
//! を正規化して累積位置を返す。 移動要望の「加速フレーム数」「減速フレーム数」を
//! 別指定する仕様に対応する (フレーム数 → 全体に対する割合に直して渡す)。
//!
//! accel=decel=0 のとき linear と一致する。 accel+decel が 1 を超える場合は
//! 比率を保ったまま 1 に収まるよう縮める。
//!
//! @param t      正規化時間 [0,1] (内部でクランプ)。
//! @param accel  加速区間の長さ (全体に対する割合, 0..1)。
//! @param decel  減速区間の長さ (全体に対する割合, 0..1)。
//! @return 累積位置 [0,1]。 t=0 で 0、 t=1 で 1。
inline float trapezoid(float t, float accel, float decel)
{
	t = detail::clamp01(t);
	if (accel < 0.0f) accel = 0.0f;
	if (decel < 0.0f) decel = 0.0f;
	// 区間が全体を超えるときは比率を保って縮める。
	if (accel + decel > 1.0f) {
		const float s = 1.0f / (accel + decel);
		accel *= s;
		decel *= s;
	}
	// vmax = 1 / (台形の面積)。 面積 = 1 - accel/2 - decel/2。
	const float denom = 1.0f - 0.5f * accel - 0.5f * decel;
	if (denom <= 0.0f) return t;          // 退化 (accel=decel=1) → linear 扱い
	const float vmax = 1.0f / denom;

	if (accel > 0.0f && t < accel) {
		// 加速区間: 位置 = vmax * t^2 / (2*accel)
		return vmax * t * t / (2.0f * accel);
	}
	if (decel > 0.0f && t > 1.0f - decel) {
		// 減速区間: 終端からの残距離 = vmax * (1-t)^2 / (2*decel)
		const float r = 1.0f - t;
		return 1.0f - vmax * r * r / (2.0f * decel);
	}
	// 等速区間: 位置 = vmax * (t - accel/2)
	return vmax * (t - 0.5f * accel);
}

//---------------------------------------------------------------------------
// frames ⇄ ms 変換 (要望はフレーム数指定が基本)
//---------------------------------------------------------------------------

//! @brief フレーム数を ミリ秒に変換する (既定 60fps)。 tween は ms ベースなので、
//!        フレーム数指定の仕様を受けるときに使う。
inline float frames_to_ms(float frames, float fps = 60.0f)
{
	if (fps <= 0.0f) return 0.0f;
	return frames * 1000.0f / fps;
}

//---------------------------------------------------------------------------
// tween: 1 つのスカラ値を時間で補間する再生器
//---------------------------------------------------------------------------

//! @brief from→to をスカラ補間する時間駆動の tween。
//!
//! 整形は 2 モード: use_trapezoid=false なら easing 関数、 true なら台形
//! プロファイル (accel_frac/decel_frac)。 ループ (iterations) と往復 (yoyo) を
//! 持つ。 1 回の「一方向の再生 (pass)」が duration_ms。 yoyo のとき奇数 pass は
//! to→from へ逆走する (明滅/往復に使える)。
//!
//! 典型: tick(dt_ms) を毎フレーム呼び、 value() を読んで transform/opacity に
//! 反映する。 done() が true になったら演出終了。
struct tween
{
	float from = 0.0f;
	float to   = 0.0f;
	float duration_ms = 0.0f;        //!< 1 pass の長さ (ms)。 0 なら即時に to。

	// --- 整形 ---
	bool   use_trapezoid = false;
	easing ez = easing::linear;      //!< use_trapezoid=false のとき有効。
	float  accel_frac = 0.0f;        //!< use_trapezoid=true のとき有効 (0..1)。
	float  decel_frac = 0.0f;

	// --- ループ ---
	int  iterations = 1;             //!< 一方向 pass の総数。 <=0 で無限ループ。
	bool yoyo = false;               //!< 奇数 pass を to→from に逆走させる。

	// --- 実行時状態 ---
	float elapsed_ms = 0.0f;

	//! @brief 正規化時間 [0,1] を整形する (easing か台形)。
	float shape(float t) const
	{
		return use_trapezoid ? trapezoid(t, accel_frac, decel_frac) : ease(ez, t);
	}

	//! @brief 有限ループか (false なら無限)。
	bool finite() const { return iterations > 0; }

	//! @brief 全 pass を終えて停止したか。 無限ループは常に false。
	bool done() const
	{
		if (!finite()) return false;
		if (duration_ms <= 0.0f) return true;
		return elapsed_ms >= duration_ms * static_cast<float>(iterations);
	}

	//! @brief 現在の補間値。
	float value() const
	{
		if (duration_ms <= 0.0f) return to;     // 即時
		const float total = duration_ms * static_cast<float>(iterations);

		float e = elapsed_ms;
		if (finite() && e >= total) e = total;   // 終端でクランプ

		// 何 pass 目か / pass 内ローカル時間
		int   pass = static_cast<int>(e / duration_ms);
		float local = (e - static_cast<float>(pass) * duration_ms) / duration_ms;
		if (finite() && pass >= iterations) {     // 終端: 最後の pass の末尾に固定
			pass  = iterations - 1;
			local = 1.0f;
		}
		float s = shape(detail::clamp01(local));
		// yoyo: 奇数 pass は逆走 (to→from)。
		if (yoyo && (pass & 1)) s = 1.0f - s;
		return from + (to - from) * s;
	}

	//! @brief 経過時間を進める。
	void tick(float dt_ms)
	{
		if (dt_ms <= 0.0f) return;
		elapsed_ms += dt_ms;
		if (finite()) {
			const float total = duration_ms * static_cast<float>(iterations);
			if (elapsed_ms > total) elapsed_ms = total;
		}
	}

	//! @brief 先頭へ巻き戻す (再再生用)。
	void reset() { elapsed_ms = 0.0f; }
};

} // namespace elements_modal

#endif

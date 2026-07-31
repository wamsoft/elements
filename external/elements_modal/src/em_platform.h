//---------------------------------------------------------------------------
//!@file 内部: host 非依存プラットフォーム shim
//
// overlay 経路 (overlay_session / navigator / json_layout / runtime) を SDL 非依存
// にするための最小ユーティリティ。 SDL_GetTicks / SDL_Log の置き換え。
// これにより elements_modal の overlay サブセットは SDL3 をリンクせずに (WIN32
// host 等でも) ビルドできる。 独立ウィンドウ modal (modal.cpp) は引き続き SDL host
// 専用。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_EM_PLATFORM_H
#define ELEMENTS_MODAL_EM_PLATFORM_H

#include <cstdint>

namespace elements_modal {

//! @brief 単調増加のミリ秒 (steady_clock 基準)。 SDL_GetTicks() の置き換え。
//!        アニメーションの経過時間計算に使う (絶対原点は不定で差分のみ意味を持つ)。
std::uint64_t em_now_ms();

//! @brief printf 形式の診断ログ。 SDL_Log() の置き換え。 stderr へ
//!        "elements_modal" 接頭辞 + 改行付きで出力する。
void em_logf(const char* fmt, ...);

} // namespace elements_modal

#endif // ELEMENTS_MODAL_EM_PLATFORM_H

//---------------------------------------------------------------------------
//!@file 内部: SDL keycode → Elements key_code マッピング
//
// run_modal と overlay_session で共通。 ダイアログ navigation で必要な
// Tab / Enter / Esc / 矢印 / Backspace / Delete などをカバー。
// Letters / digits / 記号は Elements 側の key_code 値が ASCII と一致するので
// そのまま通す (SDL_Keycode も同じく ASCII 互換)。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_KEY_MAP_H
#define ELEMENTS_MODAL_KEY_MAP_H

#include <SDL3/SDL.h>
#include <elements.hpp>

namespace elements_modal {

//! @brief SDL ゲームパッドボタンを Elements key_code に変換 (ダイアログナビ用)。
//!        - D-pad → 矢印キー
//!        - South (A: confirm) → Enter
//!        - East  (B: cancel)  → Escape
//!        - LB / RB → Tab (LB は shift modifier 付きで shift+tab の意で扱う想定)
//!        その他は unknown。
inline cycfi::elements::key_code sdl_gamepad_button_to_ce(int sdl_button)
{
	using ce_key = cycfi::elements::key_code;
	switch (sdl_button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:          return ce_key::enter;
		case SDL_GAMEPAD_BUTTON_EAST:           return ce_key::escape;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:        return ce_key::up;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return ce_key::down;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return ce_key::left;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return ce_key::right;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return ce_key::tab;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ce_key::tab;
		default:                                return ce_key::unknown;
	}
}

//! @brief LB ボタン (Tab の shift+Tab 相当) かどうか。 修飾キー判定用。
inline bool sdl_gamepad_button_needs_shift(int sdl_button)
{
	return sdl_button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
}

inline cycfi::elements::key_code sdl_key_to_ce(int sdl_key)
{
	using ce_key = cycfi::elements::key_code;
	switch (sdl_key) {
		case SDLK_ESCAPE:    return ce_key::escape;
		case SDLK_RETURN:    return ce_key::enter;
		case SDLK_KP_ENTER:  return ce_key::enter;
		case SDLK_TAB:       return ce_key::tab;
		case SDLK_BACKSPACE: return ce_key::backspace;
		case SDLK_INSERT:    return ce_key::insert;
		case SDLK_DELETE:    return ce_key::_delete;
		case SDLK_LEFT:      return ce_key::left;
		case SDLK_RIGHT:     return ce_key::right;
		case SDLK_UP:        return ce_key::up;
		case SDLK_DOWN:      return ce_key::down;
		case SDLK_PAGEUP:    return ce_key::page_up;
		case SDLK_PAGEDOWN:  return ce_key::page_down;
		case SDLK_HOME:      return ce_key::home;
		case SDLK_END:       return ce_key::end;
		case SDLK_SPACE:     return ce_key::space;
		default: break;
	}
	// 残りは ASCII 範囲のものをそのまま通す (Elements の key_code は ASCII と
	// 一致するので、 文字キーや数字は ce_key に cast すれば動く)。 ただし
	// SDL は小文字 letter (a-z = 0x61-0x7A) を返すが Elements は大文字 (A-Z =
	// 0x41-0x5A) を使うので、 letter のみ大文字化。
	if (sdl_key >= 'a' && sdl_key <= 'z') {
		return static_cast<ce_key>(sdl_key - ('a' - 'A'));
	}
	// 数字 / 記号 / Function keys (F1-F12 等) — 数値範囲が一致するものはそのまま
	// (SDL の F1 は SDLK_F1=0x4000003A で Elements の f1=290 と不一致なので
	// 個別 mapping が必要、 現状は dialog 操作の優先度低なので未対応)。
	if (sdl_key >= 32 && sdl_key < 127) {
		return static_cast<ce_key>(sdl_key);
	}
	return ce_key::unknown;
}

} // namespace elements_modal

#endif

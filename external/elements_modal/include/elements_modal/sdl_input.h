//---------------------------------------------------------------------------
//!@file SDL → cycfi 入力マッピング (SDL host アダプタ)
//
// overlay_session::on_* は host 非依存な cycfi の入力型 (mouse_button /
// key_code / pad_button / pad_axis) を受ける。 このヘッダは SDL イベントの
// 生値をそれらへ変換する inline ヘルパを提供する。 SDL を使うホスト
// (elements_modal の SDL サンプル、 krkrz の SDL DrawDevice アダプタ等) が
// include して使う。 SDL を引かないホスト (win32 等) は win32_input.h を使う。
//
// これは「native → 中立 (cycfi)」マッピングをホスト側に置く設計の SDL 実装。
// ライブラリ本体 (overlay_session) は SDL に一切依存しない。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_SDL_INPUT_H
#define ELEMENTS_MODAL_SDL_INPUT_H

#include <SDL3/SDL.h>
#include <elements/base_view.hpp>       // mouse_button / key_code / mod_*
#include <elements/element/gamepad.hpp> // pad_button / pad_axis

namespace elements_modal { namespace sdl_input {

//! SDL_BUTTON_* → cycfi mouse_button::what
inline cycfi::elements::mouse_button::what mouse_button(int sdl_btn)
{
	using w = cycfi::elements::mouse_button;
	switch (sdl_btn) {
		case SDL_BUTTON_MIDDLE: return w::middle;
		case SDL_BUTTON_RIGHT:  return w::right;
		default:                return w::left;
	}
}

//! SDL_KMOD_* の OR → cycfi mod_* の OR
inline int mods(int sdl_mod)
{
	namespace ce = cycfi::elements;
	int m = 0;
	if (sdl_mod & SDL_KMOD_SHIFT) m |= ce::mod_shift;
	if (sdl_mod & SDL_KMOD_CTRL)  m |= ce::mod_control;
	if (sdl_mod & SDL_KMOD_ALT)   m |= ce::mod_alt;
	return m;
}

//! SDL_Keycode → cycfi key_code (ダイアログ navigation で使う分をカバー)。
//! Letters/digits/記号は cycfi key_code が ASCII 準拠なのでそのまま通す
//! (SDL は小文字 letter を返すので大文字化)。
inline cycfi::elements::key_code key(int sdl_key)
{
	using k = cycfi::elements::key_code;
	switch (sdl_key) {
		case SDLK_ESCAPE:    return k::escape;
		case SDLK_RETURN:    return k::enter;
		case SDLK_KP_ENTER:  return k::enter;
		case SDLK_TAB:       return k::tab;
		case SDLK_BACKSPACE: return k::backspace;
		case SDLK_INSERT:    return k::insert;
		case SDLK_DELETE:    return k::_delete;
		case SDLK_LEFT:      return k::left;
		case SDLK_RIGHT:     return k::right;
		case SDLK_UP:        return k::up;
		case SDLK_DOWN:      return k::down;
		case SDLK_PAGEUP:    return k::page_up;
		case SDLK_PAGEDOWN:  return k::page_down;
		case SDLK_HOME:      return k::home;
		case SDLK_END:       return k::end;
		case SDLK_SPACE:     return k::space;
		default: break;
	}
	if (sdl_key >= 'a' && sdl_key <= 'z')
		return static_cast<k>(sdl_key - ('a' - 'A'));
	if (sdl_key >= 32 && sdl_key < 127)
		return static_cast<k>(sdl_key);
	return k::unknown;
}

//! SDL_GAMEPAD_BUTTON_* → cycfi pad_button
inline cycfi::elements::pad_button pad_button(int sdl_button)
{
	using b = cycfi::elements::pad_button;
	switch (sdl_button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:          return b::a;
		case SDL_GAMEPAD_BUTTON_EAST:           return b::b;
		case SDL_GAMEPAD_BUTTON_WEST:           return b::x;
		case SDL_GAMEPAD_BUTTON_NORTH:          return b::y;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:        return b::dpad_up;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return b::dpad_down;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return b::dpad_left;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return b::dpad_right;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return b::lb;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return b::rb;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return b::l3;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return b::r3;
		case SDL_GAMEPAD_BUTTON_BACK:           return b::back;
		case SDL_GAMEPAD_BUTTON_START:          return b::start;
		case SDL_GAMEPAD_BUTTON_GUIDE:          return b::guide;
		default:                                return b::unknown;
	}
}

//! SDL_GAMEPAD_AXIS_* → cycfi pad_axis
inline cycfi::elements::pad_axis pad_axis(int sdl_axis)
{
	using a = cycfi::elements::pad_axis;
	switch (sdl_axis) {
		case SDL_GAMEPAD_AXIS_LEFTX:         return a::left_x;
		case SDL_GAMEPAD_AXIS_LEFTY:         return a::left_y;
		case SDL_GAMEPAD_AXIS_RIGHTX:        return a::right_x;
		case SDL_GAMEPAD_AXIS_RIGHTY:        return a::right_y;
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return a::lt;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return a::rt;
		default:                             return a::unknown;
	}
}

//! SDL の int16 軸生値 (-32768..32767) → 正規化 [-1, +1]
inline float axis_value(int raw_value)
{
	float v = static_cast<float>(raw_value) / 32767.0f;
	if (v < -1.0f) v = -1.0f;
	if (v >  1.0f) v =  1.0f;
	return v;
}

}} // namespace elements_modal::sdl_input

#endif // ELEMENTS_MODAL_SDL_INPUT_H

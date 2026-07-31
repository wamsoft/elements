//---------------------------------------------------------------------------
//!@file Win32 → cycfi 入力マッピング (Win32 host アダプタ)
//
// overlay_session::on_* は host 非依存な cycfi の入力型を受ける。 このヘッダは
// Win32 の仮想キーコード (VK_*) / マウス / 修飾キー状態を cycfi の入力型へ変換
// する inline ヘルパを提供する。 Win32 ネイティブなホスト (krkrz の WINVER
// DrawDevice アダプタ、 Win32 サンプル等) が include して使う。 SDL を使う
// ホストは sdl_input.h を使う。
//
// sdl_input.h と対になる「native → 中立 (cycfi)」マッピングの Win32 実装。
// ライブラリ本体 (overlay_session) は Win32 にも SDL にも依存しない。
//
// 注: ゲームパッドは Win32 では XInput / Windows.Gaming.Input を使うが、 それらは
// このヘッダの対象外 (呼出側で pad_button/pad_axis へ変換して渡す)。 キーボードと
// マウスのみを扱う。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_WIN32_INPUT_H
#define ELEMENTS_MODAL_WIN32_INPUT_H

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <elements/base_view.hpp>   // mouse_button / key_code / mod_*

namespace elements_modal { namespace win32_input {

//! 現在の修飾キー状態 (GetKeyState) → cycfi mod_* の OR
inline int mods()
{
	namespace ce = cycfi::elements;
	int m = 0;
	if (GetKeyState(VK_SHIFT)   & 0x8000) m |= ce::mod_shift;
	if (GetKeyState(VK_CONTROL) & 0x8000) m |= ce::mod_control;
	if (GetKeyState(VK_MENU)    & 0x8000) m |= ce::mod_alt;
	return m;
}

//! Win32 仮想キーコード (VK_*) → cycfi key_code。 ダイアログ navigation で必要な
//! 分をカバー。 文字/数字は cycfi key_code が大文字 ASCII 準拠で、 Win32 の VK も
//! 英字 = 'A'-'Z' (0x41-0x5A) / 数字 = '0'-'9' (0x30-0x39) と一致するのでそのまま。
//! 記号や実際の文字入力は WM_CHAR → on_text_input で扱う (ここでは扱わない)。
inline cycfi::elements::key_code key(unsigned int vk)
{
	using k = cycfi::elements::key_code;
	switch (vk) {
		case VK_ESCAPE: return k::escape;
		case VK_RETURN: return k::enter;
		case VK_TAB:    return k::tab;
		case VK_BACK:   return k::backspace;
		case VK_INSERT: return k::insert;
		case VK_DELETE: return k::_delete;
		case VK_LEFT:   return k::left;
		case VK_RIGHT:  return k::right;
		case VK_UP:     return k::up;
		case VK_DOWN:   return k::down;
		case VK_PRIOR:  return k::page_up;
		case VK_NEXT:   return k::page_down;
		case VK_HOME:   return k::home;
		case VK_END:    return k::end;
		case VK_SPACE:  return k::space;
		default: break;
	}
	// 英字 (VK_A..VK_Z = 0x41..0x5A) / 数字 (VK_0..VK_9 = 0x30..0x39) は
	// cycfi key_code の値と一致する。
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
		return static_cast<k>(vk);
	return k::unknown;
}

//! cycfi mouse_button::what のショートカット (Win32 のマウスメッセージは
//! WM_LBUTTONDOWN 等で左右中が分かれているので、 呼出側が対応する値を渡す)。
inline cycfi::elements::mouse_button::what mouse_left()   { return cycfi::elements::mouse_button::left; }
inline cycfi::elements::mouse_button::what mouse_middle() { return cycfi::elements::mouse_button::middle; }
inline cycfi::elements::mouse_button::what mouse_right()  { return cycfi::elements::mouse_button::right; }

}} // namespace elements_modal::win32_input

#endif // ELEMENTS_MODAL_WIN32_INPUT_H

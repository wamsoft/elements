//---------------------------------------------------------------------------
// elements_modal: 独立 SDL_Window 経由のモーダル実行
//
// 純粋 SDL3 + cycfi::elements + ThorVG ベース。 JSON parse + view 構築 →
// 内容サイズで SDL_Window 生成 → ループで描画 + 入力を Elements に流す。
// close_on_click=true な button click または ESC で終了。
//---------------------------------------------------------------------------
#include "elements_modal/modal.h"
#include "json_layout.h"
#include "key_map.h"   // sdl_key_to_ce (キーボードのみ。 gamepad は新 API 直接)

#include <SDL3/SDL.h>

#include <elements.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace ce = cycfi::elements;

namespace elements_modal {

namespace {

//---------------------------------------------------------------------------
// SDL → Elements 変換
//---------------------------------------------------------------------------
ce::mouse_button::what to_mouse_what(Uint8 sdl_btn)
{
	switch (sdl_btn) {
		case SDL_BUTTON_LEFT:   return ce::mouse_button::left;
		case SDL_BUTTON_MIDDLE: return ce::mouse_button::middle;
		case SDL_BUTTON_RIGHT:  return ce::mouse_button::right;
		default:                return ce::mouse_button::left;
	}
}

int sdl_mods_to_elements(SDL_Keymod mod)
{
	int m = 0;
	if (mod & SDL_KMOD_SHIFT) m |= ce::mod_shift;
	if (mod & SDL_KMOD_CTRL)  m |= ce::mod_control;
	if (mod & SDL_KMOD_ALT)   m |= ce::mod_alt;
	return m;
}

// UTF-8 1 シーケンスを codepoint に展開。 不正は 0 を返して 1 byte 進める。
std::uint32_t decode_utf8(const char*& p, const char* end)
{
	if (p >= end) return 0;
	unsigned char c = static_cast<unsigned char>(*p++);
	if (c < 0x80) return c;
	std::uint32_t cp = 0;
	int extras = 0;
	if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extras = 1; }
	else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extras = 2; }
	else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extras = 3; }
	else return 0;
	for (int i = 0; i < extras; ++i) {
		if (p >= end) return 0;
		cp = (cp << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
	}
	return cp;
}

// SDL_GamepadButton → ce::pad_button (Elements 標準マッピング)。
// 未対応ボタンは pad_button::unknown を返す。
ce::pad_button sdl_to_pad_button(int sdl_button)
{
	using b = ce::pad_button;
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

ce::pad_axis sdl_to_pad_axis(int sdl_axis)
{
	using a = ce::pad_axis;
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

} // anonymous

//---------------------------------------------------------------------------
// 公開 API
//---------------------------------------------------------------------------
bool run_modal(const std::string& json_utf8,
               const config& cfg,
               result& out_result)
{
	if (cfg.width <= 0 || cfg.height <= 0) {
		SDL_Log("elements_modal::run_modal: invalid window size");
		return false;
	}

	// ランタイム (ThorVG + フォント) 初期化。 重複呼出は no-op。
	if (!init(cfg.font_directory)) {
		return false;
	}

	// ゲームパッドサブシステムを保証 (既に init 済みなら no-op)。 失敗しても
	// ダイアログ自体はキーボード / マウスで継続できるので警告のみ。
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		SDL_Log("elements_modal::run_modal: SDL_INIT_GAMEPAD failed: %s",
			SDL_GetError());
	}

	// JSON → element ツリー。 イベント callback で結果を収集。
	// close_button_ids は build 後の parsed_layout から取って判定するが、
	// callback は parse 中に渡す必要があるので、 set への参照を外に出して
	// 後で fill する形にする。
	struct collector {
		std::string action;
		std::map<std::string, value_t> values;
		bool finished = false;
		std::set<std::string> close_button_ids;   // build 後に埋める
		event_callback on_event;                  // config.on_event のコピー
	} col;
	col.on_event = cfg.on_event;

	auto layout = parse_from_string(json_utf8,
		[&col](const std::string& id, bool is_button_click, const value_t& payload) {
			// 外部 callback には常に転送 (state 変化も button click も)。
			if (col.on_event) col.on_event(id, is_button_click, payload);
			if (is_button_click) {
				// "close_on_click": true な button だけ終了させる。
				if (col.close_button_ids.count(id)) {
					col.action = id;
					col.finished = true;
				}
			} else {
				col.values[id] = payload;
			}
		});
	col.close_button_ids = layout.close_button_ids;

	if (!layout.root) {
		SDL_Log("elements_modal::run_modal: layout parse failed");
		return false;
	}

	// cfg.pixel_scale で view extent と pixel buffer の関係を切替:
	//   - scale=1.0 (デフォルト): view extent = ウィンドウサイズ (logical=pixel)
	//   - scale=2.0: view extent = window/2 logical、 pixel buffer は window 通り
	//     (= 2x 密度の高解像度描画)
	const float kScale = (cfg.pixel_scale > 0.0f) ? cfg.pixel_scale : 1.0f;

	// 1 パス目: view を要求サイズで作って content の自然サイズを測る。
	// view::limits() は content() 内で set_limits() 経由で計算済みなので、
	// SDL_Window を持たない状態でもそのまま読める。
	float req_w_logical = static_cast<float>(cfg.width)  / kScale;
	float req_h_logical = static_cast<float>(cfg.height) / kScale;

	auto view_ptr = std::make_unique<ce::view>(
		ce::extent{ req_w_logical, req_h_logical });
	view_ptr->content(ce::hold_any(layout.root));

	// JSON の "input" ブロックで指定された view 設定 (arrow_focus_nav / pad mode
	// / pad button binding / shortcut) を適用。 setup は content() の後でないと
	// id 解決ができないので、 ここで実行。
	if (layout.apply_input) layout.apply_input(*view_ptr);

	// JSON の "initial_focus": true 指定の要素にフォーカスを当てる。 指定が
	// なければ Elements 標準動作 (focus なし) のまま。 view::focus() は asio::post
	// で次の idle に deferred されるので、 描画タイミングは気にしなくてよい。
	if (layout.initial_focus) {
		view_ptr->focus(layout.initial_focus);
	}

	// 2 パス目: 自然サイズ (limits.min) で SDL_Window を作る。 cfg の値は
	// 上限として機能 — 内容が要求サイズより大きい場合は要求サイズで切る
	// (= スクロールバー等の制御は呼出側 / JSON 内で)。
	auto vlim = view_ptr->limits();
	float fit_w_logical = vlim.min.x;
	float fit_h_logical = vlim.min.y;
	if (fit_w_logical <= 0.0f || fit_w_logical > req_w_logical)
		fit_w_logical = req_w_logical;
	if (fit_h_logical <= 0.0f || fit_h_logical > req_h_logical)
		fit_h_logical = req_h_logical;
	int win_w_px = static_cast<int>(fit_w_logical * kScale + 0.5f);
	int win_h_px = static_cast<int>(fit_h_logical * kScale + 0.5f);
	if (win_w_px < 1) win_w_px = 1;
	if (win_h_px < 1) win_h_px = 1;

	// view を fit サイズに合わせて 1 パス目で測ったサイズより縮める
	// (cfg のフルサイズで作っていたので、 自然サイズが小さければ shrink)。
	view_ptr->size(ce::extent{ fit_w_logical, fit_h_logical });

	// SDL ウィンドウ生成 (cfg.parent が指定されていれば SDL_WINDOW_MODAL で
	// 親に対するモーダル化)。 atomic 設定のため CreateWithProperties 経由。
	const std::string& title = cfg.title_utf8.empty()
		? std::string{"Modal Dialog"} : cfg.title_utf8;

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, win_w_px);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, win_h_px);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
	if (cfg.parent) {
		SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, cfg.parent);
		SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MODAL_BOOLEAN, true);
	}
	SDL_Window* window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);
	if (!window) {
		SDL_Log("elements_modal::run_modal: SDL_CreateWindowWithProperties failed: %s",
			SDL_GetError());
		return false;
	}
	SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
	if (!renderer) {
		SDL_Log("elements_modal::run_modal: SDL_CreateRenderer failed: %s",
			SDL_GetError());
		SDL_DestroyWindow(window);
		return false;
	}
	SDL_StartTextInput(window);

	int tex_w = win_w_px, tex_h = win_h_px;
	std::vector<std::uint32_t> staging(static_cast<size_t>(tex_w) * tex_h, 0u);
	SDL_Texture* texture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		tex_w, tex_h);
	if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

	auto recreate_texture = [&](int w, int h) {
		if (texture) SDL_DestroyTexture(texture);
		tex_w = w;
		tex_h = h;
		staging.assign(static_cast<size_t>(w) * h, 0u);
		texture = SDL_CreateTexture(renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			w, h);
		if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	};

	ce::point last_cursor{0, 0};
	bool mouse_down = false;
	const SDL_WindowID my_win_id = SDL_GetWindowID(window);

	int dst_x = 0, dst_y = 0;
	auto to_view = [&](float sdl_x, float sdl_y) -> ce::point {
		return { (sdl_x - dst_x) / kScale, (sdl_y - dst_y) / kScale };
	};

	while (!col.finished) {
		// 各フレーム頭で window pixel サイズを query して、 変化があれば
		// recreate_texture + view->size() を呼ぶ。 SDL_EVENT_WINDOW_RESIZED は
		// 一部プラットフォームで発火タイミングが不安定 (ドラッグ中無発火等) な
		// ため、 ポーリングで補完する。
		{
			int cur_w = 0, cur_h = 0;
			SDL_GetWindowSizeInPixels(window, &cur_w, &cur_h);
			if (cur_w > 0 && cur_h > 0 && (cur_w != tex_w || cur_h != tex_h)) {
				view_ptr->size(ce::extent{
					static_cast<float>(cur_w) / kScale,
					static_cast<float>(cur_h) / kScale });
				recreate_texture(cur_w, cur_h);
			}
		}

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_EVENT_QUIT:
					col.finished = true;
					break;
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
					if (ev.window.windowID == my_win_id) col.finished = true;
					break;
				case SDL_EVENT_WINDOW_RESIZED:
					if (ev.window.windowID == my_win_id) {
						int nw = ev.window.data1;
						int nh = ev.window.data2;
						view_ptr->size(ce::extent{
							static_cast<float>(nw) / kScale,
							static_cast<float>(nh) / kScale });
						recreate_texture(nw, nh);
					}
					break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_UP: {
					if (ev.button.windowID != my_win_id) break;
					ce::point p = to_view(ev.button.x, ev.button.y);
					last_cursor = p;
					bool is_down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
					mouse_down = is_down;
					ce::mouse_button btn{
						.down = is_down,
						.num_clicks = 1,
						.state = to_mouse_what(ev.button.button),
						.modifiers = sdl_mods_to_elements(SDL_GetModState()),
						.pos = p
					};
					if (is_down) view_ptr->cursor(p, ce::cursor_tracking::hovering);
					view_ptr->click(btn);
					break;
				}
				case SDL_EVENT_MOUSE_MOTION: {
					if (ev.motion.windowID != my_win_id) break;
					ce::point p = to_view(ev.motion.x, ev.motion.y);
					last_cursor = p;
					if (mouse_down) {
						ce::mouse_button btn{
							.down = true, .num_clicks = 1,
							.state = ce::mouse_button::left,
							.modifiers = sdl_mods_to_elements(SDL_GetModState()),
							.pos = p
						};
						view_ptr->drag(btn);
					} else {
						view_ptr->cursor(p, ce::cursor_tracking::hovering);
					}
					break;
				}
				case SDL_EVENT_MOUSE_WHEEL: {
					if (ev.wheel.windowID != my_win_id) break;
					float mx, my;
					SDL_GetMouseState(&mx, &my);
					view_ptr->scroll(ce::point{ ev.wheel.x, ev.wheel.y },
					                 to_view(mx, my));
					break;
				}
				case SDL_EVENT_KEY_DOWN: {
					if (ev.key.windowID != my_win_id) break;
					// ESC は Elements 側に渡してから col.finished で抜ける。
					// 直接ハンドラ走らせると input_box 編集中の ESC 等の widget
					// 側挙動が消えるが、 ダイアログとして閉じる意図優先。
					if (ev.key.key == SDLK_ESCAPE) {
						col.finished = true;
						break;
					}
					ce::key_info ki{
						.key = sdl_key_to_ce(ev.key.key),
						.action = ev.key.repeat ? ce::key_action::repeat
						                        : ce::key_action::press,
						.modifiers = sdl_mods_to_elements(SDL_GetModState())
					};
					view_ptr->key(ki);
					break;
				}
				case SDL_EVENT_KEY_UP: {
					if (ev.key.windowID != my_win_id) break;
					ce::key_info ki{
						.key = sdl_key_to_ce(ev.key.key),
						.action = ce::key_action::release,
						.modifiers = sdl_mods_to_elements(SDL_GetModState())
					};
					view_ptr->key(ki);
					break;
				}
				case SDL_EVENT_GAMEPAD_ADDED: {
					SDL_OpenGamepad(ev.gdevice.which);
					break;
				}
				case SDL_EVENT_GAMEPAD_REMOVED: {
					if (auto* gp = SDL_GetGamepadFromID(ev.gdevice.which)) {
						SDL_CloseGamepad(gp);
					}
					break;
				}
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_UP: {
					auto btn = sdl_to_pad_button(ev.gbutton.button);
					if (btn == ce::pad_button::unknown) break;
					ce::pad_button_info info{
						.button = btn,
						.down = (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
					};
					view_ptr->pad_button_event(info);
					break;
				}
				case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
					auto ax = sdl_to_pad_axis(ev.gaxis.axis);
					if (ax == ce::pad_axis::unknown) break;
					float v = ev.gaxis.value / 32767.0f;
					if (v < -1.0f) v = -1.0f;
					if (v >  1.0f) v =  1.0f;
					view_ptr->pad_axis_event({ax, v});
					break;
				}
				case SDL_EVENT_TEXT_INPUT: {
					if (ev.text.windowID != my_win_id) break;
					const char* p = ev.text.text;
					const char* end = p + std::strlen(p);
					while (p < end) {
						std::uint32_t cp = decode_utf8(p, end);
						if (cp == 0) continue;
						ce::text_info ti{ .codepoint = cp, .modifiers = 0 };
						view_ptr->text(ti);
					}
					break;
				}
				default: break;
			}
		}
		if (col.finished) break;

		// 描画
		std::fill(staging.begin(), staging.end(), 0u);
		{
			ce::canvas cnv{ staging.data(),
			                static_cast<std::uint32_t>(tex_w),
			                static_cast<std::uint32_t>(tex_h),
			                kScale };
			view_ptr->draw(cnv);
		}

		// 実レンダ結果サイズ (logical) を取得して中央配置。 pixel 換算で texture
		// src/dst rect を計算 (scale=2 等の HiDPI モードに対応)。
		//
		// view extent は resize で動的に変わる (recreate_texture 経路) ので、
		// 「現在の tex_w / scale」 を view 論理サイズの最大値とする。 cfg.width
		// から計算してしまうとリサイズに追従しない。
		auto vlim = view_ptr->limits();
		int actual_w_logical = static_cast<int>(vlim.max.x);
		int actual_h_logical = static_cast<int>(vlim.max.y);
		const int view_w_logical = static_cast<int>(tex_w / kScale);
		const int view_h_logical = static_cast<int>(tex_h / kScale);
		if (actual_w_logical <= 0 || actual_w_logical > view_w_logical) {
			actual_w_logical = view_w_logical;
		}
		if (actual_h_logical <= 0 || actual_h_logical > view_h_logical) {
			actual_h_logical = view_h_logical;
		}
		const int actual_w_px = static_cast<int>(actual_w_logical * kScale);
		const int actual_h_px = static_cast<int>(actual_h_logical * kScale);
		dst_x = (tex_w - actual_w_px) / 2;
		dst_y = (tex_h - actual_h_px) / 2;

		if (texture) {
			SDL_UpdateTexture(texture, nullptr, staging.data(), tex_w * 4);
		}
		// background 未指定時の safety net: mid-gray clear で widget が黒沈み
		// しないようにする。
		SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
		SDL_RenderClear(renderer);
		if (texture) {
			SDL_FRect src{ 0.0f, 0.0f,
			               static_cast<float>(actual_w_px),
			               static_cast<float>(actual_h_px) };
			SDL_FRect dst{ static_cast<float>(dst_x),
			               static_cast<float>(dst_y),
			               static_cast<float>(actual_w_px),
			               static_cast<float>(actual_h_px) };
			SDL_RenderTexture(renderer, texture, &src, &dst);
		}
		SDL_RenderPresent(renderer);
		view_ptr->poll();
		SDL_Delay(8);
	}

	view_ptr.reset();
	if (texture) SDL_DestroyTexture(texture);
	SDL_StopTextInput(window);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);

	out_result.action = std::move(col.action);
	out_result.values = std::move(col.values);
	return true;
}

} // namespace elements_modal

//---------------------------------------------------------------------------
// elements_modal::overlay_session 実装
//
// run_modal が独立 SDL_Window を持つのに対し、 overlay_session は呼出側が
// 用意したサーフェスにダイアログを描画する低レベル API。 SDL_Window は持たず、
// イベントポンプも回さない。 呼出側が SDL イベントを on_xxx に流し、 毎フレーム
// render_to_buffer を呼ぶ形。
//
// 座標系:
//   - start() に渡す view_width/height は logical 座標 (= view extent)
//   - render_to_buffer に渡す buffer は pixel サイズ
//     (= view_width * pixel_scale × view_height * pixel_scale 想定)
//   - render_to_buffer に渡す surface_w/h は呼出側サーフェスの logical サイズ
//     (中央配置の基準)
//   - out_rect は surface logical 座標 (呼出側はこの位置に texture を貼る)
//   - on_mouse_* に渡す座標も surface logical (内部で last_rect を引いて view
//     local 座標に変換)
//---------------------------------------------------------------------------
#include "elements_modal/modal.h"
#include "elements_modal/animator.h"
#include "json_layout.h"
#include "key_map.h"

#include <SDL3/SDL.h>

#include <elements.hpp>

#include <algorithm>
#include <cstring>
#include <memory>

namespace ce = cycfi::elements;

namespace elements_modal {

namespace {

ce::mouse_button::what to_mouse_what(int sdl_btn)
{
	switch (sdl_btn) {
		case SDL_BUTTON_LEFT:   return ce::mouse_button::left;
		case SDL_BUTTON_MIDDLE: return ce::mouse_button::middle;
		case SDL_BUTTON_RIGHT:  return ce::mouse_button::right;
		default:                return ce::mouse_button::left;
	}
}

int sdl_mods_to_elements(int mod)
{
	int m = 0;
	if (mod & SDL_KMOD_SHIFT) m |= ce::mod_shift;
	if (mod & SDL_KMOD_CTRL)  m |= ce::mod_control;
	if (mod & SDL_KMOD_ALT)   m |= ce::mod_alt;
	return m;
}

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

} // anonymous

//---------------------------------------------------------------------------
// PIMPL
//---------------------------------------------------------------------------
struct overlay_session::impl
{
	std::unique_ptr<ce::view> view;
	std::shared_ptr<ce::element> layout_root;
	result accumulated;

	bool started        = false;
	bool finished_      = false;

	// 退場 (exit) 演出の再生中フラグ。 finish 要求時に exit 束縛があれば、
	// すぐ finished_ にせず exit 演出を再生し、 完了してから finished_ にする
	// (退場×遷移の協調)。 この間は入力を受け付けない (active() が false)。
	bool exiting_       = false;

	// JSON で "close_on_click": true が付いた button の id 集合。
	// このいずれかが click されると fire() 内で finished_=true にして
	// 終了状態にする。 含まれない button click は外部 callback だけ発火。
	std::set<std::string> close_button_ids;

	// view extent (logical)
	int view_w = 0;
	int view_h = 0;

	// 配置アンカー (0=左/上, 0.5=中央, 1=右/下) + サーフェス端からの余白 px。
	// JSON top-level "align" / "margin" から。 既定は中央。
	float anchor_x = 0.5f;
	float anchor_y = 0.5f;
	int   margin   = 0;

	// logical → pixel 倍率 (canvas に渡す)
	float scale = 1.0f;

	// 直近の描画矩形 (surface logical 座標)。 入力座標補正に使う。
	render_rect last_rect{};

	// 入力 hover 用 last cursor (view local)
	ce::point last_cursor{0.0f, 0.0f};
	bool mouse_down = false;

	// 任意の外部 callback (ホスト側の event handler ブリッジ用など)
	event_callback external_cb;

	// focus 変化に追従する「変数 → label set_text」の poll。 毎 render 前と
	// 主要な入力イベント処理後に呼ぶ。 JSON 側に vars_on_focus / text_var が
	// 一切ない場合は null。
	std::function<void()> focus_poll;

	// JSON top-level "transitions" を読んだ辞書。 ランナが get_result の
	// action と照合して次画面を決める。
	std::map<std::string, transition_spec> transitions;

	// id 解決テーブル (= parsed_layout.id_map のコピー)。 focus_by_id で
	// element を引くのに使う。
	std::map<std::string, std::shared_ptr<ce::element>> id_map;

	// id 付き widget の登録順リスト (id+type)。 list_widgets() 用。
	std::vector<overlay_session::widget_desc> widgets;

	// focus poll が更新する「現在 focus されている id」スロット。 ホストが
	// focused_id() で読む。
	std::shared_ptr<std::string> focused_id_slot;

	// hover (マウスオーバー) 追従の poll + 現在 hover id スロット。 hover トリガ演出用。
	std::function<void()> hover_poll;
	std::shared_ptr<std::string> hovered_id_slot;

	// focus トリガ演出を有効にするか (JSON "input":{"focus_anim":false})。 hover_focus
	// 併用時の focus×hover 多重発火を避ける逃がし。
	bool focus_anim = true;

	// i18n: 言語切替 closure (StringStore を捕捉)。 set_language() から呼ぶ。
	// "strings" 未定義でも parsed_layout から非 null で渡る (no-op)。
	std::function<void(const std::string&)> set_language_fn;

	// 現在の表示言語。 JSON "lang" の初期値 or set_language() で更新。
	std::string current_lang;

	// パーツ演出 (Phase A): "animate" 由来の変換アニメ。 start() で初期化し、
	// render_to_buffer で経過 ms 分 tick する。 空なら一切のコスト無し。
	animator anim;
	Uint64   last_anim_ms = 0;

	void fire(std::string_view id, bool is_button_click, const value_t& payload)
	{
		// 外部 callback はあらゆるイベント (state 変化 + 全 button click) に
		// 対して呼ぶ。 これで Dialog::onAction 経路で TJS 側が反応できる。
		std::string id_s(id);
		if (!is_button_click) {
			accumulated.values[id_s] = payload;
		}
		if (external_cb) {
			external_cb(id_s, is_button_click, payload);
		}
		// 決定 (select) 演出は button click、 値変化 (change) は state widget の
		// 値変化 (is_button_click=false) で発火。
		if (!anim.empty()) {
			anim.fire(is_button_click ? anim_binding::trigger::select
			                          : anim_binding::trigger::change, id_s);
		}
		// "close_on_click": true な button click のみセッションを終了させる。
		// 含まれない button click は外部 callback の発火だけで継続。
		if (is_button_click && close_button_ids.count(id_s)) {
			begin_finish(id_s);
		}
	}

	// セッション終了を要求する。 exit 演出があれば即終了せず再生してから終了
	// する (退場×遷移の協調)。 action は結果に記録され、 遷移先決定に使われる。
	void begin_finish(std::string action)
	{
		if (finished_ || exiting_) return;   // 既に退場処理中
		accumulated.action = std::move(action);
		if (!anim.empty() && anim.count(anim_binding::trigger::exit) > 0) {
			// exit 束縛を再生開始。 完了は render_to_buffer 側で検出して finished_ に。
			anim.fire(anim_binding::trigger::exit);
			exiting_ = true;
			last_anim_ms = SDL_GetTicks();
		} else {
			finished_ = true;
		}
	}

	// surface logical → view local 座標。 last_rect を引くだけ
	// (scale 倍率は不要 — surface も view local も logical)。
	ce::point to_view(float sx, float sy) const
	{
		return ce::point{ sx - static_cast<float>(last_rect.x),
		                  sy - static_cast<float>(last_rect.y) };
	}
};

//---------------------------------------------------------------------------
// 公開メソッド
//---------------------------------------------------------------------------
overlay_session::overlay_session() : _impl(std::make_unique<impl>()) {}
overlay_session::~overlay_session() = default;

bool overlay_session::start(const std::string& json_utf8,
                            int view_width, int view_height,
                            float pixel_scale,
                            event_callback external_cb,
                            const std::string& resource_base)
{
	_impl->external_cb = std::move(external_cb);

	if (_impl->started) {
		SDL_Log("overlay_session::start: already started");
		return false;
	}
	if (view_width <= 0 || view_height <= 0 || pixel_scale <= 0.0f) {
		SDL_Log("overlay_session::start: invalid view size or scale");
		return false;
	}

	// ランタイム (ThorVG + フォント) 初期化を保証。
	if (!init()) return false;

	// JSON 解析 + イベント callback で値収集
	impl* p = _impl.get();
	auto layout = parse_from_string(json_utf8,
		[p](std::string_view id, bool is_btn, const value_t& v) {
			p->fire(id, is_btn, v);
		},
		resource_base);

	if (!layout.root) {
		SDL_Log("overlay_session::start: layout parse failed");
		return false;
	}

	_impl->layout_root = layout.root;
	_impl->view_w = view_width;
	_impl->view_h = view_height;
	_impl->anchor_x = layout.anchor_x;
	_impl->anchor_y = layout.anchor_y;
	_impl->margin   = layout.margin;
	_impl->scale  = pixel_scale;
	_impl->close_button_ids = layout.close_button_ids;
	_impl->focus_poll = std::move(layout.focus_poll);
	_impl->hover_poll = std::move(layout.hover_poll);
	_impl->hovered_id_slot = layout.hovered_id_slot;
	_impl->focus_anim = layout.focus_anim;
	_impl->transitions = std::move(layout.transitions);
	_impl->id_map = std::move(layout.id_map);
	for (auto& it : layout.id_types)
		_impl->widgets.push_back({ std::move(it.first), std::move(it.second) });
	_impl->focused_id_slot = layout.focused_id_slot;
	_impl->set_language_fn = std::move(layout.set_language);
	_impl->current_lang = std::move(layout.lang);

	// パーツ演出束縛を animator に積み、 初期値 (進捗 0) を適用してから開始。
	for (auto& b : layout.animations) _impl->anim.add(std::move(b));
	_impl->anim.start();
	_impl->last_anim_ms = SDL_GetTicks();

	_impl->view = std::make_unique<ce::view>(
		ce::extent{ static_cast<float>(view_width),
		            static_cast<float>(view_height) });
	_impl->view->content(ce::hold_any(_impl->layout_root));

	// JSON "input" ブロックの設定 (arrow_focus_nav / pad mode / bind 等) を適用。
	// id 解決に内部 element map を使うため content() 後でないと駄目。
	if (layout.apply_input) layout.apply_input(*_impl->view);

	// JSON "initial_focus": true 指定があればフォーカス。 view::focus() は
	// asio::post で次 idle へデファードされるので順序問題なし。
	if (layout.initial_focus) {
		_impl->view->focus(layout.initial_focus);
	}

	_impl->started          = true;
	_impl->finished_        = false;
	return true;
}

void overlay_session::close(std::string action)
{
	if (!_impl->started) return;
	_impl->begin_finish(std::move(action));   // exit 演出があれば再生してから終了
}

bool overlay_session::active() const
{
	// 退場演出の再生中 (exiting_) は入力を受け付けない。 描画 (render_to_buffer)
	// は finished_ を直接見るので exit 演出中も継続する。
	return _impl->started && !_impl->finished_ && !_impl->exiting_;
}

bool overlay_session::finished() const
{
	return _impl->started && _impl->finished_;
}

const result& overlay_session::get_result() const
{
	return _impl->accumulated;
}

const std::map<std::string, transition_spec>&
overlay_session::transitions() const
{
	return _impl->transitions;
}

const std::string& overlay_session::focused_id() const
{
	static const std::string empty;
	if (!_impl || !_impl->focused_id_slot) return empty;
	return *_impl->focused_id_slot;
}

void overlay_session::focus_by_id(const std::string& id)
{
	if (!_impl || !_impl->view || id.empty()) return;
	auto it = _impl->id_map.find(id);
	if (it == _impl->id_map.end()) {
		SDL_Log("overlay_session::focus_by_id: id '%s' not found",
		        id.c_str());
		return;
	}
	_impl->view->focus(it->second);
}

std::vector<overlay_session::widget_desc> overlay_session::list_widgets() const
{
	if (!_impl) return {};
	return _impl->widgets;
}

bool overlay_session::activate_by_id(const std::string& id)
{
	if (!_impl || !_impl->view || id.empty()) return false;
	auto it = _impl->id_map.find(id);
	if (it == _impl->id_map.end()) return false;
	// focus は遅延タスクなので、 poll() で即時適用してから Enter を送る。
	_impl->view->focus(it->second);
	_impl->view->poll();
	bool handled = on_key_down(SDLK_RETURN, 0);
	on_key_up(SDLK_RETURN, 0);
	return handled || true;   // 既知 id へ送れた時点で成功扱い
}

void overlay_session::set_language(const std::string& lang)
{
	if (!_impl) return;
	_impl->current_lang = lang;
	if (_impl->set_language_fn) _impl->set_language_fn(lang);
	// set_text 済の label は次回 render_to_buffer (view->draw) で再描画される。
}

const std::string& overlay_session::language() const
{
	static const std::string empty;
	if (!_impl) return empty;
	return _impl->current_lang;
}

void overlay_session::play_animation(const std::string& trigger,
                                     const std::string& id)
{
	if (!_impl || _impl->anim.empty()) return;
	_impl->anim.fire(trigger_from_string(trigger), id);
}

void overlay_session::notify_view_resize(int new_view_width, int new_view_height)
{
	if (!_impl->view) return;
	_impl->view_w = new_view_width;
	_impl->view_h = new_view_height;
	_impl->view->size(ce::extent{
		static_cast<float>(new_view_width),
		static_cast<float>(new_view_height) });
}

overlay_session::render_rect overlay_session::get_current_rect() const
{
	return _impl->last_rect;
}

bool overlay_session::measure_content(int& out_w, int& out_h) const
{
	out_w = 0;
	out_h = 0;
	if (!_impl->view) return false;
	// run_modal と同じく view limits の min (= content の自然最小サイズ) を
	// 返す。 background が付いた layout でも box の min は ~0 なので、 layer の
	// min はそのまま content の自然サイズになる (max は box が無限大に伸ばすため
	// 使えない)。
	auto vlim = _impl->view->limits();
	out_w = static_cast<int>(vlim.min.x + 0.5f);
	out_h = static_cast<int>(vlim.min.y + 0.5f);
	return true;
}

bool overlay_session::render_to_buffer(std::uint32_t* pixel_buffer,
                                       int buffer_w_px, int buffer_h_px,
                                       int surface_w, int surface_h,
                                       render_rect& out_rect)
{
	out_rect = {};
	if (!_impl->view || !pixel_buffer || buffer_w_px <= 0 || buffer_h_px <= 0) {
		return false;
	}
	if (_impl->finished_) return false;

	// 描画前に focus poll: 変数連動 label の text を更新する。
	if (_impl->focus_poll) _impl->focus_poll();
	if (_impl->hover_poll) _impl->hover_poll();

	// focus / hover 変化を演出へ通知 (取得で前進、 喪失で復帰再生)。 poll の後に呼ぶ。
	// focus_anim=false なら focus トリガは止める (hover_focus 併用時の多重発火回避)。
	if (!_impl->anim.empty()) {
		if (_impl->focus_anim && _impl->focused_id_slot)
			_impl->anim.notify_focus(*_impl->focused_id_slot);
		if (_impl->hovered_id_slot)
			_impl->anim.notify_hover(*_impl->hovered_id_slot);
	}

	// パーツ演出を経過 ms 分進める (xform_state を書き換え → 次の draw に反映)。
	if (!_impl->anim.empty()) {
		const Uint64 now = SDL_GetTicks();
		const float dt = (now > _impl->last_anim_ms)
		               ? static_cast<float>(now - _impl->last_anim_ms) : 0.0f;
		_impl->last_anim_ms = now;
		_impl->anim.tick(dt);

		// 退場演出の完了を検出して終了確定 (退場×遷移の協調)。 exit 束縛だけを
		// 見るので、 enter の無限ループ等があっても正しく完了判定できる。
		if (_impl->exiting_ &&
		    !_impl->anim.active_any(anim_binding::trigger::exit)) {
			_impl->exiting_ = false;
			_impl->finished_ = true;
		}
	}

	const size_t pixel_count = static_cast<size_t>(buffer_w_px) * buffer_h_px;
	std::fill_n(pixel_buffer, pixel_count, 0u);

	{
		ce::canvas cnv{ pixel_buffer,
		                static_cast<std::uint32_t>(buffer_w_px),
		                static_cast<std::uint32_t>(buffer_h_px),
		                _impl->scale };
		_impl->view->draw(cnv);
	}

	// 実レンダ結果サイズで中央配置 (Elements は content の自然サイズで描く)
	auto vlim = _impl->view->limits();
	int actual_w = static_cast<int>(vlim.max.x);
	int actual_h = static_cast<int>(vlim.max.y);
	if (actual_w <= 0 || actual_w > _impl->view_w) actual_w = _impl->view_w;
	if (actual_h <= 0 || actual_h > _impl->view_h) actual_h = _impl->view_h;

	render_rect r;
	if (surface_w > 0 && surface_h > 0) {
		// アンカー配置: 0=端(margin)、 0.5=中央、 1=反対側端(margin)。
		//   pos = margin + (free - 2*margin) * anchor    (free = surface - actual)
		const int free_x = surface_w - actual_w;
		const int free_y = surface_h - actual_h;
		r.x = _impl->margin +
		      static_cast<int>((free_x - 2 * _impl->margin) * _impl->anchor_x);
		r.y = _impl->margin +
		      static_cast<int>((free_y - 2 * _impl->margin) * _impl->anchor_y);
		if (r.x < 0) r.x = 0;
		if (r.y < 0) r.y = 0;
	} else {
		r.x = 0;
		r.y = 0;
	}
	r.w = actual_w;
	r.h = actual_h;
	out_rect = r;
	_impl->last_rect = r;

	_impl->view->poll();
	return true;
}

// --- 入力イベント転送 (surface logical 座標) ---

void overlay_session::on_mouse_down(float sx, float sy, int button, int mods)
{
	if (!active()) return;
	auto p = _impl->to_view(sx, sy);
	_impl->last_cursor = p;
	_impl->mouse_down = true;
	ce::mouse_button btn{
		.down = true,
		.num_clicks = 1,
		.state = to_mouse_what(button),
		.modifiers = sdl_mods_to_elements(mods),
		.pos = p
	};
	_impl->view->cursor(p, ce::cursor_tracking::hovering);
	_impl->view->click(btn);
}

void overlay_session::on_mouse_up(float sx, float sy, int button, int mods)
{
	if (!active()) return;
	auto p = _impl->to_view(sx, sy);
	_impl->last_cursor = p;
	_impl->mouse_down = false;
	ce::mouse_button btn{
		.down = false,
		.num_clicks = 1,
		.state = to_mouse_what(button),
		.modifiers = sdl_mods_to_elements(mods),
		.pos = p
	};
	_impl->view->click(btn);
}

void overlay_session::on_mouse_move(float sx, float sy, int mods)
{
	if (!active()) return;
	auto p = _impl->to_view(sx, sy);
	_impl->last_cursor = p;
	if (_impl->mouse_down) {
		ce::mouse_button btn{
			.down = true, .num_clicks = 1,
			.state = ce::mouse_button::left,
			.modifiers = sdl_mods_to_elements(mods),
			.pos = p
		};
		_impl->view->drag(btn);
	} else {
		_impl->view->cursor(p, ce::cursor_tracking::hovering);
	}
}

void overlay_session::on_mouse_wheel(float dx, float dy,
                                     float surface_mouse_x, float surface_mouse_y)
{
	if (!active()) return;
	auto p = _impl->to_view(surface_mouse_x, surface_mouse_y);
	_impl->view->scroll(ce::point{ dx, dy }, p);
}

void overlay_session::on_mouse_leave()
{
	if (!active()) return;
	_impl->view->cursor(_impl->last_cursor, ce::cursor_tracking::leaving);
}

bool overlay_session::on_key_down(int sdl_key, int mods)
{
	if (!active()) return false;
	if (sdl_key == SDLK_ESCAPE) {
		_impl->begin_finish("");   // exit 演出があれば再生してから終了
		return true;   // Esc はダイアログが消費
	}
	ce::key_info ki{
		.key = sdl_key_to_ce(sdl_key),
		.action = ce::key_action::press,
		.modifiers = sdl_mods_to_elements(mods)
	};
	return _impl->view->key(ki);   // focus widget が処理したら true
}

bool overlay_session::on_key_up(int sdl_key, int mods)
{
	if (!active()) return false;
	ce::key_info ki{
		.key = sdl_key_to_ce(sdl_key),
		.action = ce::key_action::release,
		.modifiers = sdl_mods_to_elements(mods)
	};
	return _impl->view->key(ki);
}

void overlay_session::on_text_input(const char* utf8_text)
{
	if (!active() || !utf8_text) return;
	const char* p = utf8_text;
	const char* end = p + std::strlen(p);
	while (p < end) {
		std::uint32_t cp = decode_utf8(p, end);
		if (cp == 0) continue;
		ce::text_info ti{ .codepoint = cp, .modifiers = 0 };
		_impl->view->text(ti);
	}
}

namespace {

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

bool overlay_session::on_pad_button(int sdl_gamepad_button, bool down)
{
	if (!active()) return false;
	auto btn = sdl_to_pad_button(sdl_gamepad_button);
	if (btn == ce::pad_button::unknown) return false;
	_impl->view->pad_button_event({btn, down});
	return true;   // 既知のパッドボタンは UI が消費 (UI 操作中はゲームへ通さない)
}

void overlay_session::on_pad_axis(int sdl_gamepad_axis, int raw_value)
{
	if (!active()) return;
	auto ax = sdl_to_pad_axis(sdl_gamepad_axis);
	if (ax == ce::pad_axis::unknown) return;
	float v = static_cast<float>(raw_value) / 32767.0f;
	if (v < -1.0f) v = -1.0f;
	if (v >  1.0f) v =  1.0f;
	_impl->view->pad_axis_event({ax, v});
}

} // namespace elements_modal

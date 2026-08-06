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
#include "em_platform.h"

#include <elements.hpp>
#include <elements/support/resource_loader.hpp>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

namespace ce = cycfi::elements;

namespace elements_modal {

namespace {

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

//---------------------------------------------------------------------------
// named-action 入力バインド: 組込デフォルト標準バインド (3層の最下層)。
//
//   accept      : A (pad)         ※ Enter / 左click はネイティブ経路が実装
//   cancel      : Esc / B / 右click
//   focus_prev  : X (pad)         ※ Shift+Tab はネイティブ
//   focus_next  : Y (pad)         ※ Tab はネイティブ
//   nav_*       : (登録なし)      ※ 方向キー / dpad / 左stick は axis 機構
//   page_prev/next : LB / RB      ※ PageUp/Down キーはネイティブ (tab_view)
//   scroll_up/down : wheel        ※ 右stick(value) は axis 機構
//
// enter/tab/矢印/pgup/pgdn キー自体は「ネイティブ経路がその action の実装」
// なので登録しない (登録するとキー合成が自己再帰する。 is_identity_binding
// も参照)。
//---------------------------------------------------------------------------
const std::vector<action_binding>& builtin_default_bindings()
{
	static const std::vector<action_binding> defaults = [] {
		std::vector<action_binding> v;
		auto K = [&v](ce::key_code k, const char* a) {
			action_binding b;
			b.src = action_binding::source::key;
			b.key = k;
			b.action = a;
			v.push_back(std::move(b));
		};
		auto P = [&v](ce::pad_button p, const char* a) {
			action_binding b;
			b.src = action_binding::source::pad;
			b.pad = p;
			b.action = a;
			v.push_back(std::move(b));
		};
		auto M = [&v](ce::mouse_button::what m, const char* a) {
			action_binding b;
			b.src = action_binding::source::mouse;
			b.mbtn = m;
			b.action = a;
			v.push_back(std::move(b));
		};
		auto W = [&v](int dir, const char* a) {
			action_binding b;
			b.src = action_binding::source::wheel;
			b.wheel_dir = dir;
			b.action = a;
			v.push_back(std::move(b));
		};
		K(ce::key_code::escape,      "cancel");
		P(ce::pad_button::b,         "cancel");
		M(ce::mouse_button::right,   "cancel");
		P(ce::pad_button::a,         "accept");
		P(ce::pad_button::x,         "focus_prev");
		P(ce::pad_button::y,         "focus_next");
		P(ce::pad_button::lb,        "page_prev");
		P(ce::pad_button::rb,        "page_next");
		W(+1,                        "scroll_up");
		W(-1,                        "scroll_down");
		return v;
	}();
	return defaults;
}

// action のキー合成実装と同一のキーへのバインドか (= identity)。 これらは
// ネイティブ経路 (view::key の focus dispatch / arrow nav / tab_view の
// pgup/pgdn shortcut) が既に同じ意味を実装しているので、 shortcut 登録すると
// 「合成キー → shortcut → 再び合成」の無限ループになる。 登録スキップで
// ネイティブ経路に任せる。
bool is_identity_binding(const std::string& action, ce::key_code key, int mods)
{
	using k = ce::key_code;
	if (action == "accept")     return key == k::enter && mods == 0;
	if (action == "nav_left")   return key == k::left;
	if (action == "nav_right")  return key == k::right;
	if (action == "nav_up")     return key == k::up;
	if (action == "nav_down")   return key == k::down;
	if (action == "focus_next") return key == k::tab && mods == 0;
	if (action == "focus_prev") return key == k::tab && mods == ce::mod_shift;
	if (action == "page_prev")  return key == k::page_up;
	if (action == "page_next")  return key == k::page_down;
	return false;
}

// action 名 → SE カテゴリ ("se" ブロックのキー)。 個別 action 名での指定が
// あればそちらが優先される (play_se_for_action 参照)。
const char* action_se_category(const std::string& a)
{
	if (a == "cancel") return "cancel";
	if (a == "accept") return "accept";
	if (a.rfind("nav_", 0) == 0 || a.rfind("focus_", 0) == 0) return "nav";
	if (a.rfind("page_", 0) == 0)   return "page";
	if (a.rfind("scroll_", 0) == 0) return "scroll";
	return nullptr;
}

//---------------------------------------------------------------------------
// プロジェクト共通バインド (3層の中層): resource_base ごとに 1 回だけ
// "input_defaults.jsonc" をリソースローダ (Storages VFS 等) 経由で読み、
// 解析結果をキャッシュする。 ファイルが無ければ「無し」をキャッシュして
// 以後試さない (毎画面の起動コストゼロ)。 変更反映はアプリ再起動。
//---------------------------------------------------------------------------
const input_defaults_data* global_input_defaults(const std::string& resource_base)
{
	static std::mutex mtx;
	static std::map<std::string, input_defaults_data> cache;
	std::lock_guard<std::mutex> lk(mtx);
	auto it = cache.find(resource_base);
	if (it == cache.end()) {
		input_defaults_data d;
		const std::string name = resource_base + "input_defaults.jsonc";
		try {
			auto bytes = ce::get_resource_loader().read(name);
			if (!bytes.empty()) {
				d = parse_input_defaults(
					std::string(bytes.begin(), bytes.end()));
			}
			em_logf("elements_modal: input defaults \"%s\": %s (%u bytes)",
			        name.c_str(), d.ok ? "loaded" : "not used",
			        static_cast<unsigned>(bytes.size()));
		} catch (...) {
			// ローダが missing を例外で表す実装でも「無し」として続行。
			em_logf("elements_modal: input defaults \"%s\": read threw",
			        name.c_str());
		}
		it = cache.emplace(resource_base, std::move(d)).first;
	}
	return it->second.ok ? &it->second : nullptr;
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

	// 変数書込 closure (VariableStore を捕捉)。 set_var() から呼ぶ。
	std::function<void(const std::string&, const std::string&)> set_var_fn;

	// 現在の表示言語。 JSON "lang" の初期値 or set_language() で更新。
	std::string current_lang;

	// パーツ演出 (Phase A): "animate" 由来の変換アニメ。 start() で初期化し、
	// render_to_buffer で経過 ms 分 tick する。 空なら一切のコスト無し。
	animator anim;
	std::uint64_t last_anim_ms = 0;

	// named-action 入力バインド (組込デフォルト ⊕ input_defaults.jsonc ⊕
	// 画面別 "input"."bindings" のマージ結果)。 key/pad は view の shortcut
	// 機構に登録済みなのでここには持たない。 mouse/wheel はセッションが
	// on_mouse_down / on_mouse_wheel で直接ディスパッチする。
	std::map<int, std::string> mouse_actions;   // ce::mouse_button::what → action
	std::map<int, std::string> wheel_actions;   // +1(up) / -1(down) → action

	// SE マップ (マージ済)。 action 名 or カテゴリ → SE 名。 発火は
	// external_cb("<se>", false, SE名) でホスト通知 (ホストが kag.se 等で鳴らす)。
	std::map<std::string, std::string> se_map;

	// nav SE 用: focus 変化検出 (キーボード/dpad/stick/hover どの経路でも
	// focused_id_slot の変化として一元的に拾える)。
	std::string se_last_focused;
	bool se_focus_seen = false;

	// 合成キーイベントを view へ送る (press + release)。 named-action の
	// accept/nav/focus/page はネイティブキー経路の再利用としてこれで実装する。
	void send_key(ce::key_code kc, int mods = 0)
	{
		if (!view) return;
		view->key(ce::key_info{kc, ce::key_action::press, mods});
		view->key(ce::key_info{kc, ce::key_action::release, mods});
	}

	// スクロールを view へ送る。 amount は wheel ノッチ相当 (+ = 上)。
	// 位置はカーソル既知ならそこ、 未知 (パッド/キー操作のみ) なら view 中央。
	void scroll_amount(float amount)
	{
		if (!view) return;
		ce::point p = last_cursor;
		if (p.x <= 0.0f && p.y <= 0.0f) {
			p = ce::point{view_w * 0.5f, view_h * 0.5f};
		}
		view->scroll(ce::point{0.0f, amount}, p);
	}

	// SE 発火: se_map をキー名で引き external_cb へ通知。 未登録キーは無音。
	void play_se(const std::string& key)
	{
		if (!external_cb) return;
		auto it = se_map.find(key);
		if (it == se_map.end()) return;
		external_cb("<se>", false, value_t{it->second});
	}

	// action 名 → SE。 個別 action 名の登録が優先、 無ければカテゴリで引く。
	// accept はここでは鳴らさない (button click = fire() 側で一元発火。
	// pad A → enter 合成 → button activate → fire と二重になるため)。
	void play_se_for_action(const std::string& action)
	{
		if (action == "accept") return;
		if (se_map.count(action)) { play_se(action); return; }
		if (const char* cat = action_se_category(action)) play_se(cat);
	}

	// named UI action のディスパッチ本体。 key/pad は view shortcut callback
	// から (fire_shortcut の遅延タスク内)、 mouse/wheel は on_mouse_* から
	// 直接呼ばれる。 組込 action 以外はホストへ通知する (quick action 枠:
	// 例 {"pad":"start","action":"open_menu"} → onAction("<action>", "open_menu"))。
	void dispatch_action(const std::string& action)
	{
		if (action.empty() || action == "none") return;
		play_se_for_action(action);
		if (action == "cancel")           { begin_finish("");                  return; }
		if (action == "accept")           { send_key(ce::key_code::enter);     return; }
		if (action == "nav_up")           { send_key(ce::key_code::up);        return; }
		if (action == "nav_down")         { send_key(ce::key_code::down);      return; }
		if (action == "nav_left")         { send_key(ce::key_code::left);      return; }
		if (action == "nav_right")        { send_key(ce::key_code::right);     return; }
		if (action == "focus_next")       { send_key(ce::key_code::tab);       return; }
		if (action == "focus_prev")       { send_key(ce::key_code::tab, ce::mod_shift); return; }
		if (action == "page_prev")        { send_key(ce::key_code::page_up);   return; }
		if (action == "page_next")        { send_key(ce::key_code::page_down); return; }
		if (action == "scroll_up")        { scroll_amount(+1.0f); return; }
		if (action == "scroll_down")      { scroll_amount(-1.0f); return; }
		if (action == "scroll_page_up")   { scroll_amount(+8.0f); return; }
		if (action == "scroll_page_down") { scroll_amount(-8.0f); return; }
		if (external_cb) external_cb("<action>", false, value_t{action});
	}

	// 3層マージ済みのバインド列を適用する。 key/pad は view の shortcut 機構へ
	// (repeat / force / 優先順位を既存機構と共有)、 mouse/wheel は impl の
	// マップへ。 後勝ちマージは layers の順序で表現する。
	// この適用は apply_input (画面別 legacy "shortcuts" / tab_view の登録) より
	// 先に行うこと — 明示宣言が同一入力の action バインドを上書きできるように。
	void apply_action_bindings(
		std::initializer_list<const std::vector<action_binding>*> layers)
	{
		// マージ (後層が同一入力を上書き)
		std::map<std::pair<int, int>, action_binding> keym;
		std::map<int, action_binding> padm;
		mouse_actions.clear();
		wheel_actions.clear();
		for (const auto* layer : layers) {
			if (!layer) continue;
			for (const auto& b : *layer) {
				switch (b.src) {
				case action_binding::source::key:
					keym[{static_cast<int>(b.key), b.mods}] = b;
					break;
				case action_binding::source::pad:
					padm[static_cast<int>(b.pad)] = b;
					break;
				case action_binding::source::mouse:
					mouse_actions[static_cast<int>(b.mbtn)] = b.action;
					break;
				case action_binding::source::wheel:
					wheel_actions[b.wheel_dir] = b.action;
					break;
				}
			}
		}

		// force 既定: cancel と none は text input focus 中も効かせる
		// (従来の ESC hard-code と同挙動)。 他は非 force (text 編集を優先し、
		// pad はキー合成へフォールスルーする)。
		auto default_force = [](const std::string& action) {
			return action == "cancel" || action == "none";
		};

		impl* p = this;
		for (auto& [kk, b] : keym) {
			ce::key_info ki{static_cast<ce::key_code>(kk.first),
			                ce::key_action::press, kk.second};
			if (b.action == "none") {
				// 入力を消費して何もしない (下層バインド/ネイティブ経路も遮断)
				view->bind_shortcut(ki, []() {}, /*force=*/true);
				continue;
			}
			if (is_identity_binding(b.action, ki.key, ki.modifiers)) {
				continue;   // ネイティブ経路がその action の実装 (再帰防止)
			}
			bool force = b.force_set ? b.force : default_force(b.action);
			std::string action = b.action;
			view->bind_shortcut(ki,
				[p, action]() { p->dispatch_action(action); }, force);
		}
		for (auto& [pk, b] : padm) {
			auto btn = static_cast<ce::pad_button>(pk);
			if (b.action == "none") {
				view->bind_shortcut(btn, []() {}, /*force=*/true);
				continue;
			}
			bool force = b.force_set ? b.force : default_force(b.action);
			std::string action = b.action;
			view->bind_shortcut(btn,
				[p, action]() { p->dispatch_action(action); }, force);
		}
	}

	// SE マップの3層マージ (後勝ち)。
	void merge_se(std::initializer_list<const std::map<std::string,
	              std::string>*> layers)
	{
		se_map.clear();
		for (const auto* layer : layers) {
			if (!layer) continue;
			for (const auto& kv : *layer) se_map[kv.first] = kv.second;
		}
	}

	void fire(std::string_view id, bool is_button_click, const value_t& payload)
	{
		// 外部 callback はあらゆるイベント (state 変化 + 全 button click) に
		// 対して呼ぶ。 これで Dialog::onAction 経路で TJS 側が反応できる。
		std::string id_s(id);
		if (!is_button_click) {
			accumulated.values[id_s] = payload;
		}
		// accept SE は button click で一元発火 (マウス左click / Enter /
		// pad A→enter 合成、 どの経路でもここに集約される)。
		if (is_button_click) {
			if (se_map.count(id_s)) play_se(id_s);   // 個別 button id 指定が優先
			else                    play_se("accept");
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
			last_anim_ms = em_now_ms();
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
		em_logf("overlay_session::start: already started");
		return false;
	}
	if (view_width <= 0 || view_height <= 0 || pixel_scale <= 0.0f) {
		em_logf("overlay_session::start: invalid view size or scale");
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
		em_logf("overlay_session::start: layout parse failed");
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
	_impl->set_var_fn = std::move(layout.set_var);

	// パーツ演出束縛を animator に積み、 初期値 (進捗 0) を適用してから開始。
	for (auto& b : layout.animations) _impl->anim.add(std::move(b));
	_impl->anim.start();
	_impl->last_anim_ms = em_now_ms();

	_impl->view = std::make_unique<ce::view>(
		ce::extent{ static_cast<float>(view_width),
		            static_cast<float>(view_height) });
	_impl->view->content(ce::hold_any(_impl->layout_root));

	// 入力バインド3層 (後勝ち): ①組込デフォルト → ②プロジェクト共通
	// input_defaults.jsonc → ③画面別 "input"."bindings"。 view settings も
	// 同順 (②を先に適用し、 ③は下の apply_input が上書き)。
	const input_defaults_data* gdef = global_input_defaults(resource_base);
	if (gdef && gdef->apply_settings) gdef->apply_settings(*_impl->view);
	_impl->apply_action_bindings({
		&builtin_default_bindings(),
		gdef ? &gdef->actions.bindings : nullptr,
		&layout.actions.bindings });
	_impl->merge_se({
		gdef ? &gdef->actions.se : nullptr,
		&layout.actions.se });

	// JSON "input" ブロックの設定 (arrow_focus_nav / pad mode / bind 等) を適用。
	// id 解決に内部 element map を使うため content() 後でないと駄目。
	// legacy "shortcuts" / tab_view の登録は action バインドの後に走るので、
	// 同一入力に対する明示宣言が優先される。
	if (layout.apply_input) layout.apply_input(*_impl->view);

	// JSON "initial_focus": true 指定があればフォーカス。 view::focus() は
	// asio::post で次 idle へデファードされるので順序問題なし。
	if (layout.initial_focus) {
		_impl->view->focus(layout.initial_focus);
	}

	// "input":{"initial_focus":"<id>"} — id 指定の初期フォーカス。 要素側
	// フラグと併存した場合はこちらが勝つ (後から post されるため)。
	if (!layout.actions.initial_focus_id.empty()) {
		auto fit = _impl->id_map.find(layout.actions.initial_focus_id);
		if (fit != _impl->id_map.end()) {
			_impl->view->focus(fit->second);
		} else {
			em_logf("elements_modal: input.initial_focus id '%s' not found",
			        layout.actions.initial_focus_id.c_str());
		}
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

bool overlay_session::focus_consumes_text() const
{
	if (!_impl || !_impl->view) return false;
	return _impl->view->focus_wants_text_input();
}

void overlay_session::focus_by_id(const std::string& id)
{
	if (!_impl || !_impl->view || id.empty()) return;
	auto it = _impl->id_map.find(id);
	if (it == _impl->id_map.end()) {
		em_logf("overlay_session::focus_by_id: id '%s' not found",
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
	bool handled = on_key_down(ce::key_code::enter, 0);
	on_key_up(ce::key_code::enter, 0);
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

void overlay_session::set_var(const std::string& name, const std::string& value)
{
	if (!_impl) return;
	if (_impl->set_var_fn) _impl->set_var_fn(name, value);
	// set_text 済の label は次回 render_to_buffer (view->draw) で再描画される。
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

	// nav SE: focus 変化を検出して発火。 キーボード/dpad/stick/hover どの
	// 経路のフォーカス移動も focused_id_slot の変化として一元的に拾える。
	// 初期フォーカス (未観測→初observe) では鳴らさない。
	if (!_impl->se_map.empty() && _impl->focused_id_slot) {
		const std::string& cur = *_impl->focused_id_slot;
		if (_impl->se_focus_seen && cur != _impl->se_last_focused
		    && !cur.empty()) {
			_impl->play_se("nav");
		}
		_impl->se_last_focused = cur;
		_impl->se_focus_seen = true;
	}

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
		const std::uint64_t now = em_now_ms();
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
		// 描画密度は「呼出側が確保した buffer サイズ ÷ view logical サイズ」から
		// 毎回導出する (start() の pixel_scale 固定でなく)。 呼出側は buffer を
		// 最終 present サイズで確保すればその密度で直接ラスタライズでき、 縮小
		// present 前提の過剰レンダリングを避けられる。 従来どおり
		// view_w * pixel_scale で確保すれば従来と同じ倍率になる (後方互換)。
		const float render_scale = (_impl->view_w > 0)
			? static_cast<float>(buffer_w_px) / _impl->view_w
			: _impl->scale;
		ce::canvas cnv{ pixel_buffer,
		                static_cast<std::uint32_t>(buffer_w_px),
		                static_cast<std::uint32_t>(buffer_h_px),
		                render_scale };
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

void overlay_session::on_mouse_down(float sx, float sy, ce::mouse_button::what button, int mods)
{
	if (!active()) return;
	// mouse バインド (既定: 右click→cancel)。 マッチしたら action を発火して
	// クリック自体は消費する (widget へは流さない)。 "none" は消費のみ。
	if (auto it = _impl->mouse_actions.find(static_cast<int>(button));
	    it != _impl->mouse_actions.end()) {
		_impl->dispatch_action(it->second);
		return;
	}
	auto p = _impl->to_view(sx, sy);
	_impl->last_cursor = p;
	_impl->mouse_down = true;
	ce::mouse_button btn{
		.down = true,
		.num_clicks = 1,
		.state = button,
		.modifiers = mods,
		.pos = p
	};
	_impl->view->cursor(p, ce::cursor_tracking::hovering);
	_impl->view->click(btn);
}

void overlay_session::on_mouse_up(float sx, float sy, ce::mouse_button::what button, int mods)
{
	if (!active()) return;
	// mouse バインド対象ボタンは down 側で消費済みなので up も揃えて消費。
	if (_impl->mouse_actions.count(static_cast<int>(button))) {
		return;
	}
	auto p = _impl->to_view(sx, sy);
	_impl->last_cursor = p;
	_impl->mouse_down = false;
	ce::mouse_button btn{
		.down = false,
		.num_clicks = 1,
		.state = button,
		.modifiers = mods,
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
			.modifiers = mods,
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
	// wheel バインド (既定: up/down→scroll_up/down = 従来の view->scroll 素通し)。
	// scroll_* は生 delta のまま流して滑らかさを保存、 それ以外の action
	// (page_next 等) はノッチ 1 回分として発火、 "none" は消費のみ。
	const int dir = (dy > 0.0f) ? +1 : (dy < 0.0f ? -1 : 0);
	if (dir != 0) {
		if (auto it = _impl->wheel_actions.find(dir);
		    it != _impl->wheel_actions.end()) {
			const std::string& act = it->second;
			if (act == "none") return;
			if (act == "scroll_up" || act == "scroll_down") {
				_impl->view->scroll(ce::point{ dx, dy }, p);
				return;
			}
			_impl->dispatch_action(act);
			return;
		}
	}
	_impl->view->scroll(ce::point{ dx, dy }, p);
}

void overlay_session::on_mouse_leave()
{
	if (!active()) return;
	_impl->view->cursor(_impl->last_cursor, ce::cursor_tracking::leaving);
}

bool overlay_session::on_key_down(ce::key_code key, int mods)
{
	if (!active()) return false;
	// ESC の直接 begin_finish (hard-code) は撤廃。 既定バインド
	// escape→cancel が view の key shortcut (force=true) として登録されて
	// いるので、 view->key 経由で同じ「閉じる」に到達する (画面 JSON /
	// input_defaults.jsonc で差し替え・無効化可能)。
	ce::key_info ki{
		.key = key,
		.action = ce::key_action::press,
		.modifiers = mods
	};
	return _impl->view->key(ki);   // focus widget / shortcut が処理したら true
}

bool overlay_session::on_key_up(ce::key_code key, int mods)
{
	if (!active()) return false;
	ce::key_info ki{
		.key = key,
		.action = ce::key_action::release,
		.modifiers = mods
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

bool overlay_session::on_pad_button(ce::pad_button button, bool down)
{
	if (!active()) return false;
	if (button == ce::pad_button::unknown) return false;
	_impl->view->pad_button_event({button, down});
	return true;   // 既知のパッドボタンは UI が消費 (UI 操作中はゲームへ通さない)
}

void overlay_session::on_pad_axis(ce::pad_axis axis, float value)
{
	if (!active()) return;
	if (axis == ce::pad_axis::unknown) return;
	if (value < -1.0f) value = -1.0f;
	if (value >  1.0f) value =  1.0f;
	_impl->view->pad_axis_event({axis, value});
}

} // namespace elements_modal

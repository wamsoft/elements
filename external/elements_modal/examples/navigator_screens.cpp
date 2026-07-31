//---------------------------------------------------------------------------
// elements_modal: navigator (画面遷移) の standalone 使用例
//
// hello_modal が単一モーダル (run_modal) を示すのに対し、 こちらは
// overlay_session + navigator で **複数 JSON 画面を 1 ウィンドウ内で遷移**する
// 最小ホストを示す。 elements_console (= examples/console_screens) の main.cpp を
// ライブラリ機能だけに削いだ姿。
//
// ビルド: -DELEMENTS_MODAL_BUILD_EXAMPLES=ON で生成される
//   elements_modal_navigator_example 実行ファイル。
//
// 動作: menu → settings / about を push / pop で遷移 (settings は fade)。
//   各画面の戻り (B / Esc / 右クリック / Back ボタン) は navigator が解決する。
//   menu の Quit / Esc で終了。 遷移は stdout にログされる。
//
// ポイント (navigator の使いどころ):
//   - transitions の解釈 (<back>/<exit>/fade) と stack 操作は navigator 任せ。
//   - 画面ソースはここでは inline JSON のマップ。 マニフェスト (ファイル) を
//     使う場合は navigator(parse_app_manifest(...)) + screen_file() を使う。
//   - fade の混色は effects.h の blend_argb8888()。
//   - 画面ごとの focus 記憶 / 復元も navigator が保持する。
//---------------------------------------------------------------------------
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <elements.hpp>
#include <elements_modal/modal.h>
#include <elements_modal/navigator.h>
#include <elements_modal/effects.h>
#include <elements_modal/sdl_input.h>   // SDL イベント → cycfi 入力型 (host アダプタ)

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kW = 800;
constexpr int kH = 600;

// 画面名 → inline JSON。 実アプリではファイル (manifest) から読むことが多いが、
// navigator はファイルに依存しない: ここでは単純なマップで解決する。
const std::map<std::string, std::string>& screens()
{
	static const std::map<std::string, std::string> m = {
		{ "menu", R"json({
			"background": [28, 30, 40, 255],
			"input": { "arrow_focus_nav": true },
			"transitions": {
				"settings": { "target": "settings", "effect": "fade", "duration": 200 },
				"about":    "about",
				"quit":     "<exit>",
				"":         "<exit>"
			},
			"content": { "type": "margin", "padding": 40, "child": {
				"type": "vtile", "children": [
					{ "type": "hspacer", "width": 520 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "elements_modal — navigator demo", "size": 28 } },
					{ "type": "vspacer", "height": 24 },
					{ "type": "vsize", "height": 44, "child": {
						"type": "invert_button", "id": "settings", "text": "Settings (fade)",
						"close_on_click": true, "initial_focus": true } },
					{ "type": "vspacer", "height": 10 },
					{ "type": "vsize", "height": 44, "child": {
						"type": "invert_button", "id": "about", "text": "About",
						"close_on_click": true } },
					{ "type": "vspacer", "height": 10 },
					{ "type": "vsize", "height": 44, "child": {
						"type": "ring_button", "id": "quit", "text": "Quit",
						"outline": [200, 80, 80, 255], "close_on_click": true } },
					{ "type": "vspacer", "height": 16 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "Esc / B / right-click = quit", "size": 16 } }
				] } }
		})json" },

		{ "settings", R"json({
			"background": [40, 30, 30, 255],
			"input": { "arrow_focus_nav": true },
			"transitions": { "back": "<back>", "": "<back>" },
			"content": { "type": "margin", "padding": 40, "child": {
				"type": "vtile", "children": [
					{ "type": "hspacer", "width": 520 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "Settings", "size": 28 } },
					{ "type": "vspacer", "height": 24 },
					{ "type": "align_left", "child": {
						"type": "checkbox", "id": "fullscreen", "text": "Fullscreen", "value": false } },
					{ "type": "vspacer", "height": 24 },
					{ "type": "vsize", "height": 44, "child": {
						"type": "invert_button", "id": "back", "text": "Back",
						"close_on_click": true, "initial_focus": true } },
					{ "type": "vspacer", "height": 16 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "Esc / B / right-click / Back = return", "size": 16 } }
				] } }
		})json" },

		{ "about", R"json({
			"background": [30, 40, 30, 255],
			"input": { "arrow_focus_nav": true },
			"transitions": { "back": "<back>", "": "<back>" },
			"content": { "type": "margin", "padding": 40, "child": {
				"type": "vtile", "children": [
					{ "type": "hspacer", "width": 520 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "About", "size": 28 } },
					{ "type": "vspacer", "height": 16 },
					{ "type": "align_center", "child": {
						"type": "label", "text": "Driven by elements_modal::navigator.", "size": 18 } },
					{ "type": "vspacer", "height": 24 },
					{ "type": "vsize", "height": 44, "child": {
						"type": "invert_button", "id": "back", "text": "Back",
						"close_on_click": true, "initial_focus": true } }
				] } }
		})json" }
	};
	return m;
}

struct AppState
{
	SDL_Window*   window   = nullptr;
	SDL_Renderer* renderer = nullptr;
	SDL_Texture*  texture  = nullptr;

	std::vector<std::uint32_t> staging;          // kW * kH ARGB8888
	std::unique_ptr<elements_modal::overlay_session> session;
	elements_modal::navigator  nav;              // 画面遷移ドライバ
	bool quit = false;

	// fade 遷移用 (旧フレーム snapshot)。
	std::vector<std::uint32_t> fade_from;
	Uint64 fade_start_ms = 0;
	int    fade_duration_ms = 0;
};

// nav.current() の画面 JSON で overlay_session を作り直す。
bool start_current(AppState& app)
{
	if (app.nav.empty()) { app.quit = true; return false; }
	const std::string& name = app.nav.current();
	auto it = screens().find(name);
	if (it == screens().end()) {
		SDL_Log("no such screen: %s", name.c_str());
		app.quit = true;
		return false;
	}
	app.session = std::make_unique<elements_modal::overlay_session>();
	if (!app.session->start(it->second, kW, kH, 1.0f)) {
		SDL_Log("overlay_session::start failed for %s", name.c_str());
		app.quit = true;
		return false;
	}
	// navigator が覚えている前回 focus を復元 (無ければ initial_focus のまま)。
	const std::string& fid = app.nav.focus_to_restore(name);
	if (!fid.empty()) app.session->focus_by_id(fid);

	SDL_Log(">>> enter screen: %s", name.c_str());
	return true;
}

// session 完了 → navigator で次画面を決め、 必要なら fade を仕込む。
void on_finished(AppState& app)
{
	const std::string current = app.nav.current();
	const auto& r = app.session->get_result();
	SDL_Log("=== %s closed (action=\"%s\") ===", current.c_str(), r.action.c_str());

	// 現在 focus を記録 (再入時に復元)。
	app.nav.remember_focus(current, app.session->focused_id());

	// transitions を解決してスタックを進める。
	elements_modal::nav_step step =
		app.nav.advance(r.action, app.session->transitions());

	// fade: 旧画面の最終フレームを snapshot し、 次画面と時間で混色する。
	if (step.effect == "fade" && !app.nav.empty()) {
		app.fade_from        = app.staging;     // copy
		app.fade_start_ms    = SDL_GetTicks();
		app.fade_duration_ms = step.duration_ms > 0 ? step.duration_ms : 200;
	}

	app.session.reset();
	if (app.nav.empty()) app.quit = true;
}

} // anonymous

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char** /*argv*/)
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif

	auto app = std::make_unique<AppState>();
	app->staging.assign(std::size_t(kW) * kH, 0u);

	app->window = SDL_CreateWindow("elements_modal navigator demo", kW, kH, 0);
	if (!app->window) { SDL_Log("CreateWindow: %s", SDL_GetError()); return SDL_APP_FAILURE; }
	app->renderer = SDL_CreateRenderer(app->window, nullptr);
	if (!app->renderer) { SDL_Log("CreateRenderer: %s", SDL_GetError()); return SDL_APP_FAILURE; }
	app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888,
	                                 SDL_TEXTUREACCESS_STREAMING, kW, kH);
	if (!app->texture) { SDL_Log("CreateTexture: %s", SDL_GetError()); return SDL_APP_FAILURE; }

	// マニフェスト無し (inline JSON マップで解決) なので entry を直接指定。
	app->nav.reset_to("menu");
	if (!start_current(*app)) return SDL_APP_FAILURE;

	*appstate = app.release();
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	auto* app = static_cast<AppState*>(appstate);
	if (!app) return SDL_APP_CONTINUE;

	if (event->type == SDL_EVENT_QUIT) { app->quit = true; return SDL_APP_SUCCESS; }

	if (event->type == SDL_EVENT_GAMEPAD_ADDED) { SDL_OpenGamepad(event->gdevice.which); }

	if (!app->session || app->session->finished()) return SDL_APP_CONTINUE;
	auto& sess = *app->session;

	// SDL イベント生値 → cycfi 中立型は host アダプタ (sdl_input) が担う。
	// overlay_session 自体は SDL に依存しない。
	namespace si = elements_modal::sdl_input;
	switch (event->type) {
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (event->button.button == SDL_BUTTON_RIGHT) { sess.close(""); break; }
			sess.on_mouse_down(event->button.x, event->button.y,
			                   si::mouse_button(event->button.button),
			                   si::mods(SDL_GetModState()));
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			sess.on_mouse_up(event->button.x, event->button.y,
			                 si::mouse_button(event->button.button),
			                 si::mods(SDL_GetModState()));
			break;
		case SDL_EVENT_MOUSE_MOTION:
			sess.on_mouse_move(event->motion.x, event->motion.y,
			                   si::mods(SDL_GetModState()));
			break;
		case SDL_EVENT_KEY_DOWN:
			if (event->key.key == SDLK_ESCAPE) { sess.close(""); break; }
			sess.on_key_down(si::key(event->key.key),
			                 si::mods(SDL_GetModState()));
			break;
		case SDL_EVENT_KEY_UP:
			sess.on_key_up(si::key(event->key.key),
			               si::mods(SDL_GetModState()));
			break;
		case SDL_EVENT_TEXT_INPUT:
			sess.on_text_input(event->text.text);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			if (event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) { sess.close(""); break; }
			sess.on_pad_button(si::pad_button(event->gbutton.button), true);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			sess.on_pad_button(si::pad_button(event->gbutton.button), false);
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			sess.on_pad_axis(si::pad_axis(event->gaxis.axis),
			                 si::axis_value(event->gaxis.value));
			break;
		default: break;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	auto* app = static_cast<AppState*>(appstate);
	if (!app) return SDL_APP_FAILURE;
	if (app->quit) return SDL_APP_SUCCESS;

	// session が終わっていれば遷移 → 次画面 start。
	if (app->session && app->session->finished()) {
		on_finished(*app);
		if (app->quit) return SDL_APP_SUCCESS;
		if (!start_current(*app)) return app->quit ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
	}
	if (!app->session) return SDL_APP_SUCCESS;

	elements_modal::overlay_session::render_rect rect;
	bool drew = app->session->render_to_buffer(app->staging.data(), kW, kH, kW, kH, rect);

	// fade 中なら旧フレームと混色。
	if (drew && !app->fade_from.empty()) {
		Uint64 now = SDL_GetTicks();
		Uint64 elapsed = (now > app->fade_start_ms) ? (now - app->fade_start_ms) : 0;
		if (static_cast<int>(elapsed) >= app->fade_duration_ms) {
			app->fade_from.clear();
		} else {
			float t = app->fade_duration_ms > 0
			            ? float(elapsed) / float(app->fade_duration_ms) : 1.0f;
			elements_modal::blend_argb8888(app->fade_from.data(), app->staging.data(),
			                               t, app->staging.data(),
			                               std::size_t(kW) * kH);
		}
	}

	if (drew) SDL_UpdateTexture(app->texture, nullptr, app->staging.data(), kW * 4);

	SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
	SDL_RenderClear(app->renderer);
	if (drew) SDL_RenderTexture(app->renderer, app->texture, nullptr, nullptr);
	SDL_RenderPresent(app->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
	if (auto* app = static_cast<AppState*>(appstate)) {
		app->session.reset();
		if (app->texture)  SDL_DestroyTexture(app->texture);
		if (app->renderer) SDL_DestroyRenderer(app->renderer);
		if (app->window)   SDL_DestroyWindow(app->window);
		delete app;
	}
	elements_modal::shutdown();
	SDL_Quit();
}

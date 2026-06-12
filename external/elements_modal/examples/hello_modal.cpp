//---------------------------------------------------------------------------
// elements_modal: standalone 使用例
//
// 純粋 SDL3 + elements_modal の最小サンプル。 elements_modal を単体ライブラリ
// として他プロジェクトに組み込んだ場合の典型的な使い方を示す。
//
// ビルド: -DELEMENTS_MODAL_BUILD_EXAMPLES=ON で生成される
//   elements_modal_hello_example 実行ファイル。
//
// 動作: モーダルダイアログをポップアップで表示。 OK / Cancel ボタンと、
//   チェックボックス + 名前入力ボックスを持つ。 閉じた後、 結果を stdout に出力。
//---------------------------------------------------------------------------
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <elements.hpp>
#include <elements_modal/modal.h>

#include <cstdio>
#include <string>
#include <variant>

namespace {

const char* kSampleJson = R"json({
	"background": [40, 40, 50, 255],
	"content": {
		"type": "margin", "padding": 20,
		"child": {
			"type": "vtile",
			"children": [
				{ "type": "hspacer", "width": 420 },
				{ "type": "align_center",
				  "child": { "type": "label",
				             "text": "elements_modal demo",
				             "size": 1.3 } },
				{ "type": "group", "title": "Options",
				  "child": { "type": "margin", "padding": [10, 45, 10, 10],
				             "child": { "type": "vtile",
				                        "children": [
				                            { "type": "align_left",
				                              "child": { "type": "checkbox",
				                                         "id": "agree",
				                                         "text": "I agree",
				                                         "value": false } },
				                            { "type": "vsize", "height": 30,
				                              "child": { "type": "input_box",
				                                         "id": "name",
				                                         "placeholder": "Your name..." } },
				                            { "type": "vspacer", "height": 8 },
				                            { "type": "labeled_row",
				                              "label": "Difficulty",
				                              "label_width": 110,
				                              "child": { "type": "cycle_picker",
				                                         "id": "difficulty",
				                                         "options": ["Easy", "Normal", "Hard", "Nightmare"],
				                                         "initial": 1 } },
				                            { "type": "vspacer", "height": 8 },
				                            { "type": "labeled_row",
				                              "label": "Mode",
				                              "label_width": 110,
				                              "child": { "type": "segmented_picker",
				                                         "id": "mode",
				                                         "options": ["A", "B", "C"],
				                                         "initial": 0 } },
				                            { "type": "vspacer", "height": 8 },
				                            { "type": "labeled_row",
				                              "label": "Volume",
				                              "label_width": 110,
				                              "child": { "type": "slider_with_range",
				                                         "id": "volume",
				                                         "min": 0, "max": 100,
				                                         "initial": 50 } }
				                        ] } } },
				{ "type": "align_right",
				  "child": { "type": "htile",
				             "children": [
				                 { "type": "hsize", "width": 110,
				                   "child": { "type": "invert_button",
				                              "id": "ok", "text": "OK",
				                              "close_on_click": true,
				                              "initial_focus": true } },
				                 { "type": "hspacer", "width": 8 },
				                 { "type": "hsize", "width": 110,
				                   "child": { "type": "ring_button",
				                              "id": "cancel", "text": "Cancel",
				                              "outline": [200, 80, 80, 255],
				                              "close_on_click": true } }
				             ] } }
			]
		}
	}
})json";

void print_value(const elements_modal::value_t& v)
{
	std::visit([](auto&& val) {
		using T = std::decay_t<decltype(val)>;
		if constexpr (std::is_same_v<T, bool>) {
			std::printf("%s", val ? "true" : "false");
		} else if constexpr (std::is_same_v<T, std::int64_t>) {
			std::printf("%lld", static_cast<long long>(val));
		} else if constexpr (std::is_same_v<T, double>) {
			std::printf("%g", val);
		} else if constexpr (std::is_same_v<T, std::string>) {
			std::printf("\"%s\"", val.c_str());
		}
	}, v);
}

} // anonymous

SDL_AppResult SDL_AppInit(void** /*appstate*/, int /*argc*/, char** /*argv*/)
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	// elements_modal はフォント登録を行わないので、 example 側で elements の
	// load_fonts_from_directory を呼ぶ。 パスは親プロジェクト (elements リポ)
	// 側の CMake で絶対パスを compile def 経由で注入する。
#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif

	elements_modal::config cfg;
	cfg.title_utf8 = "elements_modal Hello";
	cfg.width  = 480;
	cfg.height = 360;
	cfg.parent = nullptr;   // standalone — モーダル親なし

	elements_modal::result r;
	if (!elements_modal::run_modal(kSampleJson, cfg, r)) {
		std::printf("run_modal failed\n");
		return SDL_APP_FAILURE;
	}

	std::printf("=== Modal closed ===\n");
	std::printf("action: \"%s\"\n", r.action.c_str());
	std::printf("values:\n");
	for (const auto& kv : r.values) {
		std::printf("  %s = ", kv.first.c_str());
		print_value(kv.second);
		std::printf("\n");
	}

	elements_modal::shutdown();
	return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void* /*appstate*/, SDL_Event* /*event*/)
{
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* /*appstate*/)
{
	// AppInit で modal を回し終わって SDL_APP_SUCCESS を返すので、
	// 通常ここには来ない。 念のため終了させる。
	return SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* /*appstate*/, SDL_AppResult /*result*/)
{
	SDL_Quit();
}

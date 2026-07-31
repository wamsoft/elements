//---------------------------------------------------------------------------
// elements_modal: overlay_session を SDL / Win32 どちらのホストからでも動かす
// クロスホストのサンプル。
//
// 目的: overlay_session の入力 API が host 非依存 (cycfi 中立型) になったこと、
//       および同じ overlay ロジックが SDL host build でも Win32 host build でも
//       動くことを示す。 native (SDL / Win32) → cycfi のマッピングは host
//       アダプタ (sdl_input.h / win32_input.h) が担い、 overlay_session 本体は
//       どちらにも依存しない。
//
// ホスト分岐は cycfi が定義する ELEMENTS_HOST_UI_LIBRARY_{SDL,WIN32} で行う
// (cycfi::elements のビルド host に一致)。 共有部 (ダイアログ JSON、 セッション
// 生成、 バッファへの描画) は両 host で全く同一。
//
// ビルド: -DELEMENTS_BUILD_EXAMPLES=ON。
//   SDL host build   → SDL_Window + SDL_Renderer で present。
//   Win32 host build → HWND + StretchDIBits で present (SDL3 非依存)。
//---------------------------------------------------------------------------
#include <elements.hpp>
#include <elements_modal/modal.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kW = 640;
constexpr int kH = 480;

// 両 host 共通のダイアログ JSON (1 画面)。
const char* kDialogJson = R"json({
	"background": [26, 28, 38, 255],
	"input": { "arrow_focus_nav": true },
	"content": { "type": "margin", "padding": 40, "child": {
		"type": "vtile", "children": [
			{ "type": "hspacer", "width": 560 },
			{ "type": "align_center", "child": {
				"type": "label", "text": "elements_modal — cross-host overlay", "size": 26 } },
			{ "type": "vspacer", "height": 20 },
			{ "type": "align_center", "child": {
				"type": "label", "text": "same overlay code on SDL and Win32 hosts", "size": 16 } },
			{ "type": "vspacer", "height": 28 },
			{ "type": "align_left", "child": {
				"type": "checkbox", "id": "opt", "text": "an option", "value": false } },
			{ "type": "vspacer", "height": 24 },
			{ "type": "vsize", "height": 44, "child": {
				"type": "invert_button", "id": "ok", "text": "OK",
				"close_on_click": true, "initial_focus": true } },
			{ "type": "vspacer", "height": 16 },
			{ "type": "align_center", "child": {
				"type": "label", "text": "Esc / OK = close", "size": 14 } }
		] } }
})json";

// overlay_session を生成して開始する (両 host 共通)。
std::unique_ptr<elements_modal::overlay_session> make_session()
{
	auto s = std::make_unique<elements_modal::overlay_session>();
	if (!s->start(kDialogJson, kW, kH, 1.0f))
		return nullptr;
	return s;
}

// staging (ARGB8888 = メモリ上 BGRA) を top-down 32bpp BMP として書き出す。
// ヘッダは手書き (struct packing 非依存)。 window を出さずに描画結果を確認する
// ヘッドレス検証 (--dump) 用。 両 host 共通。
bool write_bmp(const char* path, const std::uint32_t* px, int w, int h)
{
	std::FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	auto put16 = [&](unsigned v){ std::fputc(v & 0xff, f); std::fputc((v >> 8) & 0xff, f); };
	auto put32 = [&](unsigned v){ for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 0xff, f); };
	const unsigned data_size = unsigned(w) * unsigned(h) * 4u;
	// BITMAPFILEHEADER (14)
	std::fputc('B', f); std::fputc('M', f);
	put32(14 + 40 + data_size); put16(0); put16(0); put32(14 + 40);
	// BITMAPINFOHEADER (40) — biHeight 負 = top-down
	put32(40); put32(unsigned(w)); put32(unsigned(-h)); put16(1); put16(32);
	put32(0); put32(data_size); put32(2835); put32(2835); put32(0); put32(0);
	std::fwrite(px, 1, data_size, f);
	std::fclose(f);
	return true;
}

// window を出さずに 1 フレーム描画し BMP に保存する (--dump)。
int dump_frame(const char* out_path)
{
	auto session = make_session();
	if (!session) return 2;
	std::vector<std::uint32_t> staging(std::size_t(kW) * kH, 0u);
	elements_modal::overlay_session::render_rect rect;
	bool drew = session->render_to_buffer(staging.data(), kW, kH, kW, kH, rect);
	if (!drew) return 3;
	return write_bmp(out_path, staging.data(), kW, kH) ? 0 : 4;
}

} // anonymous

//===========================================================================
// SDL host 実装
//===========================================================================
#if defined(ELEMENTS_HOST_UI_LIBRARY_SDL)

#include <SDL3/SDL.h>
#include <elements_modal/sdl_input.h>

int main(int argc, char** argv)
{
#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif
	// ヘッドレス検証: --dump <path.bmp> で 1 フレーム描画して終了 (window 不要)。
	if (argc >= 3 && std::strcmp(argv[1], "--dump") == 0)
		return dump_frame(argv[2]);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return 1;
	}

	SDL_Window*   window   = SDL_CreateWindow("cross-host overlay (SDL)", kW, kH, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
	SDL_Texture*  texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
	                                            SDL_TEXTUREACCESS_STREAMING, kW, kH);

	std::vector<std::uint32_t> staging(std::size_t(kW) * kH, 0u);
	auto session = make_session();
	namespace si = elements_modal::sdl_input;

	bool running = (session != nullptr);
	while (running) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			auto& sess = *session;
			switch (ev.type) {
				case SDL_EVENT_QUIT: running = false; break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					sess.on_mouse_down(ev.button.x, ev.button.y,
					                   si::mouse_button(ev.button.button), si::mods(SDL_GetModState()));
					break;
				case SDL_EVENT_MOUSE_BUTTON_UP:
					sess.on_mouse_up(ev.button.x, ev.button.y,
					                 si::mouse_button(ev.button.button), si::mods(SDL_GetModState()));
					break;
				case SDL_EVENT_MOUSE_MOTION:
					sess.on_mouse_move(ev.motion.x, ev.motion.y, si::mods(SDL_GetModState()));
					break;
				case SDL_EVENT_KEY_DOWN:
					sess.on_key_down(si::key(ev.key.key), si::mods(SDL_GetModState()));
					break;
				case SDL_EVENT_KEY_UP:
					sess.on_key_up(si::key(ev.key.key), si::mods(SDL_GetModState()));
					break;
				case SDL_EVENT_TEXT_INPUT:
					sess.on_text_input(ev.text.text);
					break;
				default: break;
			}
		}
		if (session->finished()) { running = false; break; }

		elements_modal::overlay_session::render_rect rect;
		bool drew = session->render_to_buffer(staging.data(), kW, kH, kW, kH, rect);
		if (drew) SDL_UpdateTexture(texture, nullptr, staging.data(), kW * 4);

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		if (drew) SDL_RenderTexture(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);
		SDL_Delay(16);
	}

	session.reset();
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

//===========================================================================
// Win32 host 実装
//===========================================================================
#elif defined(ELEMENTS_HOST_UI_LIBRARY_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <elements_modal/win32_input.h>

namespace {

struct WinApp
{
	std::unique_ptr<elements_modal::overlay_session> session;
	std::vector<std::uint32_t> staging;
	bool finished = false;
};

// staging (ARGB8888 = メモリ上 BGRA) を top-down DIB として client 全面へ present。
void present(HWND hwnd, WinApp& app)
{
	elements_modal::overlay_session::render_rect rect;
	bool drew = app.session &&
		app.session->render_to_buffer(app.staging.data(), kW, kH, kW, kH, rect);
	if (!drew) return;

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = kW;
	bmi.bmiHeader.biHeight      = -kH;   // top-down
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	RECT cr;
	GetClientRect(hwnd, &cr);
	HDC dc = GetDC(hwnd);
	SetStretchBltMode(dc, HALFTONE);
	StretchDIBits(dc,
		0, 0, cr.right - cr.left, cr.bottom - cr.top,
		0, 0, kW, kH,
		app.staging.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
	ReleaseDC(hwnd, dc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	auto* app = reinterpret_cast<WinApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	namespace wi = elements_modal::win32_input;

	switch (msg) {
		case WM_DESTROY: PostQuitMessage(0); return 0;
		case WM_PAINT: {
			PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
			if (app) present(hwnd, *app);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_LBUTTONDOWN:
			if (app && app->session)
				app->session->on_mouse_down((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
				                            wi::mouse_left(), wi::mods());
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
			if (app && app->session)
				app->session->on_mouse_up((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
				                          wi::mouse_left(), wi::mods());
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_MOUSEMOVE:
			if (app && app->session)
				app->session->on_mouse_move((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), wi::mods());
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_KEYDOWN:
			if (app && app->session)
				app->session->on_key_down(wi::key((unsigned)wp), wi::mods());
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_KEYUP:
			if (app && app->session)
				app->session->on_key_up(wi::key((unsigned)wp), wi::mods());
			return 0;
		case WM_CHAR:
			if (app && app->session && wp >= 0x20) {
				char utf8[8]; int n = WideCharToMultiByte(CP_UTF8, 0,
					reinterpret_cast<wchar_t*>(&wp), 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
				utf8[n] = '\0';
				app->session->on_text_input(utf8);
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

} // anonymous

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int nShow)
{
#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif
	// ヘッドレス検証: --dump <path.bmp> で 1 フレーム描画して終了 (window 不要)。
	if (lpCmdLine) {
		if (const char* d = std::strstr(lpCmdLine, "--dump")) {
			const char* p = d + 6;
			while (*p == ' ' || *p == '"') ++p;
			std::string path(p);
			while (!path.empty() && (path.back() == ' ' || path.back() == '"'))
				path.pop_back();
			return dump_frame(path.c_str());
		}
	}

	WinApp app;
	app.staging.assign(std::size_t(kW) * kH, 0u);
	app.session = make_session();
	if (!app.session) return 1;

	WNDCLASS wc{};
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = hInst;
	wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = TEXT("ElementsModalCrossHost");
	RegisterClass(&wc);

	RECT r{ 0, 0, kW, kH };
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindow(wc.lpszClassName, TEXT("cross-host overlay (Win32)"),
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
	SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
	ShowWindow(hwnd, nShow);

	MSG msg;
	bool running = true;
	while (running) {
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) { running = false; break; }
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (!running) break;
		if (app.session && app.session->finished()) break;
		// アニメ (フォーカスリング等) を進めるため一定間隔で再描画。
		InvalidateRect(hwnd, nullptr, FALSE);
		Sleep(16);
	}
	app.session.reset();
	DestroyWindow(hwnd);
	return 0;
}

#else
#  error "overlay_crosshost sample requires ELEMENTS_HOST_UI_LIBRARY_{SDL,WIN32}"
#endif

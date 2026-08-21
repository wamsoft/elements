//---------------------------------------------------------------------------
// elements_modal: レイアウト JSON をヘッドレスで駆動してスクリーンショットを
// 撮る検証ツール。 window を作らず overlay_session を直接回すので、 描画結果の
// 目視確認 (キャレット位置、 レイアウト崩れ等) をコマンドラインで再現できる。
//
// 使い方:
//   layout_shot <layout.json> <out_dir> [イベント...]
// イベント (発火フレーム @F 付き、 複数指定可):
//   --click X Y F     … フレーム F で (X,Y) を左クリック (down+up)
//   --text STR F      … フレーム F で文字列入力
//   --key CODE F      … フレーム F でキー down+up (10進 key_code 値)
//   --var NAME VAL F  … フレーム F で set_var(NAME, VAL)
//   --cap F           … フレーム F 描画後に out_dir/shot_F.bmp を保存
//   --frames N        … 駆動フレーム数 (既定 30)
// 時間は 1 フレーム 16ms の決定的クロック。
//---------------------------------------------------------------------------
#include <elements.hpp>
#include <elements_modal/modal.h>
#include "../src/em_platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ce = cycfi::elements;

namespace {

constexpr int kW = 480;
constexpr int kH = 360;

std::uint64_t g_now_ms = 100000;

bool write_bmp(const char* path, const std::uint32_t* px, int w, int h)
{
	std::FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	auto put16 = [&](unsigned v){ std::fputc(v & 0xff, f); std::fputc((v >> 8) & 0xff, f); };
	auto put32 = [&](unsigned v){ for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 0xff, f); };
	const unsigned data_size = unsigned(w) * unsigned(h) * 4u;
	std::fputc('B', f); std::fputc('M', f);
	put32(14 + 40 + data_size); put16(0); put16(0); put32(14 + 40);
	put32(40); put32(unsigned(w)); put32(unsigned(-h)); put16(1); put16(32);
	put32(0); put32(data_size); put32(2835); put32(2835); put32(0); put32(0);
	std::fwrite(px, 1, data_size, f);
	std::fclose(f);
	return true;
}

struct event
{
	enum class kind { click, text, key, var, cap };
	kind        what;
	int         frame = 0;
	float       x = 0, y = 0;
	std::string str;
	std::string str2;
	int         key = 0;
};

} // anonymous

int main(int argc, char** argv)
{
#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif
	if (argc < 3) {
		std::fprintf(stderr, "usage: layout_shot <layout.json> <out_dir> [events...]\n");
		return 2;
	}
	std::ifstream in(argv[1], std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "layout_shot: cannot open %s\n", argv[1]);
		return 2;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string json = ss.str();
	const std::string out_dir = argv[2];

	std::vector<event> events;
	int frames = 30;
	for (int i = 3; i < argc; ++i) {
		auto need = [&](int n) { return i + n < argc; };
		if (!std::strcmp(argv[i], "--click") && need(3)) {
			event e; e.what = event::kind::click;
			e.x = float(std::atof(argv[i+1])); e.y = float(std::atof(argv[i+2]));
			e.frame = std::atoi(argv[i+3]); i += 3;
			events.push_back(e);
		} else if (!std::strcmp(argv[i], "--text") && need(2)) {
			event e; e.what = event::kind::text;
			e.str = argv[i+1]; e.frame = std::atoi(argv[i+2]); i += 2;
			events.push_back(e);
		} else if (!std::strcmp(argv[i], "--key") && need(2)) {
			event e; e.what = event::kind::key;
			e.key = std::atoi(argv[i+1]); e.frame = std::atoi(argv[i+2]); i += 2;
			events.push_back(e);
		} else if (!std::strcmp(argv[i], "--var") && need(3)) {
			event e; e.what = event::kind::var;
			e.str = argv[i+1]; e.str2 = argv[i+2];
			e.frame = std::atoi(argv[i+3]); i += 3;
			events.push_back(e);
		} else if (!std::strcmp(argv[i], "--cap") && need(1)) {
			event e; e.what = event::kind::cap;
			e.frame = std::atoi(argv[i+1]); i += 1;
			events.push_back(e);
		} else if (!std::strcmp(argv[i], "--frames") && need(1)) {
			frames = std::atoi(argv[i+1]); i += 1;
		} else {
			std::fprintf(stderr, "layout_shot: bad arg %s\n", argv[i]);
			return 2;
		}
	}

	elements_modal::em_set_clock(+[]() { return g_now_ms; });

	auto session = std::make_unique<elements_modal::overlay_session>();
	if (!session->start(json, kW, kH, 1.0f)) {
		std::fprintf(stderr, "layout_shot: session start failed\n");
		return 3;
	}

	std::vector<std::uint32_t> buf(std::size_t(kW) * kH, 0u);

	for (int frame = 0; frame < frames; ++frame) {
		g_now_ms += 16;
		for (const auto& e : events) {
			if (e.frame != frame) continue;
			switch (e.what) {
				case event::kind::click:
					session->on_mouse_move(e.x, e.y, 0);
					session->on_mouse_down(e.x, e.y, ce::mouse_button::left, 0);
					session->on_mouse_up(e.x, e.y, ce::mouse_button::left, 0);
					break;
				case event::kind::text:
					session->on_text_input(e.str.c_str());
					break;
				case event::kind::key:
					session->on_key_down(ce::key_code(e.key), 0);
					session->on_key_up(ce::key_code(e.key), 0);
					break;
				case event::kind::var:
					session->set_var(e.str, e.str2);
					break;
				case event::kind::cap:
					break;
			}
		}
		if (session->update() || true) {
			elements_modal::overlay_session::render_rect rect{};
			session->render_to_buffer(buf.data(), kW, kH, kW, kH, rect);
		}
		for (const auto& e : events) {
			if (e.what == event::kind::cap && e.frame == frame) {
				char path[1024];
				std::snprintf(path, sizeof(path), "%s/shot_%02d.bmp",
				              out_dir.c_str(), frame);
				write_bmp(path, buf.data(), kW, kH);
				std::printf("cap frame %d -> %s\n", frame, path);
			}
		}
	}
	return 0;
}

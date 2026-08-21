//---------------------------------------------------------------------------
// elements_modal: 部分再描画 (render_to_buffer_partial) の検証ツール。
//
// 同じレイアウト・同じ入力列で 2 つの overlay_session を lockstep で駆動し、
//   A = 毎フレーム全面 render_to_buffer (基準)
//   B = 実ホスト同様、 update() が true のフレームだけ render_to_buffer_partial
//       (false のフレームは前回 staging を提示 = バッファ据え置き)
// の staging を毎フレームピクセル比較する。 不一致 = 部分再描画の取りこぼし
// (ダーティ矩形の過小 / カリングの誤爆 / needs_render の立て漏れ)。
//
// 時間は em_set_clock で 1 フレーム 16ms の決定的な偽クロックに差し替える
// (演出の tick まで再現可能にするため)。 window を一切作らないヘッドレス構成
// なので CI / コマンドラインで回せる。 不一致が出た最初のフレームは A/B/差分
// マスクを BMP で書き出す。
//
// 使い方: elements_modal_partial_diff [出力ディレクトリ (既定 ".")] [追加レイアウト.json...]
//   追加レイアウトを与えると "custom" シナリオとして同じ入力列で検証する
//   (実アプリの画面構造を持ち込んで部分再描画の取りこぼしを機械検出する用)。
// 終了コード: 0 = 全シナリオ一致、 1 = 不一致あり、 2+ = 起動失敗。
//---------------------------------------------------------------------------
#include <elements.hpp>
#include <elements_modal/modal.h>
#include "../src/em_platform.h"   // em_set_clock (検証用の決定的クロック)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ce = cycfi::elements;

namespace {

constexpr int kW = 480;
constexpr int kH = 360;

// 決定的な偽クロック (1 フレーム = 16ms、 run_scenario がフレーム毎に進める)
std::uint64_t g_now_ms = 100000;

// シナリオ 1: 静的な重なり。
//  - initial_focus 付き button (開いた直後の遅延フォーカス適用を踏む)
//  - layer + floating で「フォーカス対象に重なる前面要素」を作る
//    (フォーカス矩形の部分再描画で、 重なっている前面要素も一緒に
//     描き直されることを検証する)
const char* kLayoutStatic = R"json({
	"background": [30, 30, 60, 255],
	"input": { "arrow_focus_nav": true },
	"content": { "type": "layer", "children": [
		{ "type": "floating", "at": [40, 200, 180, 60], "child": {
			"type": "box", "color": [200, 80, 80, 160] } },
		{ "type": "margin", "padding": 30, "child": {
			"type": "vtile", "gap": 10, "children": [
				{ "type": "hspacer", "width": 260 },
				{ "type": "button", "id": "a", "text": "Alpha", "initial_focus": true },
				{ "type": "button", "id": "b", "text": "Beta" },
				{ "type": "button", "id": "c", "text": "Gamma" }
			] } }
	] }
})json";

// シナリオ 2: enter 演出付きの重なり。 前面の floating 箱が画面外から
// フォーカスボタンの上へ move で入ってくる (linear で毎 tick 数 px 動く)。
// 演出完了 tick の最終位置もダーティにされることを検証する。
const char* kLayoutAnim = R"json({
	"background": [30, 30, 60, 255],
	"input": { "arrow_focus_nav": true },
	"content": { "type": "layer", "children": [
		{ "type": "floating", "at": [40, 200, 180, 60], "child": {
			"type": "box", "color": [200, 80, 80, 160],
			"animate": { "type": "move", "from": [-200, 0], "to": [0, 0],
			             "frames": 8, "easing": "linear" } } },
		{ "type": "margin", "padding": 30, "child": {
			"type": "vtile", "gap": 10, "children": [
				{ "type": "hspacer", "width": 260 },
				{ "type": "button", "id": "a", "text": "Alpha", "initial_focus": true },
				{ "type": "button", "id": "b", "text": "Beta" },
				{ "type": "button", "id": "c", "text": "Gamma" }
			] } }
	] }
})json";

// シナリオ 3: focus 演出。 initial_focus のボタン自身が focus 取得で拡大し、
// 隣へはみ出す (フォーカス移動で逆再生も発生)。 initial_focus →
// focus 演出 → 完了 tick、 という実ホストで観測された経路そのもの。
const char* kLayoutFocusAnim = R"json({
	"background": [30, 30, 60, 255],
	"input": { "arrow_focus_nav": true },
	"content": { "type": "margin", "padding": 30, "child": {
		"type": "vtile", "gap": 10, "children": [
			{ "type": "hspacer", "width": 260 },
			{ "type": "button", "id": "a", "text": "Alpha", "initial_focus": true,
			  "animate": { "type": "scale", "from": [1.0, 1.0], "to": [1.2, 1.2],
			               "frames": 6, "easing": "linear", "on": "focus" } },
			{ "type": "button", "id": "b", "text": "Beta",
			  "animate": { "type": "scale", "from": [1.0, 1.0], "to": [1.2, 1.2],
			               "frames": 6, "easing": "linear", "on": "focus" } },
			{ "type": "button", "id": "c", "text": "Gamma" }
		] } }
})json";

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

// 1 フレーム分の入力 (両セッションへ同一に流す)
struct stimulus
{
	enum class kind { none, key_down, key_up, mouse_move };
	kind         what = kind::none;
	ce::key_code key  = ce::key_code::unknown;
	float        x = 0, y = 0;
};

stimulus at_frame(int frame)
{
	using k = stimulus::kind;
	switch (frame) {
		// 0..9: 無入力 (開いた直後の initial_focus 遅延適用〜演出再生を踏む)
		case 10: return { k::key_down, ce::key_code::tab };
		case 11: return { k::key_up,   ce::key_code::tab };
		case 14: return { k::key_down, ce::key_code::down };
		case 15: return { k::key_up,   ce::key_code::down };
		case 18: return { k::key_down, ce::key_code::up };
		case 19: return { k::key_up,   ce::key_code::up };
		// hover (button 上へ移動 → 離脱)
		case 22: return { k::mouse_move, ce::key_code::unknown, 240.0f, 150.0f };
		case 26: return { k::mouse_move, ce::key_code::unknown, 10.0f, 10.0f };
		default: return {};
	}
}

void apply(elements_modal::overlay_session& s, const stimulus& st)
{
	switch (st.what) {
		case stimulus::kind::key_down:   s.on_key_down(st.key, 0); break;
		case stimulus::kind::key_up:     s.on_key_up(st.key, 0);   break;
		case stimulus::kind::mouse_move: s.on_mouse_move(st.x, st.y, 0); break;
		case stimulus::kind::none:       break;
	}
}

std::unique_ptr<elements_modal::overlay_session> make_session(const char* json)
{
	auto s = std::make_unique<elements_modal::overlay_session>();
	if (!s->start(json, kW, kH, 1.0f)) s.reset();
	return s;
}

// 1 シナリオを lockstep 駆動して不一致フレーム数を返す (-1 = 起動失敗)。
// buf_w/buf_h をビュー論理サイズ (kW×kH) より小さく渡すと縮小密度で
// レンダリングする (実ホストの「present サイズ直接ラスタライズ」相当。
// device px ↔ user 座標の変換を含むダーティ矩形経路の検証になる)。
int run_scenario(const char* name, const char* json, int frames,
                 const std::string& out_dir,
                 int buf_w = kW, int buf_h = kH)
{
	auto sess_full    = make_session(json);
	auto sess_partial = make_session(json);
	if (!sess_full || !sess_partial) {
		std::fprintf(stderr, "%s: session start failed\n", name);
		return -1;
	}

	const std::size_t npx = std::size_t(buf_w) * buf_h;
	std::vector<std::uint32_t> buf_full(npx, 0u);
	std::vector<std::uint32_t> buf_partial(npx, 0u);
	std::vector<std::uint32_t> buf_diff(npx, 0u);

	int bad_frames = 0;
	bool dumped = false;

	for (int frame = 0; frame < frames; ++frame) {
		g_now_ms += 16;   // 決定的な 1 フレーム

		const stimulus st = at_frame(frame);
		apply(*sess_full, st);
		apply(*sess_partial, st);

		const bool need_a = sess_full->update();
		const bool need_b = sess_partial->update();
		(void)need_a;

		elements_modal::overlay_session::render_rect rect{}, up{};
		// A: 常に全面 (基準)。
		if (!sess_full->render_to_buffer(buf_full.data(), buf_w, buf_h, kW, kH, rect)) {
			std::fprintf(stderr, "%s frame %d: full render failed\n", name, frame);
			return -1;
		}
		// B: 実ホストの手順 — 変化があったフレームだけ部分再描画。
		if (need_b) {
			if (!sess_partial->render_to_buffer_partial(
					buf_partial.data(), buf_w, buf_h, kW, kH, rect, up)) {
				std::fprintf(stderr, "%s frame %d: partial render failed\n", name, frame);
				return -1;
			}
		}

		// 比較
		int mism = 0, bl = buf_w, bt = buf_h, br = -1, bb = -1;
		for (int y = 0; y < buf_h; ++y) {
			const std::uint32_t* pa = buf_full.data()    + std::size_t(y) * buf_w;
			const std::uint32_t* pb = buf_partial.data() + std::size_t(y) * buf_w;
			for (int x = 0; x < buf_w; ++x) {
				if (pa[x] != pb[x]) {
					++mism;
					if (x < bl) bl = x;
					if (x > br) br = x;
					if (y < bt) bt = y;
					if (y > bb) bb = y;
				}
			}
		}

		std::printf("%s frame %2d: stim=%d need_b=%d partial=(%d,%d %dx%d) mismatch=%d",
			name, frame, int(st.what), int(need_b), up.x, up.y, up.w, up.h, mism);
		if (mism) {
			std::printf(" bbox=(%d,%d)-(%d,%d)", bl, bt, br, bb);
			++bad_frames;
			if (!dumped) {
				dumped = true;
				for (std::size_t i = 0; i < npx; ++i)
					buf_diff[i] = (buf_full[i] != buf_partial[i]) ? 0xffff00ffu
					            : (buf_full[i] & 0x00ffffffu) | 0x40000000u;
				const std::string base = out_dir + "/pd_" + name;
				write_bmp((base + "_full.bmp").c_str(),    buf_full.data(),    buf_w, buf_h);
				write_bmp((base + "_partial.bmp").c_str(), buf_partial.data(), buf_w, buf_h);
				write_bmp((base + "_diff.bmp").c_str(),    buf_diff.data(),    buf_w, buf_h);
				std::printf(" (dumped)");
			}
		}
		std::printf("\n");
	}
	return bad_frames;
}

} // anonymous

int main(int argc, char** argv)
{
#ifdef ELEMENTS_MODAL_DEMO_FONTS_DIR
	cycfi::elements::load_fonts_from_directory(ELEMENTS_MODAL_DEMO_FONTS_DIR);
#endif
	const std::string out_dir = (argc >= 2) ? argv[1] : ".";

	elements_modal::em_set_clock(+[]() { return g_now_ms; });

	// テキストランキャッシュは仕様として bit-identical でない (初見 =
	// アウトライン描画、 2 回目以降 = 整数スナップ付きビットマップ貼付)。
	// A/B がグローバルキャッシュを共有すると「A が初見を消費 → 同フレームの
	// B は 2 回目扱い」で経路が割れて偽陽性になるため、 検証では無効化して
	// 部分再描画ロジックそのものの比較に純化する。
#ifdef _WIN32
	_putenv_s("ELEMENTS_TEXTCACHE_OFF", "1");
#else
	setenv("ELEMENTS_TEXTCACHE_OFF", "1", 1);
#endif

	// 各シナリオは等倍と縮小密度 (present サイズ直接ラスタライズ相当) の
	// 両方で検証する。 縮小時は device px ↔ view 論理 px の変換を含む
	// ダーティ矩形/カリング経路が exercised される (等倍は恒等変換なので
	// 変換の欠落を検出できない)。
	int total_bad = 0;
	struct scen { const char* name; const char* json; };
	const scen scens[] = {
		{"static",     kLayoutStatic},
		{"anim",       kLayoutAnim},
		{"focus_anim", kLayoutFocusAnim},
	};
	for (auto const& s : scens) {
		const int r = run_scenario(s.name, s.json, 32, out_dir);
		if (r < 0) return 2;
		total_bad += r;
		const std::string sname = std::string(s.name) + "_s";
		const int rs = run_scenario(sname.c_str(), s.json, 32, out_dir,
		                            kW * 3 / 4, kH * 3 / 4);
		if (rs < 0) return 2;
		total_bad += rs;
	}

	for (int a = 2; a < argc; ++a) {
		std::FILE* f = std::fopen(argv[a], "rb");
		if (!f) {
			std::fprintf(stderr, "partial_diff: cannot open %s\n", argv[a]);
			return 2;
		}
		std::string json;
		char tmp[4096];
		for (std::size_t n; (n = std::fread(tmp, 1, sizeof(tmp), f)) > 0; )
			json.append(tmp, n);
		std::fclose(f);
		char name[32];
		std::snprintf(name, sizeof(name), "custom%d", a - 1);
		const int rc = run_scenario(name, json.c_str(), 32, out_dir);
		if (rc < 0) return 2;
		total_bad += rc;
		// 縮小密度 (present サイズ直接ラスタライズ相当) でも検証する。
		std::snprintf(name, sizeof(name), "custom%d_s", a - 1);
		const int rs = run_scenario(name, json.c_str(), 32, out_dir,
		                            kW * 3 / 4, kH * 3 / 4);
		if (rs < 0) return 2;
		total_bad += rs;
	}

	std::printf(total_bad ? "FAIL: %d frame(s) mismatched\n"
	                      : "PASS: all frames matched\n", total_bad);
	return total_bad ? 1 : 0;
}

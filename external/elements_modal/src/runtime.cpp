//---------------------------------------------------------------------------
// elements_modal: ThorVG 初期化のみを管理する。
//
// フォント登録 (load_fonts_from_directory / register_font / Storages 経由など)
// は呼出側 (吉里吉里本体や hello_modal 例) の責務とする。 ライブラリ内部で
// フォルダ走査や std::filesystem 依存を持たない方針 — 組み込み環境 (NX 等)
// で filesystem が無いケースや、 ホスト側が独自の VFS / Storages を持って
// いるケースに合わせるため。
//---------------------------------------------------------------------------
#include "elements_modal/modal.h"

#include "em_platform.h"

#include <thorvg.h>

#include <algorithm>
#include <mutex>
#include <thread>

namespace elements_modal {

namespace {

std::mutex s_init_mutex;
bool s_tvg_initialized = false;

bool ensure_tvg_inited()
{
	if (s_tvg_initialized) return true;
	auto threads = std::max<unsigned>(1, std::thread::hardware_concurrency() - 1);
	if (tvg::Initializer::init(threads) != tvg::Result::Success) {
		em_logf("elements_modal: tvg::Initializer::init failed");
		return false;
	}
	s_tvg_initialized = true;
	return true;
}

} // anonymous

bool init(const std::string& /*font_directory*/, bool /*load_default_fonts*/)
{
	std::lock_guard<std::mutex> lock(s_init_mutex);
	return ensure_tvg_inited();
}

void shutdown()
{
	std::lock_guard<std::mutex> lock(s_init_mutex);
	if (s_tvg_initialized) {
		tvg::Initializer::term();
		s_tvg_initialized = false;
	}
}

} // namespace elements_modal

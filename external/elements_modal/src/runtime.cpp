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
#include "json_layout.h"   // release_atlas_resources
#include <elements/support/detail/scratch_context.hpp>  // release_shared_scratch

#include <elements/support/theme.hpp>
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
	// ThorVG を畳む前にアトラスの pixmap を手放す。 残したままだと静的
	// デストラクタが «終了済みの ThorVG» を叩いて落ちる (json_layout.h の
	// release_atlas_resources 参照)。
	release_atlas_resources();
	// 測定用 scratch canvas も捨てる。 生きていると SwRenderer::term() が
	// 弾かれ、 Initializer::term() が LoaderMgr::term() の手前で早期 return
	// する → フォントローダが ThorVG の静的リストに残り、 atexit で破棄済みの
	// フォントマネージャを触って落ちる。
	cycfi::elements::detail::release_shared_scratch();
	if (s_tvg_initialized) {
		tvg::Initializer::term();
		s_tvg_initialized = false;
	}
}

//---------------------------------------------------------------------------
// フォーカスリング表示のアプリ全体スイッチ。
//
// 素材で hilite / 押し下げを持つ画像 UI (PSD 由来の atlas_button 等) では、
// lib が汎用に描く青枠が絵の上に重なって邪魔になる。 画面ごとではなく
// アプリ全体の見た目方針なので、 グローバルテーマのフラグとして持たせる。
//---------------------------------------------------------------------------

void set_focus_ring_enabled(bool on)
{
	auto thm = cycfi::elements::get_theme();
	if (thm.focus_ring_enabled == on) return;
	thm.focus_ring_enabled = on;
	cycfi::elements::set_theme(thm);
}

bool focus_ring_enabled()
{
	return cycfi::elements::get_theme().focus_ring_enabled;
}

} // namespace elements_modal

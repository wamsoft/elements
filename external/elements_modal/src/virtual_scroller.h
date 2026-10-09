//---------------------------------------------------------------------------
//!@file 内部: 行を使い回すスムーズスクロール一覧 (virtual_scroller)
//
// 窓 (クリップ領域) に収まる行数 + 1 行ぶんのセルだけを子 canvas に並べ、
// スクロール位置 (px) に合わせてセル全体を 1px 単位でずらす。 行の境目を
// またいだら「先頭行」をホストへ通知し、 ホストがセルの中身を差し替える。
// 件数が増えてもセル数は一定。
//
// 入力:
//   ホイール        … 1 ノッチで wheel_rows 行 (アニメーション)
//   PageUp/PageDown … 表示行数ぶん (アニメーション)。 Home / End は両端
//   ↑↓ (キー/パッド) … フォーカスが窓の端の行にあり、 その先にまだ行が
//                      あれば 1 行送ってフォーカスは同じセルのまま
//                      (中身が 1 行ずれるので次の項目へ移ったように見える)
//   ←→↑↓ (item_linear 時) … 項目の順番で前 (← ↑) / 次 (→ ↓) の選べる項目へ。
//                      行の端では折り返し、 選べない項目は飛ばし、 窓の外なら
//                      送ってからそのセルへフォーカスを移す。 先頭より前は
//                      一覧の外の上方向へ、 末尾より後ろは一覧の外の下方向へ。
//                      キー操作で一覧の外からフォーカスが入ったセルが選べない
//                      項目なら、 いちばん近い選べる項目へ移す (窓の上半分に
//                      入ったときは後ろ、 下半分なら前を先に探す)
//   スクロールバー  … ratio() で位置を直接指定 (アニメーションなし)。
//                      bar_active(false) で行へ揃える
//   ドラッグ         … drag_scroll 有効時。 しきい値を超えたらセルの押下を
//                      取り消して 1:1 で追従し、 離した速度で慣性移動
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_VIRTUAL_SCROLLER_H
#define ELEMENTS_MODAL_VIRTUAL_SCROLLER_H

#include <elements.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace elements_modal {

//! 毎フレーム (overlay_session::update の中) で状態を進めたい要素の口。
//! 見た目が変わったら tick の中で v.refresh(要素) を出す。
class frame_ticker
{
public:
	virtual ~frame_ticker() = default;
	virtual void tick(cycfi::elements::view& v, std::uint64_t now_ms) = 0;
};

class virtual_scroller : public cycfi::elements::proxy_base, public frame_ticker
{
public:
	using element      = cycfi::elements::element;
	using element_ptr  = cycfi::elements::element_ptr;
	using context      = cycfi::elements::context;
	using basic_context = cycfi::elements::basic_context;
	using view_limits  = cycfi::elements::view_limits;
	using point        = cycfi::elements::point;
	using mouse_button = cycfi::elements::mouse_button;
	using key_info     = cycfi::elements::key_info;
	using cursor_tracking = cycfi::elements::cursor_tracking;

	struct config
	{
		float  row_height      = 0.0f;   // 1 行の送り量 (px)
		int    rows_visible    = 1;      // 窓に収まる行数
		int    scroll_ms       = 200;    // 送りアニメーションの時間
		bool   rest_snap_row   = true;   // 止まったときに行へ揃える
		double wheel_rows      = 1.0;    // ホイール 1 ノッチの行数
		bool   drag_scroll     = false;  // 中身のドラッグでスクロール
		float  drag_threshold  = 2.0f;   // ドラッグ開始の移動量 (px)
		double flick_sample_ms = 100.0;  // 慣性の速度を測る直近の範囲
		double flick_dist_coef = 2.0;    // 速度 → 移動距離
		double flick_time_coef = 50.0;   // 速度 → 時間 (ms)
		bool   stop_tap_click  = true;   // 慣性中に押した押下をクリックにも使う
		bool   ack_rows        = false;  // ホストの差し替え完了 (row_ready) を待って描く
		int    item_cols       = 0;      // 1 行の項目数 (項目の順番での移動に使う)
		bool   item_linear     = false;  // 矢印キーを項目の順番での移動にする
	};

	virtual_scroller(element_ptr subject, config cfg);

	// proxy
	element const&       subject() const override { return *_subject; }
	element&             subject() override { return *_subject; }
	view_limits          limits(basic_context const& ctx) const override;
	void                 draw(context const& ctx) override;
	void                 prepare_subject(context& ctx) override;
	element*             hit_test(context const& ctx, point p, bool leaf, bool control) override;
	bool                 wants_control() const override { return true; }
	bool                 clips_subject() const override { return true; }

	bool                 click(context const& ctx, mouse_button btn) override;
	void                 drag(context const& ctx, mouse_button btn) override;
	bool                 key(context const& ctx, key_info k) override;
	void                 begin_focus(focus_request req) override;
	bool                 end_focus() override;
	bool                 scroll(context const& ctx, point dir, point p) override;
	bool                 cursor(context const& ctx, point p, cursor_tracking status) override;

	// ホストからの指定
	void                 row_count(int rows);       // 総行数
	void                 ratio(double r);           // 位置 0 (上端) .. 1 (下端)。 即時
	// 位置を送りで指定する (行へ揃えてアニメーション)。 まだ一度も描いて
	// いない (画面を開いた直後の初期値) ときは即時。
	void                 ratio_to(double r);
	void                 reveal_row(int row);       // その行が見える位置へ (最小移動)
	void                 row_ready(int row);        // ホストが中身を差し替え終えた行
	void                 bar_active(bool on);       // スクロールバーを掴んでいる間 true
	// 項目ごとの選べる ("1") / 選べない ("0") の並び (一覧の先頭から)。
	// 長さが項目の総数になる。 空なら全項目を選べる扱い
	void                 item_mask(std::string mask) { _item_mask = std::move(mask); }

	int                  top_row() const { return _top; }
	bool                 bar_held() const { return _bar_held; }
	double               ratio() const;
	double               px() const { return _px; }
	bool                 animating() const { return _anim.active; }

	//! 先頭行が変わった (ホストはセルの中身をこの行基準で差し替える)
	std::function<void(int)>    on_top_row;
	//! 位置が変わった (0 = 上端 .. 1 = 下端)。 ratio() での外部指定では呼ばない
	std::function<void(double)> on_ratio;

	void                 tick(cycfi::elements::view& v, std::uint64_t now_ms) override;

private:
	struct sample { std::uint64_t t; double d; double dt; };
	struct anim_state
	{
		bool          active = false;
		double        from = 0.0, to = 0.0;
		std::uint64_t t0 = 0;
		double        dur = 0.0;
	};
	// 窓の中のセル (フォーカスを取れる要素)。 «窓の行» ごとに x の順で並べる
	struct slot_cell { float x; element* el; cycfi::elements::rect b; };
	using slot_cells = std::vector<std::vector<slot_cell>>;

	double               max_px() const;
	int                  max_top() const;
	double               row_px(double px) const;      // 最寄りの行の位置
	double               clamp_px(double px) const;
	double               draw_offset() const;
	void                 move_to(double px, bool notify_ratio = true);
	void                 animate_to(double target, double ms);
	void                 stop_anim();
	void                 snap_rest();
	bool                 edge_step(context const& ctx, int dir);
	bool                 item_step(context const& ctx, int dir);
	bool                 item_selectable(int idx) const;
	bool                 locate_focus(context const& ctx, slot_cells& slots,
	                                  int& cur_slot, int& cur_col, cycfi::elements::rect& fr);
	void                 focus_item(cycfi::elements::view& v, slot_cells const& slots, int target);
	void                 cache_cells(context const& ctx);
	void                 check_entry(cycfi::elements::view& v);
	void                 cancel_child_press(context const& ctx, mouse_button btn);
	void                 start_flick();

	view_limits const&   child_limits(basic_context const& ctx) const;

	element_ptr          _subject;
	config               _cfg;
	// 子 canvas の大きさ。 セルの配置は固定なので一度測れば変わらない
	// (位置合わせのたびに全セルを測り直すと、 変数 1 つの書き換えごとに
	//  再描画範囲を求める経路で数 ms かかる)。
	mutable view_limits  _child_lim{};
	mutable bool         _child_lim_ok = false;
	int                  _rows = 0;
	double               _px = 0.0;
	int                  _top = 0;          // 位置から決まる先頭行
	int                  _applied = 0;      // セルが今表示している先頭行
	anim_state           _anim;
	double               _wheel_acc = 0.0;
	bool                 _dirty = false;    // 次の tick で再描画
	bool                 _rehover = false;  // 次の tick でカーソル判定をやり直す
	// いまの送りがポインタ (ホイール / ドラッグ) 由来か。 止まったときの
	// カーソル判定のやり直しはこのときだけ行う (やり直すとカーソルの下の
	// セルへフォーカスが移るので、 キー操作の送りでは行わない)。
	bool                 _pointer_scroll = false;
	bool                 _bar_held = false;  // スクロールバーを掴んでいる間
	bool                 _drawn = false;     // 一度でも描いたか
	std::string          _item_mask;         // 項目ごとの選べる / 選べない
	// 一覧の外からフォーカスが入ったときの補正。 フォーカスの移動は
	// end_focus → begin_focus の順に同じ処理の中で届くので、 end_focus を
	// 伴わない begin_focus を «外から入った» とみなす (_focus_moving は毎フレーム
	// 戻す)。 判定と移し替えは次の tick で行う (同じフレームの描画より前に
	// フォーカスが移るので、 選べない項目にフォーカスが載った姿は描かれない)。
	// tick には描画の文脈が無いので、 セルの並びは描画のときに控えておく
	// (セルの配置は固定なので一度でよい。 矩形は窓の左上・送り 0 を原点にした値)
	bool                 _focus_moving = false;
	bool                 _entry_check = false;
	slot_cells           _cells;
	cycfi::elements::rect _win{};            // 直近の描画での窓の矩形

	// ポインタ
	point                _last_cursor{};
	bool                 _cursor_inside = false;
	bool                 _press = false;
	bool                 _dragging = false;
	bool                 _child_pressed = false;
	bool                 _stopped_by_press = false;
	point                _press_pos{};
	double               _press_px = 0.0;
	double               _last_y = 0.0;
	std::uint64_t        _last_ms = 0;
	std::vector<sample>  _samples;
};

} // namespace elements_modal

#endif

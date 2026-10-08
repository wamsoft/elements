//---------------------------------------------------------------------------
// virtual_scroller — 行を使い回すスムーズスクロール一覧 (virtual_scroller.h 参照)
//---------------------------------------------------------------------------
#include "virtual_scroller.h"
#include "em_platform.h"

#include <algorithm>
#include <cmath>

namespace elements_modal {

namespace ce = cycfi::elements;

virtual_scroller::virtual_scroller(element_ptr subject, config cfg)
 : _subject(std::move(subject))
 , _cfg(cfg)
{
	if (_cfg.rows_visible < 1) _cfg.rows_visible = 1;
	if (_cfg.row_height < 0.0f) _cfg.row_height = 0.0f;
}

//---------------------------------------------------------------------------
// 位置
//---------------------------------------------------------------------------
double virtual_scroller::max_px() const
{
	if (_cfg.row_height <= 0.0f) return 0.0;
	const int over = _rows - _cfg.rows_visible;
	return over > 0 ? over * double(_cfg.row_height) : 0.0;
}

int virtual_scroller::max_top() const
{
	const int over = _rows - _cfg.rows_visible;
	return over > 0 ? over : 0;
}

double virtual_scroller::row_px(double px) const
{
	if (_cfg.row_height <= 0.0f) return px;
	return std::round(px / _cfg.row_height) * _cfg.row_height;
}

double virtual_scroller::clamp_px(double px) const
{
	return std::clamp(px, 0.0, max_px());
}

double virtual_scroller::ratio() const
{
	const double m = max_px();
	return m > 0.0 ? _px / m : 0.0;
}

// セルを上へずらす量。 セルが表示している行 (_applied) を基準にするので、
// ホストの差し替えが遅れている間は 0〜1 行の範囲で止まって見える
// (1 行ぶんずらした状態は、 次の行を基準にずらし 0 で描いた状態と同じ絵)。
double virtual_scroller::draw_offset() const
{
	const double rh = _cfg.row_height;
	if (rh <= 0.0) return 0.0;
	const double off = std::round(_px) - _applied * rh;
	return std::clamp(off, 0.0, rh);
}

void virtual_scroller::move_to(double px, bool notify_ratio)
{
	px = clamp_px(px);
	const bool changed = px != _px;
	_px = px;

	int t = 0;
	if (_cfg.row_height > 0.0f)
		t = int(std::floor(_px / _cfg.row_height + 1e-6));
	t = std::clamp(t, 0, max_top());
	if (t != _top) {
		_top = t;
		if (!_cfg.ack_rows) _applied = t;
		if (on_top_row) on_top_row(t);
	}
	if (changed) {
		_dirty = true;
		if (notify_ratio && on_ratio) on_ratio(ratio());
	}
}

void virtual_scroller::animate_to(double target, double ms)
{
	target = clamp_px(target);
	if (ms <= 0.0 || target == _px) {
		_anim.active = false;
		move_to(target);
		if (_pointer_scroll) _rehover = true;
		return;
	}
	_anim.active = true;
	_anim.from = _px;
	_anim.to = target;
	_anim.t0 = em_now_ms();
	_anim.dur = ms;
	_dirty = true;
}

void virtual_scroller::stop_anim()
{
	_anim.active = false;
}

// 止まった位置を行へ揃える (rest_snap_row のとき)
void virtual_scroller::snap_rest()
{
	if (_cfg.rest_snap_row) {
		const double target = clamp_px(row_px(_px));
		if (target != _px) {
			animate_to(target, _cfg.scroll_ms);
			return;
		}
	}
	if (_pointer_scroll) _rehover = true;
}

//---------------------------------------------------------------------------
// ホストからの指定
//---------------------------------------------------------------------------
void virtual_scroller::row_count(int rows)
{
	_rows = rows < 0 ? 0 : rows;
	if (_anim.active) _anim.to = clamp_px(_anim.to);
	move_to(_px);   // 範囲が縮んだら位置と先頭行を収め直す
	// 範囲が変わると同じ px でも位置 (0..1) が変わるので知らせ直す
	if (on_ratio) on_ratio(ratio());
	_dirty = true;
}

void virtual_scroller::ratio(double r)
{
	stop_anim();
	_pointer_scroll = false;
	move_to(std::clamp(r, 0.0, 1.0) * max_px(), false);
}

void virtual_scroller::ratio_to(double r)
{
	if (!_drawn) {
		ratio(r);
		return;
	}
	_pointer_scroll = false;
	double target = std::clamp(r, 0.0, 1.0) * max_px();
	if (_cfg.rest_snap_row) target = row_px(target);
	animate_to(target, _cfg.scroll_ms);
}

void virtual_scroller::reveal_row(int row)
{
	if (_rows <= 0 || _cfg.row_height <= 0.0f) return;
	_pointer_scroll = false;
	row = std::clamp(row, 0, _rows - 1);
	const double base = _anim.active ? _anim.to : _px;
	const int top = int(std::floor(base / _cfg.row_height + 1e-6));
	if (row < top)
		animate_to(row * double(_cfg.row_height), _cfg.scroll_ms);
	else if (row > top + _cfg.rows_visible - 1)
		animate_to((row - _cfg.rows_visible + 1) * double(_cfg.row_height), _cfg.scroll_ms);
}

void virtual_scroller::row_ready(int row)
{
	if (row != _applied) {
		_applied = row;
		_dirty = true;
	}
}

void virtual_scroller::bar_active(bool on)
{
	_pointer_scroll = false;
	_bar_held = on;
	if (on)
		stop_anim();
	else
		snap_rest();
}

//---------------------------------------------------------------------------
// 描画
//---------------------------------------------------------------------------
virtual_scroller::view_limits const&
virtual_scroller::child_limits(basic_context const& ctx) const
{
	// 初回は子の limits を呼ぶ (副作用で slider の向き判定などが仕込まれる)
	if (!_child_lim_ok) {
		_child_lim = subject().limits(ctx);
		_child_lim_ok = true;
	}
	return _child_lim;
}

virtual_scroller::view_limits virtual_scroller::limits(basic_context const& ctx) const
{
	// 自分の大きさは配置先 (canvas の "at") が決める
	(void)child_limits(ctx);
	return {{0.0f, 0.0f}, {ce::full_extent, ce::full_extent}};
}

void virtual_scroller::prepare_subject(context& ctx)
{
	const auto& lim = child_limits(ctx);
	const float w = std::max(ctx.bounds.width(), lim.min.x);
	const float h = std::max(ctx.bounds.height() + _cfg.row_height, lim.min.y);
	ctx.bounds.top -= float(draw_offset());
	ctx.bounds.width(w);
	ctx.bounds.height(h);
}

void virtual_scroller::draw(context const& ctx)
{
	_drawn = true;
	auto state = ctx.canvas.new_state();
	ctx.canvas.add_rect(ctx.bounds);
	ctx.canvas.clip();
	proxy_base::draw(ctx);
}

virtual_scroller::element*
virtual_scroller::hit_test(context const& ctx, point p, bool leaf, bool control)
{
	if (!ctx.bounds.includes(p)) return nullptr;
	if (auto* r = proxy_base::hit_test(ctx, p, leaf, control))
		return r;
	// セルの間の隙間を押してもドラッグできるよう、 自分を当たりにする
	return _cfg.drag_scroll ? this : nullptr;
}

//---------------------------------------------------------------------------
// 入力
//---------------------------------------------------------------------------
bool virtual_scroller::cursor(context const& ctx, point p, cursor_tracking status)
{
	_last_cursor = p;
	_cursor_inside = status != cursor_tracking::leaving && ctx.bounds.includes(p);
	if (_dragging) return true;   // ドラッグ中はセルのホバーを出さない
	return proxy_base::cursor(ctx, p, status);
}

bool virtual_scroller::scroll(context const& ctx, point dir, point p)
{
	const double amt = dir.y;
	if (amt == 0.0 || _cfg.row_height <= 0.0f)
		return proxy_base::scroll(ctx, dir, p);

	// 1 ノッチ (1.0) で wheel_rows 行。 端数 (タッチパッド等) はためてから送る。
	if ((_wheel_acc > 0.0 && amt < 0.0) || (_wheel_acc < 0.0 && amt > 0.0))
		_wheel_acc = 0.0;
	_wheel_acc += amt;
	const double n = std::trunc(_wheel_acc);
	if (n == 0.0) return true;
	_wheel_acc -= n;

	// 送り中に回し足したら、 送り先から更に進める
	_pointer_scroll = true;
	const double base = _anim.active ? _anim.to : _px;
	animate_to(row_px(base) - n * _cfg.wheel_rows * _cfg.row_height, _cfg.scroll_ms);
	ctx.view.refresh(ctx);
	return true;
}

bool virtual_scroller::key(context const& ctx, key_info k)
{
	using ce::key_code;
	const bool press = k.action == ce::key_action::press
	                || k.action == ce::key_action::repeat;

	// キー操作が来たらドラッグは打ち切って行へ揃える
	if (press && _dragging) {
		_press = false;
		_dragging = false;
		_samples.clear();
		snap_rest();
	}

	if (proxy_base::key(ctx, k)) return true;
	if (!press || _cfg.row_height <= 0.0f) return false;
	_pointer_scroll = false;

	// 項目の順番での移動: ← ↑ は前、 → ↓ は次の項目へ (行の端で折り返す)
	if (_cfg.item_linear && _cfg.item_cols > 0) {
		switch (k.key) {
		case key_code::left:
		case key_code::up:
			return item_step(ctx, -1);
		case key_code::right:
		case key_code::down:
			return item_step(ctx, +1);
		default:
			break;
		}
	}

	const double rh = _cfg.row_height;
	const double base = _anim.active ? _anim.to : _px;
	switch (k.key) {
	case key_code::page_down:
		animate_to(row_px(base) + _cfg.rows_visible * rh, _cfg.scroll_ms);
		ctx.view.refresh(ctx);
		return true;
	case key_code::page_up:
		animate_to(row_px(base) - _cfg.rows_visible * rh, _cfg.scroll_ms);
		ctx.view.refresh(ctx);
		return true;
	case key_code::home:
		animate_to(0.0, _cfg.scroll_ms);
		ctx.view.refresh(ctx);
		return true;
	case key_code::end:
		animate_to(max_px(), _cfg.scroll_ms);
		ctx.view.refresh(ctx);
		return true;
	case key_code::down:
		return edge_step(ctx, +1);
	case key_code::up:
		return edge_step(ctx, -1);
	default:
		return false;
	}
}

// フォーカス中のセルが窓の端の行にあり、 その先にまだ行があれば 1 行送る。
// フォーカスは同じセルのまま (中身が 1 行ずれて次の項目を指す)。 送りは
// アニメーションしない (アニメーション中はフォーカス枠が中身とずれて見えるため)。
bool virtual_scroller::edge_step(context const& ctx, int dir)
{
	// フォーカスの連鎖を一番奥までたどる (子 canvas が大きさ指定などの
	// ラッパーに包まれていても辿れる)
	element* f = &subject();
	for (;;) {
		element* nf = f->focus();
		if (!nf || nf == f) break;
		f = nf;
	}
	if (f == &subject()) return false;

	// その要素の今の矩形 (ずらし込み) から、 窓の何行目にいるかを求める
	bool found = false;
	cycfi::elements::rect r{};
	in_context_do(ctx, *f, [&](context const& fctx) {
		r = fctx.bounds;
		found = true;
	});
	if (!found) return false;
	const double cy = (r.top + r.bottom) * 0.5 - ctx.bounds.top + draw_offset();
	const int slot_row = int(std::floor(cy / _cfg.row_height));

	if (dir > 0) {
		if (slot_row < _cfg.rows_visible - 1 || _top >= max_top()) return false;
	} else {
		if (slot_row > 0 || _top <= 0) return false;
	}
	stop_anim();
	_pointer_scroll = false;
	move_to((_top + dir) * double(_cfg.row_height));
	ctx.view.refresh(ctx);
	return true;
}

// 項目 idx を選べるか。 マスクが無ければすべて選べる
bool virtual_scroller::item_selectable(int idx) const
{
	if (_item_mask.empty()) return true;
	if (idx < 0 || idx >= int(_item_mask.size())) return false;
	return _item_mask[std::size_t(idx)] != '0';
}

// 項目の順番での移動 (item_linear)。 フォーカス中のセルを «窓の何行目・何列目»
// から項目の番号に直し、 dir 方向に選べる項目を探す。 見つかればその行が窓に
// 収まるように送ってから (アニメーションしない。 edge_step と同じ理由)、
// 送ったあとの窓で同じ行・列にあたるセルへフォーカスを移す。 セルの中身は
// ホストが先頭行の通知 (と row_ready) で差し替えるので、 移った先のセルが
// その項目を表示する。
// 先頭より前に選べる項目が無ければ、 一覧の外の上方向へ任せる (view の矢印
// ナビを «上» として続けさせる。 ← でも上の部品へ出られる)。 末尾より後ろに
// 無ければキーを使い切って何もしない。
bool virtual_scroller::item_step(context const& ctx, int dir)
{
	const int cols = _cfg.item_cols;
	const double rh = _cfg.row_height;
	if (cols <= 0 || rh <= 0.0) return false;

	// フォーカスの連鎖を一番奥までたどる (edge_step と同じ)
	element* f = &subject();
	for (;;) {
		element* nf = f->focus();
		if (!nf || nf == f) break;
		f = nf;
	}
	if (f == &subject()) return false;   // 一覧の中にフォーカスが無い
	bool found = false;
	cycfi::elements::rect fr{};
	in_context_do(ctx, *f, [&](context const& fctx) {
		fr = fctx.bounds;
		found = true;
	});
	if (!found) return false;

	// 窓の中のセル (フォーカスを取れる要素) を «窓の行» ごとに x の順で並べる。
	// 行はセルの配置 (ずらし込みを戻した位置) から求めるので、 送りの途中でも
	// 窓の何行目のセルかは変わらない。 予備の行 (窓の外) は使わない。
	struct cell { float x; element* el; cycfi::elements::rect b; };
	std::vector<std::vector<cell>> slots(std::size_t(_cfg.rows_visible));
	{
		std::vector<ce::focusable_element> list;
		ce::collect_focusable_elements(ctx, *this, list);
		for (auto const& e : list) {
			const double cy = (e.bounds.top + e.bounds.bottom) * 0.5
			                - ctx.bounds.top + draw_offset();
			const int s = int(std::floor(cy / rh));
			if (s < 0 || s >= _cfg.rows_visible) continue;
			slots[std::size_t(s)].push_back(
				{ (e.bounds.left + e.bounds.right) * 0.5f, e.el, e.bounds });
		}
		for (auto& row : slots)
			std::sort(row.begin(), row.end(),
				[](cell const& a, cell const& b) { return a.x < b.x; });
	}

	// フォーカス中のセルの行・列
	const float fx = (fr.left + fr.right) * 0.5f;
	const float fy = (fr.top + fr.bottom) * 0.5f;
	int cur_slot = -1, cur_col = -1;
	for (int s = 0; s < _cfg.rows_visible && cur_slot < 0; ++s) {
		const auto& row = slots[std::size_t(s)];
		for (int c = 0; c < int(row.size()); ++c) {
			if (row[std::size_t(c)].b.includes({fx, fy})) {
				cur_slot = s;
				cur_col = c;
				break;
			}
		}
	}
	if (cur_slot < 0 || cur_col >= cols) return false;

	// 選べる項目を探す (セルが今表示している先頭行 _applied が基準)
	int count = _rows * cols;
	if (!_item_mask.empty()) count = std::min(count, int(_item_mask.size()));
	const int cur = (_applied + cur_slot) * cols + cur_col;
	int target = -1;
	for (int i = cur + dir; i >= 0 && i < count; i += dir) {
		if (item_selectable(i)) {
			target = i;
			break;
		}
	}
	if (target < 0) {
		if (dir < 0) {
			ctx.view.redirect_arrow_focus(ce::key_code::up);
			return false;
		}
		return true;
	}

	// その行が窓に収まるように送る
	const int row = target / cols;
	const int col = target % cols;
	const double base = _anim.active ? _anim.to : _px;
	stop_anim();
	_pointer_scroll = false;
	int top = std::clamp(int(std::lround(base / rh)), 0, max_top());
	if (row < top)
		top = row;
	else if (row > top + _cfg.rows_visible - 1)
		top = row - _cfg.rows_visible + 1;
	top = std::clamp(top, 0, max_top());
	move_to(top * rh);

	const int slot = row - top;
	if (slot < 0 || slot >= _cfg.rows_visible) return true;
	const auto& cells = slots[std::size_t(slot)];
	if (col >= int(cells.size())) return true;
	ctx.view.focus(*cells[std::size_t(col)].el);
	ctx.view.refresh(ctx);
	return true;
}

// セルの押下を取り消す。 ボタンは «範囲外で離す» とクリックにならないので、
// 範囲外へ動かしてから離したことにする (動かさないと押下表示が残る)。
void virtual_scroller::cancel_child_press(context const& ctx, mouse_button btn)
{
	mouse_button away = btn;
	away.pos = point{-100000.0f, -100000.0f};
	away.down = true;
	proxy_base::drag(ctx, away);
	away.down = false;
	proxy_base::click(ctx, away);
}

bool virtual_scroller::click(context const& ctx, mouse_button btn)
{
	if (!_cfg.drag_scroll || btn.state != mouse_button::left)
		return proxy_base::click(ctx, btn);

	if (btn.down) {
		// 送り / 慣性中に押したらその場で止める
		_pointer_scroll = true;
		_stopped_by_press = _anim.active;
		stop_anim();
		_press = true;
		_dragging = false;
		_press_pos = btn.pos;
		_press_px = _px;
		_last_y = btn.pos.y;
		_last_ms = em_now_ms();
		_samples.clear();
		_wheel_acc = 0.0;
		_child_pressed = false;
		if (!_stopped_by_press || _cfg.stop_tap_click)
			_child_pressed = proxy_base::click(ctx, btn);
		// 子が掴まなくても、 以後の drag / up を受け取るために掴む
		return true;
	}

	if (!_press) return proxy_base::click(ctx, btn);
	_press = false;
	if (_dragging) {
		_dragging = false;
		start_flick();
		ctx.view.refresh(ctx);
		return true;
	}
	if (_child_pressed) proxy_base::click(ctx, btn);
	_child_pressed = false;
	snap_rest();
	return true;
}

void virtual_scroller::drag(context const& ctx, mouse_button btn)
{
	if (!_cfg.drag_scroll || !_press) {
		proxy_base::drag(ctx, btn);
		return;
	}
	if (!_dragging) {
		const float dx = btn.pos.x - _press_pos.x;
		const float dy = btn.pos.y - _press_pos.y;
		if (std::abs(dx) <= _cfg.drag_threshold && std::abs(dy) <= _cfg.drag_threshold) {
			if (_child_pressed) proxy_base::drag(ctx, btn);
			return;
		}
		// ドラッグ開始: セルの押下とホバーを消す (離してもクリックにしない)
		if (_child_pressed) cancel_child_press(ctx, btn);
		_child_pressed = false;
		proxy_base::cursor(ctx, btn.pos, cursor_tracking::leaving);
		_dragging = true;
	}

	// 速度の記録: スクロール方向の移動量 (指を上へ動かす = 下へ送る) と経過
	const std::uint64_t now = em_now_ms();
	const double d = -(btn.pos.y - _last_y);
	const double dt = now > _last_ms ? double(now - _last_ms) : 0.0;
	_last_y = btn.pos.y;
	_last_ms = now;
	const double keep = _cfg.flick_sample_ms;
	_samples.erase(std::remove_if(_samples.begin(), _samples.end(),
		[now, keep](sample const& s) { return double(now - s.t) > keep; }),
		_samples.end());
	_samples.push_back({now, d, dt});

	move_to(_press_px - (btn.pos.y - _press_pos.y));
	ctx.view.refresh(ctx);
}

// 離したときの速度から慣性で動かす。 直近 flick_sample_ms の移動を、 新しい
// ものほど重く (経過の 2 乗で減衰) 平均して速度とし、 距離 = 速度 × 距離係数、
// 時間 = √|速度| × 時間係数。 行き先は行へ丸めて範囲に収め、 縮んだ距離の
// 比率で時間も縮める。
void virtual_scroller::start_flick()
{
	const std::uint64_t now = em_now_ms();
	const double keep = _cfg.flick_sample_ms;
	double v = 0.0;
	std::size_t cnt = 0;
	for (auto const& s : _samples)
		if (double(now - s.t) <= keep) ++cnt;
	if (cnt > 0) {
		const double div = 1.0 / double(cnt);
		const double span_max = keep * 2.0;
		for (auto const& s : _samples) {
			const double age = double(now - s.t);
			if (age > keep) continue;
			const double span = (span_max - age) / span_max;
			v += s.d * s.dt * div * span * span;
		}
	}
	_samples.clear();

	if (v == 0.0) {
		snap_rest();
		return;
	}
	const double dist = v * _cfg.flick_dist_coef;
	double time = std::sqrt(std::abs(v)) * _cfg.flick_time_coef;
	double to = _px + dist;
	if (_cfg.rest_snap_row) to = row_px(to);
	to = clamp_px(to);
	if (dist != 0.0) time *= std::abs(to - _px) / std::abs(dist);
	if (to == _px) {
		snap_rest();
		return;
	}
	animate_to(to, time);
}

//---------------------------------------------------------------------------
// 毎フレーム
//---------------------------------------------------------------------------
void virtual_scroller::tick(ce::view& v, std::uint64_t now_ms)
{
	if (_anim.active) {
		const double el = now_ms > _anim.t0 ? double(now_ms - _anim.t0) : 0.0;
		const double t = _anim.dur > 0.0 ? el / _anim.dur : 1.0;
		if (t >= 1.0) {
			_anim.active = false;
			move_to(_anim.to);
			if (_pointer_scroll) _rehover = true;
		} else {
			const double e = t * (2.0 - t);   // 減速 (ease-out)
			move_to(_anim.from + (_anim.to - _anim.from) * e);
		}
		_dirty = true;
	}
	if (_dirty) {
		_dirty = false;
		v.refresh(*this);
	}
	// 止まったら、 カーソルの下にあるセルのホバーを付け直す。 ホストの差し替え
	// (row_ready) を待つ一覧では、 届くまでセルが 1 行ずれた位置で描かれて
	// いるので、 届いてから付け直す (先に付け直すと、 ずれた位置にあった別の
	// セルへホバーとフォーカスが移ってしまう)
	if (_rehover && !_press && !_anim.active && (!_cfg.ack_rows || _applied == _top)) {
		_rehover = false;
		if (_cursor_inside)
			v.cursor(_last_cursor, cursor_tracking::hovering);
	}
}

} // namespace elements_modal

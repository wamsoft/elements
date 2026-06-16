//---------------------------------------------------------------------------
// navigator 実装 — 画面遷移スタックのドライバ。
//
// resolve_transition() は SDL 非依存の純関数。 navigator はそれを使って
// スタックを駆動し、 focus / 言語のメモリを保持する。 描画 / 入力 / ファイル
// 読込はホスト責務 (overlay_session への依存も持たない)。
//---------------------------------------------------------------------------
#include <elements_modal/navigator.h>

namespace elements_modal {

namespace {

//! "<replace:NAME>" 形式から NAME を取り出す。 形式不正なら空文字列。
std::string parse_replace_name(const std::string& target)
{
	// "<replace:" = 9 文字、 末尾 '>' = 1 文字。
	if (target.size() < 10) return {};
	return target.substr(9, target.size() - 10);
}

} // namespace

nav_step resolve_transition(
	const std::string& action,
	const std::map<std::string, transition_spec>& transitions,
	bool is_entry)
{
	nav_step step;

	auto it = transitions.find(action);
	if (it != transitions.end()) {
		const std::string& target = it->second.target;
		step.effect      = it->second.effect;
		step.duration_ms = it->second.duration_ms;

		if (target.empty() || target == "<exit>") {
			step.action = nav_action::exit;
		} else if (target == "<back>") {
			step.action = nav_action::pop;
		} else if (target == "<stay>") {
			step.action = nav_action::stay;
		} else if (target.rfind("<replace:", 0) == 0 && target.back() == '>') {
			step.action = nav_action::replace;
			step.name   = parse_replace_name(target);
		} else {
			step.action = nav_action::push;
			step.name   = target;  // "<name>" の山括弧不要形式はそのまま画面名
		}
		return step;
	}

	// 未定義 action のフォールバック (旧ランナ互換):
	//   空 action  … entry なら exit / 子なら pop
	//   非空 action … entry なら stay (遷移先不明で据置) / 子なら pop
	if (action.empty()) {
		step.action = is_entry ? nav_action::exit : nav_action::pop;
	} else {
		step.action = is_entry ? nav_action::stay : nav_action::pop;
	}
	return step;
}

navigator::navigator(app_manifest manifest)
	: _manifest(std::move(manifest))
{
}

void navigator::reset_to(const std::string& entry)
{
	_stack.clear();
	std::string e = !entry.empty() ? entry : _manifest.entry;
	if (!e.empty()) _stack.push_back(e);
}

const std::string& navigator::current() const
{
	static const std::string empty;
	return _stack.empty() ? empty : _stack.back();
}

nav_step navigator::advance(
	const std::string& action,
	const std::map<std::string, transition_spec>& transitions)
{
	nav_step step = resolve_transition(action, transitions, at_entry());

	switch (step.action) {
		case nav_action::push:
			_stack.push_back(step.name);
			break;
		case nav_action::pop:
			if (!_stack.empty()) _stack.pop_back();
			break;
		case nav_action::replace:
			// 名前が空なら据置 (= stay 相当)。
			if (!_stack.empty() && !step.name.empty()) _stack.back() = step.name;
			break;
		case nav_action::stay:
			break;
		case nav_action::exit:
			_stack.clear();
			break;
	}
	return step;
}

void navigator::remember_focus(const std::string& screen, const std::string& id)
{
	if (id.empty()) return;
	_last_focus_per_screen[screen] = id;
}

const std::string& navigator::focus_to_restore(const std::string& screen) const
{
	static const std::string empty;
	auto it = _last_focus_per_screen.find(screen);
	return it != _last_focus_per_screen.end() ? it->second : empty;
}

std::string navigator::screen_file(const std::string& name) const
{
	if (!_manifest.ok) return {};
	auto it = _manifest.screens.find(name);
	return it != _manifest.screens.end() ? it->second : std::string{};
}

} // namespace elements_modal

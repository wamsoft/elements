//---------------------------------------------------------------------------
// elements_modal: JSON → Elements 要素ツリー変換
//
// 純粋 C++ + SDL_Log + std::function ベース。 JSON / JSONC (// 行コメント /
// /* */ ブロックコメント / 末尾カンマ) を文字列リテラル保護つき前段 strip で
// 解釈し、 picojson でパース、 cycfi::elements のツリーに変換する。
//---------------------------------------------------------------------------
#include "json_layout.h"

#include <SDL3/SDL.h>
#include <picojson/picojson.h>

#include <cctype>
#include <cstring>
#include <utility>

namespace ce = cycfi::elements;
using element_ptr = std::shared_ptr<ce::element>;

namespace elements_modal {

namespace {

//---------------------------------------------------------------------------
// JSON 文字列 → Elements enum 変換
//---------------------------------------------------------------------------

//! "enter"/"escape"/... / "a"-"z" / "0"-"9" / "f1"-"f12" 等を ce::key_code に
//! 変換。 未知名は key_code::unknown を返す。
ce::key_code parse_key_code(const std::string& s)
{
	using k = ce::key_code;
	if (s.empty()) return k::unknown;
	// 一文字 letter / digit
	if (s.size() == 1) {
		char c = s[0];
		if (c >= 'a' && c <= 'z') return static_cast<k>(c - 'a' + 'A');
		if (c >= 'A' && c <= 'Z') return static_cast<k>(c);
		if (c >= '0' && c <= '9') return static_cast<k>(c);
	}
	if (s == "enter" || s == "return") return k::enter;
	if (s == "escape" || s == "esc")   return k::escape;
	if (s == "tab")                    return k::tab;
	if (s == "space")                  return k::space;
	if (s == "backspace")              return k::backspace;
	if (s == "delete")                 return k::_delete;
	if (s == "insert")                 return k::insert;
	if (s == "left")                   return k::left;
	if (s == "right")                  return k::right;
	if (s == "up")                     return k::up;
	if (s == "down")                   return k::down;
	if (s == "home")                   return k::home;
	if (s == "end")                    return k::end;
	if (s == "page_up" || s == "pgup") return k::page_up;
	if (s == "page_down" || s == "pgdn") return k::page_down;
	// f1..f12
	if (s.size() >= 2 && (s[0] == 'f' || s[0] == 'F')) {
		int n = 0;
		for (size_t i = 1; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9') { n = 0; break; }
			n = n * 10 + (s[i] - '0');
		}
		if (n >= 1 && n <= 12) {
			return static_cast<k>(int(k::f1) + (n - 1));
		}
	}
	return k::unknown;
}

//! "shift"/"ctrl"/"alt"/"super"/"action" の OR を返す。
int parse_modifiers(const picojson::array& arr)
{
	int m = 0;
	for (const auto& v : arr) {
		if (!v.is<std::string>()) continue;
		const auto& s = v.get<std::string>();
		if      (s == "shift")   m |= ce::mod_shift;
		else if (s == "ctrl" ||
		         s == "control") m |= ce::mod_control;
		else if (s == "alt")     m |= ce::mod_alt;
		else if (s == "super" ||
		         s == "cmd" ||
		         s == "command") m |= ce::mod_super;
		else if (s == "action")  m |= ce::mod_action;
	}
	return m;
}

ce::pad_button parse_pad_button(const std::string& s)
{
	using b = ce::pad_button;
	if (s == "a")               return b::a;
	if (s == "b")               return b::b;
	if (s == "x")               return b::x;
	if (s == "y")               return b::y;
	if (s == "dpad_up")         return b::dpad_up;
	if (s == "dpad_down")       return b::dpad_down;
	if (s == "dpad_left")       return b::dpad_left;
	if (s == "dpad_right")      return b::dpad_right;
	if (s == "lb" ||
	    s == "l1")              return b::lb;
	if (s == "rb" ||
	    s == "r1")              return b::rb;
	if (s == "lt" ||
	    s == "l2" ||
	    s == "lt_click")        return b::lt_click;
	if (s == "rt" ||
	    s == "r2" ||
	    s == "rt_click")        return b::rt_click;
	if (s == "l3")              return b::l3;
	if (s == "r3")              return b::r3;
	if (s == "back")            return b::back;
	if (s == "start")           return b::start;
	if (s == "guide")           return b::guide;
	return b::unknown;
}

ce::pad_axis_mode parse_axis_mode(const std::string& s, ce::pad_axis_mode dflt)
{
	using m = ce::pad_axis_mode;
	if (s == "disabled") return m::disabled;
	if (s == "focus")    return m::focus;
	if (s == "value")    return m::value;
	if (s == "both")     return m::both;
	return dflt;
}


//---------------------------------------------------------------------------
// picojson helpers
//---------------------------------------------------------------------------
const picojson::value* get_field(const picojson::object& o, const char* key)
{
	auto it = o.find(key);
	return (it != o.end()) ? &it->second : nullptr;
}

std::string string_or(const picojson::object& o, const char* key,
                      const std::string& dflt = "")
{
	if (auto* v = get_field(o, key); v && v->is<std::string>()) {
		return v->get<std::string>();
	}
	return dflt;
}

double number_or(const picojson::object& o, const char* key, double dflt)
{
	if (auto* v = get_field(o, key); v && v->is<double>()) {
		return v->get<double>();
	}
	return dflt;
}

const picojson::array* get_array(const picojson::object& o, const char* key)
{
	if (auto* v = get_field(o, key); v && v->is<picojson::array>()) {
		return &v->get<picojson::array>();
	}
	return nullptr;
}

int int_at(const picojson::array& arr, size_t i, int dflt = 0)
{
	if (i < arr.size() && arr[i].is<double>()) {
		return static_cast<int>(arr[i].get<double>());
	}
	return dflt;
}

ce::color parse_color(const picojson::array& arr)
{
	int r = int_at(arr, 0, 0);
	int g = int_at(arr, 1, 0);
	int b = int_at(arr, 2, 0);
	int a = (arr.size() >= 4) ? int_at(arr, 3, 255) : 255;
	return ce::rgba(r, g, b, a);
}

//---------------------------------------------------------------------------
// JSONC 前処理 — コメント / 末尾カンマを strip。 文字列リテラルは保護。
//---------------------------------------------------------------------------
std::string preprocess_jsonc(const std::string& in)
{
	// pass 1: コメント strip
	std::string s1;
	s1.reserve(in.size());
	enum { N1_NORMAL, N1_STRING, N1_LINE_COMMENT, N1_BLOCK_COMMENT } st1 = N1_NORMAL;
	for (size_t i = 0; i < in.size(); ++i) {
		char c = in[i];
		char nx = (i + 1 < in.size()) ? in[i + 1] : '\0';
		switch (st1) {
		case N1_NORMAL:
			if      (c == '"') { st1 = N1_STRING; s1 += c; }
			else if (c == '/' && nx == '/') { st1 = N1_LINE_COMMENT;  ++i; }
			else if (c == '/' && nx == '*') { st1 = N1_BLOCK_COMMENT; ++i; }
			else { s1 += c; }
			break;
		case N1_STRING:
			s1 += c;
			if (c == '\\' && i + 1 < in.size()) {
				s1 += in[++i];
			} else if (c == '"') {
				st1 = N1_NORMAL;
			}
			break;
		case N1_LINE_COMMENT:
			if (c == '\n') { st1 = N1_NORMAL; s1 += c; }
			break;
		case N1_BLOCK_COMMENT:
			if      (c == '*' && nx == '/') { st1 = N1_NORMAL; ++i; }
			else if (c == '\n') { s1 += c; }
			break;
		}
	}
	// pass 2: 末尾カンマ strip
	std::string s2;
	s2.reserve(s1.size());
	bool in_string = false;
	for (size_t i = 0; i < s1.size(); ++i) {
		char c = s1[i];
		if (in_string) {
			s2 += c;
			if (c == '\\' && i + 1 < s1.size()) {
				s2 += s1[++i];
			} else if (c == '"') {
				in_string = false;
			}
			continue;
		}
		if (c == '"') {
			in_string = true;
			s2 += c;
			continue;
		}
		if (c == ',') {
			size_t j = i + 1;
			while (j < s1.size() &&
			       std::isspace(static_cast<unsigned char>(s1[j]))) ++j;
			if (j < s1.size() && (s1[j] == ']' || s1[j] == '}')) {
				continue;   // skip trailing comma
			}
		}
		s2 += c;
	}
	return s2;
}

//---------------------------------------------------------------------------
// LayoutBuilder — element ツリーを再帰生成
//---------------------------------------------------------------------------
class LayoutBuilder
{
public:
	explicit LayoutBuilder(event_callback cb) : _cb(std::move(cb)) {}

	void set_default_locale(std::string locale) { _default_locale = std::move(locale); }
	element_ptr build(const picojson::value& v);

	// "initial_focus": true が指定された要素 (なければ nullptr)。
	// build() 完了後、 ホストが view.focus(...) に渡すために取得する。
	element_ptr take_initial_focus() { return std::move(_initial_focus); }

	// id 付き要素 → element_ptr のマップ。 shortcut の "target": "<id>" 解決用。
	std::map<std::string, element_ptr> take_id_map() { return std::move(_id_to_element); }

	// "close_on_click": true が指定された button の id 集合。
	std::set<std::string> take_close_button_ids() { return std::move(_close_button_ids); }

private:
	event_callback _cb;
	std::string _default_locale;
	element_ptr _initial_focus;
	std::map<std::string, element_ptr> _id_to_element;
	std::set<std::string> _close_button_ids;

	// type ごとのビルダ
	element_ptr build_label       (const picojson::object& o);
	element_ptr build_button      (const picojson::object& o);
	element_ptr build_vtile       (const picojson::object& o);
	element_ptr build_htile       (const picojson::object& o);
	element_ptr build_margin      (const picojson::object& o);
	element_ptr build_box         (const picojson::object& o);
	element_ptr build_layer       (const picojson::object& o);
	element_ptr build_align       (const picojson::object& o, const std::string& kind);
	element_ptr build_hsize       (const picojson::object& o);
	element_ptr build_vsize       (const picojson::object& o);
	element_ptr build_hspacer     (const picojson::object& o);
	element_ptr build_vspacer     (const picojson::object& o);
	element_ptr build_spacer      (const picojson::object& o);
	element_ptr build_scroller    (const picojson::object& o);
	element_ptr build_checkbox    (const picojson::object& o);
	element_ptr build_toggle_button(const picojson::object& o);
	element_ptr build_slide_switch(const picojson::object& o);
	element_ptr build_input_box   (const picojson::object& o);
	element_ptr build_group       (const picojson::object& o);
	element_ptr build_selection_menu(const picojson::object& o);
	element_ptr build_side_margin (const picojson::object& o, char side);
	element_ptr build_hmin_size   (const picojson::object& o);
	element_ptr build_vmin_size   (const picojson::object& o);
	element_ptr build_cycle_picker(const picojson::object& o, int variant); // 0=cycle, 1=framed, 2=segmented
	element_ptr build_invert_button(const picojson::object& o);
	element_ptr build_ring_button (const picojson::object& o);
	element_ptr build_slider      (const picojson::object& o);
	element_ptr build_slider_with_range(const picojson::object& o);
	element_ptr build_labeled_row (const picojson::object& o);
	element_ptr build_pad_icon    (const picojson::object& o);

	element_ptr build_child(const picojson::object& o);
	std::vector<element_ptr> build_children(const picojson::object& o);

	void fire_button(const std::string& id);
	void fire_value(const std::string& id, const value_t& payload);

	// "initial_focus": true が指定されていれば、 渡された element を初期 focus
	// 候補として記録する。 share 済みポインタを expected。 複数指定された場合は
	// 最初の build 順序で先勝ち (上書きしない)。
	void note_initial_focus(const picojson::object& o, const element_ptr& shared);

	// "id": "..." が指定されていれば、 id → element の対応を登録する。
	// shortcut 等の bind target 解決用。 button / checkbox / toggle_button /
	// slide_switch / input_box / selection_menu などの focusable / value widget
	// で呼ぶ。
	void register_id(const picojson::object& o, const element_ptr& shared);
};

element_ptr LayoutBuilder::build(const picojson::value& v)
{
	if (!v.is<picojson::object>()) return nullptr;
	const auto& o = v.get<picojson::object>();
	auto type = string_or(o, "type");

	if (type == "label")         return build_label(o);
	if (type == "button")        return build_button(o);
	if (type == "vtile")         return build_vtile(o);
	if (type == "htile")         return build_htile(o);
	if (type == "margin")        return build_margin(o);
	if (type == "box")           return build_box(o);
	if (type == "layer")         return build_layer(o);
	if (type == "align_center")  return build_align(o, "center");
	if (type == "align_left")    return build_align(o, "left");
	if (type == "align_right")   return build_align(o, "right");
	if (type == "hsize")         return build_hsize(o);
	if (type == "vsize")         return build_vsize(o);
	if (type == "hspacer")       return build_hspacer(o);
	if (type == "vspacer")       return build_vspacer(o);
	if (type == "spacer")        return build_spacer(o);
	if (type == "scroller")      return build_scroller(o);
	if (type == "checkbox")      return build_checkbox(o);
	if (type == "check_box")     return build_checkbox(o);
	if (type == "toggle_button") return build_toggle_button(o);
	if (type == "slide_switch")  return build_slide_switch(o);
	if (type == "input_box")     return build_input_box(o);
	if (type == "group")         return build_group(o);
	if (type == "selection_menu")return build_selection_menu(o);
	if (type == "top_margin")    return build_side_margin(o, 't');
	if (type == "left_margin")   return build_side_margin(o, 'l');
	if (type == "right_margin")  return build_side_margin(o, 'r');
	if (type == "bottom_margin") return build_side_margin(o, 'b');
	if (type == "hmin_size")     return build_hmin_size(o);
	if (type == "vmin_size")     return build_vmin_size(o);
	if (type == "cycle_picker")        return build_cycle_picker(o, 0);
	if (type == "framed_cycle_picker") return build_cycle_picker(o, 1);
	if (type == "segmented_picker")    return build_cycle_picker(o, 2);
	if (type == "invert_button") return build_invert_button(o);
	if (type == "ring_button")   return build_ring_button(o);
	if (type == "slider")        return build_slider(o);
	if (type == "slider_with_range") return build_slider_with_range(o);
	if (type == "labeled_row")   return build_labeled_row(o);
	if (type == "filler")        return ce::share(ce::element{});
	if (type == "pad_icon")      return build_pad_icon(o);

	SDL_Log("elements_modal: unknown element type: %s", type.c_str());
	return nullptr;
}

element_ptr LayoutBuilder::build_child(const picojson::object& o)
{
	if (auto* v = get_field(o, "child")) return build(*v);
	return nullptr;
}

std::vector<element_ptr> LayoutBuilder::build_children(const picojson::object& o)
{
	std::vector<element_ptr> result;
	if (auto* arr = get_array(o, "children")) {
		result.reserve(arr->size());
		for (const auto& cv : *arr) {
			if (auto c = build(cv)) result.push_back(std::move(c));
		}
	}
	return result;
}

void LayoutBuilder::fire_button(const std::string& id)
{
	if (_cb && !id.empty()) _cb(id, /*is_button_click=*/true, value_t{});
}

void LayoutBuilder::fire_value(const std::string& id, const value_t& payload)
{
	if (_cb && !id.empty()) _cb(id, /*is_button_click=*/false, payload);
}

void LayoutBuilder::note_initial_focus(const picojson::object& o,
                                       const element_ptr& shared)
{
	if (_initial_focus) return;   // 先勝ち
	auto* v = get_field(o, "initial_focus");
	if (v && v->is<bool>() && v->get<bool>()) {
		_initial_focus = shared;
	}
}

void LayoutBuilder::register_id(const picojson::object& o,
                                 const element_ptr& shared)
{
	auto id = string_or(o, "id");
	if (!id.empty() && shared) {
		_id_to_element[id] = shared;
	}
}

//---------------------------------------------------------------------------
// 各 element 種別
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_label(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string locale = string_or(o, "locale", _default_locale);

	auto base = ce::label(text);
	if (auto* v = get_field(o, "size"); v && v->is<double>()) {
		auto sized = base.relative_font_size(static_cast<float>(v->get<double>()));
		if (!locale.empty()) return ce::share(sized.locale(std::move(locale)));
		return ce::share(std::move(sized));
	}
	if (!locale.empty()) return ce::share(base.locale(std::move(locale)));
	return ce::share(std::move(base));
}

element_ptr LayoutBuilder::build_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	auto btn = ce::button(text);
	if (!id.empty()) {
		auto cb_id = id;
		auto cb = _cb;
		btn.on_click = [cb_id, cb](bool) {
			if (cb && !cb_id.empty()) cb(cb_id, /*is_button_click=*/true, value_t{});
		};
	}
	// Elements 新 API では basic_button が標準で wants_focus()=true + Space/Enter
	// で activate を呼ぶので、 key_intercept wrap は不要。 そのまま share する。
	auto shared = ce::share(std::move(btn));
	register_id(o, shared);
	note_initial_focus(o, shared);
	// "close_on_click": true な button だけホスト側で finish フラグを立てる対象。
	// デフォルト (省略) は閉じず、 onAction だけ発火する。
	if (!id.empty()) {
		auto* v = get_field(o, "close_on_click");
		if (v && v->is<bool>() && v->get<bool>()) {
			_close_button_ids.insert(id);
		}
	}
	return shared;
}

element_ptr LayoutBuilder::build_vtile(const picojson::object& o)
{
	ce::vtile_composite tile;
	for (auto& c : build_children(o)) tile.push_back(c);
	return ce::share(std::move(tile));
}

element_ptr LayoutBuilder::build_htile(const picojson::object& o)
{
	ce::htile_composite tile;
	for (auto& c : build_children(o)) tile.push_back(c);
	return ce::share(std::move(tile));
}

element_ptr LayoutBuilder::build_margin(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;

	float l = 0, t = 0, r = 0, b = 0;
	if (auto* arr = get_array(o, "padding")) {
		if      (arr->size() == 4) {
			l = static_cast<float>(int_at(*arr, 0));
			t = static_cast<float>(int_at(*arr, 1));
			r = static_cast<float>(int_at(*arr, 2));
			b = static_cast<float>(int_at(*arr, 3));
		}
		else if (arr->size() == 2) {
			l = r = static_cast<float>(int_at(*arr, 0));
			t = b = static_cast<float>(int_at(*arr, 1));
		}
		else if (arr->size() == 1) {
			l = t = r = b = static_cast<float>(int_at(*arr, 0));
		}
	} else if (auto* v = get_field(o, "padding"); v && v->is<double>()) {
		l = t = r = b = static_cast<float>(v->get<double>());
	}
	return ce::share(ce::margin({l, t, r, b}, ce::hold_any(child)));
}

element_ptr LayoutBuilder::build_box(const picojson::object& o)
{
	ce::color c = ce::rgba(0, 0, 0, 255);
	if (auto* arr = get_array(o, "color")) c = parse_color(*arr);
	return ce::share(ce::box(c));
}

element_ptr LayoutBuilder::build_layer(const picojson::object& o)
{
	// children[0] が最前面
	ce::layer_composite ly;
	auto children = build_children(o);
	for (auto it = children.rbegin(); it != children.rend(); ++it) {
		ly.push_back(*it);
	}
	return ce::share(std::move(ly));
}

element_ptr LayoutBuilder::build_align(const picojson::object& o, const std::string& kind)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	if (kind == "center") return ce::share(ce::align_center(ce::hold_any(child)));
	if (kind == "left")   return ce::share(ce::align_left(ce::hold_any(child)));
	if (kind == "right")  return ce::share(ce::align_right(ce::hold_any(child)));
	return child;
}

element_ptr LayoutBuilder::build_hsize(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	float w = static_cast<float>(number_or(o, "width", 0.0));
	return ce::share(ce::hsize(w, ce::hold_any(child)));
}

element_ptr LayoutBuilder::build_vsize(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	float h = static_cast<float>(number_or(o, "height", 0.0));
	return ce::share(ce::vsize(h, ce::hold_any(child)));
}

element_ptr LayoutBuilder::build_hspacer(const picojson::object& o)
{
	// horizontal fixed + vertical stretchy。 fixed_size 両軸固定にすると
	// 親の vtile/htile が max を 0 まで縮めるので、 軸別ラッパを使う。
	float w = static_cast<float>(number_or(o, "width", 0.0));
	return ce::share(ce::hsize(w, ce::element{}));
}

element_ptr LayoutBuilder::build_vspacer(const picojson::object& o)
{
	// vertical fixed + horizontal stretchy。 build_hspacer と対称。
	float h = static_cast<float>(number_or(o, "height", 0.0));
	return ce::share(ce::vsize(h, ce::element{}));
}

element_ptr LayoutBuilder::build_spacer(const picojson::object& o)
{
	float w = static_cast<float>(number_or(o, "width", 0.0));
	float h = static_cast<float>(number_or(o, "height", 0.0));
	if (auto* arr = get_array(o, "size")) {
		w = static_cast<float>(int_at(*arr, 0, static_cast<int>(w)));
		h = static_cast<float>(int_at(*arr, 1, static_cast<int>(h)));
	}
	return ce::share(ce::fixed_size(ce::point{w, h}, ce::element{}));
}

element_ptr LayoutBuilder::build_scroller(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	return ce::share(ce::vscroller(ce::hold_any(child)));
}

element_ptr LayoutBuilder::build_checkbox(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	bool init = false;
	if (auto* v = get_field(o, "value"); v && v->is<bool>()) init = v->get<bool>();

	auto cb = ce::check_box(text);
	cb.value(init);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		cb.on_click = [cb_id, user_cb](bool state) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(cb));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

element_ptr LayoutBuilder::build_toggle_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	bool init = false;
	if (auto* v = get_field(o, "value"); v && v->is<bool>()) init = v->get<bool>();

	auto tb = ce::toggle_button(text);
	tb.value(init);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		tb.on_click = [cb_id, user_cb](bool state) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(tb));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

element_ptr LayoutBuilder::build_slide_switch(const picojson::object& o)
{
	std::string id = string_or(o, "id");
	bool init = false;
	if (auto* v = get_field(o, "value"); v && v->is<bool>()) init = v->get<bool>();

	auto sw = ce::slide_switch();
	sw.value(init);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		sw.on_click = [cb_id, user_cb](bool state) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(sw));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

element_ptr LayoutBuilder::build_input_box(const picojson::object& o)
{
	auto placeholder = string_or(o, "placeholder");
	std::string id = string_or(o, "id");
	float size = static_cast<float>(number_or(o, "size", 1.0));

	auto pair = ce::input_box(placeholder, size);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		pair.second->on_text = [cb_id, user_cb](std::string_view text) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{std::string(text)});
		};
	}
	auto shared = ce::share(std::move(pair.first));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

element_ptr LayoutBuilder::build_group(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	auto title = string_or(o, "title");
	float label_size = static_cast<float>(number_or(o, "label_size", 1.0));
	if (title.empty()) {
		return ce::share(ce::group(ce::hold_any(child)));
	}
	return ce::share(ce::group(title, ce::hold_any(child), label_size));
}

//---------------------------------------------------------------------------
// selection_menu — ドロップダウン選択。
//   { "type": "selection_menu", "id": "...", "options": ["a", "b", ...],
//     "selected": 0 }
// 選択変更時に event_callback(id, false, string payload = 選択テキスト)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_selection_menu(const picojson::object& o)
{
	std::string id = string_or(o, "id");
	std::vector<std::string> labels;
	if (auto* arr = get_array(o, "options")) {
		labels.reserve(arr->size());
		for (const auto& v : *arr) {
			if (v.is<std::string>()) labels.push_back(v.get<std::string>());
		}
	}
	if (labels.empty()) {
		SDL_Log("elements_modal: selection_menu without 'options'");
		return nullptr;
	}

	int selected = 0;
	if (auto* v = get_field(o, "selected"); v && v->is<double>()) {
		selected = static_cast<int>(v->get<double>());
	}
	if (selected < 0 || static_cast<size_t>(selected) >= labels.size()) {
		selected = 0;
	}

	auto cb_id = id;
	auto user_cb = _cb;
	auto on_select = [cb_id, user_cb](std::string_view text) {
		if (user_cb && !cb_id.empty()) {
			user_cb(cb_id, /*is_button_click=*/false,
			        value_t{std::string(text)});
		}
	};

	auto sm = ce::selection_menu(on_select, labels);
	sm.second->set_text(labels[selected]);
	auto shared = ce::share(std::move(sm.first));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

//---------------------------------------------------------------------------
// 単方向 margin — top_margin / left_margin / right_margin / bottom_margin
//   { "type": "top_margin", "value": 10, "child": {...} }
// 反対辺は 0、 指定辺のみ value を反映。 多用される DSL の対応物。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_side_margin(const picojson::object& o, char side)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	float v = static_cast<float>(number_or(o, "value", 0.0));
	float l = (side == 'l') ? v : 0.0f;
	float t = (side == 't') ? v : 0.0f;
	float r = (side == 'r') ? v : 0.0f;
	float b = (side == 'b') ? v : 0.0f;
	return ce::share(ce::margin({l, t, r, b}, ce::hold_any(child)));
}

//---------------------------------------------------------------------------
// hmin_size / vmin_size — 最小サイズ制約 (子の自然サイズを下回らせない)
//   { "type": "hmin_size", "width": 280, "child": {...} }
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_hmin_size(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	float w = static_cast<float>(number_or(o, "width", 0.0));
	return ce::share(ce::hmin_size(w, ce::hold_any(child)));
}

element_ptr LayoutBuilder::build_vmin_size(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	float h = static_cast<float>(number_or(o, "height", 0.0));
	return ce::share(ce::vmin_size(h, ce::hold_any(child)));
}

//---------------------------------------------------------------------------
// pickers — cycle / framed_cycle / segmented
//   { "type": "cycle_picker",          "id": "...", "options": ["a","b",..], "initial": 0 }
//   { "type": "framed_cycle_picker",   ... }
//   { "type": "segmented_picker",      ... }
// 選択変更時に event_callback(id, false, int64_t index)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_cycle_picker(const picojson::object& o, int variant)
{
	std::string id = string_or(o, "id");
	std::vector<std::string> opts;
	if (auto* arr = get_array(o, "options")) {
		opts.reserve(arr->size());
		for (const auto& v : *arr) {
			if (v.is<std::string>()) opts.push_back(v.get<std::string>());
		}
	}
	if (opts.empty()) {
		SDL_Log("elements_modal: %s without 'options'",
			variant == 0 ? "cycle_picker"
			: variant == 1 ? "framed_cycle_picker"
			: "segmented_picker");
		return nullptr;
	}

	std::size_t initial = 0;
	if (auto* v = get_field(o, "initial"); v && v->is<double>()) {
		auto raw = static_cast<long long>(v->get<double>());
		if (raw < 0) raw = 0;
		if (static_cast<size_t>(raw) >= opts.size()) raw = static_cast<long long>(opts.size() - 1);
		initial = static_cast<std::size_t>(raw);
	}

	float fs = static_cast<float>(number_or(o, "font_size", 1.0));

	auto cb_id = id;
	auto user_cb = _cb;
	auto on_change = [cb_id, user_cb](std::size_t i) {
		if (user_cb && !cb_id.empty()) {
			user_cb(cb_id, /*is_button_click=*/false,
			        value_t{static_cast<std::int64_t>(i)});
		}
	};

	element_ptr shared;
	if (variant == 0) {
		auto p = std::make_shared<ce::cycle_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		shared = p;
	} else if (variant == 1) {
		auto p = std::make_shared<ce::framed_cycle_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		shared = p;
	} else {
		auto p = std::make_shared<ce::segmented_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		shared = p;
	}
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

//---------------------------------------------------------------------------
// invert_button / ring_button — console 風スタイラ付きの momentary button
//   { "type": "invert_button", "text": "OK", "id": "..." }
//   { "type": "ring_button",   "text": "RUN", "id": "...", "outline": [r,g,b,a] }
// 通常の button と同じく id があれば click で event_callback、
// close_on_click: true で modal 終了。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_invert_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	float size = static_cast<float>(number_or(o, "size", 1.0));
	auto btn = ce::invert_button(text, size);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		btn.on_click = [cb_id, user_cb](bool) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/true, value_t{});
		};
	}
	auto shared = ce::share(std::move(btn));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (!id.empty()) {
		auto* v = get_field(o, "close_on_click");
		if (v && v->is<bool>() && v->get<bool>()) {
			_close_button_ids.insert(id);
		}
	}
	return shared;
}

element_ptr LayoutBuilder::build_ring_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	ce::color outline = ce::colors::white;
	if (auto* arr = get_array(o, "outline")) outline = parse_color(*arr);
	float size = static_cast<float>(number_or(o, "size", 1.0));

	auto btn = ce::ring_button(text, outline, size);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		btn.on_click = [cb_id, user_cb](bool) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/true, value_t{});
		};
	}
	auto shared = ce::share(std::move(btn));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (!id.empty()) {
		auto* v = get_field(o, "close_on_click");
		if (v && v->is<bool>() && v->get<bool>()) {
			_close_button_ids.insert(id);
		}
	}
	return shared;
}

//---------------------------------------------------------------------------
// slider — 0..1 範囲の素のスライダ
//   { "type": "slider", "id": "...", "initial": 0.5 }
// 値変化で event_callback(id, false, double pos)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_slider(const picojson::object& o)
{
	std::string id = string_or(o, "id");
	double initial = number_or(o, "initial", 0.5);
	if (initial < 0.0) initial = 0.0;
	if (initial > 1.0) initial = 1.0;

	auto sl = ce::slider(
		ce::basic_thumb<16>(ce::colors::white),
		ce::basic_track<6, false>(ce::colors::white.opacity(0.4f)),
		initial
	);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		sl.on_change = [cb_id, user_cb](double pos) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{pos});
		};
	}
	auto shared = ce::share(std::move(sl));
	register_id(o, shared);
	note_initial_focus(o, shared);
	return shared;
}

//---------------------------------------------------------------------------
// slider_with_range — min/max ラベル付き 0..1 スライダ
//   { "type": "slider_with_range", "id": "...", "min": 0, "max": 100,
//     "initial": 50 }
// initial は min..max スケールで指定 (double)、 内部では 0..1 に正規化。
// 値変化時に min + (max - min) * pos を value_t{double} で通知。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_slider_with_range(const picojson::object& o)
{
	std::string id = string_or(o, "id");
	int min_v = static_cast<int>(number_or(o, "min", 0.0));
	int max_v = static_cast<int>(number_or(o, "max", 100.0));
	if (max_v <= min_v) max_v = min_v + 1;
	double initial_val = number_or(o, "initial", (min_v + max_v) * 0.5);
	double span = static_cast<double>(max_v - min_v);
	double pos = (initial_val - min_v) / span;
	if (pos < 0.0) pos = 0.0;
	if (pos > 1.0) pos = 1.0;

	float fs = static_cast<float>(number_or(o, "font_size", 1.0));
	auto rs = ce::slider_with_range(min_v, max_v, pos, fs);
	// rs.focus は shared_ptr<element>。 on_change を仕込むには basic_slider_base
	// に dynamic_cast する必要あり。 lib の slider() factory は
	// basic_slider<Thumb,Track,basic_slider_base> を返すので継承関係 OK。
	if (!id.empty()) {
		if (auto sb = std::dynamic_pointer_cast<ce::basic_slider_base>(rs.focus)) {
			auto cb_id = id;
			auto user_cb = _cb;
			double mn = static_cast<double>(min_v);
			double sp = span;
			sb->on_change = [cb_id, user_cb, mn, sp](double p) {
				if (user_cb) user_cb(cb_id, /*is_button_click=*/false,
				                     value_t{mn + sp * p});
			};
		}
	}
	register_id(o, rs.focus);
	note_initial_focus(o, rs.focus);
	return rs.widget;
}

//---------------------------------------------------------------------------
// labeled_row — 左カラムに固定幅ラベル + 残りに child
//   { "type": "labeled_row", "label": "Volume", "label_width": 180,
//     "child": { ... } }
// child の最初の要素を click-focus target にする (mouse click でラベル領域を
// 叩いても child に focus が飛ぶ)。
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// pad_icon — Kenney input prompts のコントローラアイコン。
//   { "type": "pad_icon", "name": "face_south", "height": 48 }
//   { "type": "pad_icon", "name": "a",          "use_font": true, "size": 1.5 }
//
// SVG 版 (デフォルト): tvg::Picture で SVG を描画。 height は logical 高さ。
// font 版 (use_font: true): Kenney font + codepoint で label として出す。
//   size は label の relative_font_size。
// theme は global state (parse_top_level で top-level "pad_theme" を見て
// 切り替え済み)。 名前 / theme で解決できなければ pad_icon::draw が灰色
// プレースホルダを出すか、 use_font 時は空 label。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_pad_icon(const picojson::object& o)
{
	auto name = string_or(o, "name");
	if (name.empty()) {
		SDL_Log("elements_modal: pad_icon without 'name'");
		return nullptr;
	}
	bool use_font = false;
	if (auto* v = get_field(o, "use_font"); v && v->is<bool>()) {
		use_font = v->get<bool>();
	}
	if (use_font) {
		float size = static_cast<float>(number_or(o, "size", 1.0));
		auto cp = ce::resolve_pad_icon_codepoint(name);
		auto fam = ce::pad_icon_font_family();
		if (cp == 0 || fam.empty()) {
			// Fallback: render the logical name as a regular label so layout
			// stays valid.
			return ce::share(ce::label("[" + name + "]")
				.relative_font_size(size)
				.font_color(ce::colors::white));
		}
		// Embed codepoint as a UTF-8 string into a label and set its font.
		char buf[8] = {0};
		int n = 0;
		if (cp < 0x80) {
			buf[n++] = static_cast<char>(cp);
		} else if (cp < 0x800) {
			buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
			buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
			buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			buf[n++] = static_cast<char>(0xF0 | (cp >> 18));
			buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
		}
		// fam は pad_icon_font_family() の戻りで program-lifetime な
		// std::string を指す string_view なので、 font_descr の string_view
		// メンバに直接乗せて OK。
		ce::font_descr fd{fam};
		auto lbl = ce::label(std::string(buf, n))
			.relative_font_size(size)
			.font_color(ce::colors::white)
			.font(fd);
		return ce::share(std::move(lbl));
	}
	float h = static_cast<float>(number_or(o, "height", 48.0));
	bool colored = false;
	if (auto* v = get_field(o, "colored"); v && v->is<bool>()) {
		colored = v->get<bool>();
	}
	return ce::share(ce::pad_icon(name, h, colored));
}

element_ptr LayoutBuilder::build_labeled_row(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) {
		SDL_Log("elements_modal: labeled_row without 'child'");
		return nullptr;
	}
	auto text = string_or(o, "label");
	float lw = static_cast<float>(number_or(o, "label_width", 180.0));
	float fs = static_cast<float>(number_or(o, "font_size", 1.0));
	return ce::share(ce::labeled_row(std::move(text), child, lw, fs));
}

//---------------------------------------------------------------------------
// "input" ブロック → view 設定クロージャ生成。
//
// JSON 例:
//   "input": {
//     "arrow_focus_nav": true,
//     "dpad_mode":         "both",     // disabled / focus / value / both
//     "left_stick_mode":   "focus",
//     "right_stick_mode":  "value",
//     "trigger_mode":      "disabled",
//     "stick_deadzone":    0.15,
//     "stick_value_speed": 1.0,
//     "pad_bindings": [
//       { "pad": "a", "key": "enter" },
//       { "pad": "x", "key": "tab", "mods": ["shift"] }
//     ],
//     "shortcuts": [
//       { "key": "f", "mods": ["ctrl"], "target": "search_btn" },
//       { "pad": "lb", "target": "prev_btn" },
//       { "pad": "rb", "target": "next_btn", "force": true }
//     ]
//   }
//
// id_map は build 中に LayoutBuilder が収集した id → element の対応表。
//---------------------------------------------------------------------------
std::function<void(ce::view&)> build_input_applier(
	const picojson::object& input_obj,
	std::map<std::string, element_ptr> id_map)
{
	struct settings {
		bool arrow_nav_set = false;
		bool arrow_nav = false;

		bool dpad_set = false;       ce::pad_axis_mode dpad_mode    = ce::pad_axis_mode::both;
		bool lstick_set = false;     ce::pad_axis_mode lstick_mode  = ce::pad_axis_mode::focus;
		bool rstick_set = false;     ce::pad_axis_mode rstick_mode  = ce::pad_axis_mode::value;
		bool trigger_set = false;    ce::pad_axis_mode trigger_mode = ce::pad_axis_mode::disabled;

		bool deadzone_set = false;   float deadzone = 0.15f;
		bool speed_set = false;      float value_speed = 1.0f;

		struct pad_bind {
			ce::pad_button btn;
			ce::key_code   key;
			int            mods;
		};
		std::vector<pad_bind> pad_bindings;

		struct key_sc { ce::key_code key; int mods; std::string target; bool force; };
		struct pad_sc { ce::pad_button btn;          std::string target; bool force; };
		std::vector<key_sc> key_shortcuts;
		std::vector<pad_sc> pad_shortcuts;
	};
	auto cfg = std::make_shared<settings>();

	if (auto* v = get_field(input_obj, "arrow_focus_nav"); v && v->is<bool>()) {
		cfg->arrow_nav_set = true;
		cfg->arrow_nav = v->get<bool>();
	}
	if (auto* v = get_field(input_obj, "dpad_mode"); v && v->is<std::string>()) {
		cfg->dpad_set = true;
		cfg->dpad_mode = parse_axis_mode(v->get<std::string>(), cfg->dpad_mode);
	}
	if (auto* v = get_field(input_obj, "left_stick_mode"); v && v->is<std::string>()) {
		cfg->lstick_set = true;
		cfg->lstick_mode = parse_axis_mode(v->get<std::string>(), cfg->lstick_mode);
	}
	if (auto* v = get_field(input_obj, "right_stick_mode"); v && v->is<std::string>()) {
		cfg->rstick_set = true;
		cfg->rstick_mode = parse_axis_mode(v->get<std::string>(), cfg->rstick_mode);
	}
	if (auto* v = get_field(input_obj, "trigger_mode"); v && v->is<std::string>()) {
		cfg->trigger_set = true;
		cfg->trigger_mode = parse_axis_mode(v->get<std::string>(), cfg->trigger_mode);
	}
	if (auto* v = get_field(input_obj, "stick_deadzone"); v && v->is<double>()) {
		cfg->deadzone_set = true;
		cfg->deadzone = static_cast<float>(v->get<double>());
	}
	if (auto* v = get_field(input_obj, "stick_value_speed"); v && v->is<double>()) {
		cfg->speed_set = true;
		cfg->value_speed = static_cast<float>(v->get<double>());
	}

	// pad_bindings
	if (auto* arr = get_array(input_obj, "pad_bindings")) {
		for (const auto& v : *arr) {
			if (!v.is<picojson::object>()) continue;
			const auto& bo = v.get<picojson::object>();
			auto pad_name = string_or(bo, "pad");
			auto key_name = string_or(bo, "key");
			if (pad_name.empty() || key_name.empty()) continue;
			auto pb = parse_pad_button(pad_name);
			auto kc = parse_key_code(key_name);
			if (pb == ce::pad_button::unknown || kc == ce::key_code::unknown) {
				SDL_Log("elements_modal: pad_bindings: unknown pad=%s key=%s",
					pad_name.c_str(), key_name.c_str());
				continue;
			}
			int mods = 0;
			if (auto* ma = get_array(bo, "mods")) mods = parse_modifiers(*ma);
			cfg->pad_bindings.push_back({pb, kc, mods});
		}
	}

	// shortcuts
	if (auto* arr = get_array(input_obj, "shortcuts")) {
		for (const auto& v : *arr) {
			if (!v.is<picojson::object>()) continue;
			const auto& so = v.get<picojson::object>();
			auto target = string_or(so, "target");
			if (target.empty()) continue;
			bool force = false;
			if (auto* fv = get_field(so, "force"); fv && fv->is<bool>()) {
				force = fv->get<bool>();
			}
			int mods = 0;
			if (auto* ma = get_array(so, "mods")) mods = parse_modifiers(*ma);

			auto key_name = string_or(so, "key");
			if (!key_name.empty()) {
				auto kc = parse_key_code(key_name);
				if (kc == ce::key_code::unknown) {
					SDL_Log("elements_modal: shortcut: unknown key=%s", key_name.c_str());
					continue;
				}
				cfg->key_shortcuts.push_back({kc, mods, target, force});
				continue;
			}
			auto pad_name = string_or(so, "pad");
			if (!pad_name.empty()) {
				auto pb = parse_pad_button(pad_name);
				if (pb == ce::pad_button::unknown) {
					SDL_Log("elements_modal: shortcut: unknown pad=%s", pad_name.c_str());
					continue;
				}
				cfg->pad_shortcuts.push_back({pb, target, force});
				continue;
			}
			SDL_Log("elements_modal: shortcut: needs 'key' or 'pad'");
		}
	}

	// クロージャ: view が用意できた時点 (content 後) で実行
	auto id_map_shared = std::make_shared<std::map<std::string, element_ptr>>(std::move(id_map));
	return [cfg, id_map_shared](ce::view& view_) {
		if (cfg->arrow_nav_set)   view_.arrow_focus_navigation(cfg->arrow_nav);
		if (cfg->dpad_set)        view_.dpad_mode(cfg->dpad_mode);
		if (cfg->lstick_set)      view_.left_stick_mode(cfg->lstick_mode);
		if (cfg->rstick_set)      view_.right_stick_mode(cfg->rstick_mode);
		if (cfg->trigger_set)     view_.trigger_mode(cfg->trigger_mode);
		if (cfg->deadzone_set)    view_.stick_deadzone(cfg->deadzone);
		if (cfg->speed_set)       view_.stick_value_speed(cfg->value_speed);

		for (auto const& b : cfg->pad_bindings) {
			view_.bind_pad_button(b.btn, b.key, b.mods);
		}
		for (auto const& s : cfg->key_shortcuts) {
			auto it = id_map_shared->find(s.target);
			if (it == id_map_shared->end()) {
				SDL_Log("elements_modal: shortcut target not found: %s",
					s.target.c_str());
				continue;
			}
			ce::key_info ki{s.key, ce::key_action::press, s.mods};
			view_.bind_shortcut(ki, it->second, s.force);
		}
		for (auto const& s : cfg->pad_shortcuts) {
			auto it = id_map_shared->find(s.target);
			if (it == id_map_shared->end()) {
				SDL_Log("elements_modal: shortcut target not found: %s",
					s.target.c_str());
				continue;
			}
			view_.bind_shortcut(s.btn, it->second, s.force);
		}
	};
}

//---------------------------------------------------------------------------
// Top level parsing
//---------------------------------------------------------------------------
parsed_layout build_top_level(const picojson::value& root, event_callback cb)
{
	parsed_layout result;
	if (!root.is<picojson::object>()) {
		SDL_Log("elements_modal: top-level must be an object");
		return result;
	}
	const auto& o = root.get<picojson::object>();

	if (auto* arr = get_array(o, "size")) {
		result.width  = int_at(*arr, 0, result.width);
		result.height = int_at(*arr, 1, result.height);
	}

	// "pad_theme": "xbox"|"ps"|"switch"|"keyboard"|"none" — 任意。 指定が
	// あれば content build 前に global pad theme を切り替え、 build_pad_icon
	// 内の resolve が新 theme で走る。 指定なしの場合は呼出側 (argv 等) で
	// セットされた既存値を維持。
	if (auto* v = get_field(o, "pad_theme"); v && v->is<std::string>()) {
		auto t = ce::parse_pad_theme(v->get<std::string>());
		if (t != ce::pad_theme::none ||
		    v->get<std::string>() == "none") {
			ce::set_pad_theme(t);
		}
	}

	LayoutBuilder builder(std::move(cb));
	if (auto* v = get_field(o, "locale"); v && v->is<std::string>()) {
		builder.set_default_locale(v->get<std::string>());
	}

	element_ptr content;
	if (auto* v = get_field(o, "content")) content = builder.build(*v);

	if (!content) {
		SDL_Log("elements_modal: missing or invalid 'content'");
		return result;
	}

	if (auto* arr = get_array(o, "background")) {
		ce::color bg = parse_color(*arr);
		ce::layer_composite ly;
		ly.push_back(ce::share(ce::box(bg)));
		ly.push_back(content);
		result.root = ce::share(std::move(ly));
	} else {
		result.root = content;
	}
	result.initial_focus = builder.take_initial_focus();
	result.close_button_ids = builder.take_close_button_ids();

	// "input" ブロック (任意): view に対する arrow_focus_nav / pad mode /
	// pad bindings / shortcuts を設定するクロージャを作る。
	if (auto* v = get_field(o, "input"); v && v->is<picojson::object>()) {
		result.apply_input = build_input_applier(v->get<picojson::object>(),
			builder.take_id_map());
	}
	return result;
}

} // anonymous

//---------------------------------------------------------------------------
// 公開 API
//---------------------------------------------------------------------------
parsed_layout parse_from_string(const std::string& json_utf8,
                                event_callback cb)
{
	// JSONC 前処理 (const + cbegin/cend で渡す。 non-const iterator だと
	// picojson の template instance の都合で parse 結果が破壊されるケースがある)。
	const std::string preprocessed = preprocess_jsonc(json_utf8);

	picojson::value v;
	std::string err;
	picojson::parse(v, preprocessed.cbegin(), preprocessed.cend(), &err);
	if (!err.empty()) {
		SDL_Log("elements_modal: parse error: %s", err.c_str());
		return {};
	}
	return build_top_level(v, std::move(cb));
}

} // namespace elements_modal

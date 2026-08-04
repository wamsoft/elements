//---------------------------------------------------------------------------
// elements_modal: JSON → Elements 要素ツリー変換
//
// 純粋 C++ + SDL_Log + std::function ベース。 JSON / JSONC (// 行コメント /
// /* */ ブロックコメント / 末尾カンマ) を文字列リテラル保護つき前段 strip で
// 解釈し、 picojson でパース、 cycfi::elements のツリーに変換する。
//---------------------------------------------------------------------------
#include "json_layout.h"

#include "em_platform.h"
#include <picojson/picojson.h>
#include <elements/element/anchored_text.hpp>   // C6: 絶対 baseline アンカー描画

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

// bool 読み取り: JSON の true/false に加えて number の 0 / 非 0 も受け付ける。
// 吉里吉里 TJS2 など bool 型を持たないホスト言語から辞書を JSON 化して渡すと
// true が 1 (number) で届くため、 bool フィールドは数値も真偽値として扱う。
// v が bool / number 以外 (null, string, ...) なら false を返し out は不変。
bool bool_field(const picojson::value* v, bool& out)
{
	if (!v) return false;
	if (v->is<bool>()) { out = v->get<bool>(); return true; }
	if (v->is<double>()) { out = v->get<double>() != 0.0; return true; }
	return false;
}

// フィールドが存在して truthy なら true を返すショートカット。
bool truthy_field(const picojson::value* v)
{
	bool b = false;
	return bool_field(v, b) && b;
}

const picojson::array* get_array(const picojson::object& o, const char* key)
{
	if (auto* v = get_field(o, key); v && v->is<picojson::array>()) {
		return &v->get<picojson::array>();
	}
	return nullptr;
}

//---------------------------------------------------------------------------
// フォントサイズ解決ヘルパ。
//
// "size" / "font_size" は **絶対ピクセル**。 "size_scale" / "font_size_scale"
// は **テーマ既定 (label_font._size、 通常 14px) に対する倍率**。 両方
// 指定された場合は size が優先。 どちらも未指定なら theme 既定 (= 1.0
// 倍率) と等価の px を返す。
//
// resolve_font_px: px 値そのまま (label.font_size() に渡す用)。
// resolve_font_scale: lib widget が内部で `font._size * scale` を計算する
// 都合上、 dispatch で px を scale に戻して渡すための変換。 theme の base
// が 14 のとき "size": 28 → scale 2.0 になる。
//---------------------------------------------------------------------------
float resolve_font_px(const picojson::object& o,
                      const char* size_key,
                      const char* scale_key)
{
	if (auto* v = get_field(o, size_key); v && v->is<double>()) {
		return static_cast<float>(v->get<double>());
	}
	float base = cycfi::elements::get_theme().label_font._size;
	if (auto* v = get_field(o, scale_key); v && v->is<double>()) {
		return base * static_cast<float>(v->get<double>());
	}
	return base;
}

float resolve_font_scale(const picojson::object& o,
                         const char* size_key,
                         const char* scale_key)
{
	float base = cycfi::elements::get_theme().label_font._size;
	if (base <= 0.0f) base = 14.0f;  // 念のため div-by-zero 回避
	return resolve_font_px(o, size_key, scale_key) / base;
}

// 指定の有無だけ判定。 default 適用させたくない場面用。
bool has_font_field(const picojson::object& o,
                    const char* size_key,
                    const char* scale_key)
{
	return get_field(o, size_key) != nullptr ||
	       get_field(o, scale_key) != nullptr;
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
// VariableStore — 名前付き文字列変数 + subscriber notify。
// JSON top-level "vars": {name: value} で初期化、 focusable widget の
// "vars_on_focus": {name: value} で書込、 label の "text_var": "name" で読込。
// poll closure が focus 変化を検知して write を実行 → subscribers (label の
// set_text closure) が呼ばれる。
//---------------------------------------------------------------------------
class VariableStore
{
public:
	void set_initial(const std::string& name, std::string value)
	{
		_values[name] = std::move(value);
	}
	const std::string* get(const std::string& name) const
	{
		auto it = _values.find(name);
		return it == _values.end() ? nullptr : &it->second;
	}
	void set(const std::string& name, const std::string& value)
	{
		auto& cur = _values[name];
		if (cur == value) return;
		cur = value;
		auto it = _subs.find(name);
		if (it == _subs.end()) return;
		for (auto& cb : it->second) cb(cur);
	}
	void subscribe(const std::string& name,
	               std::function<void(const std::string&)> cb)
	{
		_subs[name].push_back(std::move(cb));
	}

private:
	std::map<std::string, std::string> _values;
	std::map<std::string, std::vector<std::function<void(const std::string&)>>> _subs;
};

//---------------------------------------------------------------------------
// StringStore — textID → 言語別文字列の対応表 + 現在言語 + subscriber notify。
// VariableStore と同じ subscribe/notify パターンの i18n 版。
// JSON top-level "strings": { id: { lang: "string" } } で対応表を登録、
// top-level "lang": "ja" で初期言語、 label の "text_id": "id" で参照する。
// set_language() で全 subscriber (label の set_text closure) を再発火し、
// 実行中の言語切替に全 widget が追従する (= EUI Phase 2 の核心)。
//---------------------------------------------------------------------------
class StringStore
{
public:
	// "strings" の 1 エントリを登録 (id → {lang: string})。
	void set_entry(const std::string& id, std::map<std::string, std::string> by_lang)
	{
		_table[id] = std::move(by_lang);
	}

	// 現在言語を設定。 既に subscribe 済みの label をすべて再解決して通知。
	void set_language(const std::string& lang)
	{
		if (_lang == lang) return;
		_lang = lang;
		for (auto& kv : _subs) {
			std::string v = resolve(kv.first);
			for (auto& cb : kv.second) cb(v);
		}
		// text_id に紐づかない一般の言語変更 subscriber (locale_variant 等) を発火。
		for (auto& cb : _lang_subs) cb(_lang);
	}

	const std::string& language() const { return _lang; }

	// textID を現在言語で解決。 未知 id は id 文字列をそのまま返す。
	// 現在言語にエントリが無ければ先頭言語へフォールバック。
	std::string resolve(const std::string& id) const
	{
		auto it = _table.find(id);
		if (it == _table.end() || it->second.empty()) return id;
		const auto& by_lang = it->second;
		if (!_lang.empty()) {
			auto jt = by_lang.find(_lang);
			if (jt != by_lang.end()) return jt->second;
		}
		return by_lang.begin()->second;
	}

	// 対応表に id が存在するか (build_label の fallback 判定用)。
	bool has(const std::string& id) const { return _table.count(id) != 0; }

	void subscribe(const std::string& id,
	               std::function<void(const std::string&)> cb)
	{
		_subs[id].push_back(std::move(cb));
	}

	// text_id に依らず「言語が変わったら呼ぶ」subscriber (locale_variant のデッキ
	// 切替など)。 set_language() のたびに新言語で発火する。
	void subscribe_language(std::function<void(const std::string&)> cb)
	{
		_lang_subs.push_back(std::move(cb));
	}

private:
	std::map<std::string, std::map<std::string, std::string>> _table; // id -> {lang: str}
	std::string _lang;
	std::map<std::string, std::vector<std::function<void(const std::string&)>>> _subs;
	std::vector<std::function<void(const std::string&)>> _lang_subs;
};

//---------------------------------------------------------------------------
// LayoutBuilder — element ツリーを再帰生成
//---------------------------------------------------------------------------
class LayoutBuilder
{
public:
	explicit LayoutBuilder(event_callback cb) : _cb(std::move(cb)) {}

	void set_default_locale(std::string locale) { _default_locale = std::move(locale); }
	void set_resource_base(std::string base) { _resource_base = std::move(base); }

	// top-level "font_scale" (既定 1.0)。 明示 "size"/"size_scale" を持たない
	// widget の既定フォント倍率として使う。 button/toggle/radio/check_box は
	// styler の size 引数に、 label は font_size(px) の既定に流す。 1.0 のとき
	// 従来と完全一致 (opt-in)。
	void set_font_scale(float s) { _font_scale = (s > 0.0f) ? s : 1.0f; }
	std::map<std::string, cycfi::elements::pixmap_ptr>& atlases() { return _atlases; }
	element_ptr build(const picojson::value& v);

	// build() の型ディスパッチ本体 ("animate" ラップ前)。
	element_ptr build_dispatch(const picojson::object& o, const std::string& type);

	// 要素に "animate" があれば変換 proxy で包み、 演出束縛を _animations に
	// 積む。 無ければ el をそのまま返す。 Phase A は enter 発火のみ。
	element_ptr apply_animation(const picojson::object& o, element_ptr el);

	// build 中に集めたパーツ演出束縛 (xform_state を proxy と共有)。
	std::vector<anim_binding> take_animations() { return std::move(_animations); }

	// 相対パスを resource_base (= ホストが指定するベースディレクトリ) で
	// 解決して fs::path にする。 path が絶対ならそのまま。
	cycfi::fs::path resolve_resource(const std::string& path) const
	{
		if (path.empty()) return {};
		bool absolute = (path[0] == '/' || path[0] == '\\'
		                 || (path.size() > 1 && path[1] == ':'));
		if (absolute || _resource_base.empty()) {
			return cycfi::fs::path(path);
		}
		return cycfi::fs::path(_resource_base + path);
	}

	// "initial_focus": true が指定された要素 (なければ nullptr)。
	// build() 完了後、 ホストが view.focus(...) に渡すために取得する。
	element_ptr take_initial_focus() { return std::move(_initial_focus); }

	// id 付き要素 → element_ptr のマップ。 shortcut の "target": "<id>" 解決
	// + ホスト側 focus_by_id 用。 ランタイム複数参照ありうるので shared で
	// 渡せるラップ版も提供。
	std::map<std::string, element_ptr> take_id_map() { return std::move(_id_to_element); }
	const std::map<std::string, element_ptr>& id_map() const { return _id_to_element; }

	// id 付き要素を「登録順」で id+type 列挙 (エージェント/デバッグの UI ツリー
	// dump 用)。 type は JSON の "type" 文字列。
	std::vector<std::pair<std::string, std::string>> take_id_types()
	{ return std::move(_id_types); }

	// focus poll クロージャが内部で更新する「現在 focused id」スロット。
	// LayoutBuilder と take_focus_poll() のクロージャで shared (= 共有
	// shared_ptr<string>)。 ホストは parsed_layout 経由でこの slot を覗いて
	// "今 focus されてる id" を得る。
	std::shared_ptr<std::string> focused_id_slot() { return _focused_id_slot; }

	// hover poll が更新する「現在 hover されている id」スロット。
	std::shared_ptr<std::string> hovered_id_slot() { return _hovered_id_slot; }

	// "close_on_click": true が指定された button の id 集合。
	std::set<std::string> take_close_button_ids() { return std::move(_close_button_ids); }

	// VariableStore は labels の subscribers が参照するので shared_ptr で持つ。
	// 親 (top-level) が "vars" 初期値を流し込むために借用 setter。
	std::shared_ptr<VariableStore> vars() { return _vars; }

	// StringStore (i18n 対応表)。 親 (top-level) が "strings"/"lang" を流し込み、
	// label の text_id が参照する。 set_language closure 生成にも使う。
	std::shared_ptr<StringStore> strings() { return _strings; }

	// focus poll クロージャを生成。 毎フレーム呼ぶと現在の focus を見て
	// vars_on_focus を _vars に流し込み、 subscribers (label set_text) を発火。
	// take 系メソッドなので 1 回しか呼ばない。
	std::function<void()> take_focus_poll();

	// hover poll クロージャを生成。 毎フレーム呼ぶと button 系の hilite() を見て
	// 現在 hover されている id を _hovered_id_slot に書く。 1 回だけ呼ぶ。
	std::function<void()> take_hover_poll();

	// build 中に install する「view& を引数に取る」追加 set-up クロージャ。
	// 主に bind_shortcut を仕掛けたい widget (tab_view など) が使う。
	// build_top_level でこれらを apply_input にチェーンして公開する。
	void add_deferred_view_callback(std::function<void(cycfi::elements::view&)> cb)
	{
		_deferred_view_cbs.push_back(std::move(cb));
	}
	std::vector<std::function<void(cycfi::elements::view&)>>
	take_deferred_view_callbacks()
	{
		return std::move(_deferred_view_cbs);
	}

private:
	event_callback _cb;
	std::string _default_locale;
	std::string _resource_base;
	float _font_scale = 1.0f;   // top-level "font_scale" (既定 1.0 = 従来一致)

	// widget の実効フォント倍率。 明示 "size"/"size_scale" があればそれを
	// 優先 (resolve_font_scale = px/base)、 なければ top-level _font_scale。
	// button/toggle/radio/check_box の styler size 引数に渡す用。
	float effective_font_scale(const picojson::object& o) const
	{
		if (has_font_field(o, "size", "size_scale"))
			return resolve_font_scale(o, "size", "size_scale");
		return _font_scale;
	}

	// アトラス画像 (atlas_image / atlas_button / atlas_slider 用) を JSON
	// top-level "atlases" で名前→pixmap_ptr に解決する。 同名 atlas が複数
	// widget で参照されたら同じ pixmap_ptr を共有。
	std::map<std::string, cycfi::elements::pixmap_ptr> _atlases;
	element_ptr _initial_focus;
	std::map<std::string, element_ptr> _id_to_element;
	std::vector<std::pair<std::string, std::string>> _id_types;  // 登録順 id+type
	std::set<std::string> _close_button_ids;
	std::vector<anim_binding> _animations;  // "animate" から生成した演出束縛
	std::shared_ptr<VariableStore> _vars = std::make_shared<VariableStore>();
	std::shared_ptr<StringStore> _strings = std::make_shared<StringStore>();
	std::map<std::string, std::map<std::string, std::string>> _vars_on_focus;
	std::vector<std::pair<std::string, std::function<bool()>>> _focusables;
	std::vector<std::pair<std::string, std::function<bool()>>> _hoverables;
	std::vector<std::function<void(cycfi::elements::view&)>> _deferred_view_cbs;
	std::shared_ptr<std::string> _focused_id_slot = std::make_shared<std::string>();
	std::shared_ptr<std::string> _hovered_id_slot = std::make_shared<std::string>();

	// focusable build site で、 id と「現在 focus されているか」を返す
	// closure を _focusables に積む。 typed shared_ptr を weak_ptr で保持。
	template <typename P>
	void note_focusable(const std::string& id, std::shared_ptr<P> ptr)
	{
		if (id.empty() || !ptr) return;
		std::weak_ptr<P> w = ptr;
		_focusables.emplace_back(id, [w]() {
			auto p = w.lock();
			return p && p->focused();
		});
		note_hoverable(id, ptr);   // hover も同時登録 (button 系のみ poll で true)
	}

	// hover 対象 (button 系 = basic_button 派生) を id と「現在 hover (hilite) 中か」を
	// 返す closure として _hoverables に積む。 element ptr を weak で保持し、 poll 時に
	// basic_button へ dynamic_cast して hilite() を読む (proxy も basic_button を継承)。
	void note_hoverable(const std::string& id, const element_ptr& el)
	{
		if (id.empty() || !el) return;
		std::weak_ptr<ce::element> w = el;
		_hoverables.emplace_back(id, [w]() {
			auto p = w.lock();
			auto* b = dynamic_cast<ce::basic_button*>(p.get());
			return b && b->hilite();
		});
	}

	// "vars_on_focus": {name: value} を JSON object から読んで _vars_on_focus[id]
	// に登録。 値が文字列でない要素は無視。
	void note_vars_on_focus(const picojson::object& o, const std::string& id)
	{
		if (id.empty()) return;
		auto* v = get_field(o, "vars_on_focus");
		if (!v || !v->is<picojson::object>()) return;
		const auto& obj = v->get<picojson::object>();
		auto& m = _vars_on_focus[id];
		for (auto& kv : obj) {
			if (kv.second.is<std::string>()) m[kv.first] = kv.second.get<std::string>();
		}
	}

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

	// i18n: proxy ベースの button (invert/ring/plain) で "text_id" があれば、
	// styler (= proxy の subject、 text_writer 派生) を StringStore に subscribe
	// して言語切替で set_text する。 初期値も resolve して同期する。
	void subscribe_button_text_id(const picojson::object& o, const element_ptr& shared);
	element_ptr build_slider      (const picojson::object& o);
	element_ptr build_slider_with_range(const picojson::object& o);
	element_ptr build_labeled_row (const picojson::object& o);
	element_ptr build_pad_icon    (const picojson::object& o);
	element_ptr build_band        (const picojson::object& o);
	element_ptr build_sprite_button(const picojson::object& o);
	element_ptr build_gizmo_image (const picojson::object& o);
	element_ptr build_floating    (const picojson::object& o);
	element_ptr build_canvas      (const picojson::object& o);
	element_ptr build_locale_variant(const picojson::object& o);
	element_ptr build_atlas_image (const picojson::object& o);
	element_ptr build_animated_sprite(const picojson::object& o);
	element_ptr build_atlas_button(const picojson::object& o);
	element_ptr build_atlas_toggle(const picojson::object& o);
	element_ptr build_atlas_choice(const picojson::object& o);
	element_ptr build_atlas_slider(const picojson::object& o);
	element_ptr build_atlas_progress(const picojson::object& o);
	element_ptr build_radio_button (const picojson::object& o);

	// 名前 → pixmap_ptr 解決。 未登録ならログ + nullptr。
	cycfi::elements::pixmap_ptr lookup_atlas(const std::string& name);
	element_ptr build_tab_view    (const picojson::object& o);

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

	element_ptr el = build_dispatch(o, type);
	// "animate" 指定があれば変換 proxy で包み、 演出束縛を登録する (Phase A)。
	if (el) el = apply_animation(o, std::move(el));
	return el;
}

element_ptr LayoutBuilder::build_dispatch(const picojson::object& o,
                                          const std::string& type)
{
	if (type == "label")         return build_label(o);
	if (type == "button")        return build_button(o);
	if (type == "vtile")         return build_vtile(o);
	if (type == "htile")         return build_htile(o);
	if (type == "margin")        return build_margin(o);
	if (type == "box")           return build_box(o);
	if (type == "layer")         return build_layer(o);
	if (type == "align_center")        return build_align(o, "center");
	if (type == "align_left")          return build_align(o, "left");
	if (type == "align_right")         return build_align(o, "right");
	if (type == "align_top")           return build_align(o, "top");
	if (type == "align_middle")        return build_align(o, "middle");
	if (type == "align_bottom")        return build_align(o, "bottom");
	if (type == "align_center_middle") return build_align(o, "center_middle");
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
	if (type == "band")          return build_band(o);
	if (type == "sprite_button") return build_sprite_button(o);
	if (type == "gizmo_image")   return build_gizmo_image(o);
	if (type == "floating")      return build_floating(o);
	if (type == "canvas")        return build_canvas(o);
	if (type == "locale_variant") return build_locale_variant(o);
	if (type == "atlas_image")    return build_atlas_image(o);
	if (type == "animated_sprite") return build_animated_sprite(o);
	if (type == "atlas_button")   return build_atlas_button(o);
	if (type == "atlas_toggle")   return build_atlas_toggle(o);
	if (type == "atlas_check")    return build_atlas_toggle(o);  // alias
	if (type == "atlas_choice")   return build_atlas_choice(o);
	if (type == "atlas_radio")    return build_atlas_choice(o);  // alias
	if (type == "atlas_slider")   return build_atlas_slider(o);
	if (type == "atlas_progress") return build_atlas_progress(o);
	if (type == "radio_button")   return build_radio_button(o);
	if (type == "tab_view")      return build_tab_view(o);

	em_logf("elements_modal: unknown element type: %s", type.c_str());
	return nullptr;
}

//---------------------------------------------------------------------------
// apply_animation — 要素の "animate" を変換 proxy + 演出束縛に落とす (Phase A)。
//
// "animate" は 1 オブジェクト or 配列。 配列の各エントリは同じ xform_state を
// 共有するので、 移動 + 拡縮 + 回転の同時掛けが自然に合成される。 周囲は
// reflow しない (xform_base は非 reflow オーバーレイ)。
//
// 受理フィールド (各エントリ):
//   "type":     "move" | "scale" | "rotate" | "fade"   (既定 move)
//   "from"/"to": move/scale は [x,y] or スカラ、 rotate は度、 fade は % (0..100)
//   "frames":   再生フレーム数 (60fps 換算) — 無ければ "duration_ms" (既定 300)
//   "easing":   "out_cubic" 等 (台形指定が無いとき)
//   "accel"/"decel": 台形プロファイルの加速/減速割合 (指定で easing より優先)
//   "loops":    明滅/ループ回数 (0=ループ無し)
//   "yoyo":     往復 (明滅 1 回 = 2 pass)
//   "pivot":    拡縮/回転のピボット [ox,oy] (0..1, 既定中央)。 最初の指定を採用
//   "on":       発火トリガ "enter"(既定)/"focus"/"select"/"exit"。 focus/select は
//               要素の "id" と紐付き、 その id への発火だけ反応する (要素に id 必須)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::apply_animation(const picojson::object& o, element_ptr el)
{
	auto* av = get_field(o, "animate");
	if (!av || !el) return el;

	// focus/select の照合に使う対象要素 id (enter/exit では未使用)。
	const std::string owner_id = string_or(o, "id");

	std::vector<const picojson::object*> specs;
	if (av->is<picojson::object>()) {
		specs.push_back(&av->get<picojson::object>());
	} else if (av->is<picojson::array>()) {
		for (auto& e : av->get<picojson::array>())
			if (e.is<picojson::object>()) specs.push_back(&e.get<picojson::object>());
	}
	if (specs.empty()) return el;

	auto st = std::make_shared<xform_state>();

	// [x,y] 配列 or スカラ (x=y へ展開) を読む。 無ければ (dx,dy)。
	auto read_xy = [](const picojson::object& s, const char* key,
	                  float dx, float dy, float& ox, float& oy) {
		ox = dx; oy = dy;
		if (auto* v = get_field(s, key)) {
			if (v->is<picojson::array>()) {
				auto& a = v->get<picojson::array>();
				if (a.size() > 0 && a[0].is<double>()) ox = float(a[0].get<double>());
				if (a.size() > 1 && a[1].is<double>()) oy = float(a[1].get<double>());
			} else if (v->is<double>()) {
				ox = oy = float(v->get<double>());
			}
		}
	};

	bool pivot_set = false;
	for (const picojson::object* sp : specs) {
		const auto& s = *sp;
		anim_binding b;
		b.st = st;

		// 発火トリガ。 エントリ毎の "on" を優先し、 無ければ enter。 focus/select
		// 束縛は要素 id へ紐付け、 その id への発火だけに反応する。
		b.trig = trigger_from_string(string_or(s, "on", "enter"));
		b.id   = owner_id;

		// duration: "frames" 優先 (要望はフレーム数指定が基本)。
		float dur_ms;
		if (auto* fv = get_field(s, "frames"); fv && fv->is<double>())
			dur_ms = frames_to_ms(float(fv->get<double>()));
		else
			dur_ms = float(number_or(s, "duration_ms", 300.0));
		b.prog.from = 0.0f; b.prog.to = 1.0f; b.prog.duration_ms = dur_ms;

		// 開始遅延 (スタッガー/シーケンス用)。 "delay" はフレーム、 "delay_ms" は ms。
		if (auto* dv = get_field(s, "delay"); dv && dv->is<double>())
			b.prog.delay_ms = frames_to_ms(float(dv->get<double>()));
		else
			b.prog.delay_ms = float(number_or(s, "delay_ms", 0.0));

		// 台形 (accel/decel) 指定があれば優先、 無ければ easing。
		const bool has_trap = get_field(s, "accel") || get_field(s, "decel");
		if (has_trap) {
			b.prog.use_trapezoid = true;
			b.prog.accel_frac = float(number_or(s, "accel", 0.0));
			b.prog.decel_frac = float(number_or(s, "decel", 0.0));
		} else {
			b.prog.ez = easing_from_string(string_or(s, "easing"), easing::linear);
		}

		// ループ/往復。 loops<0 → 無限、 0 → 1 pass、 N → N (yoyo なら 1 明滅=2pass)。
		const int loops = static_cast<int>(number_or(s, "loops", 0.0));
		bool yoyo = false;
		bool_field(get_field(s, "yoyo"), yoyo);
		b.prog.yoyo = yoyo;
		b.prog.iterations = (loops < 0)  ? 0          // 無限
		                  : (loops == 0) ? 1          // 1 回
		                  : (yoyo ? loops * 2 : loops);

		const std::string kind = string_or(s, "type");
		if (kind == "scale") {
			b.ch = anim_binding::channel::scale;
			read_xy(s, "from", 1.0f, 1.0f, b.ax, b.ay);
			read_xy(s, "to",   1.0f, 1.0f, b.bx, b.by);
		} else if (kind == "rotate") {
			b.ch = anim_binding::channel::rotate;
			b.ax = float(number_or(s, "from", 0.0));   // 度
			b.bx = float(number_or(s, "to",   0.0));
		} else if (kind == "fade") {
			b.ch = anim_binding::channel::fade;
			b.ax = float(number_or(s, "from",   0.0)) / 100.0f;   // % → [0,1]
			b.bx = float(number_or(s, "to",   100.0)) / 100.0f;
		} else {
			b.ch = anim_binding::channel::move;
			read_xy(s, "from", 0.0f, 0.0f, b.ax, b.ay);
			read_xy(s, "to",   0.0f, 0.0f, b.bx, b.by);
		}

		if (!pivot_set) {
			float ox, oy;
			read_xy(s, "pivot", 0.5f, 0.5f, ox, oy);
			st->ox = ox; st->oy = oy;
			pivot_set = true;
		}

		_animations.push_back(std::move(b));
	}

	// 内部 el を変換 proxy で包む。 id_map は内部 el を指したままで OK。
	return ce::share(xform(st, ce::hold_any(el)));
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
	if (truthy_field(get_field(o, "initial_focus"))) {
		_initial_focus = shared;
	}
}

void LayoutBuilder::register_id(const picojson::object& o,
                                 const element_ptr& shared)
{
	auto id = string_or(o, "id");
	if (!id.empty() && shared) {
		_id_to_element[id] = shared;
		_id_types.emplace_back(id, string_or(o, "type"));
	}
}

//---------------------------------------------------------------------------
// take_focus_poll — 毎フレーム呼ぶ closure を生成して返す。
// 内部状態 (vars / vars_on_focus / focusables / last_focused) を closure に
// move + shared 取込。 LayoutBuilder 死亡後も生存する。
//---------------------------------------------------------------------------
std::function<void()> LayoutBuilder::take_focus_poll()
{
	auto vars            = _vars;
	auto vars_on_focus   = std::move(_vars_on_focus);
	auto focusables      = std::move(_focusables);
	auto last_focused_id = _focused_id_slot;  // ホストと共有 (focused_id() 用)
	// "" は「何も focus されていない」を表す sentinel。 初回はその状態と
	// 比較されるので、 初回 focus に対して必ず 1 回 set される。
	return [vars, vars_on_focus, focusables, last_focused_id]() {
		std::string current;
		for (auto& kv : focusables) {
			if (kv.second()) { current = kv.first; break; }
		}
		if (current == *last_focused_id) return;
		*last_focused_id = current;
		if (current.empty()) return;
		auto it = vars_on_focus.find(current);
		if (it == vars_on_focus.end()) return;
		for (auto& var_kv : it->second) {
			vars->set(var_kv.first, var_kv.second);
		}
	};
}

//---------------------------------------------------------------------------
// take_hover_poll — button 系の hilite() を見て現在 hover 中の id を slot へ書く。
//---------------------------------------------------------------------------
std::function<void()> LayoutBuilder::take_hover_poll()
{
	auto hoverables      = std::move(_hoverables);
	auto last_hovered_id = _hovered_id_slot;
	return [hoverables, last_hovered_id]() {
		std::string current;
		for (auto& kv : hoverables) {
			if (kv.second()) { current = kv.first; break; }
		}
		if (current != *last_hovered_id) *last_hovered_id = current;
	};
}

//---------------------------------------------------------------------------
// 各 element 種別
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_label(const picojson::object& o)
{
	// "text_id": "id" (i18n) と "text_var": "varname" の動的 text 解決。
	// 優先順位: text_id > text_var > 静的 "text"。 いずれも初期 text を決め、
	// 後段で subscriber を仕掛けて言語/変数の更新で set_text が呼ばれる。
	std::string text_id  = string_or(o, "text_id");
	std::string text_var = string_or(o, "text_var");
	std::string text;
	if (!text_id.empty()) {
		// 未知 id は static "text" を fallback、 無ければ resolve() が id を返す。
		text = _strings->has(text_id) ? _strings->resolve(text_id)
		                              : string_or(o, "text", text_id);
	} else if (!text_var.empty()) {
		if (auto* init = _vars->get(text_var)) text = *init;
		else                                    text = string_or(o, "text");
	} else {
		text = string_or(o, "text");
	}
	std::string locale = string_or(o, "locale", _default_locale);

	// "size" = px 絶対 / "size_scale" = テーマ倍率 (両方なしなら theme 既定)。
	// label は font_size(px) を直接呼べる API なので px 値を渡す。
	bool has_size = has_font_field(o, "size", "size_scale");
	float sz = resolve_font_px(o, "size", "size_scale");
	// 明示 size なしでも top-level font_scale が 1.0 でなければ、 その倍率を
	// 既定サイズに適用して font_size(px) を必ず流す (label も UI 全体拡大に追従)。
	if (!has_size && _font_scale != 1.0f) {
		sz = ce::get_theme().label_font._size * _font_scale;
		has_size = true;
	}
	bool has_color = false;
	ce::color col;
	if (auto* arr = get_array(o, "color")) {
		has_color = true;
		col = parse_color(*arr);
	}

	element_ptr out;

	// "text_anchor": [ax, ay] があれば、 絶対 baseline アンカーで描く anchored_text
	// を使う (PSD 由来のテキスト位置忠実化 / C6)。 ax,ay は自身の bounds 左上からの
	// baseline 起点。 "text_align" (left/center/right) は ax を左端/中央/右端どちらの
	// 基準にするか。 "font" はフォントファミリ (空なら theme 既定)。
	if (auto* anc = get_array(o, "text_anchor"); anc && anc->size() >= 2) {
		float ax = static_cast<float>(anc->at(0).get<double>());
		float ay = static_cast<float>(anc->at(1).get<double>());
		int halign = ce::canvas::left;
		std::string ta = string_or(o, "text_align");
		if (ta == "center")     halign = ce::canvas::center;
		else if (ta == "right") halign = ce::canvas::right;
		float a_sz = has_size ? sz : ce::get_theme().label_font._size;
		ce::color a_col = has_color ? col : ce::get_theme().label_font_color;
		std::string family = string_or(o, "font");
		int tracking = static_cast<int>(number_or(o, "tracking", 0.0));
		float leading = static_cast<float>(number_or(o, "leading", 0.0));
		bool wrap = truthy_field(get_field(o, "wrap"));
		out = ce::make_anchored_text(text, family, a_sz, a_col, halign,
		                             ce::point{ax, ay}, tracking, leading, wrap, locale);
		// "runs" (run 別書式) があれば anchored_text に設定 (rich text)。 段落別
		// アラインは "para_align" (left/right/center の配列)。
		if (auto* rarr = get_array(o, "runs"); rarr && !rarr->empty()) {
			if (auto* at = dynamic_cast<ce::anchored_text*>(out.get())) {
				std::vector<ce::text_run> truns;
				for (auto& rv : *rarr) {
					if (!rv.is<picojson::object>()) continue;
					const auto& ro = rv.get<picojson::object>();
					ce::text_run tr;
					tr.text = string_or(ro, "t");
					tr.size = static_cast<float>(number_or(ro, "size", a_sz));
					if (auto* ca = get_array(ro, "color")) tr.col = parse_color(*ca);
					else tr.col = a_col;
					auto rf = ce::resolve_font_name(string_or(ro, "font"));
					tr.family = rf.ok ? rf.family : std::string{};
					tr.weight = rf.weight;
					tr.slant  = rf.slant;
					truns.push_back(std::move(tr));
				}
				at->set_runs(std::move(truns));
				if (auto* pa = get_array(o, "para_align")) {
					std::vector<int> pal;
					for (auto& v : *pa) {
						std::string s = v.is<std::string>() ? v.get<std::string>() : "left";
						pal.push_back(s == "center" ? ce::canvas::center
						            : s == "right"  ? ce::canvas::right
						            :                 ce::canvas::left);
					}
					at->set_para_aligns(std::move(pal));
				}
			}
		}
	} else {

	// label builder API は font_color / relative_font_size を呼ぶごとに
	// ラッパ型が変わるチェーン。 直接代入で繋げないので分岐する。
	// locale は文字列空なら付けない (default_locale 含む)。
	// 三項演算子 ?: は両 branch の shared_ptr 型が違うとマッチ失敗するので
	// if/else で element_ptr 代入する。
	auto base = ce::label(text);
	if (has_color && has_size) {
		auto e = base.font_color(col).font_size(sz);
		if (locale.empty()) out = ce::share(std::move(e));
		else                out = ce::share(e.locale(std::move(locale)));
	} else if (has_color) {
		auto e = base.font_color(col);
		if (locale.empty()) out = ce::share(std::move(e));
		else                out = ce::share(e.locale(std::move(locale)));
	} else if (has_size) {
		auto e = base.font_size(sz);
		if (locale.empty()) out = ce::share(std::move(e));
		else                out = ce::share(e.locale(std::move(locale)));
	} else {
		if (locale.empty()) out = ce::share(std::move(base));
		else                out = ce::share(base.locale(std::move(locale)));
	}
	}   // text_anchor else

	// text_id / text_var 指定があれば、 StringStore / VariableStore の更新で
	// label を set_text する subscriber を仕掛ける。 label は
	// default_label_styler (element + text_reader) + basic_label_styler_base
	// (Base + text_writer) なので、 text_writer インタフェース (set_text 持ち)
	// に dynamic_cast する。 static_text_box ではない (text_box は別系統)。
	if (!text_id.empty() || !text_var.empty()) {
		if (auto sp = std::dynamic_pointer_cast<ce::text_writer>(out)) {
			std::weak_ptr<ce::text_writer> w = sp;
			if (!text_id.empty()) {
				// 言語切替で再解決して set_text (EUI Phase 2 の動的更新)。
				_strings->subscribe(text_id, [w](const std::string& v) {
					if (auto p = w.lock()) p->set_text(v);
				});
			} else {
				_vars->subscribe(text_var, [w](const std::string& v) {
					if (auto p = w.lock()) p->set_text(v);
				});
			}
		} else {
			em_logf("elements_modal: label with text_id=\"%s\" text_var=\"%s\" — "
			        "text_writer 未継承で set_text 仕掛け失敗",
			        text_id.c_str(), text_var.c_str());
		}
	}
	// "id" があれば id→element に登録し、 ホストから label を参照可能にする
	// (label は focus 対象ではないので note_initial_focus は不要)。
	register_id(o, out);
	return out;
}

//---------------------------------------------------------------------------
// i18n: proxy ベース button の styler (text_writer 派生) を StringStore に
// subscribe する。 button は proxy<styler, basic_button> で、 styler が text を
// 持つ。 proxy_base::subject() で styler (= element&) を取り、 text_writer に
// cross-cast する。 raw ポインタは shared (proxy 本体) 生存中のみ有効なので、
// weak_ptr<element> で生存を確認してから set_text する。
//---------------------------------------------------------------------------
void LayoutBuilder::subscribe_button_text_id(const picojson::object& o,
                                             const element_ptr& shared)
{
	std::string text_id = string_or(o, "text_id");
	if (text_id.empty() || !shared) return;
	auto pb = std::dynamic_pointer_cast<ce::proxy_base>(shared);
	if (!pb) return;
	auto* tw = dynamic_cast<ce::text_writer*>(&pb->subject());
	if (!tw) {
		em_logf("elements_modal: button with text_id=\"%s\" — styler が "
		        "text_writer 未継承で set_text 仕掛け失敗", text_id.c_str());
		return;
	}
	tw->set_text(_strings->resolve(text_id));      // 初期言語へ同期
	std::weak_ptr<ce::element> wel = shared;
	_strings->subscribe(text_id, [wel, tw](const std::string& v) {
		if (auto el = wel.lock()) tw->set_text(v);  // el 生存中は tw も有効
	});
}

element_ptr LayoutBuilder::build_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	// font_scale / 明示 size を button styler の size 引数へ (font・余白・角丸・
	// アイコンが一括で拡大される。 draw/limits 時に theme.label_font*size を読む)。
	auto btn = ce::button(text, effective_font_scale(o));
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
	note_focusable(id, shared);   // focus 追跡 (focused_id / focus トリガ演出 用)
	note_vars_on_focus(o, id);    // focus 時に vars を書込む (メニュー説明欄など)
	subscribe_button_text_id(o, shared);  // i18n: text_id があれば言語連動
	// "close_on_click": true な button だけホスト側で finish フラグを立てる対象。
	// デフォルト (省略) は閉じず、 onAction だけ発火する。
	if (!id.empty()) {
		if (truthy_field(get_field(o, "close_on_click"))) {
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
	auto out = ce::share(ce::box(c));
	register_id(o, out);   // "id" 指定でホストから参照可能に
	return out;
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

element_ptr LayoutBuilder::build_locale_variant(const picojson::object& o)
{
	// 言語別バリアント: 現在言語に一致する子だけ表示するデッキ。 JSON:
	//   { "type":"locale_variant", "at":[..], "default":"en",
	//     "children":[ { "lang":"jp", <widget> }, { "lang":"en", <widget> } ] }
	// 各 child は "lang" 付きの通常 widget オブジェクト (lang は widget 側では
	// 無視される)。 language が変わると StringStore::subscribe_language 経由で
	// deck の select() を切り替える。 全 child は deck の同一 box を占める。
	auto deck = std::make_shared<ce::deck_composite>();
	auto langs = std::make_shared<std::vector<std::string>>();
	if (auto* arr = get_array(o, "children")) {
		for (const auto& cv : *arr) {
			if (!cv.is<picojson::object>()) continue;
			auto child = build(cv);
			if (!child) continue;
			deck->push_back(child);
			langs->push_back(string_or(cv.get<picojson::object>(), "lang"));
		}
	}
	if (deck->empty())
		return ce::share(ce::layer_composite{});   // バリアント無し = 何も描かない

	std::string def = string_or(o, "default");
	// 現在言語 -> 表示 index。 一致優先 / 無ければ default / それも無ければ 0。
	auto pick = [langs, def](const std::string& cur) -> std::size_t {
		for (std::size_t i = 0; i < langs->size(); ++i)
			if ((*langs)[i] == cur) return i;
		if (!def.empty())
			for (std::size_t i = 0; i < langs->size(); ++i)
				if ((*langs)[i] == def) return i;
		return 0;
	};
	deck->select(pick(_strings->language()));

	// 言語変更で表示子を切替 (host が毎フレーム再描画するので refresh 不要)。
	std::weak_ptr<ce::deck_composite> wdeck = deck;
	_strings->subscribe_language([wdeck, pick](const std::string& lang) {
		if (auto d = wdeck.lock()) d->select(pick(lang));
	});

	element_ptr out = deck;
	register_id(o, out);
	return out;
}

element_ptr LayoutBuilder::build_align(const picojson::object& o, const std::string& kind)
{
	auto child = build_child(o);
	if (!child) return nullptr;
	if (kind == "center")        return ce::share(ce::align_center(ce::hold_any(child)));
	if (kind == "left")          return ce::share(ce::align_left(ce::hold_any(child)));
	if (kind == "right")         return ce::share(ce::align_right(ce::hold_any(child)));
	if (kind == "top")           return ce::share(ce::align_top(ce::hold_any(child)));
	if (kind == "middle")        return ce::share(ce::align_middle(ce::hold_any(child)));
	if (kind == "bottom")        return ce::share(ce::align_bottom(ce::hold_any(child)));
	if (kind == "center_middle") return ce::share(ce::align_center_middle(ce::hold_any(child)));
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
	bool_field(get_field(o, "value"), init);

	auto cb = ce::check_box(text, effective_font_scale(o));
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
	bool_field(get_field(o, "value"), init);

	auto tb = ce::toggle_button(text, effective_font_scale(o));
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
	bool_field(get_field(o, "value"), init);

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
	// 初期値: "text"(優先) / "value"。空でなければエディットに事前投入する。
	std::string initial = string_or(o, "text");
	if (initial.empty()) initial = string_or(o, "value");

	auto pair = ce::input_box(placeholder, size);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		pair.second->on_text = [cb_id, user_cb](std::string_view text) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{std::string(text)});
		};
	}
	if (!initial.empty()) {
		// set_text はプログラム的変更で on_text を発火しないため、
		// 初期値も結果に載るよう明示的にコールバックへ流す。
		pair.second->set_text(initial);
		if (!id.empty() && _cb) _cb(id, /*is_button_click=*/false, value_t{initial});
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
		em_logf("elements_modal: selection_menu without 'options'");
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
		em_logf("elements_modal: %s without 'options'",
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

	// picker は内部で `font._size * _font_size` 計算するので scale を渡す。
	float fs = resolve_font_scale(o, "font_size", "font_size_scale");

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
		note_focusable(id, p);
		shared = p;
	} else if (variant == 1) {
		auto p = std::make_shared<ce::framed_cycle_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		note_focusable(id, p);
		shared = p;
	} else {
		auto p = std::make_shared<ce::segmented_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		note_focusable(id, p);
		shared = p;
	}
	register_id(o, shared);
	note_initial_focus(o, shared);
	note_vars_on_focus(o, id);
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
	// lib の invert_button styler は internal で `font._size * scale` する
	// ので、 JSON 側の px をテーマ base で割って scale 化して渡す。
	float size = resolve_font_scale(o, "size", "size_scale");
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
	subscribe_button_text_id(o, shared);  // i18n: text_id があれば言語連動
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	if (!id.empty()) {
		if (truthy_field(get_field(o, "close_on_click"))) {
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
	float size = resolve_font_scale(o, "size", "size_scale");

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
	subscribe_button_text_id(o, shared);  // i18n: text_id があれば言語連動
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	if (!id.empty()) {
		if (truthy_field(get_field(o, "close_on_click"))) {
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
	if (auto sb = std::dynamic_pointer_cast<ce::basic_slider_base>(shared)) {
		note_focusable(id, sb);
	}
	note_vars_on_focus(o, id);
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

	// slider_with_range の font_size は内部 label.relative_font_size 用なので
	// scale で渡す。
	float fs = resolve_font_scale(o, "font_size", "font_size_scale");
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
	if (auto sb = std::dynamic_pointer_cast<ce::basic_slider_base>(rs.focus)) {
		note_focusable(id, sb);
	}
	note_vars_on_focus(o, id);
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
		em_logf("elements_modal: pad_icon without 'name'");
		return nullptr;
	}
	bool use_font = false;
	bool_field(get_field(o, "use_font"), use_font);

	// "color": [r,g,b,a] (任意) — font mode のみ反映。 既定は白。 SVG mode
	// では現状無視 (Kenney 元 SVG の色がそのまま出る)。 SVG への tint は
	// canvas API の拡張要なので future work。
	bool has_color = false;
	ce::color tint = ce::colors::white;
	if (auto* arr = get_array(o, "color")) {
		has_color = true;
		tint = parse_color(*arr);
	}

	if (use_font) {
		// pad_font_icon の size は内部で label.relative_font_size を呼ぶので
		// scale を渡す。 JSON 側の "size" px をテーマ base で割って変換。
		float size = resolve_font_scale(o, "size", "size_scale");
		auto out = ce::pad_font_icon(name, size, tint);
		register_id(o, out);
		return out;
	}
	float h = static_cast<float>(number_or(o, "height", 48.0));
	bool colored = false;
	bool_field(get_field(o, "colored"), colored);
	// "outline": true で *_outline.svg バリアントを優先して試す。 colored と
	// 併用すると _color_xxx_outline.svg → _color_xxx.svg → xxx_outline.svg →
	// xxx.svg の順でフォールバック。
	bool outline = false;
	bool_field(get_field(o, "outline"), outline);
	if (has_color) {
		// SVG mode は現状 tint 不可。 指定があれば一度だけ警告。
		em_logf("elements_modal: pad_icon \"%s\" — \"color\" は SVG mode "
		        "では現状無視されます (use_font: true でのみ反映)",
		        name.c_str());
	}
	auto out = ce::share(ce::pad_icon(name, h, colored, outline));
	register_id(o, out);   // "id" 指定でホストから参照可能に
	return out;
}

//---------------------------------------------------------------------------
// band — 単色背景帯。 child があれば帯の上に重ねる。
//   { "type": "band", "color": [r,g,b,a], "child": { ... } }
// child を省略すると単に塗りつぶし矩形。 高さや幅は親の vsize / hsize で
// 制御する想定 (band 自体はサイズ持たず、 親の枠を埋める)。
// 将来拡張ポイント: "gradient": {...} / "image": "path" などを足すときも
// この dispatch 内で背景生成を分岐する。 単色 default は color。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_band(const picojson::object& o)
{
	ce::color c = ce::rgba(0, 0, 0, 255);
	if (auto* arr = get_array(o, "color")) c = parse_color(*arr);

	auto bg = ce::share(ce::box(c));

	auto child = build_child(o);
	if (!child) return bg;

	// layer_composite は push 順末尾が最前面 (build_layer も rbegin..rend で
	// 逆順 push して JSON children[0] を最後に push している)。 ここでは
	// bg → child の順に push して child を前面に。
	ce::layer_composite ly;
	ly.push_back(bg);
	ly.push_back(child);
	return ce::share(std::move(ly));
}

//---------------------------------------------------------------------------
// sprite_button — lib の basic_sprite (= 1 枚画像を縦に frame_height 単位で
// スライス) を sprite_button_styler に通した momentary button。
//   { "type": "sprite_button",
//     "image": "resources/buttons.png", "frame_height": 80,
//     "scale": 1.0, "id": "..." }
// 画像サイズは固定 (width = pixmap.width, height = frame_height)。 自動
// リサイズはしない。 lib の既存 file-based sprite を使うので path 指定のみ。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_sprite_button(const picojson::object& o)
{
	auto image_str = string_or(o, "image");
	if (image_str.empty()) {
		em_logf("elements_modal: sprite_button without 'image'");
		return nullptr;
	}
	float frame_height = static_cast<float>(number_or(o, "frame_height", 0.0));
	if (frame_height <= 0.0f) {
		em_logf("elements_modal: sprite_button \"%s\" needs 'frame_height'",
		        image_str.c_str());
		return nullptr;
	}
	float scale = static_cast<float>(number_or(o, "scale", 1.0));
	auto full = resolve_resource(image_str);

	std::string id = string_or(o, "id");

	try {
		auto sprite = ce::basic_sprite(full.string().c_str(),
		                              frame_height, scale);
		auto btn = ce::momentary_button(std::move(sprite));
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
		if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
			note_focusable(id, bp);
		}
		note_vars_on_focus(o, id);
		if (!id.empty()) {
			if (truthy_field(get_field(o, "close_on_click"))) {
				_close_button_ids.insert(id);
			}
		}
		return shared;
	} catch (std::exception const& e) {
		em_logf("elements_modal: sprite_button failed to load \"%s\": %s",
		        full.string().c_str(), e.what());
		return nullptr;
	}
}

//---------------------------------------------------------------------------
// gizmo_image — lib の gizmo / hgizmo / vgizmo (9-patch / 3-patch) を JSON
// から組む。 PSD ベース UI では普通サイズ固定だが、 比較用 + フレキシブル
// 背景用途で。
//   { "type": "gizmo_image",
//     "image": "resources/test_gizmo.png",
//     "axis": "9" | "h" | "v",   // 既定 "9"
//     "scale": 1.0 }
// 自動リサイズ: 親レイアウト (htile / floating 等) が与える bounds に
// 合わせて中央部分が伸縮する。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_gizmo_image(const picojson::object& o)
{
	auto image_str = string_or(o, "image");
	if (image_str.empty()) {
		em_logf("elements_modal: gizmo_image without 'image'");
		return nullptr;
	}
	float scale = static_cast<float>(number_or(o, "scale", 1.0));
	auto full = resolve_resource(image_str);
	auto axis = string_or(o, "axis", "9");

	try {
		if (axis == "h") {
			return ce::share(ce::hgizmo(full.string().c_str(), scale));
		} else if (axis == "v") {
			return ce::share(ce::vgizmo(full.string().c_str(), scale));
		} else {
			return ce::share(ce::gizmo(full.string().c_str(), scale));
		}
	} catch (std::exception const& e) {
		em_logf("elements_modal: gizmo_image failed to load \"%s\": %s",
		        full.string().c_str(), e.what());
		return nullptr;
	}
}

//---------------------------------------------------------------------------
// floating — child を指定矩形に固定配置する。 lib の floating_element の
// 薄いラッパ。 layer の中に複数並べて canvas 風に絶対座標配置できる。
//   { "type": "floating", "at": [x, y, w, h], "child": {...} }
// 親の bounds に関係なく ctx.bounds = (x, y) - (x+w, y+h) になる。 PSD で
// デザインされたレイアウトをそのまま JSON 化する用途向け。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_floating(const picojson::object& o)
{
	auto* arr = get_array(o, "at");
	if (!arr || arr->size() < 4) {
		em_logf("elements_modal: floating requires 'at': [x, y, w, h]");
		return nullptr;
	}
	float x = static_cast<float>(int_at(*arr, 0, 0));
	float y = static_cast<float>(int_at(*arr, 1, 0));
	float w = static_cast<float>(int_at(*arr, 2, 0));
	float h = static_cast<float>(int_at(*arr, 3, 0));

	auto child = build_child(o);
	if (!child) {
		em_logf("elements_modal: floating without valid 'child'");
		return nullptr;
	}
	return ce::share(ce::floating(ce::rect{x, y, x + w, y + h},
	                              ce::hold_any(child)));
}

//---------------------------------------------------------------------------
// canvas — 複数の絶対座標配置を一括宣言する糖衣。 内部は layer_composite に
// floating(rect, widget) を積んだだけ。 ホストの view extent (= 画面論理
// サイズ 1920x1080 など) を canvas の固定サイズとして覆い、 子の at で
// 内側の絶対座標を指定する。
//   { "type": "canvas",
//     "width": 1920, "height": 1080,           ← 任意 (省略時は view extent)
//     "children": [
//       { "at": [100, 200, 200, 80],
//         "type": "sprite_button", "image": "...", ... },
//       { "at": [350, 200, 200, 80],
//         "type": "invert_button", "text": "...", ... },
//       ...
//     ] }
// 各 child は通常の dispatch object に "at" を加えるだけ。 widget 自身の
// "type" / その他フィールドはそのまま。 build 時に "at" を抜いて widget を
// 組み、 floating(rect, widget) で wrap して layer に積む。
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// canvas_layer_element — `canvas` 専用の composite_base 派生。
// 子は (rect, element_ptr) の対で保持し、 bounds_of で **親 ctx.bounds.origin
// に rect をオフセットして** 子の bounds を返す。 これにより:
//   - root に置かれた canvas: ctx.bounds.origin = (0,0) なので rect 値が
//     そのまま画面座標 = 既存の絶対座標っぽい振舞いを維持
//   - 別 canvas 内にネストされた canvas: 外側 canvas が割当てた bounds.origin
//     を基点に rect 解釈 = 親に対する相対座標として動く (排他グループの
//     分離等で nested canvas を使う場合に必須)
//
// 旧実装は layer_composite + floating(absolute_rect) 方式だったが、
// floating::prepare_subject が絶対 rect を上書きするためネストが効かなかった。
//
// 仕様メモ:
//   - rect は JSON の "at": [x, y, w, h] と同じく **左上 origin + 幅高** 表現。
//   - 描画順は push 順 (= JSON children 配列の順)。 最後に push したものが
//     最前面。 hit_test も最前面から逆順に試す。
//   - 子要素はそのまま push (floating wrap しない)。 ただし共有所有のため
//     hold (or share) 済の element_ptr を渡す。
//---------------------------------------------------------------------------
namespace
{
	class canvas_layer_element : public ce::composite_base
	{
	public:
		void add(ce::rect r, element_ptr child)
		{
			_children.emplace_back(r, std::move(child));
		}

		std::size_t size() const override { return _children.size(); }

		ce::element& at(std::size_t ix) const override
		{
			return *_children[ix].second;
		}

		ce::view_limits limits(ce::basic_context const& ctx) const override
		{
			// canvas の自然 min = 子全体を覆う bounding box (left/top は 0 を仮定)。
			// max は full_extent (= 親が自由に拡げてよい)。 ホストが width /
			// height で hmin_size / vmin_size に括る運用は build_canvas 側で続行。
			//
			// 注意: 子全部の limits() を一度呼んで「副作用で内部状態を仕込む
			// 系の widget」 (例: slider_base が _is_horiz をここで設定する)
			// を巻き込む。 これを忘れると slider の thumb_bounds が縦扱いで
			// 計算されて見た目が崩壊する (実証済)。 戻り値は使わない。
			for (auto const& kv : _children)
				(void)kv.second->limits(ctx);

			float w = 0, h = 0;
			for (auto const& kv : _children) {
				w = std::max(w, kv.first.right);
				h = std::max(h, kv.first.bottom);
			}
			return {{w, h}, {ce::full_extent, ce::full_extent}};
		}

		void layout(ce::context const& ctx) override
		{
			for (std::size_t i = 0; i < size(); ++i) {
				auto& e = at(i);
				e.layout(ce::context{ctx, &e, bounds_of(ctx, i)});
			}
		}

		void draw(ce::context const& ctx) override
		{
			// bounds が変わったら relayout (layer_element 流儀)。
			auto width = ctx.bounds.width();
			auto height = ctx.bounds.height();
			if (_prev_size.x != width || _prev_size.y != height) {
				_prev_size = {width, height};
				layout(ctx);
			}
			ce::composite_base::draw(ctx);
		}

		ce::rect bounds_of(ce::context const& ctx, std::size_t i) const override
		{
			auto const& r = _children[i].first;
			return ce::rect{
				ctx.bounds.left + r.left,
				ctx.bounds.top  + r.top,
				ctx.bounds.left + r.right,
				ctx.bounds.top  + r.bottom
			};
		}

		// hit_test: 最後に push した子から優先 (= 最前面優先)。
		hit_info hit_element(ce::context const& ctx, ce::point p,
		                     bool control) const override
		{
			for (int i = int(size()) - 1; i >= 0; --i) {
				auto& e = at(i);
				if (!control || e.wants_control()) {
					auto bounds = bounds_of(ctx, i);
					if (bounds.includes(p)) {
						ce::context ectx{ctx, &e, bounds};
						if (auto leaf = e.hit_test(ectx, p, true, control))
							return hit_info{&e, leaf, bounds, int(i)};
					}
				}
			}
			return hit_info{nullptr, nullptr, ce::rect{}, -1};
		}

	private:
		std::vector<std::pair<ce::rect, element_ptr>> _children;
		ce::point _prev_size{};
	};
}

element_ptr LayoutBuilder::build_canvas(const picojson::object& o)
{
	const auto* children = get_array(o, "children");
	if (!children) {
		em_logf("elements_modal: canvas requires 'children'");
		return nullptr;
	}

	float width  = static_cast<float>(number_or(o, "width", 0.0));
	float height = static_cast<float>(number_or(o, "height", 0.0));

	auto layer = std::make_shared<canvas_layer_element>();
	for (auto& v : *children) {
		if (!v.is<picojson::object>()) continue;
		const auto& co = v.get<picojson::object>();

		auto* at = get_array(co, "at");
		if (!at || at->size() < 4) {
			em_logf("elements_modal: canvas child missing 'at': [x,y,w,h]");
			continue;
		}
		float x = static_cast<float>(int_at(*at, 0, 0));
		float y = static_cast<float>(int_at(*at, 1, 0));
		float w = static_cast<float>(int_at(*at, 2, 0));
		float h = static_cast<float>(int_at(*at, 3, 0));

		// child widget を build (v 全体を build に渡す。 "at" は dispatch 側
		// で見ないので無害)。
		auto widget = build(v);
		if (!widget) continue;

		// canvas_layer_element に直接 (rect, widget) で追加。 floating ラップ
		// は使わない (相対座標を canvas_layer_element の bounds_of で計算する)。
		layer->add(ce::rect{x, y, x + w, y + h}, std::move(widget));
	}

	element_ptr root = layer;
	if (width > 0.0f) {
		root = ce::share(ce::hmin_size(width, ce::hold_any(root)));
	}
	if (height > 0.0f) {
		root = ce::share(ce::vmin_size(height, ce::hold_any(root)));
	}
	return root;
}

//---------------------------------------------------------------------------
// atlas_image / atlas_button / atlas_toggle / atlas_slider / atlas_progress
// 共通ユーティリティ
//---------------------------------------------------------------------------
namespace
{
	// JSON の [x, y, w, h] 配列 → ce::rect。 4 要素未満は空矩形。
	ce::rect parse_xywh(const picojson::array& arr)
	{
		if (arr.size() < 4) return ce::rect{};
		float x = static_cast<float>(int_at(arr, 0, 0));
		float y = static_cast<float>(int_at(arr, 1, 0));
		float w = static_cast<float>(int_at(arr, 2, 0));
		float h = static_cast<float>(int_at(arr, 3, 0));
		return ce::rect{x, y, x + w, y + h};
	}

	// frames 指定 (object or array) を std::vector<rect> にパース。
	// object 版: 状態名キーで {normal, hilite, pressed, pressed_hilite, disabled}
	//   の順に値があるところまで使う (途中で抜けたら break)。
	// array 版:  [[x,y,w,h], ...] そのまま順番固定。
	bool parse_frames(const picojson::value* fv,
	                  const char* const* state_names,  // null 終端の配列
	                  std::vector<ce::rect>& out)
	{
		if (!fv) return false;
		if (fv->is<picojson::object>()) {
			const auto& fo = fv->get<picojson::object>();
			for (auto p = state_names; *p; ++p) {
				auto it = fo.find(*p);
				if (it == fo.end() || !it->second.is<picojson::array>()) break;
				out.push_back(parse_xywh(it->second.get<picojson::array>()));
			}
			return !out.empty();
		}
		if (fv->is<picojson::array>()) {
			const auto& fa = fv->get<picojson::array>();
			for (auto& el : fa) {
				if (el.is<picojson::array>())
					out.push_back(parse_xywh(el.get<picojson::array>()));
			}
			return !out.empty();
		}
		return false;
	}
}

ce::pixmap_ptr LayoutBuilder::lookup_atlas(const std::string& name)
{
	auto it = _atlases.find(name);
	if (it == _atlases.end()) {
		em_logf("elements_modal: atlas \"%s\" not registered "
		        "(missing top-level \"atlases\" entry?)", name.c_str());
		return nullptr;
	}
	return it->second;
}

//---------------------------------------------------------------------------
// atlas_image — アトラスから単一 sub-rect を切り出して描く飾り要素。
//   { "type": "atlas_image", "atlas": "ui", "rect": [x, y, w, h] }
// 既定は固定サイズ (= 飾り用)。 "stretch_h"/"stretch_v": true で当該軸を
// stretchable に (= 親 layout の bounds に合わせて伸縮、 9-patch 的)。
// PSD ベース UI では canvas+floating で絶対座標配置するので stretch は不要。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_image(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_image without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	auto* arr = get_array(o, "rect");
	if (!arr || arr->size() < 4) {
		em_logf("elements_modal: atlas_image \"%s\" missing 'rect': [x,y,w,h]",
		        atlas_name.c_str());
		return nullptr;
	}
	ce::rect src = parse_xywh(*arr);

	bool stretch_h = false, stretch_v = false;
	bool_field(get_field(o, "stretch_h"), stretch_h);
	bool_field(get_field(o, "stretch_v"), stretch_v);

	return ce::share(ce::atlas_image(pm, src, stretch_h, stretch_v));
}

//---------------------------------------------------------------------------
// animated_sprite — アトラスのフレーム列を fps で自動送りするスプライトアニメ
// (パラパラ / スプライトシート再生)。 アニメアイコン、 スピナー、 待機ループ等。
//   { "type": "animated_sprite", "atlas": "ui", "at": [x,y,w,h],
//     "frames": [[u,v,w,h], ...], "fps": 12, "loop": true,
//     "native_frames": false }
// frames は array (順番 = 再生順)。 loop=false は最終フレームで停止。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_animated_sprite(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: animated_sprite without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	std::vector<ce::rect> frames;
	static const char* no_states[] = { nullptr };
	if (!parse_frames(get_field(o, "frames"), no_states, frames)) {
		em_logf("elements_modal: animated_sprite \"%s\" missing 'frames' (array)",
		        atlas_name.c_str());
		return nullptr;
	}
	float fps = static_cast<float>(number_or(o, "fps", 12.0));
	bool loop = true;
	bool_field(get_field(o, "loop"), loop);
	bool native = truthy_field(get_field(o, "native_frames"));

	auto sprite = ce::share(
		ce::animated_sprite(pm, std::move(frames), fps, loop, native));
	register_id(o, sprite);   // id があればホストから参照可能に
	return sprite;
}

//---------------------------------------------------------------------------
// テキスト overlay — atlas_button / atlas_toggle / atlas_choice の上に
// ラベルを重ねる。
//
// 初期実装は `layer_composite{button, label}` を返していたが、 これは
// atlas_choice の排他動作 (= `basic_choice::activate`/`click` が
// `find_composite` で親 composite を取り、 兄弟全部の selectable を
// scan して deselect する仕組み) を壊す。 find_composite はこの inner
// layer_composite で止まってしまい、 そこには自分しか居ない → 他の
// choice 群が deselect されない。
//
// 解決: **非 composite** な proxy_base 派生で button (subject) と label
// (overlay) を保持し、 draw/layout で両方に同じ ctx.bounds を流す。
// proxy_base は composite_base ではないので find_composite はこれを
// 素通りして canvas_layer (外側 composite) を見つける = 排他動作が
// 正しく動く。 hit_test も proxy_base 既定の subject() 委譲なので click
// は button に届く。 label は draw で重ね描きされるだけ。
//---------------------------------------------------------------------------
namespace
{
	// label_decoration — proxy_base 派生で button + overlay label を保持。
	// 単一 subject (button) を持つ "proxy" として振る舞いつつ、 draw/layout で
	// label も同じ bounds で描く。
	class label_decoration : public ce::proxy_base
	{
	public:
		label_decoration(element_ptr subject_, element_ptr label_)
		 : _subject(std::move(subject_))
		 , _label(std::move(label_))
		{}

		// proxy_base 純粋仮想: subject 取得
		ce::element const& subject() const override { return *_subject; }
		ce::element&       subject() override       { return *_subject; }

		void draw(ce::context const& ctx) override
		{
			// (1) subject (button) を通常描画。 sprite_button_styler 経由で
			//     frame index 計算 + sprite 描画が走る。
			ce::proxy_base::draw(ctx);
			// (2) overlay (label) を同じ bounds で重ね描き。 label 自身は
			//     align_center_middle 等で整列を持つので bounds 内で位置を
			//     計算してくれる。
			if (_label) {
				ce::context lctx{ctx, _label.get(), ctx.bounds};
				_label->draw(lctx);
			}
		}

		void layout(ce::context const& ctx) override
		{
			// 両方に同じ bounds を渡してレイアウトさせる。 通常 layout は
			// resize 時にしか走らないが、 label の align/margin が正しい
			// 内部 bounds を持つために必要。
			ce::proxy_base::layout(ctx);
			if (_label) {
				ce::context lctx{ctx, _label.get(), ctx.bounds};
				_label->layout(lctx);
			}
		}

	private:
		element_ptr _subject;
		element_ptr _label;
	};
}

//---------------------------------------------------------------------------
// テキスト overlay ビルダ
//---------------------------------------------------------------------------
namespace
{
	element_ptr build_text_overlay_label(const picojson::object& o,
	                                     const std::string& default_locale,
	                                     StringStore* strings)
	{
		// "text_id" (i18n) があれば現在言語で解決、 無ければ static "text"。
		std::string text_id = string_or(o, "text_id");
		auto text = (!text_id.empty() && strings && strings->has(text_id))
		                ? strings->resolve(text_id)
		                : string_or(o, "text", text_id);
		auto locale = string_or(o, "locale", default_locale);
		float sz = static_cast<float>(number_or(o, "text_size", 0.0));

		ce::color col{1.0f, 1.0f, 1.0f, 1.0f};
		bool has_color = false;
		if (auto* arr = get_array(o, "text_color")) {
			col = parse_color(*arr);
			has_color = true;
		}

		// C6: "text_anchor" があればキャプションを絶対 baseline アンカーで描く
		// (anchored_text)。 label_decoration が overlay に button の bounds を渡すため、
		// anchor は button の左上基準の baseline 起点になる。 align_center_middle は
		// 使わない (中央寄せではなく PSD 位置)。
		if (auto* anc = get_array(o, "text_anchor"); anc && anc->size() >= 2) {
			float ax = static_cast<float>(anc->at(0).get<double>());
			float ay = static_cast<float>(anc->at(1).get<double>());
			int halign = ce::canvas::left;
			std::string ta = string_or(o, "text_align");
			if (ta == "center")     halign = ce::canvas::center;
			else if (ta == "right") halign = ce::canvas::right;
			float a_sz = sz > 0.0f ? sz : ce::get_theme().label_font._size;
			ce::color a_col = has_color ? col : ce::get_theme().label_font_color;
			std::string family = string_or(o, "font");
			int tracking = static_cast<int>(number_or(o, "tracking", 0.0));
			float leading = static_cast<float>(number_or(o, "leading", 0.0));
			bool wrap = truthy_field(get_field(o, "wrap"));
			auto at_elem = ce::make_anchored_text(text, family, a_sz, a_col, halign,
			                                      ce::point{ax, ay}, tracking, leading, wrap, locale);
			if (!text_id.empty() && strings) {
				if (auto sp = std::dynamic_pointer_cast<ce::text_writer>(at_elem)) {
					std::weak_ptr<ce::text_writer> w = sp;
					strings->subscribe(text_id, [w](const std::string& v) {
						if (auto p = w.lock()) p->set_text(v);
					});
				}
			}
			return at_elem;
		}

		auto base = ce::label(text);
		element_ptr lab;
		if (has_color && sz > 0.0f) {
			auto e = base.font_color(col).font_size(sz);
			if (locale.empty()) lab = ce::share(std::move(e));
			else                lab = ce::share(e.locale(std::move(locale)));
		} else if (has_color) {
			auto e = base.font_color(col);
			if (locale.empty()) lab = ce::share(std::move(e));
			else                lab = ce::share(e.locale(std::move(locale)));
		} else if (sz > 0.0f) {
			auto e = base.font_size(sz);
			if (locale.empty()) lab = ce::share(std::move(e));
			else                lab = ce::share(e.locale(std::move(locale)));
		} else {
			if (locale.empty()) lab = ce::share(std::move(base));
			else                lab = ce::share(base.locale(std::move(locale)));
		}

		// text_id 指定があれば言語切替で set_text する subscriber を仕掛ける
		// (build_label と同じ仕組み)。 wrap 前の素の label (text_writer) を狙う。
		if (!text_id.empty() && strings) {
			if (auto sp = std::dynamic_pointer_cast<ce::text_writer>(lab)) {
				std::weak_ptr<ce::text_writer> w = sp;
				strings->subscribe(text_id, [w](const std::string& v) {
					if (auto p = w.lock()) p->set_text(v);
				});
			}
		}

		// align_center_middle で button の中央に位置決め
		element_ptr aligned = ce::share(ce::align_center_middle(ce::hold_any(lab)));

		// "text_offset": [dx, dy] があれば margin で上下左右にずらす。
		// 正の dx で右、 正の dy で下に動く。 align は中央維持なので margin で
		// 周囲の bound を非対称にすれば実質オフセット。
		if (auto* off = get_array(o, "text_offset"); off && off->size() >= 2) {
			float dx = static_cast<float>(int_at(*off, 0, 0));
			float dy = static_cast<float>(int_at(*off, 1, 0));
			// (l, t, r, b) を (max(0,dx), max(0,dy), max(0,-dx), max(0,-dy))
			float l = dx > 0 ?  dx : 0;
			float t = dy > 0 ?  dy : 0;
			float r = dx < 0 ? -dx : 0;
			float b = dy < 0 ? -dy : 0;
			aligned = ce::share(ce::margin({l, t, r, b}, ce::hold_any(aligned)));
		}
		return aligned;
	}

	// btn_shared を非 composite な label_decoration で包み、 label を overlay
	// として上から描く。 "text" が空 (= overlay 不要) なら btn_shared を
	// そのまま返す。
	// 旧実装は layer_composite で button + label を兄弟として積んでいたが、
	// それだと find_composite が inner layer で止まり basic_choice の排他が
	// 効かなくなる。 label_decoration は proxy_base 派生で composite_base
	// ではないので、 find_composite が素通りして外側 (canvas layer) を
	// 見つけられる。
	element_ptr maybe_wrap_text_overlay(const picojson::object& o,
	                                    element_ptr btn_shared,
	                                    const std::string& default_locale,
	                                    StringStore* strings)
	{
		// overlay label は "text" か "text_id" のいずれかがあれば付ける。
		if (string_or(o, "text").empty() && string_or(o, "text_id").empty())
			return btn_shared;

		auto label_elem = build_text_overlay_label(o, default_locale, strings);
		return ce::share(label_decoration(std::move(btn_shared),
		                                  std::move(label_elem)));
	}
}

//---------------------------------------------------------------------------
// atlas_button — アトラスから状態別 sub-rect を sprite_button_styler に乗せて
// momentary button にする。
//   { "type": "atlas_button", "atlas": "ui", "id": "ok",
//     "frames": { "normal": [...], "hilite": [...], "pressed": [...],
//                 "pressed_hilite": [...], "disabled": [...] },
//     "text": "OK", "text_size": 24,            // 任意: ラベル overlay
//     "text_color": [r,g,b,a], "text_offset": [dx, dy] }
// frames は object (名前→rect、 normal/hilite/pressed/pressed_hilite/disabled
// の順で値があるところまで使う) または array (順番固定 0..N) を受ける。
// 通常は 4 つ以上揃える (= sprite_button_styler の既定計算が 4 frame を仮定)。
// "text" が指定されれば maybe_wrap_text_overlay で label を上面に重ねる。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_button(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_button without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	std::vector<ce::rect> frames;
	static const char* btn_states[] = {
		"normal", "hilite", "pressed", "pressed_hilite", "disabled", nullptr
	};
	auto* fv = get_field(o, "frames");
	if (!parse_frames(fv, btn_states, frames)) {
		em_logf("elements_modal: atlas_button \"%s\" missing or invalid 'frames'",
		        atlas_name.c_str());
		return nullptr;
	}

	std::string id = string_or(o, "id");

	auto sprite = ce::atlas_sprite(pm, std::move(frames),
	                               truthy_field(get_field(o, "native_frames")));
	auto btn = ce::momentary_button(std::move(sprite));
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
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	if (!id.empty()) {
		if (truthy_field(get_field(o, "close_on_click"))) {
			_close_button_ids.insert(id);
		}
	}
	return maybe_wrap_text_overlay(o, shared, _default_locale, _strings.get());
}

//---------------------------------------------------------------------------
// atlas_toggle — 2 値保持型のアトラス button (lib の toggle_button(sprite)
// 経由)。 frame index = (value ? 2 : 0) + hilite の計算を共有するため、
// frames は off_normal / off_hilite / on_normal / on_hilite (+ disabled) の
// 順 (object キー) または同順 array で受ける。 atlas_check は同 builder の
// 別名 alias。
//   { "type": "atlas_toggle", "atlas": "ui", "id": "music",
//     "initial": false,
//     "frames": {
//       "off_normal":    [...], "off_hilite": [...],
//       "on_normal":     [...], "on_hilite":  [...],
//       "disabled":      [...]
//     },
//     "text": "MUSIC", "text_size": 22 }   // overlay 同様
// 値変化で value_t{bool} を発火。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_toggle(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_toggle without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	std::vector<ce::rect> frames;
	static const char* toggle_states[] = {
		"off_normal", "off_hilite", "on_normal", "on_hilite", "disabled", nullptr
	};
	auto* fv = get_field(o, "frames");
	if (!parse_frames(fv, toggle_states, frames)) {
		em_logf("elements_modal: atlas_toggle \"%s\" missing or invalid 'frames'",
		        atlas_name.c_str());
		return nullptr;
	}

	std::string id = string_or(o, "id");
	bool init = false;
	if (!bool_field(get_field(o, "initial"), init))
		bool_field(get_field(o, "value"), init);

	auto sprite = ce::atlas_sprite(pm, std::move(frames),
	                               truthy_field(get_field(o, "native_frames")));
	auto tb = ce::toggle_button(std::move(sprite));
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
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	return maybe_wrap_text_overlay(o, shared, _default_locale, _strings.get());
}

//---------------------------------------------------------------------------
// atlas_choice — 排他選択 (ラジオボタン) のアトラス版。
// 仕組み: lib の `basic_choice` (= basic_latching_button + selectable) は
// activate / click 時に find_composite で親 composite を取得 → 兄弟全部に対し
// find_element<selectable*> を試して、 自分以外を select(false) する。 これに
// よって同じ composite に並ぶ atlas_choice 群が自動で排他動作する。
//
// canvas 内では各 child が floating で wrap されるが、 find_element は proxy
// 鎖を辿るので floating(basic_choice) でも selectable* を見つけられる。
// vtile / htile 等の通常 composite に直接並べても OK。
//
// frame 名は atlas_toggle と同じ (off_normal / off_hilite / on_normal /
// on_hilite + 任意 disabled)。 SpriteSubject 制約は `is_sprite` が派生対応
// 済みなので `latching_button<basic_choice>(atlas_sprite)` で
// `proxy<atlas_sprite, sprite_button_styler<basic_choice>>` が組まれる
// (= sprite 描画 + 排他 + 状態別 frame 切替が一気にまとまる)。
//
//   { "type": "atlas_choice", "atlas": "ui", "id": "diff_easy",
//     "selected": false,        // 初期状態 (1 グループ内で 1 つだけ true 推奨)
//     "frames": {
//       "off_normal": [...], "off_hilite": [...],
//       "on_normal":  [...], "on_hilite":  [...]
//     },
//     "text": "EASY", "text_size": 18 }
// `atlas_radio` は同 builder の別名 alias。
// 値変化で value_t{bool true} を発火 (新しく選ばれた側のみ。 deselect される
// 側は select(false) 経由なので on_click は走らない)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_choice(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_choice without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	std::vector<ce::rect> frames;
	static const char* toggle_states[] = {
		"off_normal", "off_hilite", "on_normal", "on_hilite", "disabled", nullptr
	};
	auto* fv = get_field(o, "frames");
	if (!parse_frames(fv, toggle_states, frames)) {
		em_logf("elements_modal: atlas_choice \"%s\" missing or invalid 'frames'",
		        atlas_name.c_str());
		return nullptr;
	}

	std::string id = string_or(o, "id");
	bool init = false;
	if (!bool_field(get_field(o, "selected"), init))
		bool_field(get_field(o, "value"), init);

	auto sprite = ce::atlas_sprite(pm, std::move(frames),
	                               truthy_field(get_field(o, "native_frames")));
	// latching_button<basic_choice>(sprite) → proxy<sprite, sprite_button_styler<basic_choice>>
	// 排他は basic_choice::activate/click の find_composite + 兄弟スキャン
	// による (atlas_choice 群を同じ composite=canvas layer に並べる前提)。
	// text overlay は label_decoration (非 composite proxy_base 派生) で
	// 包むので、 find_composite はそれを素通りして canvas layer まで届く。
	auto ch = ce::latching_button<ce::basic_choice>(std::move(sprite));
	ch.value(init);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		ch.on_click = [cb_id, user_cb](bool state) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(ch));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	return maybe_wrap_text_overlay(o, shared, _default_locale, _strings.get());
}

//---------------------------------------------------------------------------
// atlas_slider — track + thumb をアトラスの sub-rect で構築する 0..1 スライダ。
//   { "type": "atlas_slider", "atlas": "ui", "id": "vol",
//     "track": [x, y, w, h], "thumb": [x, y, w, h],
//     "initial": 0.5, "vertical": false }
// track はスライダ軸方向に stretchable (親 floating の bounds に合わせる)。
// thumb は固定サイズ。 値変化で value_t{double pos} を発火。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_slider(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_slider without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	auto* tr = get_array(o, "track");
	auto* th = get_array(o, "thumb");
	if (!tr || !th || tr->size() < 4 || th->size() < 4) {
		em_logf("elements_modal: atlas_slider \"%s\" needs 'track' and 'thumb' "
		        "as [x, y, w, h]", atlas_name.c_str());
		return nullptr;
	}
	ce::rect track_src = parse_xywh(*tr);
	ce::rect thumb_src = parse_xywh(*th);

	bool vertical = false;
	bool_field(get_field(o, "vertical"), vertical);

	double initial = number_or(o, "initial", 0.5);
	if (initial < 0.0) initial = 0.0;
	if (initial > 1.0) initial = 1.0;

	std::string id = string_or(o, "id");

	// track はスライダ軸方向に stretchable、 直交軸は固定。 thumb は完全固定。
	auto track_img = ce::share(ce::atlas_image(pm, track_src,
	                                          /*stretch_h=*/!vertical,
	                                          /*stretch_v=*/ vertical));
	auto thumb_img = ce::share(ce::atlas_image(pm, thumb_src,
	                                          /*stretch_h=*/false,
	                                          /*stretch_v=*/false));

	auto sl = ce::slider(ce::hold(thumb_img), ce::hold(track_img), initial);
	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		sl.on_change = [cb_id, user_cb](double pos) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false,
			                     value_t{pos});
		};
	}
	auto shared = ce::share(std::move(sl));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto sb = std::dynamic_pointer_cast<ce::basic_slider_base>(shared)) {
		note_focusable(id, sb);
	}
	note_vars_on_focus(o, id);
	return shared;
}

//---------------------------------------------------------------------------
// atlas_progress — アトラスの track + fill 2 矩形を使うゲージ。
// 非インタラクティブ。 value (0..1) で fill の幅 (vertical=true なら高さ) が
// 変わる。 "value_var" を指定すると変数 store から動的に値を取得 +
// subscriber で変更を反映 (PSD UI で HP バーや volume gauge を変数連動)。
//   { "type": "atlas_progress", "atlas": "ui",
//     "track": [x, y, w, h], "fill": [x, y, w, h],
//     "value": 0.5,                   // 初期値 (静的) または fallback
//     "value_var": "hp_pct",          // 任意: 変数 store キー (string→double)
//     "vertical": false }
// 変数の値は文字列で持つ (text_var と同じ store)。 "0.75" のような 10 進
// 文字列を std::stod でパース、 失敗時は 0 にフォールバック。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_progress(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_progress without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	auto* tr = get_array(o, "track");
	auto* fl = get_array(o, "fill");
	if (!tr || !fl || tr->size() < 4 || fl->size() < 4) {
		em_logf("elements_modal: atlas_progress \"%s\" needs 'track' and 'fill' "
		        "as [x, y, w, h]", atlas_name.c_str());
		return nullptr;
	}
	ce::rect track_src = parse_xywh(*tr);
	ce::rect fill_src  = parse_xywh(*fl);

	bool vertical = false;
	bool_field(get_field(o, "vertical"), vertical);

	double init = number_or(o, "value", 0.0);

	std::string value_var = string_or(o, "value_var");
	if (!value_var.empty()) {
		// 変数の現在値があれば init を上書き
		if (auto* cur = _vars->get(value_var)) {
			try { init = std::stod(*cur); } catch (...) { /* fallback */ }
		}
	}

	auto pg = ce::atlas_progress(pm, track_src, fill_src, init, vertical);
	auto shared = ce::share(std::move(pg));

	if (!value_var.empty()) {
		// subscriber で変更時に set_value
		auto sp = std::dynamic_pointer_cast<ce::atlas_progress>(shared);
		if (sp) {
			std::weak_ptr<ce::atlas_progress> w = sp;
			_vars->subscribe(value_var, [w](const std::string& v) {
				if (auto p = w.lock()) {
					try { p->set_value(std::stod(v)); }
					catch (...) { /* 値不正は無視 */ }
				}
			});
		} else {
			em_logf("elements_modal: atlas_progress value_var=\"%s\" — "
			        "dynamic_cast 失敗 (share された型が違う?)",
			        value_var.c_str());
		}
	}

	register_id(o, shared);
	return shared;
}

//---------------------------------------------------------------------------
// radio_button — テキスト版の排他選択 (lib 既定 styler の塗りつぶし円 + text)。
// 仕組みは atlas_choice と同じ basic_choice 経由 (lib の `radio_button(text)`
// は `choice(radio_button_styler{text})`)。 兄弟全部に対し select(false) を
// 呼ぶので、 同じ composite に並べた radio_button 群は自動排他。
//   { "type": "radio_button", "text": "Easy", "id": "diff_easy",
//     "selected": false,
//     "size": 24,                  // 任意: テキスト font_size (px) または size_scale
//     "size_scale": 1.5 }
// 値変化で value_t{bool true} を発火 (atlas_choice と同じ semantics)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_radio_button(const picojson::object& o)
{
	auto text = string_or(o, "text");
	std::string id = string_or(o, "id");
	bool init = false;
	if (!bool_field(get_field(o, "selected"), init))
		bool_field(get_field(o, "value"), init);

	// lib 側の `radio_button(std::string, float)` は
	// `choice(radio_button_styler{text, scale})` を返す
	// = proxy<radio_button_styler, basic_choice>。 scale は toggle_selector が
	// 保持し、 limits/draw でラベルフォント・インジケータ・余白を一括拡大する。
	// font_scale / 明示 size を実効倍率として流す。
	auto rb = ce::radio_button(text, effective_font_scale(o));
	rb.value(init);

	if (!id.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		rb.on_click = [cb_id, user_cb](bool state) {
			if (user_cb) user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(rb));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	note_vars_on_focus(o, id);
	return shared;
}

//---------------------------------------------------------------------------
// tab_view — タブ + ページを 1 要素として返す。
//   {
//     "type": "tab_view",
//     "initial": 0,
//     "tab_size": px (任意, または tab_size_scale),
//     "tabs": [
//       { "label": "...", "child": { ... pane ... }, "id": "..." (任意) },
//       ...
//     ]
//   }
//
// 実装は **layer_composite + hidable** ベース。 当初 deck_composite を試した
// が、 deck は draw / hit / focus chain を _selected_index でしか辿らない
// 一方で composite_base::key の TAB 循環や view の 2D arrow nav は children
// 全部を巡るので、 非表示 pane の widget に focus が落ちる問題があった。
// layer + 各 pane を hidable で wrap して、 非選択 pane は wants_focus() /
// wants_control() が false → focus 循環の対象外、 となるようにしている。
//
// PageUp/Down + LB/RB は apply_input phase で bind_shortcut。 tab on_click
// も view& 取得のため deferred 側で仕掛ける。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_tab_view(const picojson::object& o)
{
	const auto* tabs_arr = get_array(o, "tabs");
	if (!tabs_arr || tabs_arr->empty()) {
		em_logf("elements_modal: tab_view without 'tabs'");
		return nullptr;
	}

	std::size_t initial = 0;
	if (auto* v = get_field(o, "initial"); v && v->is<double>()) {
		auto raw = static_cast<long long>(v->get<double>());
		if (raw < 0) raw = 0;
		if (static_cast<std::size_t>(raw) >= tabs_arr->size())
			raw = static_cast<long long>(tabs_arr->size() - 1);
		initial = static_cast<std::size_t>(raw);
	}

	float tab_scale = resolve_font_scale(o, "tab_size", "tab_size_scale");

	// hidable で wrap した pane と tab choice を平行して構築。 hidable の
	// テンプレ Subject は ce::hold_any() で統一型 (indirect<shared_element
	// <element>>) になるので、 全 pane で同じ leaf 型に揃う。
	using HidablePane = ce::hidable_element<
		ce::indirect<ce::shared_element<ce::element>>>;
	auto hidables_owner =
		std::make_shared<std::vector<std::shared_ptr<HidablePane>>>();
	auto choices_owner =
		std::make_shared<std::vector<std::shared_ptr<ce::basic_choice>>>();

	ce::layer_composite pane_layer;
	std::vector<element_ptr> tab_btn_elems;

	for (auto& v : *tabs_arr) {
		if (!v.is<picojson::object>()) continue;
		const auto& tab_obj = v.get<picojson::object>();
		std::string label = string_or(tab_obj, "label");

		// tab ボタン: ce::tab(text) を手動展開 (size() を挟む)。
		auto styler = ce::button_styler{label}.size(tab_scale).rounded_top()
		                .active_body_color(
		                    ce::get_theme().active_tab_color.opacity(0.5f));
		auto wrapped = ce::hmin(ce::hmin_pad(20.0f, std::move(styler)));
		auto tab_btn = ce::share(ce::choice(std::move(wrapped)));
		auto choice  = std::dynamic_pointer_cast<ce::basic_choice>(tab_btn);
		if (!choice) {
			em_logf("elements_modal: tab_view tab[%zu] choice cast failed",
			        tab_btn_elems.size());
			return nullptr;
		}
		register_id(tab_obj, tab_btn);
		tab_btn_elems.push_back(tab_btn);
		choices_owner->push_back(choice);

		// pane build → hold_any → hidable で wrap。
		auto* cv = get_field(tab_obj, "child");
		auto pane = cv ? build(*cv) : nullptr;
		if (!pane) pane = ce::share(ce::element{});
		auto h_shared = ce::share(ce::hidable(ce::hold_any(pane)));
		hidables_owner->push_back(h_shared);
		pane_layer.push_back(h_shared);
	}

	if (choices_owner->empty()) {
		em_logf("elements_modal: tab_view has no valid tabs");
		return nullptr;
	}

	// 初期可視 / 選択状態。 initial pane だけ表示、 残りは hidden。
	for (std::size_t i = 0; i < hidables_owner->size(); ++i) {
		(*hidables_owner)[i]->is_hidden = (i != initial);
	}
	(*choices_owner)[initial]->select(true);

	auto pane_layer_shared = ce::share(std::move(pane_layer));

	// tab on_click + PageUp/Down + LB/RB を apply_input phase でまとめて
	// 仕掛ける (view& 必要)。 select 時は view.refresh() で全体再描画。
	//
	// !!! 重要 !!! hidables_owner / choices_owner は build_tab_view ローカルで
	// strong ref を保持しているが、 関数 return 後はその ref が消える。 weak で
	// 捕まえると activate() 内 lock() が null になりタブが効かなくなる。 strong
	// (shared_ptr by value) で捕まえること。 on_click closure / bind_shortcut
	// closure の lifetime と心中する形にする。
	{
		add_deferred_view_callback(
			[hidables_owner, choices_owner](ce::view& vw) {
				auto activate =
					[hidables_owner, choices_owner, &vw](std::size_t target) {
						std::size_t n = hidables_owner->size();
						if (target >= n) return;
						for (std::size_t j = 0; j < n; ++j) {
							(*hidables_owner)[j]->is_hidden = (j != target);
							(*choices_owner)[j]->select(j == target);
						}
						vw.refresh();
					};

				// tab on_click はこの phase で全部設定する。 build 時には
				// view 不在のため click ハンドラを仕込めない。
				for (std::size_t i = 0; i < choices_owner->size(); ++i) {
					(*choices_owner)[i]->on_click =
						[i, activate](bool state) {
							if (state) activate(i);
						};
				}

				auto step = [hidables_owner, activate](int delta) {
					std::size_t n = hidables_owner->size();
					if (n == 0) return;
					int cur = 0;
					for (std::size_t i = 0; i < n; ++i) {
						if (!(*hidables_owner)[i]->is_hidden) {
							cur = static_cast<int>(i); break;
						}
					}
					int ni = static_cast<int>(n);
					int next = ((cur + delta) % ni + ni) % ni;
					if (next != cur) activate(static_cast<std::size_t>(next));
				};

				ce::key_info pgup{ce::key_code::page_up,
				                  ce::key_action::press, 0};
				ce::key_info pgdn{ce::key_code::page_down,
				                  ce::key_action::press, 0};
				vw.bind_shortcut(pgup,
				    [step]() { step(-1); }, /*force=*/true);
				vw.bind_shortcut(pgdn,
				    [step]() { step(+1); }, /*force=*/true);
				vw.bind_shortcut(ce::pad_button::lb,
				    [step]() { step(-1); }, /*force=*/true);
				vw.bind_shortcut(ce::pad_button::rb,
				    [step]() { step(+1); }, /*force=*/true);
			});
	}

	// レイアウト: vtile(align_left(htile(tabs)), layer(hidable panes))。
	ce::htile_composite tab_row;
	for (auto& tb : tab_btn_elems) tab_row.push_back(tb);
	auto tab_row_shared = ce::share(std::move(tab_row));

	ce::vtile_composite root;
	root.push_back(ce::share(ce::align_left(ce::hold_any(tab_row_shared))));
	root.push_back(pane_layer_shared);
	return ce::share(std::move(root));
}

element_ptr LayoutBuilder::build_labeled_row(const picojson::object& o)
{
	auto child = build_child(o);
	if (!child) {
		em_logf("elements_modal: labeled_row without 'child'");
		return nullptr;
	}
	auto text = string_or(o, "label");
	float lw = static_cast<float>(number_or(o, "label_width", 180.0));
	// labeled_row の font_size は内部 label.relative_font_size 用 scale。
	float fs = resolve_font_scale(o, "font_size", "font_size_scale");
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

		bool hover_focus_set = false;
		bool hover_focus = true;

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

	if (bool b = false; bool_field(get_field(input_obj, "arrow_focus_nav"), b)) {
		cfg->arrow_nav_set = true;
		cfg->arrow_nav = b;
	}
	if (bool b = false; bool_field(get_field(input_obj, "hover_focus"), b)) {
		cfg->hover_focus_set = true;
		cfg->hover_focus = b;
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
				em_logf("elements_modal: pad_bindings: unknown pad=%s key=%s",
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
			bool_field(get_field(so, "force"), force);
			int mods = 0;
			if (auto* ma = get_array(so, "mods")) mods = parse_modifiers(*ma);

			auto key_name = string_or(so, "key");
			if (!key_name.empty()) {
				auto kc = parse_key_code(key_name);
				if (kc == ce::key_code::unknown) {
					em_logf("elements_modal: shortcut: unknown key=%s", key_name.c_str());
					continue;
				}
				cfg->key_shortcuts.push_back({kc, mods, target, force});
				continue;
			}
			auto pad_name = string_or(so, "pad");
			if (!pad_name.empty()) {
				auto pb = parse_pad_button(pad_name);
				if (pb == ce::pad_button::unknown) {
					em_logf("elements_modal: shortcut: unknown pad=%s", pad_name.c_str());
					continue;
				}
				cfg->pad_shortcuts.push_back({pb, target, force});
				continue;
			}
			em_logf("elements_modal: shortcut: needs 'key' or 'pad'");
		}
	}

	// クロージャ: view が用意できた時点 (content 後) で実行
	auto id_map_shared = std::make_shared<std::map<std::string, element_ptr>>(std::move(id_map));
	return [cfg, id_map_shared](ce::view& view_) {
		if (cfg->arrow_nav_set)   view_.arrow_focus_navigation(cfg->arrow_nav);
		if (cfg->hover_focus_set) view_.hover_focus(cfg->hover_focus);
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
				em_logf("elements_modal: shortcut target not found: %s",
					s.target.c_str());
				continue;
			}
			ce::key_info ki{s.key, ce::key_action::press, s.mods};
			view_.bind_shortcut(ki, it->second, s.force);
		}
		for (auto const& s : cfg->pad_shortcuts) {
			auto it = id_map_shared->find(s.target);
			if (it == id_map_shared->end()) {
				em_logf("elements_modal: shortcut target not found: %s",
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
parsed_layout build_top_level(const picojson::value& root, event_callback cb,
                              const std::string& resource_base)
{
	parsed_layout result;
	if (!root.is<picojson::object>()) {
		em_logf("elements_modal: top-level must be an object");
		return result;
	}
	const auto& o = root.get<picojson::object>();

	if (auto* arr = get_array(o, "size")) {
		result.width  = int_at(*arr, 0, result.width);
		result.height = int_at(*arr, 1, result.height);
	}

	// 配置アンカー: "align" (string) + "margin" (number)。 既定は中央。
	if (auto* v = get_field(o, "align"); v && v->is<std::string>()) {
		const std::string& a = v->get<std::string>();
		float ax = 0.5f, ay = 0.5f;
		// 縦: top/bottom、 横: left/right。 含まれない軸は中央のまま。
		if (a.find("top")    != std::string::npos) ay = 0.0f;
		if (a.find("bottom") != std::string::npos) ay = 1.0f;
		if (a.find("left")   != std::string::npos) ax = 0.0f;
		if (a.find("right")  != std::string::npos) ax = 1.0f;
		// "center" のみ (top/bottom/left/right 無し) は中央。
		result.anchor_x = ax;
		result.anchor_y = ay;
	}
	result.margin = static_cast<int>(number_or(o, "margin", 0.0));

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
	if (!resource_base.empty()) {
		builder.set_resource_base(resource_base);
	}
	if (auto* v = get_field(o, "locale"); v && v->is<std::string>()) {
		builder.set_default_locale(v->get<std::string>());
	}

	// "font_scale" (float, 既定 1.0) — このダイアログのウィジェット既定フォント
	// 倍率。 明示 size を持たない button/toggle/radio/check_box/label が追従する。
	// 省略 (=1.0) 時は従来と完全一致 (opt-in、 他メニューへ不影響)。
	builder.set_font_scale(static_cast<float>(number_or(o, "font_scale", 1.0)));

	// "vars": {name: value} — VariableStore に初期値を登録。 build_label
	// の text_var が参照する。 build より先に流し込んでおく。
	if (auto* v = get_field(o, "vars"); v && v->is<picojson::object>()) {
		auto vars = builder.vars();
		for (auto& kv : v->get<picojson::object>()) {
			if (kv.second.is<std::string>()) {
				vars->set_initial(kv.first, kv.second.get<std::string>());
			}
		}
	}

	// "strings" ブロック (任意, i18n): { textId: { lang: "string" } } を
	// StringStore に登録。 "lang": "ja" で初期言語。 build_label の text_id が
	// 参照するので content build より先に流し込む。
	if (auto* v = get_field(o, "strings"); v && v->is<picojson::object>()) {
		auto strings = builder.strings();
		for (auto& kv : v->get<picojson::object>()) {
			if (!kv.second.is<picojson::object>()) continue;
			std::map<std::string, std::string> by_lang;
			for (auto& lv : kv.second.get<picojson::object>()) {
				if (lv.second.is<std::string>()) {
					by_lang[lv.first] = lv.second.get<std::string>();
				}
			}
			builder.strings()->set_entry(kv.first, std::move(by_lang));
		}
	}
	if (auto* v = get_field(o, "lang"); v && v->is<std::string>()) {
		builder.strings()->set_language(v->get<std::string>());
		result.lang = v->get<std::string>();
	}

	// "atlases" ブロック (任意): name → { path, scale }。 atlas_image /
	// atlas_button / atlas_slider が参照する pixmap_ptr を名前で共有可能に
	// するための事前ロード。 path は LayoutBuilder の resource_base を
	// 起点にして resolve_resource() で解決 (絶対パスはそのまま)。
	// !!! content build より先に解決すること。 そうしないと content 中の
	// atlas_* dispatch が "atlas not registered" になる。
	if (auto* v = get_field(o, "atlases"); v && v->is<picojson::object>()) {
		for (auto& kv : v->get<picojson::object>()) {
			const std::string& name = kv.first;
			const auto& spec = kv.second;
			std::string path_str;
			float scale = 1.0f;
			if (spec.is<std::string>()) {
				path_str = spec.get<std::string>();
			} else if (spec.is<picojson::object>()) {
				const auto& so = spec.get<picojson::object>();
				path_str = string_or(so, "path");
				scale = static_cast<float>(number_or(so, "scale", 1.0));
			} else {
				em_logf("elements_modal: atlases[\"%s\"] must be string or object",
				        name.c_str());
				continue;
			}
			if (path_str.empty()) {
				em_logf("elements_modal: atlases[\"%s\"] missing path",
				        name.c_str());
				continue;
			}
			auto full = builder.resolve_resource(path_str);
			try {
				auto pm = std::make_shared<ce::pixmap>(full, scale);
				builder.atlases()[name] = pm;
				em_logf("elements_modal: loaded atlas \"%s\" from \"%s\"",
				        name.c_str(), full.string().c_str());
			} catch (std::exception const& e) {
				em_logf("elements_modal: failed to load atlas \"%s\" "
				        "from \"%s\": %s",
				        name.c_str(), full.string().c_str(), e.what());
			}
		}
	}

	element_ptr content;
	if (auto* v = get_field(o, "content")) content = builder.build(*v);

	if (!content) {
		em_logf("elements_modal: missing or invalid 'content'");
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
	result.animations = builder.take_animations();

	// id_map と focused_id_slot をホスト公開用に取得。 input ブロックの
	// shortcut 解決でも id_map を使うので、 takeMove する前にコピーを保存。
	result.id_map = builder.id_map();
	result.id_types = builder.take_id_types();
	result.focused_id_slot = builder.focused_id_slot();
	result.hovered_id_slot = builder.hovered_id_slot();

	// "input":{"focus_anim":false} で focus トリガ演出を無効化 (hover_focus 併用時の
	// focus×hover 多重発火を避ける逃がし)。 既定 true。
	if (auto* v = get_field(o, "input"); v && v->is<picojson::object>()) {
		bool_field(get_field(v->get<picojson::object>(), "focus_anim"),
		           result.focus_anim);
	}

	// "input" ブロック (任意): view に対する arrow_focus_nav / pad mode /
	// pad bindings / shortcuts を設定するクロージャを作る。
	std::function<void(ce::view&)> input_cb;
	if (auto* v = get_field(o, "input"); v && v->is<picojson::object>()) {
		input_cb = build_input_applier(v->get<picojson::object>(),
			builder.take_id_map());
	}
	// deferred_view_cbs: build 中に積まれた追加 view-setup (例: tab_view
	// の PageUp/Down + LB/RB バインド)。 input_cb の後に順次実行する。
	auto deferred_cbs = builder.take_deferred_view_callbacks();
	if (input_cb || !deferred_cbs.empty()) {
		result.apply_input =
			[input_cb = std::move(input_cb),
			 deferred = std::move(deferred_cbs)](ce::view& vw) {
				if (input_cb) input_cb(vw);
				for (auto& cb : deferred) cb(vw);
			};
	}

	// "transitions" ブロック (任意): action id → 遷移仕様。 string 形式は
	// target だけセット、 object 形式は effect / duration_ms も読む。
	// マニフェスト駆動ランナがこれを参照する。
	if (auto* v = get_field(o, "transitions"); v && v->is<picojson::object>()) {
		for (auto& kv : v->get<picojson::object>()) {
			transition_spec ts;
			if (kv.second.is<std::string>()) {
				ts.target = kv.second.get<std::string>();
			} else if (kv.second.is<picojson::object>()) {
				const auto& obj = kv.second.get<picojson::object>();
				ts.target = string_or(obj, "target");
				ts.effect = string_or(obj, "effect");
				if (auto* d = get_field(obj, "duration");
				    d && d->is<double>()) {
					ts.duration_ms = static_cast<int>(d->get<double>());
				}
			} else {
				em_logf("elements_modal: transitions[\"%s\"] must be string "
				        "or object", kv.first.c_str());
				continue;
			}
			result.transitions[kv.first] = std::move(ts);
		}
	}

	// i18n: 言語切替 closure。 StringStore を shared_ptr 捕捉して保持するので、
	// この closure を持つ限り対応表 + label subscribers が生存する。 "strings"
	// 未定義でも StringStore は空のまま生成済みなので no-op として安全。
	result.set_language = [strings = builder.strings()](const std::string& lang) {
		strings->set_language(lang);
	};

	// ホスト主導の変数書込 closure。 VariableStore を shared_ptr 捕捉するので
	// set_language と同じく closure 保持中は store + subscribers が生存する。
	result.set_var = [vars = builder.vars()](const std::string& name,
	                                         const std::string& value) {
		vars->set(name, value);
	};

	// take 系は最後に。 内部 state を move する。
	result.focus_poll = builder.take_focus_poll();
	result.hover_poll = builder.take_hover_poll();
	return result;
}

} // anonymous

//---------------------------------------------------------------------------
// 公開 API
//---------------------------------------------------------------------------
parsed_layout parse_from_string(const std::string& json_utf8,
                                event_callback cb,
                                const std::string& resource_base)
{
	// JSONC 前処理 (const + cbegin/cend で渡す。 non-const iterator だと
	// picojson の template instance の都合で parse 結果が破壊されるケースがある)。
	const std::string preprocessed = preprocess_jsonc(json_utf8);

	picojson::value v;
	std::string err;
	picojson::parse(v, preprocessed.cbegin(), preprocessed.cend(), &err);
	if (!err.empty()) {
		em_logf("elements_modal: parse error: %s", err.c_str());
		return {};
	}
	return build_top_level(v, std::move(cb), resource_base);
}

//---------------------------------------------------------------------------
// app_manifest 解析。 entry + screens (name → path) を読むだけのシンプル版。
//---------------------------------------------------------------------------
app_manifest parse_app_manifest(const std::string& json_utf8)
{
	app_manifest m;
	const std::string preprocessed = preprocess_jsonc(json_utf8);

	picojson::value v;
	std::string err;
	picojson::parse(v, preprocessed.cbegin(), preprocessed.cend(), &err);
	if (!err.empty()) {
		em_logf("elements_modal: manifest parse error: %s", err.c_str());
		return m;
	}
	if (!v.is<picojson::object>()) {
		em_logf("elements_modal: manifest top-level must be object");
		return m;
	}
	const auto& o = v.get<picojson::object>();

	m.entry = string_or(o, "entry");
	if (m.entry.empty()) {
		em_logf("elements_modal: manifest missing 'entry'");
		return m;
	}

	if (auto* sv = get_field(o, "screens"); sv && sv->is<picojson::object>()) {
		for (auto& kv : sv->get<picojson::object>()) {
			if (kv.second.is<std::string>()) {
				m.screens[kv.first] = kv.second.get<std::string>();
			} else {
				em_logf("elements_modal: manifest screens[\"%s\"] must be "
				        "string (= JSON file path)", kv.first.c_str());
			}
		}
	}
	if (m.screens.find(m.entry) == m.screens.end()) {
		em_logf("elements_modal: manifest entry '%s' not in screens map",
		        m.entry.c_str());
		return m;
	}
	m.ok = true;
	return m;
}

} // namespace elements_modal

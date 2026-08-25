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
#include <elements/element/block_text.hpp>       // text_area (ホスト折返し + 文字送り)
#include <elements/element/image.hpp>            // ce::image (mem:// サムネ再ロード)
#include <elements_modal/modal.h>                // refresh_mem_image 宣言

#include <algorithm>
#include <array>     // focus_nav (明示フォーカスナビ) の方向テーブル
#include <cctype>
#include <chrono>    // choice_nav_group の pad エッジ検出
#include <cmath>
#include <cstdio>    // std::sscanf (at_var)
#include <cstdlib>   // std::atof (pj_num)
#include <cstring>
#include <map>       // mem:// image widget レジストリ
#include <memory>    // weak_ptr / shared_ptr
#include <mutex>
#include <utility>
#include <vector>

namespace ce = cycfi::elements;
using element_ptr = std::shared_ptr<ce::element>;

namespace elements_modal {

//---------------------------------------------------------------------------
// リソースパスリゾルバ (modal.h 公開 API)。 ホストが set すると、 相対パスの
// 解決 (resolve_resource / input_defaults) が全てここを通る。 未設定なら
// 従来どおり origin_base 前置。
//---------------------------------------------------------------------------
namespace {
	resource_resolver& resolver_slot()
	{
		static resource_resolver fn;
		return fn;
	}
}

void set_resource_resolver(resource_resolver fn)
{
	resolver_slot() = std::move(fn);
}

const resource_resolver& get_resource_resolver()
{
	return resolver_slot();
}

//---------------------------------------------------------------------------
// JSON 文字列 → Elements enum 変換
//
// JSON の入力バインドで使う語彙 ("enter" / "dpad_up" / "shift" 等) の変換表。
// ホストが「名前で入力を注入する」(検証パネル / REPL / 自動テスト) ときも
// 同じ語彙で書けるよう公開している (宣言は modal.h)。
//---------------------------------------------------------------------------

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

int parse_modifier(const std::string& s)
{
	if (s == "shift")   return ce::mod_shift;
	if (s == "ctrl" ||
	    s == "control") return ce::mod_control;
	if (s == "alt")     return ce::mod_alt;
	if (s == "super" ||
	    s == "cmd" ||
	    s == "command") return ce::mod_super;
	if (s == "action")  return ce::mod_action;
	return 0;
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
	// 位置基準 (刻印ではなく配置で指す)
	if (s == "face_south")      return b::face_south;
	if (s == "face_east")       return b::face_east;
	if (s == "face_west")       return b::face_west;
	if (s == "face_north")      return b::face_north;
	return b::unknown;
}

namespace {

//! "shift"/"ctrl"/... の配列を修飾ビットの OR にする (JSON バインド用)。
int parse_modifiers(const picojson::array& arr)
{
	int m = 0;
	for (const auto& v : arr) {
		if (v.is<std::string>()) m |= parse_modifier(v.get<std::string>());
	}
	return m;
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

//! "right"/"middle" → mouse_button::what。 左は widget 直接操作専用なので
//! バインド対象外 (false)。
bool parse_mouse_button(const std::string& s, ce::mouse_button::what& out)
{
	if (s == "right")  { out = ce::mouse_button::right;  return true; }
	if (s == "middle") { out = ce::mouse_button::middle; return true; }
	return false;
}


//---------------------------------------------------------------------------
// picojson helpers
//---------------------------------------------------------------------------
const picojson::value* get_field(const picojson::object& o, const char* key)
{
	auto it = o.find(key);
	return (it != o.end()) ? &it->second : nullptr;
}

// picojson を PICOJSON_USE_INT64 でビルドすると、 JSON の整数リテラル (774 等)
// は double でなく int64_t で保持され、 v.is<double>() が false になる。 数値読取りが
// is<double>() のみだと整数指定 (座標 "at"・色・"size" 等) が全て既定値 0 に落ち、
// canvas の座標が全部 0 になって描画されない。
//
// ただしこのコードは **int64 無しビルド (elements_console 等) でもコンパイル/動作**
// する必要があり、 int64_t 型 API (is<int64_t>/get<int64_t>) は非 int64 ビルドの
// picojson::value に存在しないため使えない。 そこで「double か、 それ以外で
// string/bool/null/array/object でないもの (= 数値)」で判定し、 int64 側は
// to_str()→atof で double 化する。 両ビルドで安全。
inline bool pj_is_num(const picojson::value& v)
{
	if (v.is<double>()) return true;
	if (v.is<bool>() || v.is<std::string>() || v.is<picojson::null>()
	    || v.is<picojson::array>() || v.is<picojson::object>())
		return false;
	return true;   // 残り = 数値 (int64 ビルドの整数)
}
inline double pj_num(const picojson::value& v, double dflt = 0.0)
{
	if (v.is<double>()) return v.get<double>();
	if (!pj_is_num(v)) return dflt;
	return std::atof(v.to_str().c_str());   // int64 も "774" 等になり atof で double 化
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
	if (auto* v = get_field(o, key); v && pj_is_num(*v)) {
		return pj_num(*v);
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
	if (pj_is_num(*v)) { out = pj_num(*v) != 0.0; return true; }
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
// value_display — スライダ等の 0..1 値を «人が読む数値» の文字列にする整形指定。
//
//   "display": { "min": 0, "max": 100, "step": 1, "digits": 0,
//                "pad": 0, "prefix": "", "suffix": "%" }
//
// pos (0..1) を min..max へ写し、 step (>0) があればその倍数へ丸め、 digits 桁の
// 10 進文字列にして prefix/suffix を付ける。 pad>0 なら整数部を 0 埋め (音量 "05"
// のような固定桁表示用)。 "display" 自体が無ければ既定 (0..100 / 整数 / 装飾なし)
// で、 表示変数を指定したときだけ使われる。
//---------------------------------------------------------------------------
struct value_display
{
	double min = 0.0;
	double max = 100.0;
	double step = 0.0;
	int digits = 0;
	int pad = 0;
	std::string prefix;
	std::string suffix;

	std::string format(double pos) const
	{
		if (pos < 0.0) pos = 0.0;
		if (pos > 1.0) pos = 1.0;
		double v = min + pos * (max - min);
		if (step > 0.0) v = std::round(v / step) * step;
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.*f", digits < 0 ? 0 : digits, v);
		std::string body(buf);
		if (pad > 0) {
			// 0 埋めは整数部のみ (符号は先頭に残す)。
			std::string sign;
			if (!body.empty() && (body[0] == '-' || body[0] == '+')) {
				sign = body.substr(0, 1);
				body.erase(0, 1);
			}
			std::size_t int_len = body.find('.');
			if (int_len == std::string::npos) int_len = body.size();
			while (int_len < static_cast<std::size_t>(pad)) {
				body.insert(body.begin(), '0');
				++int_len;
			}
			body = sign + body;
		}
		return prefix + body + suffix;
	}
};

value_display parse_value_display(const picojson::object& o)
{
	value_display d;
	auto* v = get_field(o, "display");
	if (!v || !v->is<picojson::object>()) return d;
	const auto& d_o = v->get<picojson::object>();
	d.min    = number_or(d_o, "min", d.min);
	d.max    = number_or(d_o, "max", d.max);
	d.step   = number_or(d_o, "step", d.step);
	d.digits = static_cast<int>(number_or(d_o, "digits", d.digits));
	d.pad    = static_cast<int>(number_or(d_o, "pad", d.pad));
	d.prefix = string_or(d_o, "prefix");
	d.suffix = string_or(d_o, "suffix");
	return d;
}

// スライダの «生値» 変数に書く 10 進文字列 (0..1)。 桁を抑えて同値書込での
// 無駄な subscriber 発火を防ぐ (VariableStore::set は同値なら no-op)。
std::string fmt_slider_raw(double pos)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.4f", pos);
	return std::string(buf);
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
	if (auto* v = get_field(o, size_key); v && pj_is_num(*v)) {
		return static_cast<float>(pj_num(*v));
	}
	float base = cycfi::elements::get_theme().label_font._size;
	if (auto* v = get_field(o, scale_key); v && pj_is_num(*v)) {
		return base * static_cast<float>(pj_num(*v));
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
	if (i < arr.size() && pj_is_num(arr[i])) {
		return static_cast<int>(pj_num(arr[i]));
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
// flatten (グループの焼き込み) の再焼き契機に使う共通リビジョン。
// 変数 / 言語が変わるたびに bump し、 flatten proxy が値の変化を検知して
// 焼き直す。 どの変数が変わったかまでは見ない粗い判定だが、 取りこぼすと
// 「文字が古いまま残る」という分かりにくい不具合になるので安全側に倒す。
using layout_revision = std::shared_ptr<std::uint64_t>;

class VariableStore
{
public:
	void set_revision(layout_revision r) { _rev = std::move(r); }

	void set_initial(const std::string& name, std::string value)
	{
		_values[name] = std::move(value);
	}
	const std::string* get(const std::string& name) const
	{
		auto it = _values.find(name);
		return it == _values.end() ? nullptr : &it->second;
	}
	// 現在値の一括参照 (検証ツールの変数一覧用)。
	const std::map<std::string, std::string>& values() const { return _values; }
	// 戻り値 = 値が実際に変わったか (同値書込は false、 subscriber も発火しない)。
	// ホストの再描画要否 (ダーティ) 判定に使える。
	bool set(const std::string& name, const std::string& value)
	{
		auto& cur = _values[name];
		if (cur == value) return false;
		cur = value;
		if (_rev) ++(*_rev);   // flatten の焼き直し契機
		auto it = _subs.find(name);
		if (it != _subs.end()) {
			for (auto& s : it->second) {
				// 部分再描画用: 見た目が変わる要素をホストへ通知する。
				// **変更前と変更後の 2 回**呼ぶ — text が縮む場合、 変更後の
				// 見た目だけでは伸びていた頃の描画を覆えず消し残るため。
				// owner 未登録の subscriber は通知されず、 ホスト側は
				// 「範囲不明 = 全面」にフォールバックする。
				if (_on_changed) {
					if (auto e = s.owner.lock()) _on_changed(*e);
				}
				s.cb(cur);
				if (_on_changed) {
					if (auto e = s.owner.lock()) _on_changed(*e);
				}
			}
		}
		// 観測フック: 「どの変数がどう変わったか」を外へ出す。 subscriber の
		// 有無に関係なく発火するので、 誰も見ていない変数の変化も拾える。
		if (_watcher) _watcher(name, cur);
		return true;
	}
	// owner = この変数で見た目が変わる要素 (省略可)。 部分再描画のダーティ
	// 範囲特定に使うだけなので、 渡さなくても動作は変わらない (全面になる)。
	void subscribe(const std::string& name,
	               std::function<void(const std::string&)> cb,
	               element_ptr owner = {})
	{
		_subs[name].push_back(sub{ std::move(cb), owner });
	}
	// 変数変化で見た目が変わった要素の通知先 (ホストが仕掛ける)。
	void set_change_notifier(std::function<void(ce::element&)> f)
	{
		_on_changed = std::move(f);
	}
	// 変数変化 (名前, 新しい値) の観測先 (ホストが仕掛ける)。 set_change_notifier
	// が部分再描画のために「どの要素か」を返すのに対し、 こちらは「どの変数か」を
	// 外へ出すためのもの (検証パネルへの push 等)。
	void set_watcher(std::function<void(const std::string&, const std::string&)> f)
	{
		_watcher = std::move(f);
	}

private:
	struct sub
	{
		std::function<void(const std::string&)> cb;
		std::weak_ptr<ce::element> owner;
	};
	std::map<std::string, std::string> _values;
	std::map<std::string, std::vector<sub>> _subs;
	std::function<void(ce::element&)> _on_changed;
	std::function<void(const std::string&, const std::string&)> _watcher;
	layout_revision _rev;
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
	void set_revision(layout_revision r) { _rev = std::move(r); }

	// "strings" の 1 エントリを登録 (id → {lang: string})。
	void set_entry(const std::string& id, std::map<std::string, std::string> by_lang)
	{
		_table[id] = std::move(by_lang);
	}

	// 現在言語を設定。 既に subscribe 済みの label をすべて再解決して通知。
	// 戻り値 = 言語が実際に変わったか (同一言語の再設定は false)。
	bool set_language(const std::string& lang)
	{
		if (_lang == lang) return false;
		_lang = lang;
		if (_rev) ++(*_rev);   // flatten の焼き直し契機 (言語で文字が変わる)
		for (auto& kv : _subs) {
			std::string v = resolve(kv.first);
			for (auto& cb : kv.second) cb(v);
		}
		// text_id に紐づかない一般の言語変更 subscriber (locale_variant 等) を発火。
		for (auto& cb : _lang_subs) cb(_lang);
		return true;
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

	// 対応表に現れる言語コードの一覧 (辞書順・重複なし)。 画面がどの言語を
	// 持っているかをホスト (言語切替 UI) に見せるためのもの。
	std::vector<std::string> languages() const
	{
		std::vector<std::string> out;
		for (const auto& kv : _table) {
			for (const auto& lv : kv.second) {
				if (std::find(out.begin(), out.end(), lv.first) == out.end())
					out.push_back(lv.first);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

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
	layout_revision _rev;
};

//---------------------------------------------------------------------------
// 指定番号表示テキスト ("text_list" / "text_list_id" + "index_var") — label と
// text_area で共用。 index_var の値 (10 進 index 文字列) でリストの 1 本を選ぶ。
//
// "text_list_id" は i18n 版で、 各要素が textID。 index で引いてから現在言語で
// 解決するので、 言語切替でも表示中の 1 本がその場で差し替わる (picker の
// "options_id" と同じ考え方)。 "text_list" 併記時は「i18n 非対応ランタイム /
// 未知 id」用の静的 fallback として同 index が使われる。
//---------------------------------------------------------------------------
struct TextListSpec
{
	std::vector<std::string> statics;   // "text_list"
	std::vector<std::string> ids;       // "text_list_id" (優先)

	bool empty() const { return statics.empty() && ids.empty(); }

	// index → 現在言語の表示文字列。 StringStore を引数で受けるのは、 この
	// resolver を StringStore 自身が持つ closure にも入れるため (shared_ptr で
	// capture すると自己参照サイクルになる)。 範囲外 index は clamp。
	std::string at(StringStore* ss, int idx) const
	{
		const std::vector<std::string>& primary = ids.empty() ? statics : ids;
		if (primary.empty()) return {};
		if (idx < 0) idx = 0;
		if (idx >= static_cast<int>(primary.size())) idx = static_cast<int>(primary.size()) - 1;
		if (ids.empty()) return statics[idx];
		const std::string& tid = ids[idx];
		if (ss && ss->has(tid)) return ss->resolve(tid);
		if (idx < static_cast<int>(statics.size())) return statics[idx];
		return tid;
	}
};

TextListSpec read_text_list(const picojson::object& o)
{
	TextListSpec spec;
	if (auto* la = get_array(o, "text_list")) {
		for (auto& v : *la) if (v.is<std::string>()) spec.statics.push_back(v.get<std::string>());
	}
	if (auto* la = get_array(o, "text_list_id")) {
		for (auto& v : *la) if (v.is<std::string>()) spec.ids.push_back(v.get<std::string>());
	}
	return spec;
}

//---------------------------------------------------------------------------
// LayoutBuilder — element ツリーを再帰生成
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// style.row_height 用: 子の縦 min/max を height まで引き上げる proxy。
// ce::vmin_size は子の縦 max でクランプされるため、 固定高ウィジェット
// (button 等、 縦 max = 自然高) には効かない。 こちらは行としての高さを
// 親 (vtile) に確保させ、 子には行全体の bounds を渡す — button は行
// いっぱいの body を描き、 glyph 系 (check_box 等) は自分の bounds 内に
// 収まるので隣接要素への干渉は起きない。
//---------------------------------------------------------------------------
class row_min_size_element : public ce::proxy_base
{
public:
	row_min_size_element(float height, element_ptr subject_)
	 : _height(height)
	 , _subject(std::move(subject_))
	{}

	ce::element const& subject() const override { return *_subject; }
	ce::element&       subject() override       { return *_subject; }

	ce::view_limits limits(ce::basic_context const& ctx) const override
	{
		auto l = _subject->limits(ctx);
		if (l.min.y < _height) l.min.y = _height;
		if (l.max.y < l.min.y) l.max.y = l.min.y;
		return l;
	}

private:
	float       _height;
	element_ptr _subject;
};

// focus_link 装飾の「ホバー → リンク先フォーカス」配線情報。 build 時は
// target の id 文字列しか無いので、 take_deferred_view_callbacks で element
// へ解決して埋める (実体のプロキシは hover_focus_link_element、 後方で定義)。
struct hover_link_wire
{
	std::vector<std::pair<std::string, element_ptr>> resolved;
	std::shared_ptr<std::string> focused_slot;   // 現在フォーカス中の id
};

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

	// top-level "style" ブロックの既定値 (いずれも 0 = 無効 = 従来一致)。
	//   tile_gap   : "gap" 未指定の vtile/htile の子間隙間 px
	//   row_height : button / toggle_button / check_box / slide_switch /
	//                input_box / selection_menu の既定最小高 px (vmin_size 相当)
	void set_tile_gap(float g)   { _tile_gap = (g > 0.0f) ? g : 0.0f; }
	void set_row_height(float h) { _row_height = (h > 0.0f) ? h : 0.0f; }

	std::map<std::string, cycfi::elements::pixmap_ptr>& atlases() { return _atlases; }
	element_ptr build(const picojson::value& v);

	// build() の型ディスパッチ本体 ("animate" ラップ前)。
	element_ptr build_dispatch(const picojson::object& o, const std::string& type);

	// 要素に "animate" があれば変換 proxy で包み、 演出束縛を _animations に
	// 積む。 無ければ el をそのまま返す。 Phase A は enter 発火のみ。
	element_ptr apply_animation(const picojson::object& o, element_ptr el);

	// 要素に "opacity" / "opacity_var" があれば不透明度 proxy で包む。
	// 無ければ el をそのまま返す。
	element_ptr apply_enabled_fade(const picojson::object& o, element_ptr el);
	element_ptr apply_flatten(const picojson::object& o, element_ptr el);
	element_ptr apply_opacity(const picojson::object& o, element_ptr el);
	element_ptr apply_visible(const picojson::object& o, element_ptr el);

	// build 中に集めたパーツ演出束縛 (xform_state を proxy と共有)。
	std::vector<anim_binding> take_animations() { return std::move(_animations); }

	// 相対パスを解決して fs::path にする。 path が絶対ならそのまま。
	// ホストが set_resource_resolver() を設定していればそちらへ委譲
	// (origin = この画面の resource_base)。 未設定なら resource_base 前置。
	cycfi::fs::path resolve_resource(const std::string& path) const
	{
		if (path.empty()) return {};
		bool absolute = (path[0] == '/' || path[0] == '\\'
		                 || (path.size() > 1 && path[1] == ':'));
		if (absolute) {
			return cycfi::fs::path(path);
		}
		if (const auto& r = get_resource_resolver()) {
			return cycfi::fs::path(r(path, _resource_base));
		}
		if (_resource_base.empty()) {
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

	// focus_link 装飾の要素一覧 (parsed_layout::focus_link_elements 用)。
	// take 系メソッドなので 1 回しか呼ばない。
	std::vector<std::weak_ptr<ce::element>> take_focus_link_elements()
	{ return std::move(_focus_link_elements); }

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
		// "focus_nav" 指定 (register_id で収集) があれば、 build 完了後の
		// ここで id を element へ解決し、 view::focus_nav_override を配線
		// するクロージャを積む (SGOCT-133)。
		if (!_focus_nav_specs.empty()) {
			auto tbl = std::make_shared<std::map<
				cycfi::elements::element*,
				std::array<element_ptr, 4>>>();
			for (auto& s : _focus_nav_specs) {
				std::array<element_ptr, 4> t{};
				for (int i = 0; i < 4; ++i) {
					if (s.ids[i].empty()) continue;
					auto it = _id_to_element.find(s.ids[i]);
					if (it != _id_to_element.end()) t[i] = it->second;
					else em_logf("elements_modal: focus_nav target not found: %s",
					             s.ids[i].c_str());
				}
				(*tbl)[s.el.get()] = std::move(t);
			}
			_focus_nav_specs.clear();
			_deferred_view_cbs.push_back(
				[tbl](cycfi::elements::view& vw) {
					vw.focus_nav_override(
						[tbl](cycfi::elements::element* cur, int dir)
							-> cycfi::elements::element* {
							if (dir < 0 || dir > 3) return nullptr;
							auto it = tbl->find(cur);
							if (it == tbl->end()) return nullptr;
							auto& t = it->second;
							return t[dir] ? t[dir].get() : nullptr;
						});
				});
		}

		// focus_link 装飾のホバー配線: target id を element へ解決して埋める
		// (view には触らないが、 id 解決の完了地点がここなので同居させる)。
		if (!_hover_link_wires.empty()) {
			for (auto& [wire, ids] : _hover_link_wires) {
				wire->focused_slot = _focused_id_slot;
				for (auto& id : ids) {
					auto it = _id_to_element.find(id);
					if (it != _id_to_element.end())
						wire->resolved.push_back({id, it->second});
					else em_logf("elements_modal: focus_link hover target "
					             "not found: %s", id.c_str());
				}
			}
			_hover_link_wires.clear();
		}
		return std::move(_deferred_view_cbs);
	}

private:
	event_callback _cb;
	std::string _default_locale;
	std::string _resource_base;

	// "focus_nav" の収集分 (register_id で積み、 take_deferred_view_callbacks
	// で id 解決 + view 配線に変換する)。 ids は left/right/up/down の順。
	struct focus_nav_spec {
		element_ptr el;
		std::array<std::string, 4> ids;
	};
	std::vector<focus_nav_spec> _focus_nav_specs;

	float _font_scale = 1.0f;   // top-level "font_scale" (既定 1.0 = 従来一致)
	float _tile_gap = 0.0f;     // style.tile_gap (0 = 従来どおり密着)
	float _row_height = 0.0f;   // style.row_height (0 = 無効)

	// widget の実効フォント倍率。 明示 "size"/"size_scale" があればそれを
	// 優先 (resolve_font_scale = px/base)、 なければ top-level _font_scale。
	// button/toggle/radio/check_box の styler size 引数に渡す用。
	float effective_font_scale(const picojson::object& o) const
	{
		if (has_font_field(o, "size", "size_scale"))
			return resolve_font_scale(o, "size", "size_scale");
		return _font_scale;
	}

	// style.row_height: 行ウィジェット (button 系 / input_box 等) に既定の
	// 最小高を与える。 row_min_size_element で包む (縦 min/max を引き上げる
	// ので固定高ウィジェットにも効く)。 自然サイズがこれより大きい場合は
	// 影響しない (最小値のみ)。
	element_ptr apply_row_height(element_ptr el) const
	{
		if (_row_height <= 0.0f || !el) return el;
		return std::make_shared<row_min_size_element>(_row_height, std::move(el));
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
	// flatten の焼き直し契機 (両ストアが bump する)。 build 時に配線する。
	layout_revision _rev = std::make_shared<std::uint64_t>(0);
	std::map<std::string, std::map<std::string, std::string>> _vars_on_focus;
	std::vector<std::pair<std::string, std::function<bool()>>> _focusables;
	std::vector<std::pair<std::string, std::function<bool()>>> _hoverables;
	std::vector<std::function<void(cycfi::elements::view&)>> _deferred_view_cbs;
	std::shared_ptr<std::string> _focused_id_slot = std::make_shared<std::string>();
	// "focus_link": フォーカス中の id を受け取って見た目を変える飾り要素。
	// take_focus_poll がフォーカス変化のたびに現在の id で呼ぶ。
	std::vector<std::function<void(const std::string&)>> _focus_links;
	// focus_link 装飾の要素本体 (部分再描画ホストのダーティ登録用)。
	std::vector<std::weak_ptr<ce::element>> _focus_link_elements;
	// focus_link 装飾のホバー配線 (target id は deferred で element へ解決)
	std::vector<std::pair<std::shared_ptr<hover_link_wire>,
	                      std::vector<std::string>>> _hover_link_wires;
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

	// i18n: "options_id" 付き picker を言語切替に追従させる。 言語が変わったら
	// 各 textID を StringStore で再解決して set_options (選択 index は維持)。
	// closure は StringStore 自身 (_lang_subs) に格納されるので raw StringStore*
	// 捕捉で生存問題なし (shared_ptr 捕捉だと自己参照サイクルになる)。
	template <typename P>
	void subscribe_picker_options(std::shared_ptr<P> p,
	                              const std::vector<std::string>& opt_ids)
	{
		if (opt_ids.empty() || !p) return;
		std::weak_ptr<P> w = p;
		StringStore* ss = _strings.get();
		_strings->subscribe_language([w, ss, ids = opt_ids](const std::string&) {
			if (auto sp = w.lock()) {
				std::vector<std::string> opts;
				opts.reserve(ids.size());
				for (const auto& tid : ids) opts.push_back(ss->resolve(tid));
				sp->set_options(std::move(opts));
			}
		});
	}

	// index_var の var→picker 一方向同期 (quiet)。 picker→var は on_change 側で
	// set 済なので、 これで index_var が双方向になる (ホストの setVar 一発で
	// 表示も依存 widget も追従)。 set_index は on_change を発火しないので
	// on_change → vars->set → ここ、のエコーはループしない。
	template <typename P>
	void subscribe_picker_index_var(std::shared_ptr<P> p,
	                                const std::string& index_var)
	{
		if (index_var.empty() || !p) return;
		std::weak_ptr<P> w = p;
		_vars->subscribe(index_var, [w](const std::string& v) {
			long idx = 0;
			try { idx = std::stol(v); } catch (...) { return; }
			if (idx < 0) return;
			if (auto sp = w.lock()) {
				if (static_cast<std::size_t>(idx) != sp->index())
					sp->set_index(static_cast<std::size_t>(idx));
			}
		}, p);
	}

	// "selected_var"/"selected_value": choice (atlas_choice / radio_button) を
	// ラジオグループ変数に連動させる。 var == value のとき選択状態。 グループ
	// 全員が同じ var を subscribe する前提なので、 ホストの setVar 一発で
	// グループ全体の選択が入れ替わる (basic_choice::select は on_click を
	// 発火しない quiet 操作)。 choice→var はビルダが on_click に仕込む書き戻し
	// (選択された側のみ) で行う = 双方向。
	void subscribe_choice_selected_var(const element_ptr& shared,
	                                   const std::string& sel_var,
	                                   const std::string& sel_val)
	{
		if (sel_var.empty() || !shared) return;
		auto bc = std::dynamic_pointer_cast<ce::basic_choice>(shared);
		if (!bc) {
			em_logf("elements_modal: selected_var=\"%s\" on non-choice element",
			        sel_var.c_str());
			return;
		}
		std::weak_ptr<ce::basic_choice> w = bc;
		_vars->subscribe(sel_var, [w, sel_val](const std::string& v) {
			if (auto sp = w.lock()) sp->select(v == sel_val);
		}, bc);
	}

	// selected_var 一式を JSON から読む。 selected_value 省略時は "1"
	// (単独 ON/OFF 用途)。 var に既存値があれば初期選択状態を上書きする。
	void read_choice_selected_var(const picojson::object& o,
	                              std::string& sel_var, std::string& sel_val,
	                              bool& init)
	{
		sel_var = string_or(o, "selected_var");
		if (sel_var.empty()) return;
		sel_val = string_or(o, "selected_value");
		if (sel_val.empty()) sel_val = "1";
		if (auto* cur = _vars->get(sel_var)) init = (*cur == sel_val);
	}

	// "enabled_var": picker 選択肢の有効/無効 mask を変数連動にする
	// (cycle_picker / atlas_cycle_picker のみ)。 値は index 順の '0'/'1' 文字列
	// (例 "10111011" = index 1 を無効)。 mask より後ろの index は有効扱い。
	// step/click/pad は無効 index をスキップ、 現在選択が無効化されたら最寄りの
	// 有効 index へ進めて on_change 発火 (依存 widget が追従する)。
	void wire_picker_enabled_var(std::shared_ptr<ce::cycle_picker> p,
	                             const picojson::object& o)
	{
		std::string var = string_or(o, "enabled_var");
		if (var.empty() || !p) return;
		auto apply = [](ce::cycle_picker& pk, const std::string& v) {
			std::vector<bool> mask;
			mask.reserve(v.size());
			for (char c : v) mask.push_back(c != '0');
			pk.set_enabled(std::move(mask));
		};
		if (auto* cur = _vars->get(var)) apply(*p, *cur);
		std::weak_ptr<ce::cycle_picker> w = p;
		_vars->subscribe(var, [w, apply](const std::string& v) {
			if (auto sp = w.lock()) apply(*sp, v);
		}, p);
	}

	// "enabled_var": button (atlas_button / button / invert_button / ring_button)
	// の有効/無効を変数連動にする。 値 "0" で無効、 それ以外 (既定) で有効。
	// 無効時は click が効かず、 描画は disabled frame があればそれ、 無ければ
	// 半透明 (sprite_button_styler / basic_button::draw のフェード) になる。
	void wire_button_enabled_var(const element_ptr& shared,
	                             const picojson::object& o)
	{
		std::string var = string_or(o, "enabled_var");
		if (var.empty()) return;
		auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared);
		if (!bp) return;
		auto apply = [](ce::basic_button& b, const std::string& v) {
			b.enable(v != "0");
		};
		if (auto* cur = _vars->get(var)) apply(*bp, *cur);
		std::weak_ptr<ce::basic_button> w = bp;
		_vars->subscribe(var, [w, apply](const std::string& v) {
			if (auto sp = w.lock()) apply(*sp, v);
		}, bp);
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
	element_ptr build_text_box    (const picojson::object& o);
	element_ptr build_text_area   (const picojson::object& o);
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
	element_ptr build_atlas_cycle_picker(const picojson::object& o);

	// picker 系共通: "options" / "options_id" (StringStore 解決、 優先) /
	// "initial" のパース。 options が空なら false (ビルド中止)。
	bool parse_picker_options(const picojson::object& o, const char* type_name,
	                          std::vector<std::string>& opts,
	                          std::vector<std::string>& opt_ids,
	                          std::size_t& initial);

	// picker 系共通: "index_var" — 選択 index を VariableStore と連動させる。
	// 変数に既に値があれば initial をそれで上書き (範囲内のみ)。
	std::string resolve_index_var(const picojson::object& o,
	                              std::size_t n_options, std::size_t& initial);
	element_ptr build_invert_button(const picojson::object& o);
	element_ptr build_ring_button (const picojson::object& o);

	// i18n: proxy ベースの button (invert/ring/plain) で "text_id" があれば、
	// styler (= proxy の subject、 text_writer 派生) を StringStore に subscribe
	// して言語切替で set_text する。 初期値も resolve して同期する。
	void subscribe_button_text_id(const picojson::object& o, const element_ptr& shared);
	// "index_var" + text_list(_id) を text_writer な widget へ結線する
	// (label / text_area 共用)。 index 変化と言語切替の両方で set_text する。
	// index_var 未指定 / リスト空 / text_writer 非派生なら no-op。
	void bind_text_list(const std::string& index_var, const TextListSpec& list,
	                    int initial_index, const element_ptr& out);
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
	element_ptr build_image       (const picojson::object& o);
	element_ptr build_animated_sprite(const picojson::object& o);
	element_ptr build_atlas_button(const picojson::object& o);
	element_ptr build_atlas_toggle(const picojson::object& o);
	element_ptr build_atlas_choice(const picojson::object& o);
	element_ptr build_atlas_slider(const picojson::object& o);
	element_ptr build_atlas_progress(const picojson::object& o);
	element_ptr build_atlas_number(const picojson::object& o);
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

//---------------------------------------------------------------------------
// "focus_point": [ax, ay] — focus hot point のアンカー比 (0..1、 既定 0.5,0.5
// = 中心) を widget 単位で上書きする proxy。 cursor-warp ナビでカーソルを
// 飛ばす先を、 透過の大きい絵や特殊形状の widget で個別調整するための逃がし。
// has_custom を返すので widget 既定 (slider の thumb 等) より優先される。
//---------------------------------------------------------------------------
namespace {

template <typename Subject>
class focus_point_element : public ce::proxy<Subject>
{
public:
	focus_point_element(Subject subject, float ax, float ay)
	 : ce::proxy<Subject>(std::move(subject)), _ax(ax), _ay(ay)
	{}

	bool has_custom_focus_hot_point() const override { return true; }
	ce::point focus_hot_point(ce::context const& ctx) override
	{
		return ce::point{
			ctx.bounds.left + ctx.bounds.width()  * _ax,
			ctx.bounds.top  + ctx.bounds.height() * _ay };
	}

private:
	float _ax, _ay;
};

element_ptr apply_focus_point(const picojson::object& o, element_ptr el)
{
	auto* v = get_field(o, "focus_point");
	if (!v || !v->is<picojson::array>()) return el;
	const auto& arr = v->get<picojson::array>();
	if (arr.size() < 2 || !pj_is_num(arr[0]) || !pj_is_num(arr[1])) {
		em_logf("elements_modal: focus_point must be [ax, ay] (0..1)");
		return el;
	}
	float ax = static_cast<float>(pj_num(arr[0]));
	float ay = static_cast<float>(pj_num(arr[1]));
	return ce::share(focus_point_element(ce::hold_any(std::move(el)), ax, ay));
}

} // anonymous (focus_point)

//---------------------------------------------------------------------------
// focus_link 装飾のホバー → リンク先フォーカス
//
// 行下地 (IMG 〜行 のパーツ) のように「クリックできる実コントロールより広い
// 行領域」へマウスオーバーしたとき、 行内のコントロールへフォーカス (オレンジ
// 枠) を移すためのプロキシ。 実コントロールは z 上位で先に hit するので、
// コントロール外 (行ラベル部など) のホバーだけがここへ届く。 クリック等の
// 操作は何もしない (従来どおり)。
//---------------------------------------------------------------------------
namespace {

template <typename Subject>
class hover_focus_link_element : public ce::proxy<Subject>
{
public:
	hover_focus_link_element(Subject subject,
	                         std::shared_ptr<hover_link_wire> wire)
	 : ce::proxy<Subject>(std::move(subject)), _wire(std::move(wire))
	{}

	ce::element* hit_test(ce::context const& ctx, ce::point p,
	                      bool /*leaf*/, bool /*control*/) override
	{
		// 装飾自体はコントロールではないが、 hover 判定 (control=true の
		// hit_element) に載せるため自分を返す
		return ctx.bounds.includes(p) ? this : nullptr;
	}
	bool wants_control() const override { return true; }

	bool cursor(ce::context const& ctx, ce::point /*p*/,
	            ce::cursor_tracking status) override
	{
		if (status == ce::cursor_tracking::leaving) return false;
		if (!ctx.view.hover_focus()) return false;
		if (!_wire || _wire->resolved.empty()) return false;
		// 行内のどれかが既にフォーカス中なら動かさない (choice 行で選択中の
		// 項目からフォーカスを引き剥がさないため)
		if (_wire->focused_slot) {
			for (auto& kv : _wire->resolved)
				if (kv.first == *_wire->focused_slot) return false;
		}
		ctx.view.focus(_wire->resolved.front().second);
		return false;
	}

private:
	std::shared_ptr<hover_link_wire> _wire;
};

} // anonymous (hover_focus_link)

//---------------------------------------------------------------------------
// "opacity" / "opacity_var" — 要素単位の不透明度
//
// canvas の global_alpha (0..1) を子の描画中だけ掛ける proxy。 オフスクリーン
// 合成ではなく「その要素が描く fill / stroke / text / image の alpha に乗算」
// なので軽い。 字幕窓の下地だけ薄くする、 といった用途向け
// (設定で透過率を変えられるよう opacity_var で変数連動もできる)。
//---------------------------------------------------------------------------
namespace {

template <typename Subject>
class opacity_element : public ce::proxy<Subject>
{
public:
	opacity_element(Subject subject, std::shared_ptr<float> a)
	 : ce::proxy<Subject>(std::move(subject)), _alpha(std::move(a))
	{}

	void draw(ce::context const& ctx) override
	{
		const float a = _alpha ? *_alpha : 1.0f;
		if (a >= 1.0f) { ce::proxy<Subject>::draw(ctx); return; }
		if (a <= 0.0f) return;                    // 完全透明なら描かない
		auto& cnv = ctx.canvas;
		const float prev = cnv.global_alpha();
		cnv.global_alpha(prev * a);
		ce::proxy<Subject>::draw(ctx);
		cnv.global_alpha(prev);
	}

private:
	std::shared_ptr<float> _alpha;
};


//---------------------------------------------------------------------------
// "visible_var" — 表示 / 非表示を変数で切り替える
//
// 値 "0" (と "false" / 空文字) で非表示、 それ以外で表示。 非表示のときは
//   ・描かない
//   ・フォーカスを受け取らない (方向ナビの飛び先にならない)
//   ・当たり判定に出ない (クリックが下の要素へ抜ける)
//   ・入力イベントを一切受けない
// 機種によって不要な設定項目を消す用途 (PC 専用のフルスクリーン切替を
// コンソール版で出さない等) を想定している。
//
// ⚠ **場所は空けたまま**であることに注意。 canvas は子を絶対座標で配置する
//    ので、 非表示にしてもそこに空白が残り、 下の項目は詰まらない。 詰めたい
//    ならレイアウト自体を機種別に用意する必要がある。
//---------------------------------------------------------------------------
namespace {

template <typename Subject>
class visible_element : public ce::proxy<Subject>
{
public:
	visible_element(Subject subject, std::shared_ptr<bool> vis)
	 : ce::proxy<Subject>(std::move(subject)), _vis(std::move(vis))
	{}

	bool shown() const { return !_vis || *_vis; }

	void draw(ce::context const& ctx) override
	{
		if (!shown()) return;
		ce::proxy<Subject>::draw(ctx);
	}

	ce::element* hit_test(ce::context const& ctx, ce::point p,
	                      bool leaf, bool control) override
	{
		if (!shown()) return nullptr;
		return ce::proxy<Subject>::hit_test(ctx, p, leaf, control);
	}

	bool wants_focus() const override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::wants_focus();
	}

	bool wants_control() const override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::wants_control();
	}

	bool click(ce::context const& ctx, ce::mouse_button btn) override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::click(ctx, btn);
	}

	bool key(ce::context const& ctx, ce::key_info k) override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::key(ctx, k);
	}

	bool cursor(ce::context const& ctx, ce::point p,
	            ce::cursor_tracking status) override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::cursor(ctx, p, status);
	}

	bool scroll(ce::context const& ctx, ce::point dir, ce::point p) override
	{
		if (!shown()) return false;
		return ce::proxy<Subject>::scroll(ctx, dir, p);
	}

private:
	std::shared_ptr<bool> _vis;
};

} // anonymous (visible)

} // anonymous (opacity)

//---------------------------------------------------------------------------
// "flatten" — グループを 1 枚に焼いてから描く
//
// opacity / opacity_var は canvas の global_alpha に乗算するだけなので、
// 重なった子はそれぞれ独立にブレンドされる。 ボタン画像の上にラベルが乗る
// ような構図を薄くすると、 文字の下から画像が透けて「文字だけ浮く」見え方に
// なる (背景も二重に薄くなる)。
//
//   正しい合成 : 背景*(1-a) + [画像+文字を合成したもの]*a
//   乗算のみ   : 背景*(1-a)^2 + 画像*a*(1-a) + 文字*a
//
// "flatten": true を付けると、 子を一度オフスクリーンの pixmap へ描いてから
// 1 枚の画像として描き戻す。 canvas::draw(pixmap, ...) は global_alpha を
// ThorVG の Picture::opacity() として **画像全体に 1 回だけ** 適用するので、
// これで本来のグループ不透明度になる。
//
// 焼き直しは「サイズ / スケールが変わったとき」と「変数 or 言語が変わった
// とき (layout_revision)」。 hover / focus のように変数を介さない状態変化は
// 検知できないので、 状態で見た目が変わる要素には現状向かない。
//---------------------------------------------------------------------------
namespace {

template <typename Subject>
class flatten_element : public ce::proxy<Subject>
{
public:
	flatten_element(Subject subject, layout_revision rev)
	 : ce::proxy<Subject>(std::move(subject)), _rev(std::move(rev))
	{}

	void draw(ce::context const& ctx) override
	{
		const auto b = ctx.bounds;
		const float sc = ctx.canvas.scale() > 0.0f ? ctx.canvas.scale() : 1.0f;
		const int w = int(std::ceil(b.width()  * sc));
		const int h = int(std::ceil(b.height() * sc));
		if (w <= 0 || h <= 0) return;

		const std::uint64_t rev = _rev ? *_rev : 0;
		if (!_pm || _w != w || _h != h || _rev_baked != rev)
		{
			bake(ctx, b, w, h, sc);
			_w = w; _h = h; _rev_baked = rev;
		}
		if (_pm) ctx.canvas.draw(*_pm, b);
	}

private:
	void bake(ce::context const& ctx, ce::rect b, int w, int h, float sc)
	{
		_pm.reset();
		// pixmap は「ピクセル数 + scale」で持ち、 論理サイズ = px * scale。
		// 描き戻しで src = 論理サイズ = bounds になるよう scale = 1/sc。
		auto pm = std::make_unique<ce::pixmap>(
			ce::point{float(w), float(h)}, 1.0f / sc);
		{
			ce::pixmap_context pctx(*pm);
			if (!pctx.buffer()) return;
			// 子は原点基準で描かせる (bounds を原点へ寄せた context を作る)。
			ce::canvas cnv(pctx.buffer(), std::uint32_t(w), std::uint32_t(h), sc);
			ce::context sub{ctx.view, cnv, this,
				ce::rect{0, 0, b.width(), b.height()}};
			ce::proxy<Subject>::draw(sub);
			cnv.flush();
			// pctx のデストラクタが buffer を pixmap へ commit する
		}
		_pm = std::move(pm);
	}

	std::unique_ptr<ce::pixmap> _pm;
	layout_revision _rev;
	int _w = 0, _h = 0;
	std::uint64_t _rev_baked = std::uint64_t(-1);
};

} // anonymous (flatten)

element_ptr LayoutBuilder::build(const picojson::value& v)
{
	if (!v.is<picojson::object>()) return nullptr;
	const auto& o = v.get<picojson::object>();
	auto type = string_or(o, "type");

	element_ptr el = build_dispatch(o, type);
	// "animate" 指定があれば変換 proxy で包み、 演出束縛を登録する (Phase A)。
	if (el) el = apply_animation(o, std::move(el));
	// "focus_point" 指定があれば focus hot point 上書き proxy で包む。
	if (el) el = apply_focus_point(o, std::move(el));
	// "enabled_var" 指定がボタン以外に付いていたら、 無効時フェード proxy で包む。
	// (ボタンは basic_button::enable で自前にフェードするので対象外)
	if (el) el = apply_enabled_fade(o, std::move(el));
	// "flatten" 指定があれば、 先に 1 枚へ焼く proxy で包む。
	// opacity より内側 (先に適用) にすることで、 焼いた 1 枚に対して
	// opacity が掛かる = 正しいグループ不透明度になる。
	if (el) el = apply_flatten(o, std::move(el));
	// "opacity" / "opacity_var" 指定があれば不透明度 proxy で包む。
	if (el) el = apply_opacity(o, std::move(el));
	// "visible_var" 指定があれば表示制御 proxy で包む (いちばん外側)。
	// 非表示のときは中を一切触らせないので、 最外周である必要がある。
	if (el) el = apply_visible(o, std::move(el));
	return el;
}

// "enabled_var" をボタン以外の要素にも効かせる。
//
// ボタンは basic_button::enable() で自前に無効表示 (テーマの disabled_opacity)
// をするが、 ラベルなど隣に並ぶ要素は別ウィジェットなので取り残され、
// 「板だけ薄くなって文字が濃いまま浮く」見え方になっていた。
// ボタン以外にも同じ変数を書けば一緒に薄くなるようにする。
element_ptr LayoutBuilder::apply_enabled_fade(const picojson::object& o, element_ptr el)
{
	if (!el) return el;
	std::string var = string_or(o, "enabled_var");
	if (var.empty()) return el;
	// ボタンは自前でフェードするので二重掛けしない
	if (std::dynamic_pointer_cast<ce::basic_button>(el)) return el;

	const float off = ce::get_theme().disabled_opacity;
	auto alpha = std::make_shared<float>(1.0f);
	auto apply = [alpha, off](const std::string& v) {
		*alpha = (v == "0") ? off : 1.0f;
	};
	if (auto* cur = _vars->get(var)) apply(*cur);
	_vars->subscribe(var, apply, el);
	return ce::share(opacity_element(ce::hold_any(std::move(el)), alpha));
}

element_ptr LayoutBuilder::apply_flatten(const picojson::object& o, element_ptr el)
{
	if (!el) return el;
	const auto* v = get_field(o, "flatten");
	if (!v) return el;
	bool on = false;
	if (v->is<bool>())        on = v->get<bool>();
	else if (v->is<double>()) on = v->get<double>() != 0.0;
	if (!on) return el;
	// 両ストアが bump する revision を渡す (変数 / 言語の変化で焼き直す)。
	_vars->set_revision(_rev);
	_strings->set_revision(_rev);
	return ce::share(flatten_element(ce::hold_any(std::move(el)), _rev));
}

element_ptr LayoutBuilder::apply_opacity(const picojson::object& o, element_ptr el)
{
	const bool has_val = get_field(o, "opacity") != nullptr;
	std::string var = string_or(o, "opacity_var");
	if (!has_val && var.empty()) return el;

	auto alpha = std::make_shared<float>(
		static_cast<float>(number_or(o, "opacity", 1.0)));
	auto clamp01 = [](float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); };
	*alpha = clamp01(*alpha);

	if (!var.empty()) {
		// 変数 store 連動。 値は 0..1 の 10 進小数。 パース不能な値は無視 (現状維持)。
		if (auto* init = _vars->get(var)) {
			try { *alpha = clamp01(std::stof(*init)); } catch (...) {}
		}
		_vars->subscribe(var, [alpha, clamp01](const std::string& v) {
			try { *alpha = clamp01(std::stof(v)); } catch (...) {}
		}, el);
	}
	return ce::share(opacity_element(ce::hold_any(std::move(el)), alpha));
}

// "visible_var" — 表示 / 非表示を変数で切り替える (値 "0"/"false"/空 で非表示)。
// 機種によって不要な項目を消す用途。 場所は空けたままなので注意 (visible_element
// のコメント参照)。
element_ptr LayoutBuilder::apply_visible(const picojson::object& o, element_ptr el)
{
	if (!el) return el;
	std::string var = string_or(o, "visible_var");
	if (var.empty()) return el;

	auto vis = std::make_shared<bool>(true);
	auto parse = [](const std::string& v) {
		return !(v == "0" || v == "false" || v.empty());
	};
	if (auto* cur = _vars->get(var)) *vis = parse(*cur);
	_vars->subscribe(var, [vis, parse](const std::string& v) {
		*vis = parse(v);
	}, el);
	return ce::share(visible_element(ce::hold_any(std::move(el)), vis));
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
	if (type == "text_box")      return build_text_box(o);
	if (type == "text_area")     return build_text_area(o);
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
	if (type == "atlas_cycle_picker")  return build_atlas_cycle_picker(o);
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
	if (type == "image")          return build_image(o);
	if (type == "animated_sprite") return build_animated_sprite(o);
	if (type == "atlas_button")   return build_atlas_button(o);
	if (type == "atlas_toggle")   return build_atlas_toggle(o);
	if (type == "atlas_check")    return build_atlas_toggle(o);  // alias
	if (type == "atlas_choice")   return build_atlas_choice(o);
	if (type == "atlas_radio")    return build_atlas_choice(o);  // alias
	if (type == "atlas_slider")   return build_atlas_slider(o);
	if (type == "atlas_progress") return build_atlas_progress(o);
	if (type == "atlas_number")   return build_atlas_number(o);
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
				if (a.size() > 0 && pj_is_num(a[0])) ox = float(pj_num(a[0]));
				if (a.size() > 1 && pj_is_num(a[1])) oy = float(pj_num(a[1]));
			} else if (pj_is_num(*v)) {
				ox = oy = float(pj_num(*v));
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
		if (auto* fv = get_field(s, "frames"); fv && pj_is_num(*fv))
			dur_ms = frames_to_ms(float(pj_num(*fv)));
		else
			dur_ms = float(number_or(s, "duration_ms", 300.0));
		b.prog.from = 0.0f; b.prog.to = 1.0f; b.prog.duration_ms = dur_ms;

		// 開始遅延 (スタッガー/シーケンス用)。 "delay" はフレーム、 "delay_ms" は ms。
		if (auto* dv = get_field(s, "delay"); dv && pj_is_num(*dv))
			b.prog.delay_ms = frames_to_ms(float(pj_num(*dv)));
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
	// "focus_nav": { "left"/"right"/"up"/"down": "<id>" } — 矢印/D-Pad の
	// フォーカス移動先をウィジェット単位で明示する (幾何ナビより優先、
	// 未指定方向は幾何ナビにフォールバック)。 ソフトキーボードのような
	// 段ずれレイアウトで意図どおりの移動を定義するための機構 (SGOCT-133)。
	// ターゲット id は build 完了後に解決するので文字列のまま控える。
	if (auto* fv = get_field(o, "focus_nav"); fv && fv->is<picojson::object>()) {
		const auto& fo = fv->get<picojson::object>();
		focus_nav_spec spec;
		spec.el = shared;
		spec.ids[0] = string_or(fo, "left");
		spec.ids[1] = string_or(fo, "right");
		spec.ids[2] = string_or(fo, "up");
		spec.ids[3] = string_or(fo, "down");
		if (shared && (!spec.ids[0].empty() || !spec.ids[1].empty() ||
		               !spec.ids[2].empty() || !spec.ids[3].empty()))
			_focus_nav_specs.push_back(std::move(spec));
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
	auto focus_links     = std::move(_focus_links);
	// "" は「何も focus されていない」を表す sentinel。 初回はその状態と
	// 比較されるので、 初回 focus に対して必ず 1 回 set される。
	return [vars, vars_on_focus, focusables, last_focused_id, focus_links]() {
		std::string current;
		for (auto& kv : focusables) {
			if (kv.second()) { current = kv.first; break; }
		}
		if (current == *last_focused_id) return;
		*last_focused_id = current;
		// focus_link の飾り要素へ通知 (フォーカスが外れた "" も配る)。
		for (auto& fn : focus_links) fn(current);
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
void LayoutBuilder::bind_text_list(const std::string& index_var,
                                   const TextListSpec& list, int initial_index,
                                   const element_ptr& out)
{
	if (index_var.empty() || list.empty()) return;
	auto sp = std::dynamic_pointer_cast<ce::text_writer>(out);
	if (!sp) return;
	std::weak_ptr<ce::text_writer> w = sp;
	// 現在 index。 index_var 側と言語切替側の両方から参照する。
	auto idx_slot = std::make_shared<int>(initial_index);
	// VariableStore に入る closure なので StringStore は shared_ptr 捕捉で
	// よい (VariableStore → StringStore の一方向参照、 サイクルなし)。
	// owner (out) は部分再描画のダーティ矩形用。
	_vars->subscribe(index_var,
		[w, idx_slot, ss = _strings, list](const std::string& v) {
			if (auto p = w.lock()) {
				*idx_slot = std::atoi(v.c_str());
				p->set_text(list.at(ss.get(), *idx_slot));
			}
		}, out);
	// i18n: 言語が変わったら現在 index を新言語で引き直す。 closure は
	// StringStore 自身が持つので raw ポインタ捕捉 (options_id と同じ)。
	if (!list.ids.empty()) {
		StringStore* ssp = _strings.get();
		_strings->subscribe_language(
			[w, idx_slot, ssp, list](const std::string&) {
				if (auto p = w.lock())
					p->set_text(list.at(ssp, *idx_slot));
			});
	}
}

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
	// "text_list": [...] + "index_var": "name" — テキストリストの指定番号を表示する
	// 高度なラベル。 index_var の値 (整数文字列) を添字にリストから引く。 button の
	// "vars_on_focus":{name:"N"} で focus 連動、 或いはホストの setVar で切替。
	// text_id/text_var と併用時は index_var 優先。 i18n 版 "text_list_id" 込みの
	// 仕様は TextListSpec 側のコメント参照。
	std::string index_var = string_or(o, "index_var");
	const TextListSpec text_list = read_text_list(o);
	int list_idx = 0;
	if (!index_var.empty() && !text_list.empty()) {
		if (auto* init = _vars->get(index_var)) list_idx = std::atoi(init->c_str());
		text = text_list.at(_strings.get(), list_idx);
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
		float ax = static_cast<float>(pj_num(anc->at(0)));
		float ay = static_cast<float>(pj_num(anc->at(1)));
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
		// "fit": true — 実測幅が at 矩形の幅を超えるときだけフォントを縮めて
		// 収める。 多言語化 (EN/TC/SC) で訳文が枠に入らないのを、 訳を詰めずに
		// 表示側で吸収するための指定。 "fit_min_scale" で縮小の下限倍率を変えられる
		// (既定 0.5)。 言語切替で text が差し替わっても描画のたびに測り直すので
		// 追従する。 wrap / runs (rich text) との併用は無効。
		if (truthy_field(get_field(o, "fit"))) {
			if (auto* at = dynamic_cast<ce::anchored_text*>(out.get()))
				at->set_fit_width(true,
					static_cast<float>(number_or(o, "fit_min_scale", 0.5)));
		}
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

	// "text_align" (left/center/right): text_anchor 無しの素 label でも横アライン
	// を反映する (uitool の表示 = 左寄せ既定 と実機を一致させる)。 キー無しは従来
	// どおり theme 既定 (center|middle) — 手書き JSON の互換維持。
	int halign = -1;
	{
		std::string ta = string_or(o, "text_align");
		if      (ta == "left")   halign = ce::canvas::left;
		else if (ta == "center") halign = ce::canvas::center;
		else if (ta == "right")  halign = ce::canvas::right;
	}
	// label builder API は font_color / relative_font_size を呼ぶごとに
	// ラッパ型が変わるチェーン。 直接代入で繋げないので、 仕上げ (text_align /
	// locale → share) はジェネリックラムダで一元化する。
	// locale は文字列空なら付けない (default_locale 含む)。
	auto finish = [&](auto e) {
		auto done = [&](auto e2) {
			if (locale.empty()) out = ce::share(std::move(e2));
			else                out = ce::share(e2.locale(std::move(locale)));
		};
		if (halign >= 0) done(e.text_align(halign));
		else             done(std::move(e));
	};
	auto base = ce::label(text);
	if      (has_color && has_size) finish(base.font_color(col).font_size(sz));
	else if (has_color)             finish(base.font_color(col));
	else if (has_size)              finish(base.font_size(sz));
	else                            finish(std::move(base));
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
				}, out);
			}
		} else {
			em_logf("elements_modal: label with text_id=\"%s\" text_var=\"%s\" — "
			        "text_writer 未継承で set_text 仕掛け失敗",
			        text_id.c_str(), text_var.c_str());
		}
	}
	// index_var + text_list(_id): index 変化でリストから引いて set_text
	// (指定番号表示ラベル)。 text_list_id なら言語切替でも同じ index を引き直す。
	bind_text_list(index_var, text_list, list_idx, out);
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
	wire_button_enabled_var(shared, o);
	note_vars_on_focus(o, id);    // focus 時に vars を書込む (メニュー説明欄など)
	subscribe_button_text_id(o, shared);  // i18n: text_id があれば言語連動
	// "close_on_click": true な button だけホスト側で finish フラグを立てる対象。
	// デフォルト (省略) は閉じず、 onAction だけ発火する。
	if (!id.empty()) {
		if (truthy_field(get_field(o, "close_on_click"))) {
			_close_button_ids.insert(id);
		}
	}
	return apply_row_height(shared);
}

element_ptr LayoutBuilder::build_vtile(const picojson::object& o)
{
	// "gap": 子要素間の隙間 px (未指定なら style.tile_gap、 それも無ければ 0 =
	// 従来どおり密着)。 子の間へ vspacer を自動挿入するのと等価。
	const float gap = static_cast<float>(number_or(o, "gap", _tile_gap));
	ce::vtile_composite tile;
	bool first = true;
	for (auto& c : build_children(o)) {
		if (!first && gap > 0.0f)
			tile.push_back(ce::share(ce::vsize(gap, ce::element{})));
		tile.push_back(c);
		first = false;
	}
	return ce::share(std::move(tile));
}

element_ptr LayoutBuilder::build_htile(const picojson::object& o)
{
	// "gap": build_vtile と同じ (横方向は hspacer 相当を挿入)。
	const float gap = static_cast<float>(number_or(o, "gap", _tile_gap));
	ce::htile_composite tile;
	bool first = true;
	for (auto& c : build_children(o)) {
		if (!first && gap > 0.0f)
			tile.push_back(ce::share(ce::hsize(gap, ce::element{})));
		tile.push_back(c);
		first = false;
	}
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
	} else if (auto* v = get_field(o, "padding"); v && pj_is_num(*v)) {
		l = t = r = b = static_cast<float>(pj_num(*v));
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

//---------------------------------------------------------------------------
// text_box — 複数行・自動折返しの静的テキスト (cycfi static_text_box)。
//   { "type": "text_box", "text": "...", "size": 13, "color": [r,g,b,a],
//     "mono": 1, "text_var": "varname" }
//   幅は親のレイアウト (hsize 等) が決め、 高さは折返し結果に追従する。
//   長文は親に scroller を置いてスクロールする。 "text_var" は label と同じ
//   変数 store 購読で、 ホストの setVar により本文を丸ごと差し替えられる
//   (ライセンス表示等の長文ビューア向け。 行 label を大量に並べるより軽い)。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_text_box(const picojson::object& o)
{
	std::string text_var = string_or(o, "text_var");
	std::string text;
	if (!text_var.empty()) {
		if (auto* init = _vars->get(text_var)) text = *init;
		else                                    text = string_or(o, "text");
	} else {
		text = string_or(o, "text");
	}

	bool mono = truthy_field(get_field(o, "mono"));
	auto descr = mono ? ce::get_theme().mono_spaced_font
	                  : ce::get_theme().text_box_font;
	float px = resolve_font_px(o, "size", "size_scale");
	if (!has_font_field(o, "size", "size_scale")) {
		px = descr._size;
		if (_font_scale != 1.0f) px *= _font_scale;
	}
	ce::font fnt{ descr.size(px) };

	auto col = ce::get_theme().text_box_font_color;
	if (auto* arr = get_array(o, "color")) col = parse_color(*arr);

	auto sp = std::make_shared<ce::static_text_box>(std::move(text), fnt, col);
	if (!text_var.empty()) {
		std::weak_ptr<ce::static_text_box> w = sp;
		_vars->subscribe(text_var, [w](const std::string& v) {
			if (auto p = w.lock()) p->set_text(v);
		}, sp);
	}
	element_ptr out = sp;
	register_id(o, out);
	return out;
}

//---------------------------------------------------------------------------
// text_area — 矩形に流し込む静的テキスト (ce::block_text_box)。
//   { "type": "text_area", "text": "...", "size": 36, "color": [r,g,b,a],
//     "font": "Noto Sans JP", "align": "left|center|right",
//     "line_spacing": 12, "base": "auto|ltr|rtl",
//     "count_var": "sub_count", "count": -1,
//     "text_id": "...", "text_var": "..." }
//
//   text_box との違い:
//     text_box  … cycfi 内蔵の幅貪欲 wrap。 従来互換。 高さは内容に追従。
//     text_area … ホストが差し込んだ block text バックエンド (krkrz なら
//                 glyphware) で折り返す。 行頭行末禁則が効き、 本体
//                 Layer.drawShapedTextArea と改行位置が一致する。 加えて
//                 整列・行間・文字送り (count) を持つ。 字幕/セリフ窓向け。
//
//   "count_var" が本命の使い方: ホストが setVar("sub_count", "12") するだけで
//   文字送りが進む。 折返しは全文で確定するので送ってもリフローしない。
//   -1 (または負値) で全部表示。 クラスタ単位 (合字・結合列・絵文字 ZWJ
//   シーケンスで 1) なので、 本体の shapedTextCount と同じ数え方。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_text_area(const picojson::object& o)
{
	// 初期テキスト解決は label と同規約 (text_id > text_var > 静的 text、
	// "text_list(_id)" + "index_var" があればそちらが優先)。
	std::string text_id  = string_or(o, "text_id");
	std::string text_var = string_or(o, "text_var");
	std::string text;
	if (!text_id.empty()) {
		text = _strings->has(text_id) ? _strings->resolve(text_id)
		                              : string_or(o, "text", text_id);
	} else if (!text_var.empty()) {
		if (auto* init = _vars->get(text_var)) text = *init;
		else                                    text = string_or(o, "text");
	} else {
		text = string_or(o, "text");
	}
	std::string index_var = string_or(o, "index_var");
	const TextListSpec text_list = read_text_list(o);
	int list_idx = 0;
	if (!index_var.empty() && !text_list.empty()) {
		if (auto* init = _vars->get(index_var)) list_idx = std::atoi(init->c_str());
		text = text_list.at(_strings.get(), list_idx);
	}

	// フォント: "font" があれば family 解決、 無ければ theme の text_box_font。
	// "font" は comma 区切り families。 未指定なら theme の text_box_font
	// (= 登録済フォントを Latin → CJK → Emoji の順に並べたもの)。
	// family は descr が string_view で参照するので、 font を作るまで生かす。
	std::string family = string_or(o, "font");
	auto descr = family.empty() ? ce::get_theme().text_box_font
	                            : ce::font_descr{family};
	float px = resolve_font_px(o, "size", "size_scale");
	if (!has_font_field(o, "size", "size_scale")) {
		px = descr._size;
		if (_font_scale != 1.0f) px *= _font_scale;
	}

	auto col = ce::get_theme().text_box_font_color;
	if (auto* arr = get_array(o, "color")) col = parse_color(*arr);

	auto sp = std::make_shared<ce::block_text_box>(
		std::move(text), ce::font{descr.size(px)}, px, col);

	std::string al = string_or(o, "align");
	if      (al == "center") sp->set_align(ce::block_text_request::align_center);
	else if (al == "right")  sp->set_align(ce::block_text_request::align_right);

	std::string base = string_or(o, "base");
	if      (base == "ltr") sp->set_base_direction(ce::block_text_request::dir_ltr);
	else if (base == "rtl") sp->set_base_direction(ce::block_text_request::dir_rtl);

	sp->set_line_spacing(static_cast<float>(number_or(o, "line_spacing", 0.0)));
	sp->set_count(static_cast<int>(number_or(o, "count", -1.0)));

	element_ptr out = sp;

	// 動的テキスト (label と同じ subscriber 規約)。
	if (!text_id.empty()) {
		std::weak_ptr<ce::block_text_box> w = sp;
		_strings->subscribe(text_id, [w](const std::string& v) {
			if (auto p = w.lock()) p->set_text(v);
		});
	} else if (!text_var.empty()) {
		std::weak_ptr<ce::block_text_box> w = sp;
		_vars->subscribe(text_var, [w](const std::string& v) {
			if (auto p = w.lock()) p->set_text(v);
		}, out);
	}

	// index_var + text_list(_id): index 変化でリストから引いて set_text
	// (指定番号表示。 text_list_id なら言語切替でも同じ index を引き直す)。
	bind_text_list(index_var, text_list, list_idx, out);

	// 文字送り: 変数 store の整数値をそのまま count に流す。
	std::string count_var = string_or(o, "count_var");
	if (!count_var.empty()) {
		if (auto* init = _vars->get(count_var))
			sp->set_count(std::atoi(init->c_str()));
		std::weak_ptr<ce::block_text_box> w = sp;
		_vars->subscribe(count_var, [w](const std::string& v) {
			if (auto p = w.lock()) p->set_count(std::atoi(v.c_str()));
		}, out);
	}

	register_id(o, out);
	return out;
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
	return apply_row_height(shared);
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
	return apply_row_height(shared);
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
	return apply_row_height(shared);
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
	return apply_row_height(shared);
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
	if (auto* v = get_field(o, "selected"); v && pj_is_num(*v)) {
		selected = static_cast<int>(pj_num(*v));
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
	return apply_row_height(shared);
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
// "index_var" (任意) で選択 index を VariableStore と連動: build 時に初期
// index を書き込み (text_list / rect_list 等の依存 widget と初期表示を揃える)、
// 選択変更のたびに set する。 変数に既に値があれば initial として採用。
//---------------------------------------------------------------------------
bool LayoutBuilder::parse_picker_options(const picojson::object& o,
                                         const char* type_name,
                                         std::vector<std::string>& opts,
                                         std::vector<std::string>& opt_ids,
                                         std::size_t& initial)
{
	if (auto* arr = get_array(o, "options")) {
		opts.reserve(arr->size());
		for (const auto& v : *arr) {
			if (v.is<std::string>()) opts.push_back(v.get<std::string>());
		}
	}
	// i18n: "options_id" — 各要素を StringStore の textID として現在言語で
	// 解決 ("options" より優先)。 言語切替は subscribe_picker_options で追従。
	if (auto* arr = get_array(o, "options_id")) {
		opt_ids.reserve(arr->size());
		for (const auto& v : *arr) {
			if (v.is<std::string>()) opt_ids.push_back(v.get<std::string>());
		}
	}
	if (!opt_ids.empty()) {
		opts.clear();
		opts.reserve(opt_ids.size());
		for (const auto& tid : opt_ids) opts.push_back(_strings->resolve(tid));
	}
	if (opts.empty()) {
		em_logf("elements_modal: %s without 'options'", type_name);
		return false;
	}

	initial = 0;
	if (auto* v = get_field(o, "initial"); v && pj_is_num(*v)) {
		auto raw = static_cast<long long>(pj_num(*v));
		if (raw < 0) raw = 0;
		if (static_cast<size_t>(raw) >= opts.size()) raw = static_cast<long long>(opts.size() - 1);
		initial = static_cast<std::size_t>(raw);
	}
	return true;
}

std::string LayoutBuilder::resolve_index_var(const picojson::object& o,
                                             std::size_t n_options,
                                             std::size_t& initial)
{
	std::string index_var = string_or(o, "index_var");
	if (!index_var.empty()) {
		if (auto* cur = _vars->get(index_var)) {
			try {
				auto v = std::stol(*cur);
				if (v >= 0 && static_cast<std::size_t>(v) < n_options)
					initial = static_cast<std::size_t>(v);
			} catch (...) { /* fallback */ }
		}
	}
	return index_var;
}

element_ptr LayoutBuilder::build_cycle_picker(const picojson::object& o, int variant)
{
	std::string id = string_or(o, "id");
	std::vector<std::string> opts;
	std::vector<std::string> opt_ids;
	std::size_t initial = 0;
	if (!parse_picker_options(o,
			variant == 0 ? "cycle_picker"
			: variant == 1 ? "framed_cycle_picker"
			: "segmented_picker",
			opts, opt_ids, initial))
		return nullptr;
	std::string index_var = resolve_index_var(o, opts.size(), initial);

	// picker は内部で `font._size * _font_size` 計算するので scale を渡す。
	float fs = resolve_font_scale(o, "font_size", "font_size_scale");

	auto cb_id = id;
	auto user_cb = _cb;
	auto vars = _vars;
	auto on_change = [cb_id, user_cb, vars, index_var](std::size_t i) {
		if (!index_var.empty()) vars->set(index_var, std::to_string(i));
		if (user_cb && !cb_id.empty()) {
			user_cb(cb_id, /*is_button_click=*/false,
			        value_t{static_cast<std::int64_t>(i)});
		}
	};

	element_ptr shared;
	std::size_t actual = initial;
	if (variant == 0) {
		auto p = std::make_shared<ce::cycle_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		note_focusable(id, p);
		subscribe_picker_options(p, opt_ids);
		subscribe_picker_index_var(p, index_var);
		wire_picker_enabled_var(p, o);   // 初期 mask で index が動く可能性あり
		actual = p->index();
		shared = p;
	} else if (variant == 1) {
		auto p = std::make_shared<ce::framed_cycle_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		note_focusable(id, p);
		subscribe_picker_options(p, opt_ids);
		subscribe_picker_index_var(p, index_var);
		shared = p;
	} else {
		auto p = std::make_shared<ce::segmented_picker>(std::move(opts), initial);
		p->on_change = std::move(on_change);
		p->font_size(fs);
		note_focusable(id, p);
		subscribe_picker_options(p, opt_ids);
		subscribe_picker_index_var(p, index_var);
		shared = p;
	}
	register_id(o, shared);
	note_initial_focus(o, shared);
	note_vars_on_focus(o, id);
	// 依存 widget (text_list / rect_list 等) の初期表示を実選択に揃える。
	if (!index_var.empty()) _vars->set(index_var, std::to_string(actual));
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
	wire_button_enabled_var(shared, o);
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
// スライダ (slider / slider_with_range / atlas_slider) の変数連動をまとめて仕込む。
//
//   widget → var : 値変化で "value_var" (生 0..1 の 10 進文字列) と
//                  "display_var" (value_display で整形した表示用文字列) を書く。
//   var → widget : "value_var" の外部変更で値を反映し (通知のみ・on_change は
//                  発火しない)、 display_var も追従させる。
//
// これで «スライダの値を数値で表示» が label の "text_var": display_var だけで
// 組める (ホストのコールバック実装が不要)。 fill 形式は gauge の描画値も更新。
// 既存の on_change (ホストへの event callback / fill ゲージ更新) は保持して
// 変数更新の後に呼ぶ。 VariableStore::set は同値なら no-op なので、
// var → widget → var のエコーは 1 往復で自然に止まる。
//---------------------------------------------------------------------------
namespace {
void wire_slider_vars(const std::shared_ptr<VariableStore>& vars,
                      const std::shared_ptr<ce::basic_slider_base>& sb,
                      const std::string& value_var,
                      const std::string& display_var,
                      const value_display& disp,
                      const std::shared_ptr<ce::atlas_progress>& gauge = {})
{
	if (!vars || !sb) return;
	if (value_var.empty() && display_var.empty()) return;

	// widget → var
	auto prev = sb->on_change;
	sb->on_change = [prev, vars, value_var, display_var, disp](double pos) {
		if (!value_var.empty()) vars->set(value_var, fmt_slider_raw(pos));
		if (!display_var.empty()) vars->set(display_var, disp.format(pos));
		if (prev) prev(pos);
	};

	// 初期値の種まき (label が最初のフレームから正しい数値を出せるように)。
	const double init = sb->value();
	if (!value_var.empty() && !vars->get(value_var))
		vars->set_initial(value_var, fmt_slider_raw(init));
	if (!display_var.empty()) vars->set(display_var, disp.format(init));

	// var → widget (+ display 追従)
	if (!value_var.empty()) {
		std::weak_ptr<ce::basic_slider_base> ws = sb;
		std::weak_ptr<ce::atlas_progress> wg = gauge;
		vars->subscribe(value_var, [ws, wg, vars, display_var, disp](const std::string& v) {
			double d = 0.0;
			try { d = std::stod(v); } catch (...) { return; }
			if (d < 0.0) d = 0.0;
			if (d > 1.0) d = 1.0;
			if (auto s = ws.lock()) s->value(d);
			if (auto g = wg.lock()) g->set_value(d);
			if (!display_var.empty()) vars->set(display_var, disp.format(d));
		});
	}
}
} // anonymous

//---------------------------------------------------------------------------
// slider — 0..1 範囲の素のスライダ
//   { "type": "slider", "id": "...", "initial": 0.5,
//     "value_var": "vol", "display_var": "vol_text",
//     "display": { "min": 0, "max": 100, "suffix": "%" } }
// 値変化で event_callback(id, false, double pos)。 value_var / display_var は
// wire_slider_vars 参照 (数値表示は label の "text_var": display_var で受ける)。
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
		wire_slider_vars(_vars, sb, string_or(o, "value_var"),
		                 string_or(o, "display_var"), parse_value_display(o));
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
		// 表示整形の既定はこの widget の min/max スケール ("display" で上書き可)。
		value_display disp = parse_value_display(o);
		if (!get_field(o, "display")) {
			disp.min = static_cast<double>(min_v);
			disp.max = static_cast<double>(max_v);
		}
		wire_slider_vars(_vars, sb, string_or(o, "value_var"),
		                 string_or(o, "display_var"), disp);
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
//
// 名前は 2 系統ある。 どちらの基準でボタンを指したいかで選ぶこと。
//   位置基準 face_south / face_east / face_west / face_north
//     … 「下のボタン」「上のボタン」。 theme が変わっても位置は不変で、
//        描かれる絵はその機種でその位置にあるボタンになる。
//   刻印基準 a / b / x / y (PS では cross / circle / square / triangle)
//     … 「A と書かれたボタン」。 任天堂系は A が右・B が下なので、
//        位置基準とは別の絵になる。
// 入力側 (bindings の "pad") も同じ 2 系統を持つので、 割り当てと表示は
// 同じ基準どうしで組にする (例: "pad":"a" の説明は name:"a"、
// "pad":"face_north" の説明は name:"face_north")。
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

		// "at_var" (座標の変数駆動): 子 ix の配置 rect を差し替える。
		// _prev_size を無効化して次 draw で relayout させる。
		void set_rect(std::size_t ix, ce::rect r)
		{
			if (ix >= _children.size()) return;
			_children[ix].first = r;
			_prev_size = {-1.0f, -1.0f};
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

//---------------------------------------------------------------------------
// choice_nav_group — "choice_nav": true の canvas グループを 1 フォーカス
// 対象にする proxy (focus_unit_element 派生 = 方向フォーカスナビは中に
// 降りず、 このグループ自体が 1 focusable になる)。
// 左右キー / パッド横軸で選択メンバー (selectable = atlas_choice /
// radio_button な子) を切替え、 クリック時と同じ semantics でイベント発火
// (新しく選ばれた側の on_click(true) のみ)。
// **端では反対側へ折り返す** ("choice_wrap": false で従来どおり素通しにできる)。
// 素通しだと「ON/OFF の OFF でさらに右」「言語の最後でさらに右」が別の行へ
// フォーカスを飛ばしてしまい、 選択操作のつもりが縦移動になる。
// ただしメンバーが 1 つしか無いグループは折り返しても意味が無いので素通しする。
// フォーカス表示 = 選択中メンバーの hilite 兼用 (focus 中だけ
// 選択メンバーを hilite フレームにする)。 マウスクリックは従来どおり子へ。
//---------------------------------------------------------------------------
namespace
{
	class choice_nav_group : public ce::focus_unit_element
	{
	public:
		choice_nav_group(element_ptr subject_, std::vector<element_ptr> members,
		                 bool wrap = true)
		 : _subject(std::move(subject_))
		 , _members(std::move(members))
		 , _wrap(wrap)
		{}

		ce::element const& subject() const override { return *_subject; }
		ce::element&       subject() override       { return *_subject; }

		void draw(ce::context const& ctx) override
		{
			sync_hilite();
			ce::proxy_base::draw(ctx);
		}

		// cursor-warp ナビの飛び先はグループ中心でなく「選択中メンバーの
		// hot point」。 グループ全体が 1 focusable (focus_unit) なので、
		// 中心だとメンバー間の空白に飛びうる。
		bool has_custom_focus_hot_point() const override { return true; }
		ce::point focus_hot_point(ce::context const& ctx) override
		{
			int sel = selected_index();
			if (sel >= 0 && sel < int(_members.size())) {
				ce::point out{};
				bool got = false;
				this->in_context_do(ctx, *_members[sel],
					[&out, &got, this, sel](ce::context const& mctx) {
						out = _members[sel]->focus_hot_point(mctx);
						got = true;
					});
				if (got) return out;
			}
			return ce::center_point(ctx.bounds);
		}

		bool key(ce::context const& ctx, ce::key_info k) override
		{
			if (!ctx.enabled)
				return false;
			if (k.action != ce::key_action::press &&
			    k.action != ce::key_action::repeat)
				return false;
			int delta = 0;
			switch (k.key) {
			case ce::key_code::left:  delta = -1; break;
			case ce::key_code::right: delta = +1; break;
			default: return false;
			}
			if (step_selection(delta)) {
				ctx.view.refresh(ctx);
				return true;
			}
			return false;   // 端 → view の focus nav へ素通し
		}

		bool pad_axis(ce::context const& ctx, ce::pad_axis_info info) override
		{
			// picker 系と同じエッジ検出 (engage 0.55 / release 0.20 のヒステリシス)。
			// 時刻ベースの quiet 窓は poll 周期依存で、render cache により poll が
			// 止まると押しっぱなしを再押下と誤認して二重ステップになるため廃止
			// (view が release 時に v=0 を一度配送してくる前提)。
			if (!ctx.enabled)
				return false;
			if (info.axis != ce::pad_axis::dpad_x &&
			    info.axis != ce::pad_axis::left_x &&
			    info.axis != ce::pad_axis::right_x)
				return false;
			float mag = std::abs(info.value);
			if (mag < 0.20f) {
				_pad_engaged = false;
				return false;
			}
			if (mag > 0.55f && !_pad_engaged) {
				_pad_engaged = true;
				if (step_selection(info.value < 0.0f ? -1 : +1)) {
					ctx.view.refresh(ctx);
					return true;
				}
				return false;   // 端 → view の focus nav へ素通し
			}
			return true;
		}

	private:
		int selected_index() const
		{
			for (int i = 0; i < int(_members.size()); ++i) {
				if (auto* s = ce::find_element<ce::selectable*>(_members[i].get()))
					if (s->is_selected()) return i;
			}
			return -1;
		}

		bool step_selection(int delta)
		{
			if (_members.empty())
				return false;
			const int n = int(_members.size());
			int cur = selected_index();
			int next = (cur < 0) ? (delta > 0 ? 0 : n - 1) : cur + delta;
			if (_wrap && n > 1) {
				// 端は反対の端へ折り返す。 メンバーが 1 つしか無いときは
				// 折り返しても自分自身なので、 素通しさせて view の focus nav
				// に任せる (下の next == cur で false になる)。
				if (next < 0)  next = n - 1;
				if (next >= n) next = 0;
			}
			if (next < 0 || next >= n || next == cur)
				return false;
			if (cur >= 0)
				if (auto* s = ce::find_element<ce::selectable*>(_members[cur].get()))
					s->select(false);
			if (auto* s = ce::find_element<ce::selectable*>(_members[next].get()))
				s->select(true);
			if (auto* b = ce::find_element<ce::basic_button*>(_members[next].get()))
				if (b->on_click) b->on_click(true);
			return true;
		}

		void sync_hilite()
		{
			// focus 中だけ選択メンバーの hilite を管理する。 非 focus 時は
			// 手を出さない (マウス hover の hilite を殺さないため)。
			int want = focused() ? selected_index() : -1;
			if (want == _hilited)
				return;
			if (_hilited >= 0 && _hilited < int(_members.size()))
				if (auto* b = ce::find_element<ce::basic_button*>(_members[_hilited].get()))
					b->hilite(false);
			if (want >= 0)
				if (auto* b = ce::find_element<ce::basic_button*>(_members[want].get()))
					b->hilite(true);
			_hilited = want;
		}

		element_ptr _subject;
		std::vector<element_ptr> _members;
		int _hilited = -1;
		bool _pad_engaged = false;   // pad-axis ヒステリシス状態
		bool _wrap = true;           // 端で反対側へ折り返すか
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

	// "choice_nav": true — この canvas の selectable な直接子 (atlas_choice /
	// radio_button) をまとめて 1 フォーカス対象の左右トグルグループにする。
	bool choice_nav = truthy_field(get_field(o, "choice_nav"));
	// 左右ナビの並びは **配置座標 (画面上の左→右) 順**で決める。 children の
	// 記載順は PSD のレイヤ順に由来していて視覚順と一致するとは限らず、 実際に
	// 子の順序が右→左になっていて左右操作が逆になる、 という実例があった。
	// (x, y) を持って回して最後に並べ替える。
	std::vector<std::pair<ce::point, element_ptr>> nav_placed;
	std::vector<element_ptr> nav_members;

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

		if (choice_nav && ce::find_element<ce::selectable*>(widget.get()))
			nav_placed.push_back({ce::point{x, y}, widget});

		// canvas_layer_element に直接 (rect, widget) で追加。 floating ラップ
		// は使わない (相対座標を canvas_layer_element の bounds_of で計算する)。
		layer->add(ce::rect{x, y, x + w, y + h}, std::move(widget));

		// "at_var": 配置 rect を変数で駆動 (map 画面のキャラ位置等)。 変数値は
		// "x,y" (サイズは "at" のまま) または "x,y,w,h" の 10 進 px 文字列。
		// パース不能 / 要素不足の値は無視 (現状維持)。 初期値は "at"。
		if (std::string at_var = string_or(co, "at_var"); !at_var.empty()) {
			std::size_t ix = layer->size() - 1;
			std::weak_ptr<canvas_layer_element> wl = layer;
			float iw = w, ih = h;
			auto apply = [wl, ix, iw, ih](const std::string& v) {
				float nx = 0, ny = 0, nw = 0, nh = 0;
				int n = std::sscanf(v.c_str(), " %f , %f , %f , %f",
				                    &nx, &ny, &nw, &nh);
				if (n < 2) return;
				ce::rect r = (n >= 4)
					? ce::rect{nx, ny, nx + nw, ny + nh}
					: ce::rect{nx, ny, nx + iw, ny + ih};
				if (auto l = wl.lock()) l->set_rect(ix, r);
			};
			if (auto* cur = _vars->get(at_var)) apply(*cur);
			_vars->subscribe(at_var, apply);
		}
	}

	if (choice_nav) {
		// 画面上の左→右 (同じ x なら上→下) に並べ替えてからナビ対象にする。
		std::stable_sort(nav_placed.begin(), nav_placed.end(),
			[](auto const& a, auto const& b) {
				if (a.first.x != b.first.x) return a.first.x < b.first.x;
				return a.first.y < b.first.y;
			});
		nav_members.reserve(nav_placed.size());
		for (auto& np : nav_placed) nav_members.push_back(std::move(np.second));
	}

	element_ptr root = layer;
	if (choice_nav) {
		if (nav_members.empty()) {
			em_logf("elements_modal: canvas choice_nav: no selectable children");
		} else {
			// "choice_wrap": 端で反対側へ折り返すか (既定 true)。
			// false にすると従来どおり端で素通しし、 view の focus nav に渡る。
			bool cw = true;
			bool_field(get_field(o, "choice_wrap"), cw);
			auto grp = std::make_shared<choice_nav_group>(
				root, std::move(nav_members), cw);
			std::string id = string_or(o, "id");
			note_focusable(id, grp);
			register_id(o, grp);
			note_initial_focus(o, grp);
			note_vars_on_focus(o, id);
			root = grp;
		}
	}
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

	bool stretch_h = false, stretch_v = false;
	bool_field(get_field(o, "stretch_h"), stretch_h);
	bool_field(get_field(o, "stretch_v"), stretch_v);

	// "frames" + "focus_link": 別ウィジェットのフォーカスに追従して絵を変える飾り。
	//   { "type": "atlas_image", "atlas": "ui",
	//     "frames": { "normal": [x,y,w,h], "hilite": [x,y,w,h] },
	//     "focus_link": "PICKER_機種" }
	// 行の下地のように「自分はフォーカスを取らないが、 行内のコントロールが
	// フォーカスされていることを示したい」飾りのためのもの。 focus_link に
	// 指定した id がフォーカスを持っている間だけ hilite を描く。
	if (auto* fv = get_field(o, "frames");
	    fv && fv->is<picojson::object>() && get_field(o, "focus_link")) {
		const auto& fr = fv->get<picojson::object>();
		auto* n_arr = get_array(fr, "normal");
		auto* h_arr = get_array(fr, "hilite");
		if (!n_arr || n_arr->size() < 4 || !h_arr || h_arr->size() < 4) {
			em_logf("elements_modal: atlas_image \"%s\" focus_link needs "
			        "frames.normal / frames.hilite as [x,y,w,h]",
			        atlas_name.c_str());
		} else {
			const ce::rect r_normal = parse_xywh(*n_arr);
			const ce::rect r_hilite = parse_xywh(*h_arr);
			// "focus_link" は id 文字列、 または id の配列 (1 行に複数の
			// コントロールがある場合は、 そのどれかが focus されていれば hilite)。
			std::vector<std::string> targets;
			if (auto* lv = get_field(o, "focus_link")) {
				if (lv->is<std::string>()) {
					targets.push_back(lv->get<std::string>());
				} else if (lv->is<picojson::array>()) {
					for (auto& e : lv->get<picojson::array>())
						if (e.is<std::string>()) targets.push_back(e.get<std::string>());
				}
			}
			// normal / hilite は寸法が違うことがある (PSD の 通常 と オーバー は
			// 同心だが オーバー の方が一回り大きい)。 atlas_sprite の native
			// モードは各 frame を実寸のまま bounds 中央へ描くので、 どちらの
			// frame も潰れずに済む (box は generate.py 側でユニオンにしてある)。
			auto img = std::make_shared<ce::atlas_sprite>(
				pm, std::vector<ce::rect>{r_normal, r_hilite}, /*native*/ true);
			std::weak_ptr<ce::atlas_sprite> w = img;
			_focus_links.push_back(
				[w, targets](const std::string& focused) {
					if (auto p = w.lock()) {
						const bool on = !focused.empty()
							&& std::find(targets.begin(), targets.end(), focused)
							   != targets.end();
						p->index(on ? 1 : 0);
					}
				});
			_focus_link_elements.push_back(img);
			register_id(o, img);
			// 行領域のどこへマウスオーバーしてもリンク先コントロールへ
			// フォーカスが移るように、 ホバートリガのプロキシで包んで返す
			// (SGOCT 系フィードバック: クリック可能領域の上だけだと分かりづらい)
			auto wire = std::make_shared<hover_link_wire>();
			_hover_link_wires.push_back({wire, targets});
			return ce::share(hover_focus_link_element(
				ce::hold_any(element_ptr(img)), std::move(wire)));
		}
	}

	// "rect_list" + "index_var": ソース矩形のリストを変数 index で切替える
	// (picker の index_var と連動する機種別スクショ等)。 rect より優先。
	//   { "type": "atlas_image", "atlas": "ui",
	//     "rect_list": [[x,y,w,h], ...], "index_var": "machine" }
	// index が範囲外の値は無視 (現状維持)。 全 rect 同寸法を推奨 (limits は
	// 現 rect 基準なので、 canvas floating の "at" 固定配置で使うこと)。
	if (auto* rl = get_array(o, "rect_list"); rl && !rl->empty()) {
		std::vector<ce::rect> rects;
		for (auto& el : *rl) {
			if (el.is<picojson::array>())
				rects.push_back(parse_xywh(el.get<picojson::array>()));
		}
		if (rects.empty()) {
			em_logf("elements_modal: atlas_image \"%s\" rect_list has no "
			        "[x,y,w,h] entries", atlas_name.c_str());
			return nullptr;
		}
		std::string index_var = string_or(o, "index_var");
		std::size_t idx = 0;
		if (!index_var.empty()) {
			if (auto* cur = _vars->get(index_var)) {
				try {
					auto v = std::stol(*cur);
					if (v >= 0 && static_cast<std::size_t>(v) < rects.size())
						idx = static_cast<std::size_t>(v);
				} catch (...) { /* fallback */ }
			}
		}
		auto img = std::make_shared<ce::atlas_image>(
			pm, rects[idx], stretch_h, stretch_v);
		if (!index_var.empty()) {
			std::weak_ptr<ce::atlas_image> w = img;
			_vars->subscribe(index_var,
				[w, rects = std::move(rects)](const std::string& v) {
					std::size_t i = 0;
					try {
						auto n = std::stol(v);
						if (n < 0 || static_cast<std::size_t>(n) >= rects.size())
							return;
						i = static_cast<std::size_t>(n);
					} catch (...) { return; }
					if (auto p = w.lock()) p->sub_rect(rects[i]);
				}, img);
		}
		register_id(o, img);
		return img;
	}

	auto* arr = get_array(o, "rect");
	if (!arr || arr->size() < 4) {
		em_logf("elements_modal: atlas_image \"%s\" missing 'rect': [x,y,w,h]",
		        atlas_name.c_str());
		return nullptr;
	}
	ce::rect src = parse_xywh(*arr);

	return ce::share(ce::atlas_image(pm, src, stretch_h, stretch_v));
}

//---------------------------------------------------------------------------
// image — 単一の画像ファイルをパス指定で読み込み、 与えられた bounds に
// アスペクト比維持で fit 描画する飾り要素 (atlas 非依存)。
//   { "type": "image", "image": "resources/logo.png", "at": [x,y,w,h] }
// "image" のパスは resource_loader (Storages VFS) で解決 → stb / ThorVG で
// mem:// image widget レジストリ — registerImage でバイトが差し替わった際に、
// 構築済みの image を set_image で再デコードして即時反映するための対応表。
//   build_image が mem:// image を登録 → ホストが registerImage(key,..) 後に
//   refresh_mem_image(key) を呼ぶ → 該当 widget を再ロード (画面再構築不要)。
//---------------------------------------------------------------------------
namespace {

struct MemImageEntry {
	std::weak_ptr<ce::image> widget;
	cycfi::fs::path          src;    // 再デコード元 ("mem://key")
	float                    scale;  // fit は 1.0f (set_image は _fit を変えない)
};

std::mutex& mem_image_mutex() { static std::mutex m; return m; }
std::map<std::string, std::vector<MemImageEntry>>& mem_image_map()
{
	static std::map<std::string, std::vector<MemImageEntry>> m;
	return m;
}

// "mem:" + 任意個スラッシュ を除いた残りを key に。 非 mem:// は false。
// (fs::path 正規化で "mem://"→"mem:/" 等になっても拾えるよう、 ホスト側
//  ParseMemName と同じくスラッシュ数に依存しない)
bool parse_mem_key(const std::string& name, std::string& out)
{
	constexpr char scheme[] = "mem:";
	constexpr std::size_t slen = 4;
	if (name.size() <= slen || name.compare(0, slen, scheme) != 0) return false;
	std::size_t i = slen;
	while (i < name.size() && (name[i] == '/' || name[i] == '\\')) ++i;
	if (i >= name.size()) return false;
	out = name.substr(i);
	return true;
}

void mem_image_registry_add(const std::string& key,
                            const std::shared_ptr<ce::image>& img,
                            const cycfi::fs::path& src, float scale)
{
	std::lock_guard<std::mutex> lk(mem_image_mutex());
	mem_image_map()[key].push_back(MemImageEntry{ img, src, scale });
}

} // namespace

//---------------------------------------------------------------------------
// デコード。 "mem://<name>" を渡すとホスト注入画像ストア (TJS
// ElementsDialog.registerImage で登録) から読む。 セーブサムネイル等の
// 実行時画像に使う。 読込に失敗 (未登録 / ファイル無し) した場合は空要素を
// 返す (レイアウトは維持、 何も描かない)。 pixmap は build 時に一度読むが、
// mem:// は登録し直して refresh_mem_image() を呼べばその場で差し替わる。
//   "scale": float (任意) — 指定時は fit でなく native×scale 固定サイズ。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_image(const picojson::object& o)
{
	auto path = string_or(o, "image");
	if (path.empty()) {
		em_logf("elements_modal: image without 'image' path");
		return ce::share(ce::element{});
	}
	try {
		// "mem://" はホスト注入ストアのキーなので resource_base を前置しない。
		// それ以外は通常のリソースパス解決 (resource_base 起点)。
		cycfi::fs::path fp = (path.rfind("mem://", 0) == 0)
			? cycfi::fs::path(path)
			: resolve_resource(path);
		double scale = number_or(o, "scale", 0.0);
		std::shared_ptr<ce::image> img;
		if (scale > 0.0) {
			img = ce::share(ce::image(fp, static_cast<float>(scale)));
		} else {
			// 既定: 与えられた bounds にアスペクト維持で収める
			img = ce::share(ce::image(fp, ce::image::fit));
		}
		register_id(o, img);
		// "mem://" は実行時に registerImage でバイトが差し替わりうる。 差し替え
		// 時に refresh_mem_image() で set_image できるよう widget を登録しておく
		// (セーブ直後のサムネイル即時反映)。
		if (std::string key; parse_mem_key(path, key)) {
			float reload_scale = (scale > 0.0) ? static_cast<float>(scale) : 1.0f;
			mem_image_registry_add(key, img, fp, reload_scale);
		}
		return img;
	} catch (...) {
		// pixmap 読込失敗 (未登録 mem:// / ファイル無し / デコード失敗)。
		// 空スロット等では想定内なので空要素で継続。
		return ce::share(ce::element{});
	}
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

	// "visible_var" で出したり消したりするアニメは、 出るたびに頭から再生する。
	// 経過時間は隠れている間も進んでいるので、 これが無いと loop=false の
	// アニメが 2 回目以降ずっと最終フレームのままになる。
	// (表示制御そのものは apply_visible が最外周で行う)
	if (std::string vis_var = string_or(o, "visible_var"); !vis_var.empty()) {
		auto shown = std::make_shared<bool>(true);
		auto parse = [](const std::string& v) {
			return !(v == "0" || v == "false" || v.empty());
		};
		if (auto* cur = _vars->get(vis_var)) *shown = parse(*cur);
		std::weak_ptr<ce::animated_sprite> weak = sprite;
		_vars->subscribe(vis_var, [weak, shown, parse](const std::string& v) {
			bool const now = parse(v);
			if (now && !*shown) {
				if (auto s = weak.lock()) s->restart();
			}
			*shown = now;
		}, sprite);
	}
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
			float ax = static_cast<float>(pj_num(anc->at(0)));
			float ay = static_cast<float>(pj_num(anc->at(1)));
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
	wire_button_enabled_var(shared, o);
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
// "value_var" (任意) で VariableStore と連動: 変数に既存値があれば初期状態を
// 上書き ("0" / "false" / 空 = off)、 クリックで "0"/"1" を書き戻し、 変数の
// 変更は状態へ反映のみ (on_click は発火しない)。 atlas_slider と同じ規約。
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
	std::string value_var = string_or(o, "value_var");
	auto var_truthy = [](const std::string& v) {
		return !(v.empty() || v == "0" || v == "false");
	};
	if (!value_var.empty()) {
		if (auto* cur = _vars->get(value_var)) init = var_truthy(*cur);
	}

	auto sprite = ce::atlas_sprite(pm, std::move(frames),
	                               truthy_field(get_field(o, "native_frames")));
	auto tb = ce::toggle_button(std::move(sprite));
	tb.value(init);
	if (!id.empty() || !value_var.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		auto vars = _vars;
		tb.on_click = [cb_id, user_cb, vars, value_var](bool state) {
			if (!value_var.empty()) vars->set(value_var, state ? "1" : "0");
			if (user_cb && !cb_id.empty())
				user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(tb));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
		if (!value_var.empty()) {
			// 変数 → トグル状態の一方向同期 (on_click は発火しない)。
			std::weak_ptr<ce::basic_button> w = bp;
			_vars->subscribe(value_var, [w, var_truthy](const std::string& v) {
				if (auto sp = w.lock()) sp->value(var_truthy(v));
			}, bp);
		}
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
	std::string sel_var, sel_val;
	read_choice_selected_var(o, sel_var, sel_val, init);

	auto sprite = ce::atlas_sprite(pm, std::move(frames),
	                               truthy_field(get_field(o, "native_frames")));
	// latching_button<basic_choice>(sprite) → proxy<sprite, sprite_button_styler<basic_choice>>
	// 排他は basic_choice::activate/click の find_composite + 兄弟スキャン
	// による (atlas_choice 群を同じ composite=canvas layer に並べる前提)。
	// text overlay は label_decoration (非 composite proxy_base 派生) で
	// 包むので、 find_composite はそれを素通りして canvas layer まで届く。
	auto ch = ce::latching_button<ce::basic_choice>(std::move(sprite));
	ch.value(init);
	if (!id.empty() || !sel_var.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		auto vars = _vars;
		ch.on_click = [cb_id, user_cb, vars, sel_var, sel_val](bool state) {
			if (state && !sel_var.empty()) vars->set(sel_var, sel_val);
			if (user_cb && !cb_id.empty())
				user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(ch));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	subscribe_choice_selected_var(shared, sel_var, sel_val);
	note_vars_on_focus(o, id);
	return maybe_wrap_text_overlay(o, shared, _default_locale, _strings.get());
}

//---------------------------------------------------------------------------
// atlas_slider — track + thumb (または fill) をアトラスの sub-rect で構築する
// 0..1 スライダ。
//   { "type": "atlas_slider", "atlas": "ui", "id": "vol",
//     "track": [x, y, w, h], "thumb": [x, y, w, h],
//     "initial": 0.5, "vertical": false }
// track はスライダ軸方向に stretchable (親 floating の bounds に合わせる)。
// thumb は固定サイズ。 値変化で value_t{double pos} を発火。
//
// fill 形式 (ゲージ型スライダ): "thumb" の代わりに "fill" を指定すると、
// atlas_progress と同じ track+fill 描画のまま**操作可能**なスライダになる
// (クリック/ドラッグ/矢印キー/パッドで値変更)。 "fill_at" は fill の配置先を
// track ソース矩形の左上原点 px で指定 (インセットされたバー素材向け)。
//   { "type": "atlas_slider", "atlas": "ui", "id": "vol",
//     "track": [x, y, w, h], "fill": [x, y, w, h], "fill_at": [dx, dy, w, h],
//     "initial": 0.5 }
//
// どちらの形式も "value_var" (任意) で VariableStore と **双方向**連動: 変数変更で
// 値が追従し (通知のみ、 イベントは発火しない)、 ユーザ操作では on_change が発火して
// 変数側も更新される。 値は "0.75" 形式の 10 進文字列 (常に 0..1)。
//
// 数値表示: "display_var" を指定すると、 値を "display" の指定で整形した文字列を
// その変数へ書く。 label 側に "text_var": <display_var> を置けば «スライダに連動した
// 数値表示» になる (ホスト実装不要)。
//   { "type": "atlas_slider", "atlas": "ui", "id": "vol",
//     "track": [...], "thumb": [...], "value_var": "vol",
//     "display_var": "vol_text",
//     "display": { "min": 0, "max": 100, "step": 1, "digits": 0, "suffix": "%" } }
//---------------------------------------------------------------------------
namespace {
// fill 形式スライダの見えない thumb (0x0)。 slider_base は thumb の大きさを
// 差し引いて可動域を計算するので、 0 サイズなら track 全域が可動域になる。
struct em_null_thumb : cycfi::elements::element
{
	cycfi::elements::view_limits
	limits(cycfi::elements::basic_context const&) const override
	{
		return {{0, 0}, {0, 0}};
	}
};

// fill 形式スライダの base。 thumb (= フォーカスカーソル) の可動域と
// クリック/ドラッグの値マッピングを、 widget 全幅ではなくゲージ部分
// (fill_at の範囲) に合わせる。 トラック画像の両端にラベル (0 / 100 等) が
// 焼き込まれている素材では、 全幅基準だとカーソルが目盛からずれて見える
// (値 0 でラベル 0 の左、 100 でラベル 100 の上に出る) ため (SGOCT-147)。
// fx/fw 既定 (0, 1) = 従来どおり全幅。 縦スライダは従来動作のまま。
struct em_fill_slider_base : cycfi::elements::basic_slider_base
{
	using cycfi::elements::basic_slider_base::basic_slider_base;
	float fx = 0.0f;   // ゲージ左端 (トラック幅比)
	float fw = 1.0f;   // ゲージ幅 (トラック幅比)
	bool  fill_vertical = false;

	cycfi::elements::rect
	thumb_bounds(cycfi::elements::context const& ctx) const override
	{
		auto r = cycfi::elements::slider_base::thumb_bounds(ctx);
		if (fill_vertical || (fx == 0.0f && fw == 1.0f)) return r;
		auto b = ctx.bounds;
		float w = r.width();
		float x = b.left + b.width() * fx
		        + (b.width() * fw - w) * (float)value();
		r.left = x;
		r.right = x + w;
		return r;
	}
	double
	value_from_point(cycfi::elements::context const& ctx,
	                 cycfi::elements::point p) override
	{
		if (fill_vertical || (fx == 0.0f && fw == 1.0f))
			return cycfi::elements::slider_base::value_from_point(ctx, p);
		auto b = ctx.bounds;
		float x0 = b.left + b.width() * fx;
		float w  = b.width() * fw;
		if (w <= 0.0f) return value();
		double v = (p.x - x0) / w;
		return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
	}
};
} // anonymous

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
	auto* fl = get_array(o, "fill");
	bool fill_mode = (fl && fl->size() >= 4);
	bool has_track = (tr && tr->size() >= 4);
	// track は thumb 形式では省略可 (溝が背景画像側に描いてある素材)。
	// fill 形式はゲージ描画の基準に track が必須。
	if ((fill_mode && !has_track) || (!fill_mode && (!th || th->size() < 4))) {
		em_logf("elements_modal: atlas_slider \"%s\" needs 'thumb' "
		        "(track optional) or 'track'+'fill' as [x, y, w, h]",
		        atlas_name.c_str());
		return nullptr;
	}
	ce::rect track_src = has_track ? parse_xywh(*tr) : ce::rect{};

	bool vertical = false;
	bool_field(get_field(o, "vertical"), vertical);

	double initial = number_or(o, "initial", 0.5);
	if (initial < 0.0) initial = 0.0;
	if (initial > 1.0) initial = 1.0;

	std::string id = string_or(o, "id");
	std::string value_var = string_or(o, "value_var");
	if (!value_var.empty()) {
		if (auto* cur = _vars->get(value_var)) {
			try {
				initial = std::stod(*cur);
				if (initial < 0.0) initial = 0.0;
				if (initial > 1.0) initial = 1.0;
			} catch (...) { /* fallback */ }
		}
	}

	element_ptr shared;
	std::shared_ptr<ce::atlas_progress> gauge;   // fill 形式のみ
	if (fill_mode) {
		ce::rect fill_src = parse_xywh(*fl);
		ce::rect fill_at{};
		if (auto* fa = get_array(o, "fill_at"); fa && fa->size() >= 4)
			fill_at = parse_xywh(*fa);
		gauge = std::make_shared<ce::atlas_progress>(
			pm, track_src, fill_src, initial, vertical, fill_at);
		auto sl = ce::basic_slider<em_null_thumb, decltype(ce::hold(gauge)),
		                           em_fill_slider_base>(
			em_null_thumb{}, ce::hold(gauge), initial);
		// thumb 可動域をゲージ部分 (fill_at) に合わせる (SGOCT-147)
		if (track_src.width() > 0 && fill_at.width() > 0) {
			sl.fx = fill_at.left / track_src.width();
			sl.fw = fill_at.width() / track_src.width();
		}
		sl.fill_vertical = vertical;
		auto cb_id = id;
		auto user_cb = _cb;
		std::weak_ptr<ce::atlas_progress> wg = gauge;
		sl.on_change = [cb_id, user_cb, wg](double pos) {
			if (auto g = wg.lock()) g->set_value(pos);
			if (user_cb && !cb_id.empty())
				user_cb(cb_id, /*is_button_click=*/false, value_t{pos});
		};
		shared = ce::share(std::move(sl));
	} else {
		ce::rect thumb_src = parse_xywh(*th);
		// track はスライダ軸方向に stretchable、 直交軸は固定。 thumb は完全固定。
		// track 省略時 (溝が背景側に描いてある素材) は見えない stretchable 要素を
		// 敷く (widget の box いっぱいが可動域になる)。
		element_ptr track_img = has_track
			? ce::share(ce::atlas_image(pm, track_src,
			                            /*stretch_h=*/!vertical,
			                            /*stretch_v=*/ vertical))
			: ce::share(cycfi::elements::element{});
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
		shared = ce::share(std::move(sl));
	}
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto sb = std::dynamic_pointer_cast<ce::basic_slider_base>(shared)) {
		note_focusable(id, sb);
		// 変数連動 (双方向 + 表示用の整形変数)。 変数 → スライダ値 (+fill 描画) は
		// 通知のみで on_change を発火せず、 ユーザ操作側は value_var/display_var を
		// 書く。 数値表示は label の "text_var": display_var で受ける。
		wire_slider_vars(_vars, sb, value_var, string_or(o, "display_var"),
		                 parse_value_display(o), gauge);
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
	ce::rect fill_at{};
	if (auto* fa = get_array(o, "fill_at"); fa && fa->size() >= 4)
		fill_at = parse_xywh(*fa);

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

	auto pg = ce::atlas_progress(pm, track_src, fill_src, init, vertical, fill_at);
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
			}, sp);
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
// atlas_number — 数字素材 (0-9 の sub-rect) で数値を描く表示専用パーツ。
// フォントではなく «絵の数字» で出したいスコア / 残数 / 音量表示用。
//
//   { "type": "atlas_number", "atlas": "ui",
//     "digits": [[x,y,w,h] × 10],        // 0,1,...,9 の順
//     // 等間隔に並んだ 1 枚素材なら分割指定でもよい:
//     "digits_rect": [x, y, w, h], "count": 10, "digits_vertical": false,
//     "glyphs": { "-": [x,y,w,h], "%": [x,y,w,h] },   // 数字以外 (任意)
//     "text": "50",                      // 静的初期値
//     "text_var": "vol_text",            // 文字列変数を購読 (スライダの
//                                        // display_var をそのまま指定できる)
//     "value_var": "vol",                // 生値 (0..1) を購読して
//     "display": { "min": 0, "max": 100, "suffix": "%" },   // 自前で整形
//     "align": "left" | "center" | "right",
//     "spacing": 0, "scale": 1.0, "space_width": 0 }
//
// text_var と value_var は併用可 (先に text_var が来た値を優先して上書き)。
// スライダ側で display_var を出しているなら text_var 指定が一番簡単。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_number(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_number without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	// 数字 10 個の矩形: 明示リスト or 等分割指定。
	std::vector<ce::rect> digits;
	if (auto* ds = get_array(o, "digits")) {
		for (const auto& d : *ds) {
			if (d.is<picojson::array>()) {
				const auto& a = d.get<picojson::array>();
				if (a.size() >= 4) digits.push_back(parse_xywh(a));
			}
		}
	} else if (auto* dr = get_array(o, "digits_rect"); dr && dr->size() >= 4) {
		ce::rect r = parse_xywh(*dr);
		int count = static_cast<int>(number_or(o, "count", 10));
		bool vert = false;
		bool_field(get_field(o, "digits_vertical"), vert);
		if (count > 0) {
			float w = vert ? r.width() : r.width() / count;
			float h = vert ? r.height() / count : r.height();
			for (int i = 0; i < count; ++i) {
				float x = vert ? r.left : r.left + w * i;
				float y = vert ? r.top + h * i : r.top;
				digits.push_back(ce::rect{x, y, x + w, y + h});
			}
		}
	}
	if (digits.empty()) {
		em_logf("elements_modal: atlas_number \"%s\" needs 'digits' "
		        "([[x,y,w,h] × 10]) or 'digits_rect' + 'count'",
		        atlas_name.c_str());
		return nullptr;
	}

	std::map<std::string, ce::rect> glyphs;
	if (auto* g = get_field(o, "glyphs"); g && g->is<picojson::object>()) {
		for (const auto& [k, v] : g->get<picojson::object>()) {
			if (v.is<picojson::array>()) {
				const auto& a = v.get<picojson::array>();
				if (a.size() >= 4) glyphs.emplace(k, parse_xywh(a));
			}
		}
	}

	auto align = ce::atlas_number::align_x::left;
	{
		std::string a = string_or(o, "align");
		if (a == "center") align = ce::atlas_number::align_x::center;
		else if (a == "right") align = ce::atlas_number::align_x::right;
	}
	auto num = std::make_shared<ce::atlas_number>(
		pm, std::move(digits), std::move(glyphs),
		static_cast<float>(number_or(o, "spacing", 0.0)),
		static_cast<float>(number_or(o, "scale", 1.0)), align);
	num->space_width(static_cast<float>(number_or(o, "space_width", 0.0)));

	// 初期テキスト: text > (value_var の現在値を display 整形) > 空。
	value_display disp = parse_value_display(o);
	std::string value_var = string_or(o, "value_var");
	std::string text_var  = string_or(o, "text_var");
	std::string init = string_or(o, "text");
	if (init.empty() && !text_var.empty()) {
		if (auto* cur = _vars->get(text_var)) init = *cur;
	}
	if (init.empty() && !value_var.empty()) {
		if (auto* cur = _vars->get(value_var)) {
			try { init = disp.format(std::stod(*cur)); } catch (...) { /* 無視 */ }
		}
	}
	if (!init.empty()) num->set_text(init);

	element_ptr shared = num;
	std::weak_ptr<ce::atlas_number> w = num;
	if (!text_var.empty()) {
		// 文字列変数をそのまま表示 (スライダの display_var / ホストの set_var)。
		_vars->subscribe(text_var, [w](const std::string& v) {
			if (auto p = w.lock()) p->set_text(v);
		}, shared);
	}
	if (!value_var.empty()) {
		// 生値 (0..1) を自前で整形して表示。
		_vars->subscribe(value_var, [w, disp](const std::string& v) {
			double d = 0.0;
			try { d = std::stod(v); } catch (...) { return; }
			if (auto p = w.lock()) p->set_text(disp.format(d));
		}, shared);
	}
	register_id(o, shared);
	return shared;
}

//---------------------------------------------------------------------------
// atlas_cycle_picker — 左右矢印ボタン絵 + 選択テキスト表示領域の画像ピッカー。
// 選択モデル (step / wrap / key / pad) は cycle_picker と同一。 フォーカス中は
// 両矢印が hilite フレームになる (= フォーカス表示)。
//   { "type": "atlas_cycle_picker", "atlas": "ui", "id": "machine",
//     "left":  { "normal": [x,y,w,h], "hilite": [x,y,w,h] },
//     "right": { "normal": [x,y,w,h], "hilite": [x,y,w,h] },
//     "left_at": [dx,dy,w,h], "right_at": [dx,dy,w,h], "text_at": [dx,dy,w,h],
//     "options": [..] / "options_id": [..], "initial": 0,
//     "font_size": px, "color": [r,g,b,a], "index_var": "machine" }
// *_at は widget bounds 左上原点の相対 px (canvas floating "at" と併用)。
// 選択変更時に event_callback(id, false, int64_t index)。 options_id /
// index_var の意味は cycle_picker と同じ。
//---------------------------------------------------------------------------
element_ptr LayoutBuilder::build_atlas_cycle_picker(const picojson::object& o)
{
	auto atlas_name = string_or(o, "atlas");
	if (atlas_name.empty()) {
		em_logf("elements_modal: atlas_cycle_picker without 'atlas'");
		return nullptr;
	}
	auto pm = lookup_atlas(atlas_name);
	if (!pm) return nullptr;

	std::string id = string_or(o, "id");
	std::vector<std::string> opts;
	std::vector<std::string> opt_ids;
	std::size_t initial = 0;
	if (!parse_picker_options(o, "atlas_cycle_picker", opts, opt_ids, initial))
		return nullptr;
	std::string index_var = resolve_index_var(o, opts.size(), initial);

	// "left"/"right": { "normal": [..], "hilite": [..] }。 hilite 省略時は
	// normal と同じ (= フォーカス表示なし)。
	auto parse_arrow = [&o](const char* key,
	                        ce::atlas_cycle_picker::arrow_frames& out) -> bool {
		auto* v = get_field(o, key);
		if (!v || !v->is<picojson::object>()) return false;
		const auto& ao = v->get<picojson::object>();
		auto* n = get_array(ao, "normal");
		if (!n || n->size() < 4) return false;
		out.normal = parse_xywh(*n);
		auto* h = get_array(ao, "hilite");
		out.hilite = (h && h->size() >= 4) ? parse_xywh(*h) : out.normal;
		return true;
	};
	ce::atlas_cycle_picker::arrow_frames left{}, right{};
	if (!parse_arrow("left", left) || !parse_arrow("right", right)) {
		em_logf("elements_modal: atlas_cycle_picker \"%s\" needs 'left'/'right' "
		        "{normal:[x,y,w,h], hilite:[..]}", atlas_name.c_str());
		return nullptr;
	}

	auto rect_or_empty = [&o](const char* key) -> ce::rect {
		if (auto* arr = get_array(o, key); arr && arr->size() >= 4)
			return parse_xywh(*arr);
		return ce::rect{};
	};
	ce::rect left_at  = rect_or_empty("left_at");
	ce::rect right_at = rect_or_empty("right_at");
	ce::rect text_at  = rect_or_empty("text_at");
	if (left_at.width() <= 0 || right_at.width() <= 0 || text_at.width() <= 0) {
		em_logf("elements_modal: atlas_cycle_picker \"%s\" needs 'left_at'/"
		        "'right_at'/'text_at' as [dx,dy,w,h]", atlas_name.c_str());
		return nullptr;
	}

	float fs = resolve_font_scale(o, "font_size", "font_size_scale");

	auto cb_id = id;
	auto user_cb = _cb;
	auto vars = _vars;
	auto p = std::make_shared<ce::atlas_cycle_picker>(
		pm, std::move(opts), initial, left, right, left_at, right_at, text_at);
	p->on_change = [cb_id, user_cb, vars, index_var](std::size_t i) {
		if (!index_var.empty()) vars->set(index_var, std::to_string(i));
		if (user_cb && !cb_id.empty()) {
			user_cb(cb_id, /*is_button_click=*/false,
			        value_t{static_cast<std::int64_t>(i)});
		}
	};
	p->font_size(fs);
	if (auto* arr = get_array(o, "color")) p->text_color(parse_color(*arr));
	note_focusable(id, p);
	subscribe_picker_options(p, opt_ids);
	subscribe_picker_index_var(p, index_var);
	wire_picker_enabled_var(p, o);   // 初期 mask で index が動く可能性あり
	register_id(o, p);
	note_initial_focus(o, p);
	note_vars_on_focus(o, id);
	if (!index_var.empty()) _vars->set(index_var, std::to_string(p->index()));
	return p;
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
	std::string sel_var, sel_val;
	read_choice_selected_var(o, sel_var, sel_val, init);

	auto rb = ce::radio_button(text, effective_font_scale(o));
	rb.value(init);

	if (!id.empty() || !sel_var.empty()) {
		auto cb_id = id;
		auto user_cb = _cb;
		auto vars = _vars;
		rb.on_click = [cb_id, user_cb, vars, sel_var, sel_val](bool state) {
			if (state && !sel_var.empty()) vars->set(sel_var, sel_val);
			if (user_cb && !cb_id.empty())
				user_cb(cb_id, /*is_button_click=*/false, value_t{state});
		};
	}
	auto shared = ce::share(std::move(rb));
	register_id(o, shared);
	note_initial_focus(o, shared);
	if (auto bp = std::dynamic_pointer_cast<ce::basic_button>(shared)) {
		note_focusable(id, bp);
	}
	subscribe_choice_selected_var(shared, sel_var, sel_val);
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
	if (auto* v = get_field(o, "initial"); v && pj_is_num(*v)) {
		auto raw = static_cast<long long>(pj_num(*v));
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

				// PageUp/Down キーが page_prev/next アクションの実装点。
				// LB/RB は既定バインド (pad lb/rb → page_prev/next action) が
				// page_up/down キー合成でここへ届くため、 直接の pad バインドは
				// 持たない (バインド一元化。 画面 JSON の "bindings" で差替可)。
				ce::key_info pgup{ce::key_code::page_up,
				                  ce::key_action::press, 0};
				ce::key_info pgdn{ce::key_code::page_down,
				                  ce::key_action::press, 0};
				vw.bind_shortcut(pgup,
				    [step]() { step(-1); }, /*force=*/true);
				vw.bind_shortcut(pgdn,
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
// "input" ブロック → action バインド関連 ("bindings" / "se" / "initial_focus")
// の解析。 画面別 "input" と input_defaults.jsonc (top-level 同形) の両方から
// 呼ばれる。 view settings 系は build_input_applier が別途担当。
//
// "bindings" の 1 要素 = { "key"|"pad"|"mouse"|"wheel": "<name>",
//                          "action": "<action>", ["mods": [...]],
//                          ["force": bool], ["target": "<id>"] }
//   - key:   "escape"/"enter"/"a".. (parse_key_code)
//   - pad:   "a"/"b"/"lb".. (parse_pad_button)
//           刻印基準 ("a"/"b"/"x"/"y") と位置基準
//           ("face_south"/"face_east"/"face_west"/"face_north") の 2 系統。
//           1 回の押下で両方が届くので、 ボタンごとにどちらで縛るかを選ぶ
//           (任天堂系と Xbox で X/Y の位置が入れ替わるため)。
//   - mouse: "right"/"middle" (左クリックは widget 直接操作のため対象外)
//   - wheel: "up"/"down"
//   - action "none" = 該当入力を無効化 (消費するが何もしない)
//   - action "passthrough" = 何も bind しない (組込デフォルトも無効化した
//     上で、 未処理としてホスト側へ素通しする)。 常駐する非モーダル
//     オーバレイで「この入力は下のゲームのもの」と宣言するのに使う。
//     "none" だと消費されてゲームまで届かない点が違う。
//---------------------------------------------------------------------------
input_action_config parse_input_actions(const picojson::object& input_obj)
{
	input_action_config cfg;

	if (auto* arr = get_array(input_obj, "bindings")) {
		for (const auto& v : *arr) {
			if (!v.is<picojson::object>()) continue;
			const auto& bo = v.get<picojson::object>();
			action_binding b;
			b.action = string_or(bo, "action");
			if (b.action.empty()) {
				em_logf("elements_modal: bindings: 'action' required");
				continue;
			}
			b.target = string_or(bo, "target");
			if (bool f = false; bool_field(get_field(bo, "force"), f)) {
				b.force = f;
				b.force_set = true;
			}

			if (auto key_name = string_or(bo, "key"); !key_name.empty()) {
				auto kc = parse_key_code(key_name);
				if (kc == ce::key_code::unknown) {
					em_logf("elements_modal: bindings: unknown key=%s",
					        key_name.c_str());
					continue;
				}
				b.src = action_binding::source::key;
				b.key = kc;
				if (auto* ma = get_array(bo, "mods")) b.mods = parse_modifiers(*ma);
			} else if (auto pad_name = string_or(bo, "pad"); !pad_name.empty()) {
				auto pb = parse_pad_button(pad_name);
				if (pb == ce::pad_button::unknown) {
					em_logf("elements_modal: bindings: unknown pad=%s",
					        pad_name.c_str());
					continue;
				}
				b.src = action_binding::source::pad;
				b.pad = pb;
			} else if (auto ms = string_or(bo, "mouse"); !ms.empty()) {
				if (!parse_mouse_button(ms, b.mbtn)) {
					em_logf("elements_modal: bindings: mouse must be "
					        "right/middle (got %s)", ms.c_str());
					continue;
				}
				b.src = action_binding::source::mouse;
			} else if (auto wh = string_or(bo, "wheel"); !wh.empty()) {
				if      (wh == "up")   b.wheel_dir = +1;
				else if (wh == "down") b.wheel_dir = -1;
				else {
					em_logf("elements_modal: bindings: wheel must be up/down "
					        "(got %s)", wh.c_str());
					continue;
				}
				b.src = action_binding::source::wheel;
			} else {
				em_logf("elements_modal: bindings: needs one of "
				        "key/pad/mouse/wheel");
				continue;
			}
			cfg.bindings.push_back(std::move(b));
		}
	}

	// "se": { "nav": "cursor.ogg", "accept": "...", "cancel": "...",
	//         "page": "...", "scroll": "...", "<action名>": "..." }
	// キーはカテゴリ名 or 個別 action 名。 発火は external_cb("<se>") 経由で
	// ホストに通知される (Elements 自体は音を持たない)。
	if (auto* v = get_field(input_obj, "se"); v && v->is<picojson::object>()) {
		for (auto& kv : v->get<picojson::object>()) {
			if (kv.second.is<std::string>())
				cfg.se[kv.first] = kv.second.get<std::string>();
		}
	}

	// "initial_focus": "<id>" — 画面を開いた時の初期 focus (画面別のみ意味を持つ)。
	// 要素側の "initial_focus": true と併存した場合はこちらが勝つ (後から適用)。
	if (auto* v = get_field(input_obj, "initial_focus"); v && v->is<std::string>())
		cfg.initial_focus_id = v->get<std::string>();

	// "cursor_warp": bool — キー/パッドでフォーカスが動いた時、 ホストがマウス
	// カーソルを focus hot point へ warp する運用 (通知は take_key_focus_move)。
	if (bool b = false; bool_field(get_field(input_obj, "cursor_warp"), b))
		cfg.cursor_warp = b ? 1 : 0;

	return cfg;
}

//---------------------------------------------------------------------------
// "input" ブロック → view 設定クロージャ生成。
//
// JSON 例:
//   "input": {
//     "arrow_focus_nav": true,
//     "arrow_focus_enter": "directional",  // first (既定) / directional
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

		bool focus_wrap_set = false;
		bool focus_wrap = false;

		bool skip_disabled_set = false;
		bool skip_disabled = false;

		bool enter_dir_set = false;
		bool enter_dir = false;

		bool repeat_set = false;
		int  repeat_delay_ms = 400;
		int  repeat_rate_ms  = 0;

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
	if (bool b = false; bool_field(get_field(input_obj, "focus_wrap"), b)) {
		cfg->focus_wrap_set = true;
		cfg->focus_wrap = b;
	}
	if (bool b = false; bool_field(get_field(input_obj, "skip_disabled"), b)) {
		cfg->skip_disabled_set = true;
		cfg->skip_disabled = b;
	}
	// "arrow_focus_enter": どこにも focus が無い状態で方向キーを押したときに
	// どれへ入るか。 "first" (既定) = 収集順の先頭 / "directional" = 押した
	// 方向の端 (右なら一番右)。 初期 focus 無しで開く確認ダイアログ向け。
	if (auto* v = get_field(input_obj, "arrow_focus_enter"); v && v->is<std::string>()) {
		std::string const m = v->get<std::string>();
		cfg->enter_dir_set = true;
		cfg->enter_dir = (m == "directional" || m == "direction");
	}
	if (auto* v = get_field(input_obj, "repeat_delay_ms"); v && pj_is_num(*v)) {
		cfg->repeat_set = true;
		cfg->repeat_delay_ms = static_cast<int>(pj_num(*v));
	}
	if (auto* v = get_field(input_obj, "repeat_rate_ms"); v && pj_is_num(*v)) {
		cfg->repeat_set = true;
		cfg->repeat_rate_ms = static_cast<int>(pj_num(*v));
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
	if (auto* v = get_field(input_obj, "stick_deadzone"); v && pj_is_num(*v)) {
		cfg->deadzone_set = true;
		cfg->deadzone = static_cast<float>(pj_num(*v));
	}
	if (auto* v = get_field(input_obj, "stick_value_speed"); v && pj_is_num(*v)) {
		cfg->speed_set = true;
		cfg->value_speed = static_cast<float>(pj_num(*v));
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
		if (cfg->focus_wrap_set)  view_.arrow_focus_wrap(cfg->focus_wrap);
		if (cfg->skip_disabled_set) view_.focus_skip_disabled(cfg->skip_disabled);
		if (cfg->enter_dir_set)   view_.arrow_focus_enter_directional(cfg->enter_dir);
		if (cfg->repeat_set)      view_.axis_repeat(cfg->repeat_delay_ms,
		                                            cfg->repeat_rate_ms);
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
// 変数参照の収集 — 「この画面はどの変数を何に使っているか」を JSON から拾う。
//
// ビルダの各所 (text_var / visible_var / enabled_var / index_var / value_var
// / at_var / …) を 1 つずつ計装するのではなく、 JSON の木を 1 回歩いて
// 「`_var` で終わるキー」を集める。 参照箇所が増えても取りこぼさないのと、
// 変数を持つ要素が build されなかった場合 (条件付きで作られない等) でも
// 「画面が使っている変数」として見えるのが利点。
//
// id は「いちばん近い祖先の "id"」を使う。 変数を持つのが id 無しの子要素
// (layout の子の at_var など) でも、 どのパーツの話かは辿れる。
//---------------------------------------------------------------------------
void collect_var_refs(const picojson::value& v, const std::string& id,
                      var_ref_map& out)
{
	if (v.is<picojson::array>()) {
		for (const auto& e : v.get<picojson::array>())
			collect_var_refs(e, id, out);
		return;
	}
	if (!v.is<picojson::object>()) return;

	const auto& o = v.get<picojson::object>();
	std::string cur = id;
	if (auto* iv = get_field(o, "id"); iv && iv->is<std::string>())
		cur = iv->get<std::string>();

	for (const auto& kv : o) {
		const std::string& key = kv.first;
		// "<なにか>_var": "変数名" — 読み取り参照。
		if (key.size() > 4 && key.compare(key.size() - 4, 4, "_var") == 0 &&
		    kv.second.is<std::string>()) {
			const std::string& name = kv.second.get<std::string>();
			if (!name.empty()) out[name].push_back({ cur, key });
			continue;
		}
		// "vars_on_focus": {name: value} — focus 時の書き込み。
		if (key == "vars_on_focus" && kv.second.is<picojson::object>()) {
			for (const auto& wv : kv.second.get<picojson::object>())
				out[wv.first].push_back({ cur, "vars_on_focus" });
			continue;
		}
		collect_var_refs(kv.second, cur, out);
	}
}

//---------------------------------------------------------------------------
// "font_languages": 言語連動フォント置換表 — 言語コード →
//   { "map": { "<family または別名>": "<置換先 family>", ... },
//     "fallback": "<その言語のときの既定 families チェーン>" (任意) }
// map は widget の "font" 指定・theme 既定チェーンの各 family トークンに
// 適用され、 "#tag=val" サフィックスは温存される。 実適用はフォント層
// (set_font_language_table) + set_language。 表はプロセスグローバルへ
// 言語単位でマージ登録する (画面 JSON top-level と app.jsonc の両方で宣言
// でき、 後から読まれた方が言語単位で上書き。 全画面同一表の運用を想定 —
// 異なる表を持つ画面の同時表示は非対応)。 エントリの無い言語は置換なし。
//---------------------------------------------------------------------------
static void apply_font_languages(const picojson::object& o)
{
	auto* v = get_field(o, "font_languages");
	if (!v || !v->is<picojson::object>())
		return;
	for (auto const& [lang, ev] : v->get<picojson::object>()) {
		if (!ev.is<picojson::object>()) continue;
		auto const& eo = ev.get<picojson::object>();
		ce::font_language_entry entry;
		if (auto* m = get_field(eo, "map"); m && m->is<picojson::object>()) {
			for (auto const& [from, to] : m->get<picojson::object>()) {
				if (to.is<std::string>())
					entry.map.emplace_back(from, to.get<std::string>());
			}
		}
		if (auto* f = get_field(eo, "fallback"); f && f->is<std::string>())
			entry.fallback = f->get<std::string>();
		ce::set_font_language_entry(lang, std::move(entry));
	}
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

	// 配置 / 拡縮の基準: "base": "window" (既定) | "content"。 overlay ホストが
	// ウィンドウ全面とホスト定義のコンテンツ矩形のどちらを基準に配置・拡縮する
	// かの宣言 (独立ウィンドウの run_modal では使わない)。 widget 内の "base"
	// (text_area の文字方向) とは別物。
	if (auto* v = get_field(o, "base"); v && v->is<std::string>()) {
		const std::string& b = v->get<std::string>();
		if (b == "content") result.placement_base = overlay_base::content;
		else if (b != "window")
			em_logf("elements_modal: unknown top-level base \"%s\" "
			        "(expected window/content)", b.c_str());
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
	if (!resource_base.empty()) {
		builder.set_resource_base(resource_base);
	}
	if (auto* v = get_field(o, "locale"); v && v->is<std::string>()) {
		builder.set_default_locale(v->get<std::string>());
	}

	// "font_languages": 言語連動フォント置換表 (詳細は apply_font_languages)。
	apply_font_languages(o);

	// "font_scale" (float, 既定 1.0) — このダイアログのウィジェット既定フォント
	// 倍率。 明示 size を持たない button/toggle/radio/check_box/label が追従する。
	// 省略 (=1.0) 時は従来と完全一致 (opt-in、 他メニューへ不影響)。
	builder.set_font_scale(static_cast<float>(number_or(o, "font_scale", 1.0)));

	// "style" ブロック (任意) — 未指定値の既定をまとめて与えるテーマ入口。
	//   font_scale : top-level "font_scale" と同じ (両方あれば style 側が優先)
	//   tile_gap   : "gap" 未指定の vtile/htile の子間隙間 px
	//   row_height : button 系 / input_box / selection_menu の既定最小高 px
	//   padding    : content 全体を包む外側余白 px
	// いずれも省略 (= 0) で従来と完全一致。 将来のテーマ一括指定 (9patch /
	// atlas スキン等) はこのブロックを拡張して受ける想定。
	float style_padding = 0.0f;
	if (auto* v = get_field(o, "style"); v && v->is<picojson::object>()) {
		const auto& so = v->get<picojson::object>();
		if (auto* fs = get_field(so, "font_scale"); fs && pj_is_num(*fs))
			builder.set_font_scale(static_cast<float>(pj_num(*fs)));
		builder.set_tile_gap(static_cast<float>(number_or(so, "tile_gap", 0.0)));
		builder.set_row_height(static_cast<float>(number_or(so, "row_height", 0.0)));
		// 無効 (disabled) 表示の不透明度。 0 以下 / 未指定でテーマ既定のまま。
		// 薄すぎて読めないときに画面ごとへ上書きできる。
		if (auto* dop = get_field(so, "disabled_opacity"); dop && pj_is_num(*dop)) {
			float v = static_cast<float>(pj_num(*dop));
			if (v > 0.0f) {
				if (v > 1.0f) v = 1.0f;
				ce::theme t = ce::get_theme();
				t.disabled_opacity = v;
				ce::set_theme(t);
			}
		}
		style_padding = static_cast<float>(number_or(so, "padding", 0.0));
	}

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

	// style.padding: content 全体を外側余白で包む (背景 box の内側)。
	if (style_padding > 0.0f) {
		const float p = style_padding;
		content = ce::share(ce::margin({p, p, p, p}, ce::hold_any(content)));
	}

	if (auto* arr = get_array(o, "background")) {
		ce::color bg = parse_color(*arr);
		element_ptr bgel = ce::share(ce::box(bg));
		// "background_opacity_var": 背景板だけの不透明度を変数連動にする
		// (0..1 の 10 進小数)。 中身 (文字やボタン) はそのままなので、
		// 「下のゲーム画面を透かす」用途で可読性を落とさない。 字幕窓の
		// 下地と同じ考え方 (ウィンドウ透過率 / UI の透過率)。
		// ※ content ではなく背景 box にだけ掛けるのが要点。 全体に掛けると
		//   重なった要素が二重にブレンドされて文字が浮く。
		if (std::string bvar = string_or(o, "background_opacity_var");
		    !bvar.empty()) {
			auto vars = builder.vars();
			auto alpha = std::make_shared<float>(1.0f);
			auto clamp01 = [](float f) {
				return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
			};
			if (auto* init = vars->get(bvar)) {
				try { *alpha = clamp01(std::stof(*init)); } catch (...) {}
			}
			vars->subscribe(bvar, [alpha, clamp01](const std::string& v) {
				try { *alpha = clamp01(std::stof(v)); } catch (...) {}
			}, bgel);
			bgel = ce::share(opacity_element(ce::hold_any(std::move(bgel)), alpha));
		}
		ce::layer_composite ly;
		ly.push_back(std::move(bgel));
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

	// deferred_view_cbs: build 中に積まれた追加 view-setup (例: tab_view
	// の PageUp/Down + LB/RB バインド、 "focus_nav" の解決)。 適用は
	// input_cb の後だが、 生成はこちらを先に行う ("focus_nav" の id 解決が
	// _id_to_element を参照するため。 直後の take_id_map が move で
	// 持ち出すので、 それより前でないと解決できない)。
	auto deferred_cbs = builder.take_deferred_view_callbacks();
	// "input" ブロック (任意): view に対する arrow_focus_nav / pad mode /
	// pad bindings / shortcuts を設定するクロージャを作る。
	// action バインド関連 ("bindings"/"se"/"initial_focus") は data として
	// parsed_layout.actions に載せ、 overlay_session が組込デフォルト +
	// input_defaults.jsonc とマージして適用する。
	std::function<void(ce::view&)> input_cb;
	if (auto* v = get_field(o, "input"); v && v->is<picojson::object>()) {
		const auto& input_obj = v->get<picojson::object>();
		result.actions = parse_input_actions(input_obj);
		input_cb = build_input_applier(input_obj, builder.take_id_map());
	}
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
				    d && pj_is_num(*d)) {
					ts.duration_ms = static_cast<int>(pj_num(*d));
				}
				// "universal" 用: rule 画像パスと境界ぼかし幅 (0-255)。
				ts.rule = string_or(obj, "rule");
				if (auto* vg = get_field(obj, "vague");
				    vg && pj_is_num(*vg)) {
					ts.vague = static_cast<int>(pj_num(*vg));
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
	// 戻り値 = 言語が実際に変わったか (ホストの再描画要否判定用)。
	result.set_language = [strings = builder.strings()](const std::string& lang) {
		return strings->set_language(lang);
	};

	// ホスト主導の変数書込 closure。 VariableStore を shared_ptr 捕捉するので
	// set_language と同じく closure 保持中は store + subscribers が生存する。
	// 戻り値 = 値が実際に変わったか (同値書込は false。 再描画要否判定用)。
	result.set_var = [vars = builder.vars()](const std::string& name,
	                                         const std::string& value) {
		return vars->set(name, value);
	};

	// 部分再描画用の通知フック設置口 (詳細は json_layout.h)。
	result.set_var_change_notifier =
		[vars = builder.vars()](std::function<void(ce::element&)> f) {
			vars->set_change_notifier(std::move(f));
		};

	// 変数の観測 (検証パネル用)。 現在値のスナップショット、 変化の通知フック、
	// そして JSON から拾った参照表。 参照表は build されなかった要素の分も
	// 含む「画面が使っている変数」の一覧になる。
	result.var_snapshot = [vars = builder.vars()]() {
		return vars->values();
	};
	result.set_var_watcher =
		[vars = builder.vars()](
			std::function<void(const std::string&, const std::string&)> f) {
			vars->set_watcher(std::move(f));
		};
	collect_var_refs(root, std::string(), result.var_refs);

	// i18n: 画面が持つ言語の一覧 ("strings" の lang キーの和集合)。
	result.languages = [strings = builder.strings()]() {
		return strings->languages();
	};

	// take 系は最後に。 内部 state を move する。
	result.focus_poll = builder.take_focus_poll();
	result.hover_poll = builder.take_hover_poll();
	result.focus_link_elements = builder.take_focus_link_elements();
	return result;
}

} // anonymous

//---------------------------------------------------------------------------
// 公開 API
//---------------------------------------------------------------------------

// 実行時画像ストア ("mem://mem_key") のバイト更新後に呼ぶと、 その key で
// 構築済みの image widget を set_image で再デコードして即時反映する。 失効した
// widget は掃除する。 host 側 registerImage() から呼ばれる (外部リンケージが要る
// ので匿名 namespace の外=この公開 API 区画に置く)。
// ※呼出側は ImageStore のロックを解放してから呼ぶこと (set_image が
//   resource_loader 経由で ImageStore を読むため、 保持したままだと再入する)。
void refresh_mem_image(const std::string& mem_key)
{
	std::lock_guard<std::mutex> lk(mem_image_mutex());
	auto it = mem_image_map().find(mem_key);
	if (it == mem_image_map().end()) return;
	auto& vec = it->second;
	for (auto e = vec.begin(); e != vec.end(); ) {
		if (auto p = e->widget.lock()) {
			try { p->set_image(e->src, e->scale); } catch (...) {}
			++e;
		} else {
			e = vec.erase(e);   // 失効 widget を掃除
		}
	}
	if (vec.empty()) mem_image_map().erase(it);
}

// input_defaults.jsonc (top-level が画面別 "input" ブロックと同形) を解析する。
// overlay_session が resource_base ごとに 1 回ロードし、 全画面の組込デフォルト
// の上へ重ねる (最後に画面別 "input" が勝つ)。 settings 部は build_input_applier
// を id_map 無しで使う (legacy "shortcuts" の target 解決は不可 = 共通ファイル
// では新形式 "bindings" を使うこと)。
input_defaults_data parse_input_defaults(const std::string& json_utf8)
{
	input_defaults_data out;
	const std::string preprocessed = preprocess_jsonc(json_utf8);
	picojson::value v;
	std::string err;
	picojson::parse(v, preprocessed.cbegin(), preprocessed.cend(), &err);
	if (!err.empty() || !v.is<picojson::object>()) {
		em_logf("elements_modal: input_defaults parse error: %s",
		        err.empty() ? "top-level must be an object" : err.c_str());
		return out;
	}
	const auto& o = v.get<picojson::object>();
	out.apply_settings = build_input_applier(o, {});
	out.actions = parse_input_actions(o);
	out.ok = true;
	return out;
}

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

	// アプリ既定の言語連動フォント置換表 (画面 JSON 側の宣言が言語単位で
	// 上書きする)。 manifest を読むだけで有効になる。
	apply_font_languages(o);

	m.ok = true;
	return m;
}

//---------------------------------------------------------------------------
// 言語連動フォント置換表のホスト直登録口 (modal.h 参照)。
//---------------------------------------------------------------------------
bool apply_font_languages_json(const std::string& json_utf8)
{
	const std::string pre = preprocess_jsonc(json_utf8);
	picojson::value v;
	std::string err;
	picojson::parse(v, pre.cbegin(), pre.cend(), &err);
	if (!err.empty() || !v.is<picojson::object>()) {
		em_logf("elements_modal: font_languages parse error: %s",
		        err.empty() ? "top-level must be object" : err.c_str());
		return false;
	}
	const auto& o = v.get<picojson::object>();
	if (get_field(o, "font_languages")) {
		apply_font_languages(o);
	} else {
		// 表そのもの ({lang: {map, fallback}}) を受けた場合はラップして流す
		picojson::object wrap;
		wrap["font_languages"] = v;
		apply_font_languages(wrap);
	}
	return true;
}

} // namespace elements_modal

//---------------------------------------------------------------------------
//!@file elements_modal: 純粋 SDL + Elements + ThorVG モーダル UI
//
// 任意の SDL3 アプリから単体で利用できる JSON ベースモーダルダイアログ。
//
// 主要な公開 API:
//   - run_modal(): 独立 SDL_Window を立てて閉じるまでブロック
//   - overlay_session: 呼出側のサーフェスに描画するオーバーレイ
//   - init() / shutdown(): ThorVG + フォントの一括初期化
//
// 複数画面の JSON 駆動遷移を組むホストは、 別ヘッダも参照:
//   - <elements_modal/navigator.h>: 画面遷移ドライバ (resolve_transition / navigator)
//   - <elements_modal/effects.h>:   fade 用 ARGB クロスブレンド (blend_argb8888)
//
// パーツ演出 (要素の "animate" による移動/拡縮/回転/フェード) は overlay_session
// が内部で駆動する。 個別利用するなら:
//   - <elements_modal/tween.h>:     イージング/台形プロファイル + tween 再生器
//   - <elements_modal/transform.h>: 非 reflow の変換 proxy (xform_state / xform)
//   - <elements_modal/animator.h>:  tween で xform_state を駆動する animator
//
// SDL_Window/SDL_Renderer の所有権はライブラリ側、 呼出側は閉じるまで待つだけ。
//---------------------------------------------------------------------------
#ifndef ELEMENTS_MODAL_MODAL_H
#define ELEMENTS_MODAL_MODAL_H

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace elements_modal {

//! @brief state widget の値型。 checkbox/toggle/slide → bool、
//!        input_box → string。 将来 slider → double 等で拡張予定。
using value_t = std::variant<bool, std::int64_t, double, std::string>;

//! @brief モーダル実行結果。
struct result
{
	//! 閉じるトリガとなった button の id。 Esc / × 閉じは空文字。
	std::string action;

	//! id を持つ state widget (checkbox / toggle_button / slide_switch /
	//! input_box 等) の最終値マップ。
	std::map<std::string, value_t> values;
};

//---------------------------------------------------------------------------
// 画面遷移 (JSON 駆動ランナ用)
//---------------------------------------------------------------------------

//! @brief 画面遷移 1 件分の仕様。 JSON 上の string 短縮形 ("foo" / "<back>")
//! では target だけセットされ、 object 形式 ({"target": "...", "effect":
//! "fade", "duration": 200}) では effect / duration_ms も埋まる。
struct transition_spec
{
	//! 遷移先。 以下のいずれか:
	//!   "<name>"         — 山括弧不要 / マニフェストの screens 登録名で push
	//!   "<back>"         — 現画面を pop
	//!   "<exit>"         — アプリ終了
	//!   "<replace:name>" — 現画面を name にすげ替え (stack 不変)
	//!   "<stay>"         — 現画面を再 enter (stack 不変)
	std::string target;

	//! 遷移エフェクト。 空文字 = なし (即切替)、 "fade" = クロスフェード。
	//! 未対応エフェクトはホスト側で警告 + 即切替フォールバック。
	std::string effect;

	//! エフェクト所要時間 ms。 0 = ホスト既定 (= 200ms 程度)。
	int duration_ms = 0;
};

//! @brief アプリマニフェスト (entry + 画面レジストリ)。 別途用意した
//! app.jsonc 等から parse_app_manifest() で読み込む。
struct app_manifest
{
	//! 起点画面の名前 (screens のキー)。
	std::string entry;

	//! 画面名 → JSON ファイル相対パス。 ランナはこのマップから現画面の
	//! JSON ファイルを解決する。 相対パスは app.jsonc 自身のあるディレクト
	//! リからの相対と解釈すること推奨。
	std::map<std::string, std::string> screens;

	//! parse 成功時 true。 失敗時は entry / screens が空のまま戻る。
	bool ok = false;
};

//! @brief マニフェスト JSON 解析 (entry + screens レジストリ)。
//! 失敗時は ok=false のオブジェクトを返す (詳細は SDL_Log)。
app_manifest parse_app_manifest(const std::string& json_utf8);

//! @brief id 付き widget のイベント callback。
//!        button の click は is_button_click=true、 state widget の値変化は
//!        is_button_click=false で呼ばれる。 button の payload は空 (default
//!        構築の value_t = bool false 相当)、 state は実値。
//!
//!        overlay_session::start / config::on_event に渡せば、 結果集約
//!        (result) と並行して外部に通知される。 ホスト側が button click
//!        や値変化を即座に拾うのに使う。
using event_callback = std::function<void(
	const std::string& id,
	bool is_button_click,
	const value_t& payload)>;

//! @brief 独立 SDL_Window 経由のモーダル実行設定。
struct config
{
	//! ウィンドウタイトル (UTF-8)。
	std::string title_utf8 = "Modal Dialog";

	//! ウィンドウサイズ (ピクセル)。 JSON の top-level "size" は無視される。
	int width  = 800;
	int height = 600;

	//! logical → pixel 倍率。 1.0 (デフォルト) で view extent = ウィンドウサイズ。
	//! 2.0 にすると view extent = width/2 × height/2 logical で、 内部ピクセル
	//! バッファだけ 2 倍密度で確保される (HiDPI 用)。
	float pixel_scale = 1.0f;

	//! モーダル親ウィンドウ。 nullptr なら親なし (z-order 固定なし)。
	//! 指定された場合 SDL_WINDOW_MODAL で生成し、 OS レベルで親側入力をブロック。
	SDL_Window* parent = nullptr;

	//! (deprecated, no-op) かつてはライブラリ内部でフォント走査を行っていた
	//! が、フォント登録は完全に呼出側の責務になった。 残しているのは API 互換の
	//! ため。 呼出側で `cycfi::elements::load_fonts_from_directory()` か
	//! `register_font()` を `run_modal()` 前に直接呼ぶこと。
	std::string font_directory;

	//! 任意のイベント通知 callback (overlay_session の external_cb と同じ役)。
	//! state widget の値変化、 および全 button click で発火する。
	//! "close_on_click": true な button click はこの callback 発火後に
	//! モーダル終了するので、 同じ id が `result::action` にも乗る。
	event_callback on_event;
};

//! @brief 独立 SDL_Window でモーダルダイアログを実行する。
//!        ダイアログが閉じるまでブロックする。
//! @param json_utf8  JSON テキスト (UTF-8)。 top-level の "size" は無視され、
//!                   ウィンドウサイズは cfg.width / cfg.height で決まる。
//! @param cfg        実行設定
//! @param out_result 結果格納先
//! @return true: 正常実行 / false: 起動失敗 (window 生成失敗 / JSON parse 失敗 等)
bool run_modal(const std::string& json_utf8,
               const config& cfg,
               result& out_result);

//! @brief ライブラリ全体の初期化 (ThorVG)。
//!        run_modal() が暗黙に呼ぶので明示呼出は通常不要。
//!
//!        フォント登録はこの関数では一切行わない (std::filesystem に依存しない
//!        方針のため)。 呼出側が `cycfi::elements::load_fonts_from_directory()`
//!        か `register_font()` / `register_font_buffer()` で必要なフォントを
//!        登録すること。 吉里吉里組み込み時は本体側 Storages 経由で登録する。
//!
//! @param font_directory     互換のため残してあるが現在は未使用。
//! @param load_default_fonts 互換のため残してあるが現在は未使用。
//! @return true: 成功 (already-initialized も成功扱い) / false: 失敗
bool init(const std::string& font_directory = {},
          bool load_default_fonts = true);

//! @brief ライブラリ全体の終了処理。 通常はプロセス終了時に自動。
void shutdown();

//---------------------------------------------------------------------------
// overlay_session — 既存 SDL_Window/SDL_Renderer 等のサーフェス上に
// モーダルダイアログを動かす低レベル API。
//
// run_modal() は独自に SDL_Window を立てるが、 overlay_session は呼出側が
// 持つ任意のサーフェス (ARGB8888 ピクセルバッファ) に Elements ダイアログを
// 描画し、 SDL イベントは呼出側がディスパッチする形。 SDL3 を使うゲームの
// 「ゲーム画面の上にダイアログを被せる」用途や、 既存 SDL_Window を再利用
// したい場合に利用する。
//
// 呼出側が毎フレーム行うこと:
//   1. SDL イベントを on_mouse_*/on_key_*/on_text_input に転送 (surface 座標で)
//   2. render_to_buffer() でピクセルバッファに描画 (中央配置矩形が返る)
//   3. その矩形位置にテクスチャを表示 (host が SDL_Renderer / GL 等で)
//   4. finished() で完了を検出し get_result() を取得
//
// SDL 依存は座標型を `float` で受ける部分だけ。 button / mods 値は呼出側で
// SDL 定数 (SDL_BUTTON_LEFT 等 / SDL_KMOD_*) を渡してもらう想定。
//---------------------------------------------------------------------------
class overlay_session
{
public:
	overlay_session();
	~overlay_session();
	overlay_session(const overlay_session&) = delete;
	overlay_session& operator=(const overlay_session&) = delete;

	//! @brief モーダル開始。 JSON パース + element ツリー構築 + 内部 view 作成。
	//! @param json_utf8    JSON テキスト (UTF-8)
	//! @param view_width   view extent (logical 座標、 = dialog の希望論理サイズ)
	//! @param view_height  view extent (logical)
	//! @param pixel_scale  logical → pixel 倍率。 高密度レンダ用 (デフォルト 1.0)。
	//!                     render_to_buffer の buffer サイズは
	//!                     view_width * pixel_scale × view_height * pixel_scale
	//!                     を呼出側で確保する想定。
	//! @param external_cb  内部 result 集約と並行して即時通知する callback (任意)。
	//!                     button click は ESC 同様に finished()=true を引き起こす。
	//! @return true: 開始成功 / false: JSON parse 失敗等
	bool start(const std::string& json_utf8,
	           int view_width, int view_height,
	           float pixel_scale = 1.0f,
	           event_callback external_cb = {},
	           const std::string& resource_base = {});

	//! @brief 外部からの強制終了要求。 action は呼出側が指定 (空文字なら通常の
	//!        Esc 相当)。 要素に exit 演出 ("on":"exit") があれば、 即終了せず
	//!        exit 演出を再生し、 完了後に finished() が true になる (退場×遷移の
	//!        協調)。 exit 演出が無ければ即 finished()。
	void close(std::string action = {});

	//! @brief モーダルが描画 / 入力受け取り可能な状態か。 退場 (exit) 演出の
	//!        再生中は false (入力は受け付けないが描画 render_to_buffer は継続)。
	bool active() const;

	//! @brief モーダルが終了したか。 exit 演出があれば、 その完了後に true。
	bool finished() const;

	//! @brief 結果取得。 active() = false の後に意味を持つ。
	const result& get_result() const;

	//! @brief JSON の top-level "transitions" を読み取った辞書を返す。
	//! key = action id ("" = 空 action / Esc / B 等)、 value = 遷移仕様。
	//! ランナが get_result().action でキー lookup → 次画面を決める。
	//! 未定義なら空 map (ランナの既定挙動 = entry なら exit / 子なら back)。
	const std::map<std::string, transition_spec>& transitions() const;

	//! @brief 直前まで focus されていた id を返す。 何も focus されていない /
	//! focus poll 未稼働なら空文字列。 画面 close 時にホストが記録すれば、
	//! 再入時に focus_by_id() で復元できる。
	const std::string& focused_id() const;

	//! @brief 現在キーボードフォーカスを持つ要素がテキスト入力を消費するか
	//! (= 編集可能な input_box / text box が focus されているか)。 ホストは
	//! これを毎フレーム監視して、 モバイル等のオンスクリーンキーボードを
	//! 「テキスト欄に focus が入ったら表示 / 外れたら非表示」に駆動する。
	bool focus_consumes_text() const;

	//! @brief 指定 id の要素に focus を移す。 id が見つからなければ no-op。
	//! 起動直後 (start() 直後 / initial_focus を上書きしたい場面) や、
	//! ホスト主導の focus 復元に使う。
	//! 注: view::focus は遅延タスクなので、 直後にキー送出しても旧 focus に届く。
	//! 「focus してすぐ起動」したい場合は activate_by_id() を使う。
	void focus_by_id(const std::string& id);

	//! @brief id 付き widget を「登録順」に列挙する (UI ツリー dump 用)。
	//! 各要素は {id, type} (type は JSON の "type" 文字列)。 現在値は
	//! get_result().values を id で引く。
	struct widget_desc { std::string id; std::string type; };
	std::vector<widget_desc> list_widgets() const;

	//! @brief id 指定で widget を起動する。 focus を**即時適用**してから Enter を
	//! 送るので、 focus_by_id + on_key_down と違いその場で効く。 button は click、
	//! checkbox/toggle/slide_switch は値トグル相当。 id が無ければ false。
	bool activate_by_id(const std::string& id);

	//! @brief i18n: 実行中の表示言語を切り替える (EUI Phase 2)。
	//! JSON top-level "strings" の対応表を持つ画面で、 "text_id" を指定した
	//! 全 label の表示文字列をその場で lang のものに更新する (text_var と同じ
	//! subscribe 機構)。 次フレームの render_to_buffer で反映される。 "strings"
	//! 未定義の画面では no-op。 lang は対応表の言語キー ("ja" / "en" 等)。
	//! 画面遷移をまたいで言語を保ちたい場合、 ホストは現在言語を保持して
	//! 新画面の start() 後に再度呼ぶ (各画面は毎回作り直されるため)。
	void set_language(const std::string& lang);

	//! @brief 現在の表示言語。 一度も set_language していない場合は JSON の
	//! "lang"、 それも無ければ空文字列。
	const std::string& language() const;

	//! @brief パーツ演出 ("animate") を発火トリガ名で明示再生する。
	//! @param trigger "enter" / "focus" / "select" / "exit"。
	//! @param id      対象要素 id。 空なら同トリガの全束縛を発火。 focus/select は
	//!                要素 id 紐付けなので id 一致 (または束縛側 id 空) のものだけ。
	//! enter/focus/select は通常 overlay_session が自動駆動する (画面表示・focus
	//! 変化・button click)。 これは退場 (exit) 演出や、 ホスト独自タイミングでの
	//! 手動発火に使う。 exit と画面遷移の協調 (再生完了を待つ等) はホスト責務。
	void play_animation(const std::string& trigger, const std::string& id = {});

	//! @brief view extent を変更 (window resize 等)。
	void notify_view_resize(int new_view_width, int new_view_height);

	//! @brief 描画矩形 (surface logical 座標、 中央配置済み)。
	struct render_rect
	{
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
	};

	//! @brief ピクセルバッファに描画 + サーフェスへの貼付位置を計算。
	//! @param pixel_buffer  ARGB8888 (LE = BGRA byte order) の書き込み先
	//! @param buffer_w_px   バッファ幅 (ピクセル、 = view_width * pixel_scale)
	//! @param buffer_h_px   バッファ高 (ピクセル、 = view_height * pixel_scale)
	//! @param surface_w     呼出側サーフェスの幅 (logical)。 中央配置の基準。
	//!                      通常 = ウィンドウクライアント領域 logical サイズ。
	//! @param surface_h     呼出側サーフェスの高さ (logical)
	//! @param out_rect      描画された矩形 (surface logical 座標)。 呼出側は
	//!                      この位置にテクスチャを表示する。
	//! @return true: アクティブなので継続描画 / false: 終了済み (描画なし)
	bool render_to_buffer(std::uint32_t* pixel_buffer,
	                      int buffer_w_px, int buffer_h_px,
	                      int surface_w, int surface_h,
	                      render_rect& out_rect);

	//! @brief 直近の render_to_buffer で計算された描画矩形 (surface logical)。
	//!        on_mouse_* に渡す座標は surface logical 座標で渡せば、 内部で
	//!        この矩形位置を引いて view local 座標に変換する。
	render_rect get_current_rect() const;

	//! @brief content の自然サイズ (view limits の min、 logical 座標) を取得。
	//!        run_modal が独立ウィンドウを内容サイズに縮めるのと同じ値で、
	//!        ホストが overlay の view extent を内容にフィットさせて上下の
	//!        余白を消すのに使う。 start() 後に呼ぶこと。
	//! @return true: 取得成功 (view 構築済み) / false: 未開始
	bool measure_content(int& out_w, int& out_h) const;

	// --- SDL イベント転送 (surface logical 座標で渡す) ---

	//! SDL_BUTTON_LEFT / MIDDLE / RIGHT 等、 mods は SDL_KMOD_* の OR
	void on_mouse_down(float surface_x, float surface_y, int button, int mods);
	void on_mouse_up  (float surface_x, float surface_y, int button, int mods);
	void on_mouse_move(float surface_x, float surface_y, int mods);
	void on_mouse_wheel(float dx, float dy,
	                    float surface_mouse_x, float surface_mouse_y);
	void on_mouse_leave();
	void notify_surface_resize(int new_w, int new_h) { (void)new_w; (void)new_h; }
	// (notify_surface_resize は将来 surface 全体の resize 対応用、 現状未実装)

	//! sdl_key = SDL_Keycode 値。 mods は SDL_KMOD_* の OR。
	//! @return true: ダイアログが入力を消費した (Esc / focus widget が処理) /
	//!         false: 未処理。 ホスト側はこれを見て、 非モーダル時に未処理キーを
	//!         ゲームへ素通しできる (キーボードフォーカスの pass-through)。
	bool on_key_down(int sdl_key, int mods);
	bool on_key_up  (int sdl_key, int mods);

	//! UTF-8 テキスト入力 (SDL_EVENT_TEXT_INPUT の text)。
	void on_text_input(const char* utf8_text);

	//! ゲームパッドの離散ボタンイベント。 sdl_gamepad_button は
	//! SDL_GAMEPAD_BUTTON_SOUTH 等の値、 down は press=true / release=false。
	//! 未対応ボタン (SDL_GAMEPAD_BUTTON_*_PADDLE 等) は内部で無視される。
	//! @return true: 消費 (既知ボタン) / false: 未対応で無視。
	bool on_pad_button(int sdl_gamepad_button, bool down);

	//! ゲームパッドのアナログ軸イベント。 sdl_gamepad_axis は
	//! SDL_GAMEPAD_AXIS_LEFTX 等の値、 raw_value は SDL の int16 範囲
	//! (-32768..32767、 トリガは 0..32767)。 内部で [-1.0, +1.0] に正規化。
	void on_pad_axis(int sdl_gamepad_axis, int raw_value);

private:
	struct impl;
	std::unique_ptr<impl> _impl;
};

} // namespace elements_modal

#endif

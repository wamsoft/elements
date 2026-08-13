/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/text_backend.hpp>
#include <elements/support/canvas.hpp>
#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <cstdlib>
#include <memory>
#include <string>

namespace cycfi { namespace elements
{
   namespace
   {
      constexpr float tvg_font_scale = 72.0f / 96.0f;

      std::string stem_from_path(std::string const& path)
      {
         auto slash = path.find_last_of("/\\");
         auto start = (slash != std::string::npos) ? slash + 1 : 0;
         auto dot = path.rfind('.');
         auto end = (dot != std::string::npos && dot > start) ? dot : path.size();
         return path.substr(start, end - start);
      }

      auto clamp8 = [](float v) -> uint8_t {
         return uint8_t(std::min(std::max(v * 255.0f, 0.0f), 255.0f));
      };

      //----------------------------------------------------------------------
      // Rasterized text-run cache
      //
      // Drawing text through tvg::Text rasterizes every glyph outline again on
      // every frame, and that is what dominates an Elements panel's cost:
      // measured ~28 us per glyph plus ~320 us per run, i.e. 20 plain labels
      // cost ~12 ms/frame against ~0.2 ms for 20 boxes. Caching the *shaped*
      // text does not help — the outline filling is the expensive part.
      //
      // So rasterize a run once into its own small bitmap (letting ThorVG do
      // it, so the result is pixel-identical to the old path) and hand that
      // bitmap to a tvg::Picture afterwards. Later frames only composite an
      // image, which is cheap.
      //
      // Only the fast path is cached: the canvas matrix must be a pure
      // scale+translate (no rotation/skew), since a rotated run should keep
      // being rendered from outlines for quality. The glyph colour is baked
      // into the bitmap (UI palettes have few colours); global_alpha is
      // applied per frame through Picture::opacity so fades do not thrash it.
      //----------------------------------------------------------------------
      struct run_key
      {
         std::string text;
         std::string font;
         std::string locale;
         float       size = 0;
         float       spacing = 1.0f;
         std::uint32_t rgba = 0;
         int         sx_q = 0;   // 量子化した描画スケール (1/64 単位)
         int         sy_q = 0;

         bool operator<(run_key const& r) const
         {
            if (sx_q != r.sx_q) return sx_q < r.sx_q;
            if (sy_q != r.sy_q) return sy_q < r.sy_q;
            if (size != r.size) return size < r.size;
            if (rgba != r.rgba) return rgba < r.rgba;
            if (spacing != r.spacing) return spacing < r.spacing;
            if (int c = text.compare(r.text)) return c < 0;
            if (int c = font.compare(r.font)) return c < 0;
            return locale.compare(r.locale) < 0;
         }
      };

      struct run_entry
      {
         std::unique_ptr<std::uint32_t[]> pixels;   // Picture が参照し続ける
         tvg::Picture* pic = nullptr;               // ref'd by the cache
         int   w = 0, h = 0;
         float ox = 0, oy = 0;      // ink 左上 - ペン原点 (device px)
         float ascent = 0, descent = 0, width = 0;
         std::uint64_t gen = 0;     // 使用中の flush 世代
         std::size_t   used = 0;    // その世代で何回使ったか
         std::uint64_t last_gen = 0;
      };

      // 総ピクセル数の上限 (4 バイト/px なので 4M px ≒ 16MB)。
      constexpr std::size_t run_cache_max_pixels = 4u * 1024u * 1024u;

      class run_cache
      {
      public:
         ~run_cache() { clear(); }

         void clear()
         {
            for (auto& kv : _runs)
               if (kv.second.pic) kv.second.pic->unref();
            _runs.clear();
            _pixels = 0;
         }

         run_entry* find(run_key const& k, std::uint64_t gen)
         {
            auto it = _runs.find(k);
            if (it == _runs.end()) return nullptr;
            it->second.last_gen = gen;
            return &it->second;
         }

         run_entry& insert(run_key const& k, run_entry&& e, std::uint64_t gen)
         {
            _pixels += std::size_t(e.w) * e.h;
            e.last_gen = gen;
            auto res = _runs.emplace(k, std::move(e));
            evict(gen);
            return res.first->second;
         }

      private:
         void evict(std::uint64_t gen)
         {
            for (auto it = _runs.begin();
                 it != _runs.end() && _pixels > run_cache_max_pixels; )
            {
               if (it->second.last_gen != gen)
               {
                  _pixels -= std::size_t(it->second.w) * it->second.h;
                  if (it->second.pic) it->second.pic->unref();
                  it = _runs.erase(it);
               }
               else ++it;
            }
         }

         std::map<run_key, run_entry> _runs;   // node-based: 参照が安定
         std::size_t _pixels = 0;
      };

      run_cache& text_run_cache()
      {
         static run_cache cache;
         return cache;
      }
   }

   class tvg_text_backend : public text_backend
   {
   public:
      ~tvg_text_backend() override = default;

      void fill_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         if (fill_text_cached(cnv, utf8_, p))
            return;
         fill_text_outline(cnv, utf8_, p);
      }

   private:

      //! ラスタ済み run を貼る速い経路。 使えない条件 (回転/スキューあり、
      //! 塗りが単色でない、 ラスタライズ失敗) なら false を返して従来経路へ。
      bool fill_text_cached(canvas& cnv, std::string_view utf8_, point p)
      {
         // 逃げ道: 環境変数で従来経路に固定できる (見た目の突き合わせ用)。
         static const bool disabled = getenv("ELEMENTS_TEXTCACHE_OFF") != nullptr;
         if (disabled) return false;

         auto const& st = cnv.get_state();

         // 変換行列が「スケール + 平行移動」でなければ従来経路
         // (回転した文字はアウトラインから描いた方が綺麗)。
         if (st.matrix.e12 != 0.0f || st.matrix.e21 != 0.0f) return false;
         const float sx = st.matrix.e11, sy = st.matrix.e22;
         if (sx <= 0.0f || sy <= 0.0f)
            return false;

         auto const* fill_c = std::get_if<color>(&st.fill_style_data);
         if (!fill_c) return false;

         std::string utf8(utf8_);
         if (utf8.empty())
            return true;    // 描くものが無い

         run_key key;
         key.text = utf8;
         key.font = st.font_file.empty() ? st.font_family : st.font_file;
         key.locale = st.text_locale;
         key.size = st.font_size;
         key.spacing = st.letter_spacing;
         key.rgba = (std::uint32_t(clamp8(fill_c->red)) << 24)
                  | (std::uint32_t(clamp8(fill_c->green)) << 16)
                  | (std::uint32_t(clamp8(fill_c->blue)) << 8)
                  | std::uint32_t(clamp8(fill_c->alpha));
         key.sx_q = int(sx * 64.0f + 0.5f);
         key.sy_q = int(sy * 64.0f + 0.5f);

         const std::uint64_t gen = cnv.flush_generation();
         run_entry* ent = text_run_cache().find(key, gen);
         if (!ent)
         {
            run_entry made;
            if (!rasterize_run(cnv, utf8, sx, sy, *fill_c, made)) return false;
            ent = &text_run_cache().insert(key, std::move(made), gen);
         }
         if (!ent->pic)
            return false;

         // キャッシュした Picture は**テンプレート**として保持し、 描画には
         // その複製を渡す (canvas に add した paint は remove で所有権ごと
         // 手放されるため、 同じインスタンスを毎フレーム貼り回せない)。
         // elements の pixmap 描画も同じ作りになっている。
         auto* paint = ent->pic->duplicate();
         if (!paint)
            return false;
         auto* pic = static_cast<tvg::Picture*>(paint);

         // 配置: ペン原点を device 空間へ移し、 ラスタ時の余白ぶん戻す。
         float dx = 0, dy = 0;
         switch (st.align & 0x3)
         {
            case canvas::text_alignment::right:  dx = -ent->width; break;
            case canvas::text_alignment::center: dx = -ent->width / 2; break;
            default: break;
         }
         switch (st.align & 0x1C)
         {
            case canvas::text_alignment::top:    dy = 0; break;
            case canvas::text_alignment::middle: dy = -(ent->ascent + ent->descent) / 2; break;
            case canvas::text_alignment::bottom: dy = -(ent->ascent + ent->descent); break;
            default: dy = -ent->ascent; break;
         }
         const float ox = st.matrix.e11 * (p.x + dx) + st.matrix.e12 * (p.y + dy) + st.matrix.e13;
         const float oy = st.matrix.e21 * (p.x + dx) + st.matrix.e22 * (p.y + dy) + st.matrix.e23;

         tvg::Matrix m = {1, 0, ox + ent->ox, 0, 1, oy + ent->oy, 0, 0, 1};
         pic->transform(m);
         if (st.global_alpha < 1.0f)
            pic->opacity(clamp8(st.global_alpha));
         if (auto* clip_shape = cnv.make_clip_shape())
            pic->clip(clip_shape);
         cnv.add_pending(pic);
         return true;
      }

      //! run を専用ビットマップへ 1 度だけ描く (ThorVG に描かせるので、
      //! 見た目は従来経路と同一)。 成功したら out に Picture を用意する。
      bool rasterize_run(canvas& cnv, std::string const& utf8,
                         float sx, float sy, color const& c, run_entry& out)
      {
         auto const& st = cnv.get_state();
         auto* text = make_text(cnv, utf8);
         if (!text) return false;
         text->fill(clamp8(c.red), clamp8(c.green), clamp8(c.blue));
         text->opacity(clamp8(c.alpha));

         tvg::TextMetrics tm;
         text->metrics(tm);
         out.ascent = tm.ascent;
         out.descent = -tm.descent;
         out.width = run_width(text, utf8, st.letter_spacing);

         // ビットマップのサイズはメトリクスから決める。 グリフは advance や
         // ascent/descent をはみ出して描かれる (アクセント、 記号、 張り出し
         // のあるフォント等) ので、 全周にフォントサイズの半分ぶん余白をとる。
         // (bounds() で ink を測る手もあるが、 canvas に update されるまで
         //  取得できないうえ、 そのための probe canvas は割に合わない)
         const int pad = std::max(2, int(std::ceil(st.font_size * std::max(sx, sy) * 0.5f)));
         const int w = int(std::ceil(out.width * sx)) + pad * 2;
         const int h = int(std::ceil((out.ascent + out.descent) * sy)) + pad * 2;
         if (w <= 0 || h <= 0 || std::size_t(w) * h > run_cache_max_pixels)
         {
            tvg::Paint::rel(text);
            return false;
         }
         out.pixels.reset(new std::uint32_t[std::size_t(w) * h]);
         std::fill_n(out.pixels.get(), std::size_t(w) * h, 0u);

         // tvg::Text の原点は行の**上端**なので (ベースラインではない —
         // 呼出側が align 用に -ascent を足しているのはそのため)、 行の左上を
         // 余白ぶんだけ内側に置く。
         const float tx = float(pad);
         const float ty = float(pad);
         tvg::Matrix placed = {sx, 0, tx, 0, sy, ty, 0, 0, 1};
         text->transform(placed);
         text->ref();   // 描画後の remove で消えないように

         auto* rc = tvg::SwCanvas::gen(tvg::EngineOption::None);
         rc->target(out.pixels.get(), w, w, h, tvg::ColorSpace::ARGB8888);
         rc->add(text);
         rc->update();
         rc->draw(false);
         rc->sync();
         rc->remove();
         delete rc;
         text->unref();   // ここまでで役目終わり (ビットマップに焼けた)

         auto* pic = tvg::Picture::gen();
         if (pic->load(out.pixels.get(), std::uint32_t(w), std::uint32_t(h),
                       tvg::ColorSpace::ARGB8888, true) != tvg::Result::Success)
         {
            tvg::Paint::rel(pic);
            return false;
         }
         pic->ref();
         out.pic = pic;
         out.w = w;
         out.h = h;
         // ビットマップ左上 - ペン原点 (device px)
         out.ox = -tx;
         out.oy = -ty;
         return true;
      }

      //! 現在の canvas 状態でテキストオブジェクトを作る (整形はここで走る)。
      tvg::Text* make_text(canvas& cnv, std::string const& utf8)
      {
         auto const& st = cnv.get_state();
         auto font_name = st.font_file.empty()
            ? st.font_family : stem_from_path(st.font_file);
         if (!st.font_file.empty())
            tvg::Text::load(st.font_file.c_str());

         auto* text = tvg::Text::gen();
         text->font(font_name.c_str());
         text->size(st.font_size * tvg_font_scale);
         if (st.letter_spacing != 1.0f)
            text->spacing(st.letter_spacing, 1.0f);
         if (!st.text_locale.empty())
            text->locale(st.text_locale.c_str());
         text->text(utf8.c_str());
         return text;
      }

      //! 字送りの総和 (align center/right 用)。
      static float run_width(tvg::Text* text, std::string const& utf8, float letter_scale)
      {
         float width = 0;
         for (const char* c = utf8.c_str(); *c; )
         {
            tvg::GlyphMetrics gm;
            int len = (*c & 0x80) == 0 ? 1 : (*c & 0xE0) == 0xC0 ? 2
                    : (*c & 0xF0) == 0xE0 ? 3 : 4;
            std::string ch(c, len);
            if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
               width += gm.advance;
            c += len;
         }
         return width * letter_scale;
      }

   public:

      //! 従来経路: アウトラインから毎フレーム描く。
      void fill_text_outline(canvas& cnv, std::string_view utf8_, point p)
      {
         std::string utf8(utf8_);

         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
         // letter spacing (tracking): scale factor on each glyph advance.
         float letter_scale = cnv.get_state().letter_spacing;
         if (letter_scale != 1.0f)
            text->spacing(letter_scale, 1.0f);
         if (!cnv.get_state().text_locale.empty())
            text->locale(cnv.get_state().text_locale.c_str());
         text->text(utf8.c_str());

         tvg::TextMetrics tm;
         text->metrics(tm);
         float ascent = tm.ascent;
         float descent = -tm.descent;

         float dx = 0, dy = 0;
         switch (cnv.get_state().align & 0x3)
         {
            case canvas::text_alignment::right:
            case canvas::text_alignment::center:
            {
               float width = 0;
               for (const char* c = utf8.c_str(); *c; )
               {
                  tvg::GlyphMetrics gm;
                  int len = (*c & 0x80) == 0 ? 1 : (*c & 0xE0) == 0xC0 ? 2
                          : (*c & 0xF0) == 0xE0 ? 3 : 4;
                  std::string ch(c, len);
                  if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
                     width += gm.advance;
                  c += len;
               }
               // spacing scales advances, so the laid-out width scales too.
               width *= letter_scale;
               dx = (cnv.get_state().align & 0x3) == canvas::text_alignment::right
                  ? -width : -width / 2;
               break;
            }
            default: break;
         }
         switch (cnv.get_state().align & 0x1C)
         {
            case canvas::text_alignment::top:    dy = 0; break;
            case canvas::text_alignment::middle: dy = -(ascent + descent) / 2; break;
            case canvas::text_alignment::bottom: dy = -(ascent + descent); break;
            default: dy = -ascent; break;
         }

         if (auto* c = std::get_if<color>(&cnv.get_state().fill_style_data))
         {
            text->fill(clamp8(c->red), clamp8(c->green), clamp8(c->blue));
            text->opacity(clamp8(c->alpha * cnv.get_state().global_alpha));
         }

         tvg::Matrix offset = {1, 0, p.x + dx, 0, 1, p.y + dy, 0, 0, 1};
         text->transform(canvas::multiply(cnv.get_state().matrix, offset));

         if (auto* clip_shape = cnv.make_clip_shape())
            text->clip(clip_shape);

         // 同じバッチへ積むだけにして、 描画は次の flush にまとめる。
         // テキスト 1 本ごとに update/draw/sync/remove を回すと、 ラベルが
         // 並ぶ画面で ThorVG のサイクルがその本数だけ走って非常に高くつく
         // (実測: ラベル 20 個で 15ms)。 描画順は add 順で保たれる。
         cnv.add_pending(text);
      }

      void stroke_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         std::string utf8(utf8_);

         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
         if (!cnv.get_state().text_locale.empty())
            text->locale(cnv.get_state().text_locale.c_str());
         text->text(utf8.c_str());

         tvg::TextMetrics tm;
         text->metrics(tm);
         float dy = -tm.ascent;

         if (auto* c = std::get_if<color>(&cnv.get_state().stroke_style_data))
         {
            text->outline(cnv.get_state().line_width_val,
               clamp8(c->red), clamp8(c->green), clamp8(c->blue));
            text->opacity(clamp8(c->alpha * cnv.get_state().global_alpha));
         }
         text->fill(0, 0, 0);

         tvg::Matrix offset = {1, 0, p.x, 0, 1, p.y + dy, 0, 0, 1};
         text->transform(canvas::multiply(cnv.get_state().matrix, offset));

         if (auto* clip_shape = cnv.make_clip_shape())
            text->clip(clip_shape);

         // 同じバッチへ積むだけにして、 描画は次の flush にまとめる。
         // テキスト 1 本ごとに update/draw/sync/remove を回すと、 ラベルが
         // 並ぶ画面で ThorVG のサイクルがその本数だけ走って非常に高くつく
         // (実測: ラベル 20 個で 15ms)。 描画順は add 順で保たれる。
         cnv.add_pending(text);
      }

      text_metrics measure_text(canvas& cnv, char const* utf8) override
      {
         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
         if (!cnv.get_state().text_locale.empty())
            text->locale(cnv.get_state().text_locale.c_str());
         text->text(utf8);

         tvg::TextMetrics tm = {};
         text->metrics(tm);

         float width = 0;
         for (const char* c = utf8; *c; )
         {
            tvg::GlyphMetrics gm;
            int len = (*c & 0x80) == 0 ? 1 : (*c & 0xE0) == 0xC0 ? 2
                    : (*c & 0xF0) == 0xE0 ? 3 : 4;
            std::string ch(c, len);
            if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
               width += gm.advance;
            c += len;
         }

         float ascent = tm.ascent, descent = -tm.descent, leading = tm.linegap;
         tvg::Paint::rel(text);
         return { ascent, descent, leading, {width, ascent + descent} };
      }

      font_metrics measure_font(canvas& cnv) override
      {
         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
         if (!cnv.get_state().text_locale.empty())
            text->locale(cnv.get_state().text_locale.c_str());
         text->text(" ");

         tvg::TextMetrics tm;
         text->metrics(tm);

         float ascent = tm.ascent, descent = -tm.descent;
         float height = tm.advance, leading = tm.linegap;
         tvg::Paint::rel(text);
         return { ascent, descent, height, leading };
      }
   };

   std::shared_ptr<text_backend> create_tvg_text_backend()
   {
      return std::make_shared<tvg_text_backend>();
   }
}}

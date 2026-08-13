/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/text_backend.hpp>
#include <elements/support/canvas.hpp>
#include <thorvg.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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
      // Shaped-text cache
      //
      // Building a tvg::Text (font + size + string -> shaping -> glyph
      // outlines) dominates the per-frame cost of an Elements panel: 20 plain
      // labels measured ~15 ms/frame against ~0.2 ms for 20 boxes. The shaped
      // result depends only on the string and the font parameters, so keep the
      // Text object alive across frames and re-use it, setting just the
      // per-draw state (color, opacity, transform, clip) each time.
      //
      // Lifetime: Canvas::add() takes ownership, so the cache calls
      // Paint::ref() once to hold its own reference; the canvas' remove()
      // then only drops the canvas' one.
      //
      // The same Text must not be added twice within one batch, so each key
      // keeps a small pool and remembers the flush generation it was last used
      // in; a string repeated within one frame takes the next pool entry.
      //----------------------------------------------------------------------
      struct text_key
      {
         std::string text;
         std::string font;
         std::string locale;
         float       size = 0;
         float       spacing = 1.0f;

         bool operator<(text_key const& r) const
         {
            if (size != r.size) return size < r.size;
            if (spacing != r.spacing) return spacing < r.spacing;
            if (int c = text.compare(r.text)) return c < 0;
            if (int c = font.compare(r.font)) return c < 0;
            return locale.compare(r.locale) < 0;
         }
      };

      struct text_entry
      {
         tvg::Text* text = nullptr;   // ref'd by the cache
         float      ascent = 0;
         float      descent = 0;
         float      width = 0;        // advance sum (align center/right)
         bool       width_valid = false;
      };

      struct text_slot
      {
         std::vector<text_entry> pool;             // 同フレーム内の重複使用ぶん
         std::uint64_t           gen = 0;          // pool を使い始めた flush 世代
         std::size_t             used = 0;         // その世代で使った本数
         std::uint64_t           last_used_gen = 0;// LRU 用
      };

      // 上限 (1 slot = 文字列 1 本ぶん)。 越えたら当該フレームで使っていない
      // slot を落とす。
      constexpr std::size_t text_cache_limit = 512;

      class text_cache
      {
      public:
         ~text_cache() { clear(); }

         void clear()
         {
            for (auto& kv : _slots)
               for (auto& e : kv.second.pool)
                  if (e.text) e.text->unref();
            _slots.clear();
         }

         //! 現在の世代でまだ使っていない entry を返す。 無ければ空の entry を
         //! 足して made=true で返す (呼出側が整形結果を書き込む)。
         text_entry& acquire(text_key const& key, std::uint64_t gen, bool& made)
         {
            auto& slot = _slots[key];
            if (slot.gen != gen) { slot.gen = gen; slot.used = 0; }
            slot.last_used_gen = gen;
            if (slot.used < slot.pool.size())
            {
               made = false;
               return slot.pool[slot.used++];
            }
            made = true;
            slot.pool.push_back(text_entry{});
            ++slot.used;
            if (_slots.size() > text_cache_limit)
               evict(gen);
            return _slots[key].pool.back();
         }

      private:
         void evict(std::uint64_t gen)
         {
            for (auto it = _slots.begin();
                 it != _slots.end() && _slots.size() > text_cache_limit; )
            {
               if (it->second.last_used_gen != gen)
               {
                  for (auto& e : it->second.pool)
                     if (e.text) e.text->unref();
                  it = _slots.erase(it);
               }
               else ++it;
            }
         }

         std::map<text_key, text_slot> _slots;
      };

      text_cache& shaped_text_cache()
      {
         static text_cache cache;
         return cache;
      }

      //! 現在の canvas 状態 + 文字列から、 整形結果を一意に決めるキーを作る。
      //! (色/変換/クリップは描画時に載せ替えるのでキーに含めない)
      text_key make_key(canvas const& cnv, std::string const& utf8)
      {
         text_key k;
         k.text = utf8;
         k.font = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : cnv.get_state().font_file;
         k.locale = cnv.get_state().text_locale;
         k.size = cnv.get_state().font_size;
         k.spacing = cnv.get_state().letter_spacing;
         return k;
      }
   }

   class tvg_text_backend : public text_backend
   {
   public:
      ~tvg_text_backend() override = default;

      void fill_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         std::string utf8(utf8_);
         float letter_scale = cnv.get_state().letter_spacing;

         // 整形済み Text をキャッシュから取る (無ければ作って整形する)。
         // 整形とメトリクス算出は初回だけで済むので、 2 回目以降は色/変換/
         // クリップを載せ替えて描くだけになる。
         bool made = false;
         text_entry& ent = shaped_text_cache().acquire(
            make_key(cnv, utf8), cnv.flush_generation(), made);
         if (made)
         {
            auto font_name = cnv.get_state().font_file.empty()
               ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
            if (!cnv.get_state().font_file.empty())
               tvg::Text::load(cnv.get_state().font_file.c_str());

            auto* t = tvg::Text::gen();
            t->font(font_name.c_str());
            t->size(cnv.get_state().font_size * tvg_font_scale);
            // letter spacing (tracking): scale factor on each glyph advance.
            if (letter_scale != 1.0f)
               t->spacing(letter_scale, 1.0f);
            if (!cnv.get_state().text_locale.empty())
               t->locale(cnv.get_state().text_locale.c_str());
            t->text(utf8.c_str());
            t->ref();   // キャッシュぶんの参照 (canvas の remove では消えない)

            tvg::TextMetrics tm;
            t->metrics(tm);
            ent.text = t;
            ent.ascent = tm.ascent;
            ent.descent = -tm.descent;
         }
         auto* text = ent.text;
         if (!text) return;
         float ascent = ent.ascent;
         float descent = ent.descent;

         float dx = 0, dy = 0;
         switch (cnv.get_state().align & 0x3)
         {
            case canvas::text_alignment::right:
            case canvas::text_alignment::center:
            {
               if (!ent.width_valid)
               {
                  // 字送りの総和 = 版面幅。 1 文字ずつ metrics を引くので
                  // 高くつく → キャッシュして 2 回目以降は使い回す。
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
                  ent.width = width * letter_scale;
                  ent.width_valid = true;
               }
               dx = (cnv.get_state().align & 0x3) == canvas::text_alignment::right
                  ? -ent.width : -ent.width / 2;
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

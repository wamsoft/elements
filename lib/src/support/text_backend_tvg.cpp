/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/text_backend.hpp>
#include <elements/support/canvas.hpp>
#include <thorvg.h>
#include <algorithm>
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
   }

   class tvg_text_backend : public text_backend
   {
   public:
      ~tvg_text_backend() override = default;

      void fill_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         cnv.flush_shapes();
         std::string utf8(utf8_);

         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
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
            text->opacity(clamp8(c->alpha));
         }

         tvg::Matrix offset = {1, 0, p.x + dx, 0, 1, p.y + dy, 0, 0, 1};
         text->transform(canvas::multiply(cnv.get_state().matrix, offset));

         if (auto* clip_shape = cnv.make_clip_shape())
            text->clip(clip_shape);

         cnv.tvg_canvas().add(text);
         cnv.tvg_canvas().update();
         cnv.tvg_canvas().draw(false);
         cnv.tvg_canvas().sync();
         cnv.tvg_canvas().remove();
      }

      void stroke_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         cnv.flush_shapes();
         std::string utf8(utf8_);

         auto* text = tvg::Text::gen();
         auto font_name = cnv.get_state().font_file.empty()
            ? cnv.get_state().font_family : stem_from_path(cnv.get_state().font_file);
         if (!cnv.get_state().font_file.empty())
            tvg::Text::load(cnv.get_state().font_file.c_str());

         text->font(font_name.c_str());
         text->size(cnv.get_state().font_size * tvg_font_scale);
         text->text(utf8.c_str());

         tvg::TextMetrics tm;
         text->metrics(tm);
         float dy = -tm.ascent;

         if (auto* c = std::get_if<color>(&cnv.get_state().stroke_style_data))
         {
            text->outline(cnv.get_state().line_width_val,
               clamp8(c->red), clamp8(c->green), clamp8(c->blue));
            text->opacity(clamp8(c->alpha));
         }
         text->fill(0, 0, 0);

         tvg::Matrix offset = {1, 0, p.x, 0, 1, p.y + dy, 0, 0, 1};
         text->transform(canvas::multiply(cnv.get_state().matrix, offset));

         if (auto* clip_shape = cnv.make_clip_shape())
            text->clip(clip_shape);

         cnv.tvg_canvas().add(text);
         cnv.tvg_canvas().update();
         cnv.tvg_canvas().draw(false);
         cnv.tvg_canvas().sync();
         cnv.tvg_canvas().remove();
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

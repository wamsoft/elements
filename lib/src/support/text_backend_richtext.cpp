/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/text_backend_richtext.hpp>
#include <elements/support/canvas.hpp>

#include <richtext/TextRenderer.hpp>
#include <richtext/TextLayout.hpp>
#include <richtext/TextStyle.hpp>
#include <richtext/Appearance.hpp>
#include <richtext/FontManager.hpp>
#include <richtext/GlyphRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace cycfi { namespace elements
{
   namespace
   {
      // UTF-8 → UTF-16 conversion
      std::u16string to_u16(std::string_view utf8)
      {
         std::u16string result;
         result.reserve(utf8.size());
         unsigned state = 0, codepoint = 0;
         for (auto ch : utf8)
         {
            auto byte = static_cast<uint8_t>(ch);
            if (byte < 0x80) { state = 0; codepoint = byte; }
            else if ((byte & 0xC0) == 0x80) {
               codepoint = (codepoint << 6) | (byte & 0x3F);
               if (--state > 0) continue;
            }
            else if ((byte & 0xE0) == 0xC0) { state = 1; codepoint = byte & 0x1F; continue; }
            else if ((byte & 0xF0) == 0xE0) { state = 2; codepoint = byte & 0x0F; continue; }
            else { state = 3; codepoint = byte & 0x07; continue; }

            if (codepoint <= 0xFFFF)
               result.push_back(char16_t(codepoint));
            else {
               codepoint -= 0x10000;
               result.push_back(char16_t(0xD800 + (codepoint >> 10)));
               result.push_back(char16_t(0xDC00 + (codepoint & 0x3FF)));
            }
         }
         return result;
      }

      richtext::TextStyle make_style(
         std::string const& font_file, float font_size)
      {
         richtext::TextStyle style;
         style.fontSize = font_size;
         if (!font_file.empty())
         {
            auto& fm = richtext::FontManager::instance();
            auto collection = fm.createCollection({font_file});
            if (collection)
               style.fontCollection = collection;
         }
         return style;
      }

      auto clamp8 = [](float v) -> uint8_t {
         return uint8_t(std::min(std::max(v * 255.0f, 0.0f), 255.0f));
      };

      richtext::Appearance make_fill_appearance(color const& c)
      {
         uint32_t argb = (uint32_t(clamp8(c.alpha)) << 24)
                       | (uint32_t(clamp8(c.red))   << 16)
                       | (uint32_t(clamp8(c.green)) << 8)
                       |  uint32_t(clamp8(c.blue));
         richtext::Appearance app;
         app.addFill(argb);
         return app;
      }

      richtext::Appearance make_stroke_appearance(color const& c, float width)
      {
         uint32_t argb = (uint32_t(clamp8(c.alpha)) << 24)
                       | (uint32_t(clamp8(c.red))   << 16)
                       | (uint32_t(clamp8(c.green)) << 8)
                       |  uint32_t(clamp8(c.blue));
         richtext::Appearance app;
         app.addStroke(argb, width);
         return app;
      }

      struct rt_metrics
      {
         float width, ascent, descent, leading, height;
      };

      rt_metrics layout_and_measure(
         richtext::TextLayout& layout,
         std::string_view utf8,
         std::string const& font_file,
         float font_size)
      {
         auto u16 = to_u16(utf8);
         auto style = make_style(font_file, font_size);
         layout.layout(u16, style);

         rt_metrics m;
         m.width = layout.getWidth();
         m.ascent = std::abs(layout.getAscent());
         m.descent = std::abs(layout.getDescent());
         m.height = layout.getHeight();
         m.leading = m.height > (m.ascent + m.descent)
            ? m.height - (m.ascent + m.descent) : 0;
         return m;
      }
   }

   class richtext_text_backend : public text_backend
   {
   public:
      ~richtext_text_backend() override
      {
         _renderer.reset();
      }

      void fill_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         cnv.flush_shapes();

         richtext::TextLayout layout;
         auto m = layout_and_measure(layout, utf8_,
            cnv.get_state().font_file, cnv.get_state().font_size);

         float dx = 0, dy = 0;
         switch (cnv.get_state().align & 0x3)
         {
            case canvas::text_alignment::right:  dx = -m.width; break;
            case canvas::text_alignment::center: dx = -m.width / 2; break;
            default: break;
         }
         switch (cnv.get_state().align & 0x1C)
         {
            case canvas::text_alignment::top:    dy = m.ascent; break;
            case canvas::text_alignment::middle: dy = (m.ascent - m.descent) / 2; break;
            case canvas::text_alignment::bottom: dy = -m.descent; break;
            default: dy = 0; break;
         }

         richtext::Appearance app;
         if (auto* c = std::get_if<color>(&cnv.get_state().fill_style_data))
            app = make_fill_appearance(*c);

         tvg::Matrix offset = {1, 0, p.x + dx, 0, 1, p.y + dy, 0, 0, 1};
         tvg::Matrix combined = canvas::multiply(cnv.get_state().matrix, offset);

         auto& renderer = ensure_renderer(cnv);
         renderer.getGlyphRenderer()->setTransform(&combined);
         renderer.drawLayout(layout, 0, 0, app);
         renderer.getGlyphRenderer()->setTransform(nullptr);

         cnv.tvg_canvas().update();
         cnv.tvg_canvas().draw(false);
         cnv.tvg_canvas().sync();
         cnv.tvg_canvas().remove();
      }

      void stroke_text(canvas& cnv, std::string_view utf8_, point p) override
      {
         cnv.flush_shapes();

         richtext::TextLayout layout;
         auto m = layout_and_measure(layout, utf8_,
            cnv.get_state().font_file, cnv.get_state().font_size);

         richtext::Appearance app;
         if (auto* c = std::get_if<color>(&cnv.get_state().stroke_style_data))
            app = make_stroke_appearance(*c, cnv.get_state().line_width_val);

         tvg::Matrix offset = {1, 0, p.x, 0, 1, p.y, 0, 0, 1};
         tvg::Matrix combined = canvas::multiply(cnv.get_state().matrix, offset);

         auto& renderer = ensure_renderer(cnv);
         renderer.getGlyphRenderer()->setTransform(&combined);
         renderer.drawLayout(layout, 0, 0, app);
         renderer.getGlyphRenderer()->setTransform(nullptr);

         cnv.tvg_canvas().update();
         cnv.tvg_canvas().draw(false);
         cnv.tvg_canvas().sync();
         cnv.tvg_canvas().remove();
      }

      text_metrics measure_text(canvas& cnv, char const* utf8) override
      {
         richtext::TextLayout layout;
         auto m = layout_and_measure(layout, utf8,
            cnv.get_state().font_file, cnv.get_state().font_size);
         return { m.ascent, m.descent, m.leading, {m.width, m.ascent + m.descent} };
      }

      font_metrics measure_font(canvas& cnv) override
      {
         richtext::TextLayout layout;
         auto m = layout_and_measure(layout, " ",
            cnv.get_state().font_file, cnv.get_state().font_size);
         return { m.ascent, m.descent, m.height, m.leading };
      }

   private:
      richtext::TextRenderer& ensure_renderer(canvas& cnv)
      {
         if (!_renderer)
         {
            _renderer = std::make_unique<richtext::TextRenderer>();
            _renderer->setCanvas(&cnv.tvg_canvas());
         }
         return *_renderer;
      }

      std::unique_ptr<richtext::TextRenderer> _renderer;
   };

   std::shared_ptr<text_backend> create_richtext_text_backend()
   {
      return std::make_shared<richtext_text_backend>();
   }
}}

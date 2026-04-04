/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   richtext/minikin glyph layout backend
=============================================================================*/
#include <elements/support/glyph_utils_richtext.hpp>
#include <elements/support/text_utils.hpp>

#include <richtext/TextLayout.hpp>
#include <richtext/TextStyle.hpp>
#include <richtext/FontManager.hpp>

#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <set>
#include <mutex>

namespace cycfi { namespace elements
{
   namespace
   {
      // UTF-8 → UTF-16 conversion
      std::u16string utf8_to_utf16(char const* first, char const* last)
      {
         std::u16string result;
         result.reserve(last - first);
         unsigned state = 0, codepoint = 0;
         for (auto p = first; p != last; ++p)
         {
            if (!decode_utf8(state, codepoint, uint8_t(*p)))
            {
               if (codepoint <= 0xFFFF)
                  result.push_back(char16_t(codepoint));
               else
               {
                  codepoint -= 0x10000;
                  result.push_back(char16_t(0xD800 + (codepoint >> 10)));
                  result.push_back(char16_t(0xDC00 + (codepoint & 0x3FF)));
               }
            }
         }
         return result;
      }

      std::vector<int> utf8_byte_counts(char const* first, char const* last)
      {
         std::vector<int> counts;
         unsigned state = 0, codepoint = 0;
         int byte_count = 0;
         for (auto p = first; p != last; ++p)
         {
            ++byte_count;
            if (!decode_utf8(state, codepoint, uint8_t(*p)))
            {
               counts.push_back(byte_count);
               byte_count = 0;
            }
         }
         return counts;
      }

      richtext::TextStyle make_text_style(font const& f, float size)
      {
         richtext::TextStyle style;
         style.fontSize = size;
         if (!f.file().empty())
         {
            auto& fm = richtext::FontManager::instance();
            auto collection = fm.createCollection({f.file()});
            if (collection)
               style.fontCollection = collection;
         }
         return style;
      }

      void build_char_positions_from_layout(
         richtext::TextLayout const& layout,
         char const* first, char const* last,
         std::vector<char_pos>& positions,
         float x_offset)
      {
         auto byte_counts = utf8_byte_counts(first, last);
         int num_chars = int(byte_counts.size());
         if (num_chars == 0)
            return;

         auto u16text = layout.getText();
         std::vector<int> u16_to_cp(u16text.size(), -1);
         int cp_idx = 0;
         for (size_t i = 0; i < u16text.size() && cp_idx < num_chars; ++i)
         {
            u16_to_cp[i] = cp_idx;
            if (u16text[i] >= 0xD800 && u16text[i] <= 0xDBFF)
            {
               if (i + 1 < u16text.size())
                  u16_to_cp[i + 1] = cp_idx;
               ++i;
            }
            ++cp_idx;
         }

         positions.resize(num_chars);
         for (int i = 0; i < num_chars; ++i)
         {
            positions[i].x = 0;
            positions[i].advance = 0;
            positions[i].num_bytes = byte_counts[i];
         }

         size_t glyph_count = layout.getGlyphCount();
         for (size_t gi = 0; gi < glyph_count; ++gi)
         {
            auto const& glyph = layout.getGlyph(gi);
            int ci = -1;
            if (glyph.charIndex < u16_to_cp.size())
               ci = u16_to_cp[glyph.charIndex];
            if (ci < 0 || ci >= num_chars)
               continue;

            int next_ci = num_chars;
            if (gi + 1 < glyph_count)
            {
               auto const& next_glyph = layout.getGlyph(gi + 1);
               if (next_glyph.charIndex < u16_to_cp.size())
               {
                  int nci = u16_to_cp[next_glyph.charIndex];
                  if (nci > ci) next_ci = nci;
               }
            }

            int span = next_ci - ci;
            float per_char = glyph.advance / std::max(1, span);
            for (int k = 0; k < span && (ci + k) < num_chars; ++k)
            {
               positions[ci + k].x = x_offset + glyph.x + k * per_char;
               positions[ci + k].advance = per_char;
            }
         }
      }
   }

   ////////////////////////////////////////////////////////////////////
   // richtext glyph layout backend
   ////////////////////////////////////////////////////////////////////
   class richtext_glyph_layout_backend : public glyph_layout_backend
   {
   public:
      ~richtext_glyph_layout_backend() override = default;

      void layout(
         char const* first, char const* last,
         font const& f, float size,
         float x_offset,
         std::vector<char_pos>& positions,
         metrics& out_metrics) override
      {
         auto style = make_text_style(f, size);

         auto u16text = (first != last)
            ? utf8_to_utf16(first, last)
            : std::u16string(1, u' ');

         richtext::TextLayout tl;
         tl.layout(u16text, style);

         out_metrics.ascent = std::abs(tl.getAscent());
         out_metrics.descent = std::abs(tl.getDescent());
         float height = out_metrics.ascent + out_metrics.descent;
         float total_height = tl.getHeight();
         out_metrics.leading = total_height > height ? total_height - height : 0;

         if (first == last)
            return;

         build_char_positions_from_layout(tl, first, last, positions, x_offset);
      }
   };

   ////////////////////////////////////////////////////////////////////
   // richtext font backend
   ////////////////////////////////////////////////////////////////////
   class richtext_font_backend : public font_backend
   {
   public:
      ~richtext_font_backend() override = default;

      void initialize() override
      {
         std::call_once(_init_flag, []()
         {
            auto& fm = richtext::FontManager::instance();
            fm.initialize();
            fm.setFontDataLoader(
               [](std::string const& file_path) -> richtext::FontDataBuffer
               {
                  std::ifstream file(file_path, std::ios::binary);
                  if (!file.is_open()) return nullptr;
                  auto data = std::make_shared<std::vector<uint8_t>>(
                     std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>()
                  );
                  if (data->empty()) return nullptr;
                  return data;
               }
            );
         });
      }

      void register_font(std::string const& file_path) override
      {
         initialize();
         std::lock_guard<std::mutex> lock(_mutex);
         if (_registered.find(file_path) == _registered.end())
         {
            auto& fm = richtext::FontManager::instance();
            if (fm.registerFont(file_path, file_path))
               _registered.insert(file_path);
         }
      }

   private:
      std::once_flag          _init_flag;
      std::set<std::string>   _registered;
      std::mutex              _mutex;
   };

   ////////////////////////////////////////////////////////////////////
   // Factory functions
   ////////////////////////////////////////////////////////////////////
   std::shared_ptr<glyph_layout_backend> create_richtext_glyph_layout_backend()
   {
      return std::make_shared<richtext_glyph_layout_backend>();
   }

   std::shared_ptr<font_backend> create_richtext_font_backend()
   {
      return std::make_shared<richtext_font_backend>();
   }
}}

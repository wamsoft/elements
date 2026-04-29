/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   FreeType + HarfBuzz glyph layout backend
=============================================================================*/
#include <elements/support/glyph_utils.hpp>
#include <elements/support/text_utils.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>

namespace cycfi { namespace elements
{
   namespace
   {
      ////////////////////////////////////////////////////////////////////
      // FreeType library singleton
      ////////////////////////////////////////////////////////////////////
      struct ft_state
      {
         FT_Library library = nullptr;
         std::map<std::string, FT_Face> faces;  // keyed by file path or memory key
         // FT_New_Memory_Face requires the buffer to outlive the face.
         // Buffers are stored here, owned by the state.
         std::map<std::string, std::vector<std::uint8_t>> mem_buffers;
         std::mutex mutex;

         ~ft_state()
         {
            for (auto& [path, face] : faces)
               FT_Done_Face(face);
            if (library)
               FT_Done_FreeType(library);
         }
      };

      ft_state& get_ft()
      {
         static ft_state s;
         return s;
      }

      FT_Face get_face(std::string const& file_path)
      {
         auto& ft = get_ft();
         std::lock_guard<std::mutex> lock(ft.mutex);

         auto it = ft.faces.find(file_path);
         if (it != ft.faces.end())
            return it->second;

         FT_Face face = nullptr;
         if (FT_New_Face(ft.library, file_path.c_str(), 0, &face) == 0)
         {
            ft.faces[file_path] = face;
            return face;
         }
         return nullptr;
      }

      ////////////////////////////////////////////////////////////////////
      // UTF-8 byte count helpers
      ////////////////////////////////////////////////////////////////////
      std::vector<int> utf8_byte_counts(char const* first, char const* last)
      {
         std::vector<int> counts;
         unsigned state = 0;
         unsigned codepoint = 0;
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
   }

   ////////////////////////////////////////////////////////////////////
   // FreeType + HarfBuzz glyph layout backend
   ////////////////////////////////////////////////////////////////////
   class ft_glyph_layout_backend : public glyph_layout_backend
   {
   public:
      ~ft_glyph_layout_backend() override = default;

      void layout(
         char const* first, char const* last,
         font const& f, float size,
         float x_offset,
         std::vector<char_pos>& positions,
         metrics& out_metrics) override
      {
         FT_Face face = nullptr;
         if (!f.file().empty())
            face = get_face(f.file());

         if (!face)
         {
            out_metrics = {0, 0, 0};
            return;
         }

         // Set character size (in 26.6 fixed point, 72 DPI)
         FT_Set_Char_Size(face, 0, FT_F26Dot6(size * 64), 72, 72);

         // Extract font metrics
         float units_scale = size / float(face->units_per_EM);
         out_metrics.ascent = std::abs(face->ascender * units_scale);
         out_metrics.descent = std::abs(face->descender * units_scale);
         float line_height = face->height * units_scale;
         out_metrics.leading = line_height - (out_metrics.ascent + out_metrics.descent);
         if (out_metrics.leading < 0) out_metrics.leading = 0;

         if (first == last)
            return;

         // Shape with HarfBuzz
         hb_font_t* hb_font = hb_ft_font_create_referenced(face);
         hb_buffer_t* buf = hb_buffer_create();

         hb_buffer_add_utf8(buf, first, int(last - first), 0, int(last - first));
         hb_buffer_guess_segment_properties(buf);

         hb_shape(hb_font, buf, nullptr, 0);

         unsigned int glyph_count = 0;
         hb_glyph_info_t* glyph_infos = hb_buffer_get_glyph_infos(buf, &glyph_count);
         hb_glyph_position_t* glyph_positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

         // Build per-codepoint positions from HarfBuzz output.
         // HarfBuzz cluster values are byte offsets in UTF-8.
         auto byte_counts = utf8_byte_counts(first, last);
         int num_chars = int(byte_counts.size());

         positions.resize(num_chars);
         for (int i = 0; i < num_chars; ++i)
         {
            positions[i].x = 0;
            positions[i].advance = 0;
            positions[i].num_bytes = byte_counts[i];
         }

         // Build byte-offset → codepoint-index map
         std::vector<int> byte_to_cp(last - first, -1);
         {
            int byte_off = 0;
            for (int ci = 0; ci < num_chars; ++ci)
            {
               if (byte_off < int(byte_to_cp.size()))
                  byte_to_cp[byte_off] = ci;
               byte_off += byte_counts[ci];
            }
         }

         // Map HarfBuzz glyphs to codepoint positions
         float cursor_x = x_offset;
         for (unsigned gi = 0; gi < glyph_count; ++gi)
         {
            float advance = glyph_positions[gi].x_advance / 64.0f;
            int cluster = int(glyph_infos[gi].cluster);

            int ci = -1;
            if (cluster >= 0 && cluster < int(byte_to_cp.size()))
               ci = byte_to_cp[cluster];

            if (ci >= 0 && ci < num_chars)
            {
               // Determine span: how many codepoints share this cluster
               int next_ci = num_chars;
               if (gi + 1 < glyph_count)
               {
                  int next_cluster = int(glyph_infos[gi + 1].cluster);
                  if (next_cluster >= 0 && next_cluster < int(byte_to_cp.size()))
                  {
                     int nci = byte_to_cp[next_cluster];
                     if (nci > ci)
                        next_ci = nci;
                  }
               }

               int span = next_ci - ci;
               float per_char = advance / std::max(1, span);

               for (int k = 0; k < span && (ci + k) < num_chars; ++k)
               {
                  positions[ci + k].x = cursor_x + k * per_char;
                  positions[ci + k].advance = per_char;
               }
            }

            cursor_x += advance;
         }

         hb_buffer_destroy(buf);
         hb_font_destroy(hb_font);
      }
   };

   ////////////////////////////////////////////////////////////////////
   // FreeType font backend
   ////////////////////////////////////////////////////////////////////
   class ft_font_backend : public font_backend
   {
   public:
      ~ft_font_backend() override = default;

      void initialize() override
      {
         auto& ft = get_ft();
         std::lock_guard<std::mutex> lock(ft.mutex);
         if (!ft.library)
            FT_Init_FreeType(&ft.library);
      }

      void register_font(std::string const& file_path) override
      {
         // Pre-load the face so it's cached
         get_face(file_path);
      }

      void register_font_buffer(std::string const& key,
                                std::uint8_t const* data,
                                std::size_t size) override
      {
         if (!data || size == 0 || key.empty()) return;
         auto& ft = get_ft();
         std::lock_guard<std::mutex> lock(ft.mutex);
         if (!ft.library)
            FT_Init_FreeType(&ft.library);
         if (ft.faces.find(key) != ft.faces.end()) return;  // already done

         // Copy buffer locally so caller can free their data.
         auto& buf = ft.mem_buffers[key];
         buf.assign(data, data + size);

         FT_Face face = nullptr;
         if (FT_New_Memory_Face(ft.library, buf.data(),
                                FT_Long(buf.size()), 0, &face) == 0)
         {
            ft.faces[key] = face;
         }
         else
         {
            // Loading failed; release the buffer.
            ft.mem_buffers.erase(key);
         }
      }
   };

   ////////////////////////////////////////////////////////////////////
   // Factory functions
   ////////////////////////////////////////////////////////////////////
   std::shared_ptr<glyph_layout_backend> create_ft_glyph_layout_backend()
   {
      return std::make_shared<ft_glyph_layout_backend>();
   }

   std::shared_ptr<font_backend> create_ft_font_backend()
   {
      return std::make_shared<ft_font_backend>();
   }

   ////////////////////////////////////////////////////////////////////
   // Global backend state
   ////////////////////////////////////////////////////////////////////
   namespace
   {
      std::shared_ptr<glyph_layout_backend>& glyph_backend_instance()
      {
         static std::shared_ptr<glyph_layout_backend> s;
         return s;
      }

      std::shared_ptr<font_backend>& font_backend_instance()
      {
         static std::shared_ptr<font_backend> s;
         return s;
      }
   }

   void set_glyph_layout_backend(std::shared_ptr<glyph_layout_backend> b)
   {
      glyph_backend_instance() = std::move(b);
   }

   std::shared_ptr<glyph_layout_backend> get_glyph_layout_backend()
   {
      auto& b = glyph_backend_instance();
      if (!b)
         b = create_ft_glyph_layout_backend();
      return b;
   }

   void set_font_backend(std::shared_ptr<font_backend> b)
   {
      font_backend_instance() = std::move(b);
   }

   std::shared_ptr<font_backend> get_font_backend()
   {
      auto& b = font_backend_instance();
      if (!b)
         b = create_ft_font_backend();
      return b;
   }
}}

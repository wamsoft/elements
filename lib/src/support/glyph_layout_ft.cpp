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
         // Fallback priority = registration order, mirroring the drawing
         // side (ThorVG FT loader: Text::load order = fallback order).
         // Caret / selection positions are computed from these advances, so
         // the face picked here must be the same one the renderer draws with.
         std::vector<FT_Face> face_order;
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
            ft.face_order.push_back(face);
            return face;
         }
         return nullptr;
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

         // 描画側 (ThorVG FT loader) と同じ規則で per-codepoint フォント
         // フォールバックを解決してからシェーピングする。 描画は「primary に
         // グリフが無い codepoint は登録順で最初に持つ face で描く」ため、
         // ここが primary 一本のままだと CJK 等フォールバック文字の advance が
         // 実描画とずれ、 キャレット / 選択矩形 / クリック位置が累積的に狂う。
         //   1. codepoint 毎に face を resolve (primary → 登録順 → primary)
         //   2. 同一 face の連続区間 (run) に分割
         //   3. run 毎に HarfBuzz で shape し、 クラスタ (run 内バイト位置) を
         //      run 先頭バイトでグローバルへ補正して連結
         // 全 face を同じポイントサイズに設定するので advance は追加の UPM
         // 正規化なしで合成できる (ThorVG 側の primaryUpem 正規化と等価)。

         struct cp_info { unsigned cp; int byte_start; int byte_len; };
         std::vector<cp_info> cps;
         {
            unsigned state = 0, codepoint = 0;
            int byte_len = 0, off = 0;
            for (auto p = first; p != last; ++p, ++off)
            {
               ++byte_len;
               if (!decode_utf8(state, codepoint, uint8_t(*p)))
               {
                  cps.push_back({codepoint, off + 1 - byte_len, byte_len});
                  byte_len = 0;
               }
            }
         }
         int num_chars = int(cps.size());

         positions.resize(num_chars);
         for (int i = 0; i < num_chars; ++i)
         {
            positions[i].x = 0;
            positions[i].advance = 0;
            positions[i].num_bytes = cps[i].byte_len;
         }

         // Build byte-offset → codepoint-index map
         std::vector<int> byte_to_cp(last - first, -1);
         for (int ci = 0; ci < num_chars; ++ci)
            byte_to_cp[cps[ci].byte_start] = ci;

         // face の resolve (描画側 FtFontManager::fallback と同じ優先順)
         std::vector<FT_Face> order;
         {
            auto& ft = get_ft();
            std::lock_guard<std::mutex> lock(ft.mutex);
            order = ft.face_order;
         }
         auto resolve = [&](unsigned cp) -> FT_Face
         {
            if (FT_Get_Char_Index(face, cp) != 0)
               return face;
            for (FT_Face f2 : order)
            {
               if (f2 == face)
                  continue;
               if (FT_Get_Char_Index(f2, cp) != 0)
                  return f2;
            }
            return face;
         };

         hb_buffer_t* buf = hb_buffer_create();
         std::map<FT_Face, hb_font_t*> hb_fonts;
         auto hb_for = [&](FT_Face f2) -> hb_font_t*
         {
            auto it = hb_fonts.find(f2);
            if (it != hb_fonts.end())
               return it->second;
            FT_Set_Char_Size(f2, 0, FT_F26Dot6(size * 64), 72, 72);
            hb_font_t* hf = hb_ft_font_create_referenced(f2);
            hb_fonts.emplace(f2, hf);
            return hf;
         };

         float cursor_x = x_offset;
         auto shape_run = [&](FT_Face f2, int ci_begin, int ci_end)
         {
            const int run_byte_start = cps[ci_begin].byte_start;
            const int run_byte_end =
               cps[ci_end - 1].byte_start + cps[ci_end - 1].byte_len;
            const int run_len = run_byte_end - run_byte_start;

            hb_buffer_reset(buf);
            hb_buffer_add_utf8(buf, first + run_byte_start, run_len, 0, run_len);
            hb_buffer_guess_segment_properties(buf);
            hb_shape(hb_for(f2), buf, nullptr, 0);

            unsigned int glyph_count = 0;
            hb_glyph_info_t* glyph_infos =
               hb_buffer_get_glyph_infos(buf, &glyph_count);
            hb_glyph_position_t* glyph_positions =
               hb_buffer_get_glyph_positions(buf, &glyph_count);

            for (unsigned gi = 0; gi < glyph_count; ++gi)
            {
               float advance = glyph_positions[gi].x_advance / 64.0f;
               int cluster = run_byte_start + int(glyph_infos[gi].cluster);

               int ci = -1;
               if (cluster >= 0 && cluster < int(byte_to_cp.size()))
                  ci = byte_to_cp[cluster];

               if (ci >= 0 && ci < num_chars)
               {
                  // Determine span: how many codepoints share this cluster
                  // (クラスタは run を跨がないので既定は run 終端)
                  int next_ci = ci_end;
                  if (gi + 1 < glyph_count)
                  {
                     int next_cluster =
                        run_byte_start + int(glyph_infos[gi + 1].cluster);
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
         };

         int run_begin = 0;
         FT_Face run_face = nullptr;
         for (int ci = 0; ci < num_chars; ++ci)
         {
            FT_Face f2 = resolve(cps[ci].cp);
            if (!run_face)
               run_face = f2;
            else if (f2 != run_face)
            {
               shape_run(run_face, run_begin, ci);
               run_face = f2;
               run_begin = ci;
            }
         }
         if (run_face)
            shape_run(run_face, run_begin, num_chars);

         hb_buffer_destroy(buf);
         for (auto& kv : hb_fonts)
            hb_font_destroy(kv.second);
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
            ft.face_order.push_back(face);
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

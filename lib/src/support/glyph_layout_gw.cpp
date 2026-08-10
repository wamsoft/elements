/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   Host-bridge (thorvg_gw_bridge) glyph layout backend.

   Counterpart of glyph_layout_ft.cpp for TVG_LOADER_GW builds: shaping and
   font metrics come from the host font engine through ThorVG's gw bridge
   (thorvg_gw_bridge.h) instead of linking FreeType+HarfBuzz directly. This
   unifies the measurement path with the text rendering path (both consume
   the same injected engine). The bridge must be registered by the host
   before any font registration / layout happens.
=============================================================================*/
#include <elements/support/glyph_utils.hpp>
#include <elements/support/text_utils.hpp>

#include <thorvg_gw_bridge.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cycfi { namespace elements
{
   namespace
   {
      ////////////////////////////////////////////////////////////////////
      // Bridge face registry (keyed by file path or memory key)
      ////////////////////////////////////////////////////////////////////
      struct gw_face
      {
         void*          handle = nullptr;
         std::uint16_t  upem = 1000;
         float          ascent = 0;     // font units, positive
         float          descent = 0;    // font units, positive
         float          height = 0;     // font units
      };

      struct gw_state
      {
         std::map<std::string, gw_face> faces;
         std::mutex mutex;

         ~gw_state()
         {
            if (auto* b = tvgGwGetBridge())
            {
               for (auto& [key, face] : faces)
                  if (face.handle)
                     b->closeFace(b->ctx, face.handle);
            }
         }
      };

      gw_state& get_gw()
      {
         static gw_state s;
         return s;
      }

      // Open a bridge face over font bytes and cache it under `key`.
      // The bridge copies the bytes (copy=1), so the caller may free them.
      bool open_face(std::string const& key, std::uint8_t const* data,
                     std::size_t size)
      {
         auto* b = tvgGwGetBridge();
         if (!b || !data || size == 0 || key.empty())
            return false;

         auto& gw = get_gw();
         std::lock_guard<std::mutex> lock(gw.mutex);
         if (gw.faces.find(key) != gw.faces.end())
            return true;   // already registered

         void* handle = b->openFace(b->ctx, reinterpret_cast<char const*>(data),
                                    std::uint32_t(size), 1 /*copy*/);
         if (!handle)
            return false;

         gw_face face;
         face.handle = handle;
         face.upem = b->unitsPerEm(b->ctx, handle);
         if (face.upem == 0) face.upem = 1000;
         face.ascent = std::abs(float(b->ascender(b->ctx, handle)));
         face.descent = std::abs(float(b->descender(b->ctx, handle)));
         face.height = float(b->height(b->ctx, handle));
         gw.faces[key] = face;
         return true;
      }

      gw_face const* find_face(std::string const& key)
      {
         auto& gw = get_gw();
         std::lock_guard<std::mutex> lock(gw.mutex);
         auto it = gw.faces.find(key);
         return (it != gw.faces.end()) ? &it->second : nullptr;
      }

      ////////////////////////////////////////////////////////////////////
      // UTF-8 byte count helper (mirror of glyph_layout_ft.cpp)
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

      ////////////////////////////////////////////////////////////////////
      // Shaped glyph collector for the bridge emit callback
      ////////////////////////////////////////////////////////////////////
      struct shaped_run
      {
         std::vector<TvgGwShapedGlyph> glyphs;
      };

      void shape_emit(void* ctx, TvgGwShapedGlyph const* g)
      {
         static_cast<shaped_run*>(ctx)->glyphs.push_back(*g);
      }
   }

   ////////////////////////////////////////////////////////////////////
   // Host-bridge glyph layout backend
   ////////////////////////////////////////////////////////////////////
   class gw_glyph_layout_backend : public glyph_layout_backend
   {
   public:
      ~gw_glyph_layout_backend() override = default;

      void layout(
         char const* first, char const* last,
         font const& f, float size,
         float x_offset,
         std::vector<char_pos>& positions,
         metrics& out_metrics) override
      {
         auto* b = tvgGwGetBridge();
         gw_face const* face = nullptr;
         if (b && !f.file().empty())
            face = find_face(f.file());

         if (!face)
         {
            out_metrics = {0, 0, 0};
            return;
         }

         // Metrics scaled from font units to the requested pixel size
         float units_scale = size / float(face->upem);
         out_metrics.ascent = face->ascent * units_scale;
         out_metrics.descent = face->descent * units_scale;
         float line_height = face->height * units_scale;
         out_metrics.leading = line_height - (out_metrics.ascent + out_metrics.descent);
         if (out_metrics.leading < 0) out_metrics.leading = 0;

         if (first == last)
            return;

         // Shape via the bridge (single run, same as the ft backend which
         // shapes with the one selected face). Advances come back in font
         // units and are scaled to pixels here.
         shaped_run run;
         b->shapeRun(b->ctx, face->handle, first, std::uint32_t(last - first),
                     nullptr, shape_emit, &run);

         // Build per-codepoint positions from the shaped output. Cluster
         // values are byte offsets in UTF-8 (mirror of glyph_layout_ft.cpp).
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

         // Map shaped glyphs to codepoint positions
         float cursor_x = x_offset;
         auto glyph_count = run.glyphs.size();
         for (std::size_t gi = 0; gi < glyph_count; ++gi)
         {
            float advance = run.glyphs[gi].xAdvance * units_scale;
            int cluster = int(run.glyphs[gi].cluster);

            int ci = -1;
            if (cluster >= 0 && cluster < int(byte_to_cp.size()))
               ci = byte_to_cp[cluster];

            if (ci >= 0 && ci < num_chars)
            {
               // Determine span: how many codepoints share this cluster
               int next_ci = num_chars;
               if (gi + 1 < glyph_count)
               {
                  int next_cluster = int(run.glyphs[gi + 1].cluster);
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
      }
   };

   ////////////////////////////////////////////////////////////////////
   // Host-bridge font backend
   ////////////////////////////////////////////////////////////////////
   class gw_font_backend : public font_backend
   {
   public:
      ~gw_font_backend() override = default;

      void initialize() override
      {
         // Nothing to do: the host engine is initialized by the host, and
         // the bridge is registered before fonts are loaded.
      }

      void register_font(std::string const& file_path) override
      {
         // Read the file and register it as a memory buffer (the ft backend
         // used FreeType's own file IO here).
         std::ifstream in(file_path, std::ios::binary);
         if (!in) return;
         std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
         open_face(file_path, bytes.data(), bytes.size());
      }

      void register_font_buffer(std::string const& key,
                                std::uint8_t const* data,
                                std::size_t size) override
      {
         open_face(key, data, size);
      }
   };

   ////////////////////////////////////////////////////////////////////
   // Global backend state (mirror of glyph_layout_ft.cpp; only one of the
   // two files is compiled per build, selected by TVG_LOADER_GW)
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
         b = std::make_shared<gw_glyph_layout_backend>();
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
         b = std::make_shared<gw_font_backend>();
      return b;
   }
}}

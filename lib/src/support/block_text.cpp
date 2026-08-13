/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   Built-in block text layout + the host backend seam. See block_text.hpp.
=============================================================================*/
#include <elements/support/block_text.hpp>
#include <elements/support/glyphs.hpp>
#include <elements/support/text_utils.hpp>

#include <algorithm>
#include <limits>

namespace cycfi::elements
{
   namespace
   {
      ////////////////////////////////////////////////////////////////////////
      // The built-in backend: Elements' own width-greedy wrap
      // (master_glyphs::break_lines), with codepoints standing in for
      // clusters. No script awareness and no 禁則 — a host that cares
      // installs its own backend.
      ////////////////////////////////////////////////////////////////////////

      // Byte offset `n` codepoints into [first, last).
      std::size_t advance_codepoints(char const* first, char const* last, int n)
      {
         unsigned state = 0, cp = 0;
         char const* i = first;
         while (i != last && n > 0)
         {
            if (!decode_utf8(state, cp, std::uint8_t(*i)))
               --n;
            ++i;
         }
         return std::size_t(i - first);
      }

      int count_codepoints(char const* first, char const* last)
      {
         unsigned state = 0, cp = 0;
         int n = 0;
         for (auto i = first; i != last; ++i)
            if (!decode_utf8(state, cp, std::uint8_t(*i)))
               ++n;
         return n;
      }

      class builtin_block_text_backend : public block_text_backend
      {
      public:

         bool layout(block_text_request const& req, block_text_result& out) override
         {
            out = block_text_result{};
            if (!req.fnt)
               return false;

            char const* base = req.text.data();
            char const* last = base + req.text.size();

            master_glyphs mg{base, last, *req.fnt, req.size};
            auto metrics = mg.metrics();
            out.ascent = metrics.ascent;
            out.descent = metrics.descent;
            out.line_height = metrics.ascent + metrics.descent + metrics.leading
                            + req.line_spacing;
            if (out.line_height < 1)
               out.line_height = 1;
            if (req.count == 0)
               return true;

            std::vector<glyphs> rows;
            mg.break_lines(
               req.width > 0 ? req.width : std::numeric_limits<float>::max(), rows);

            float const line_extent = metrics.ascent + metrics.descent;
            int remaining = req.count < 0 ? std::numeric_limits<int>::max() : req.count;
            int index = 0;

            for (auto& row : rows)
            {
               float const top = index * out.line_height;
               if (req.height > 0 && top + line_extent > req.height)
                  break;
               if (remaining <= 0)
                  break;

               block_text_line line;
               line.start = std::size_t(row.begin() - base);
               line.end = std::size_t(row.end() - base);
               line.reveal_end = line.end;
               line.y = top + metrics.ascent;
               line.width = row.width();
               line.total_clusters = count_codepoints(row.begin(), row.end());
               line.clusters = (std::min)(line.total_clusters, remaining);
               if (line.clusters < line.total_clusters)
                  line.reveal_end = line.start
                     + advance_codepoints(row.begin(), row.end(), line.clusters);
               remaining -= line.clusters;
               out.drawn_clusters += line.clusters;
               out.total_clusters += line.total_clusters;
               out.width = (std::max)(out.width, line.width);

               if (req.align == block_text_request::align_center)
                  line.x = (req.width - line.width) / 2;
               else if (req.align == block_text_request::align_right)
                  line.x = req.width - line.width;

               out.lines.push_back(line);
               ++index;
            }

            out.height = index > 0 ? (index - 1) * out.line_height + line_extent : 0;
            return true;
         }

         int count_clusters(block_text_request const& req) override
         {
            char const* base = req.text.data();
            return count_codepoints(base, base + req.text.size());
         }
      };

      block_text_backend*& host_backend()
      {
         static block_text_backend* backend = nullptr;
         return backend;
      }
   }

   void set_block_text_backend(block_text_backend* backend)
   {
      host_backend() = backend;
   }

   block_text_backend& get_block_text_backend()
   {
      if (auto* b = host_backend())
         return *b;
      static builtin_block_text_backend builtin;
      return builtin;
   }

   bool has_host_block_text_backend()
   {
      return host_backend() != nullptr;
   }
}

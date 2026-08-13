/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   Block text layout — flowing a paragraph into a rectangle.

   Elements' own line breaking (`master_glyphs::break_lines`) is a simple
   width-greedy word wrap. A host that already owns a full text engine usually
   has a better one (script-aware breaking, Japanese 行頭/行末禁則, cluster
   counting for typewriter reveals) *and* needs the two to agree: the same
   string drawn by the host and by an Elements widget should break at the same
   places.

   So block layout goes through an injectable backend. The host registers its
   own implementation with `set_block_text_backend()`; without one, a built-in
   fallback uses Elements' own wrapping so the widget always works.

   Byte offsets in the result index the request's `text`.
=============================================================================*/
#if !defined(ELEMENTS_BLOCK_TEXT_AUGUST_13_2026)
#define ELEMENTS_BLOCK_TEXT_AUGUST_13_2026

#include <elements/support/font.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // One laid-out line.
   //
   // `[start, end)` is the line's text after trailing spaces are trimmed;
   // `[start, reveal_end)` is the part a `count` limit leaves visible
   // (== `end` when nothing is hidden).
   ////////////////////////////////////////////////////////////////////////////
   struct block_text_line
   {
      std::size_t    start = 0;
      std::size_t    end = 0;
      std::size_t    reveal_end = 0;
      float          x = 0;          // alignment offset from the block's left
      float          y = 0;          // baseline offset from the block's top
      float          width = 0;      // full line width, ignoring `count`
      int            clusters = 0;   // clusters revealed on this line
      int            total_clusters = 0;
   };

   ////////////////////////////////////////////////////////////////////////////
   struct block_text_request
   {
      enum alignment { align_left = 0, align_center = 1, align_right = 2 };
      enum direction { dir_auto = 0, dir_ltr = 1, dir_rtl = 2 };

      std::string_view  text;
      std::string       font_key;        // host font key (font::file())
      std::string       font_family;     // for backends that key by family
      font const*       fnt = nullptr;   // the widget's font (built-in backend)
      float             size = 12;       // px
      float             width = 0;       // wrap width; <= 0 = break at newlines
      float             height = 0;      // <= 0 = unbounded
      float             line_spacing = 0;
      int               align = align_left;
      int               base = dir_auto;
      int               count = -1;      // reveal limit in clusters; < 0 = all
   };

   ////////////////////////////////////////////////////////////////////////////
   struct block_text_result
   {
      std::vector<block_text_line>  lines;
      float          width = 0;         // widest line
      float          height = 0;        // top of the first to bottom of the last
      float          line_height = 0;   // baseline-to-baseline pitch
      float          ascent = 0;
      float          descent = 0;
      int            drawn_clusters = 0;
      int            total_clusters = 0;   // clusters of the laid-out lines
   };

   ////////////////////////////////////////////////////////////////////////////
   class block_text_backend
   {
   public:

      virtual              ~block_text_backend() = default;

      // Flow `req.text` into the rectangle. Returns false when the request
      // cannot be served (e.g. the font key does not resolve), in which case
      // the caller falls back to the built-in wrapping.
      virtual bool         layout(block_text_request const& req, block_text_result& out) = 0;

      // Total clusters of the unwrapped text — the number a `count` reveal
      // counts up to. May exceed `block_text_result::total_clusters`, since
      // spaces swallowed at a wrap point are never laid out.
      virtual int          count_clusters(block_text_request const& req) = 0;
   };

   // Install the host's backend (pass nullptr to go back to the built-in one).
   // The caller keeps ownership; it must outlive every widget that uses it.
   void                    set_block_text_backend(block_text_backend* backend);

   // Never null: the built-in fallback when no host backend is installed.
   block_text_backend&     get_block_text_backend();

   // True when a host backend is installed.
   bool                    has_host_block_text_backend();
}

#endif

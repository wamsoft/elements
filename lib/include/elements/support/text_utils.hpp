/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_TEXT_UTILS_MAY_22_2016)
#define ELEMENTS_TEXT_UTILS_MAY_22_2016

#include <elements/support/canvas.hpp>
#include <elements/support/font.hpp>
#include <string>
#include <cctype>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   void           draw_icon(canvas& cnv, rect bounds, uint32_t code, float size);
   void           draw_icon(canvas& cnv, rect bounds, uint32_t code, float size, color c);
   point          measure_icon(canvas& cnv, uint32_t cp, float size);

                  [[deprecated("Use measure_text(cnv, text, descr) instead.")]]
   point          measure_text(canvas& cnv, char const* text, font const& font_, float size);
   point          measure_text(canvas& cnv, std::string_view text, font_descr descr);
   std::string    codepoint_to_utf8(unsigned codepoint);
   bool           is_space(unsigned codepoint);
   bool           is_newline(unsigned codepoint);
   bool           is_punctuation(unsigned codepoint);
   bool           is_cjk(unsigned codepoint);
   bool           forbidden_at_line_start(unsigned codepoint);
   bool           forbidden_at_line_end(unsigned codepoint);
   bool           can_break_between(unsigned prev, unsigned next);
   unsigned       decode_utf8(unsigned& state, unsigned& codepoint, unsigned byte);
   char const*    next_utf8(char const* last, char const* utf8);
   char const*    prev_utf8(char const* start, char const* utf8);
   unsigned       codepoint(char const*& utf8);

   ////////////////////////////////////////////////////////////////////////////
   inline bool is_space(unsigned codepoint)
   {
      switch (codepoint)
      {
         case 9:        // \t
         case 11:       // \v
         case 12:       // \f
         case 32:       // space
         case 10:		   // \n
         case 13:		   // \r
         case 0xA0:     // NBSP
            return true;
         default:
            return false;
      }
   }

   // Check if codepoint is a new line
   inline bool is_newline(unsigned codepoint)
   {
      switch (codepoint)
      {
         case 10:		   // \n
         case 13:		   // \r
         case 0x85:	   // NEL
            return true;
         default:
            return false;
      }
   }

   // CJK and friends: scripts written without spaces, where a line may break
   // between almost any two characters. Covers Hiragana, Katakana (incl.
   // halfwidth), CJK ideographs (incl. extension A and compatibility), CJK
   // symbols/punctuation, Hangul, and fullwidth forms.
   inline bool is_cjk(unsigned cp)
   {
      return (cp >= 0x1100 && cp <= 0x11FF)     // Hangul Jamo
         ||  (cp >= 0x2E80 && cp <= 0x2FFF)     // CJK radicals / Kangxi
         ||  (cp >= 0x3000 && cp <= 0x303F)     // CJK symbols and punctuation
         ||  (cp >= 0x3040 && cp <= 0x30FF)     // Hiragana, Katakana
         ||  (cp >= 0x3130 && cp <= 0x318F)     // Hangul compatibility Jamo
         ||  (cp >= 0x3400 && cp <= 0x4DBF)     // CJK extension A
         ||  (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK unified ideographs
         ||  (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul syllables
         ||  (cp >= 0xF900 && cp <= 0xFAFF)     // CJK compatibility ideographs
         ||  (cp >= 0xFF00 && cp <= 0xFF60)     // fullwidth forms
         ||  (cp >= 0xFF61 && cp <= 0xFF9F)     // halfwidth Katakana
         ||  (cp >= 0x20000 && cp <= 0x2FA1F)   // CJK extensions B..F
         ;
   }

   // 行頭禁則 — characters that may not begin a line: closing brackets,
   // sentence-final punctuation, small kana, the prolonged sound mark, and
   // the usual ASCII closers/enders.
   inline bool forbidden_at_line_start(unsigned cp)
   {
      switch (cp)
      {
         // ASCII / halfwidth
         case ')': case ']': case '}': case ',': case '.':
         case '!': case '?': case ':': case ';':
            return true;
         // CJK punctuation and brackets
         case 0x3001:      // 、
         case 0x3002:      // 。
         case 0x3005:      // 々
         case 0x3009: case 0x300B: case 0x300D: case 0x300F:  // 〉》」』
         case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B:
         case 0x301C:      // 〜
         case 0x301F:
         case 0x30FB:      // ・
         case 0x30FC:      // ー
         case 0xFF01:      // ！
         case 0xFF09:      // ）
         case 0xFF0C:      // ，
         case 0xFF0E:      // ．
         case 0xFF1A:      // ：
         case 0xFF1B:      // ；
         case 0xFF1F:      // ？
         case 0xFF3D:      // ］
         case 0xFF5D:      // ｝
         case 0xFF60:      // ｠
         case 0xFF61:      // ｡
         case 0xFF63:      // ｣
         case 0xFF64:      // ､
         case 0x2019: case 0x201D:                            // ’ ”
         case 0x2026: case 0x2015:                            // … ―
            return true;
         default:
            break;
      }
      // small kana (ぁぃぅぇぉっゃゅょゎゕゖ / ァィゥェォッャュョヮヵヶ)
      switch (cp)
      {
         case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
         case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E:
         case 0x3095: case 0x3096:
         case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
         case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
         case 0x30F5: case 0x30F6:
         case 0xFF67: case 0xFF68: case 0xFF69: case 0xFF6A: case 0xFF6B:
         case 0xFF6C: case 0xFF6D: case 0xFF6E: case 0xFF6F: case 0xFF70:
            return true;
         default:
            return false;
      }
   }

   // 行末禁則 — characters that may not end a line: opening brackets and
   // leading marks.
   inline bool forbidden_at_line_end(unsigned cp)
   {
      switch (cp)
      {
         case '(': case '[': case '{':
         case 0x3008: case 0x300A: case 0x300C: case 0x300E:  // 〈《「『
         case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301A:
         case 0x301D:
         case 0xFF08:      // （
         case 0xFF3B:      // ［
         case 0xFF5B:      // ｛
         case 0xFF5F:      // ｟
         case 0xFF62:      // ｢
         case 0x2018: case 0x201C:                            // ‘ “
            return true;
         default:
            return false;
      }
   }

   // May a line break fall between these two adjacent codepoints? Only asked
   // about scripts that have no spaces: for Latin the answer is always false
   // and the caller falls back to breaking at spaces.
   inline bool can_break_between(unsigned prev, unsigned next)
   {
      if (!is_cjk(prev) && !is_cjk(next))
         return false;
      if (forbidden_at_line_end(prev) || forbidden_at_line_start(next))
         return false;
      // A digit/letter pair inside a Latin word must stay together even when
      // it sits next to CJK (a version number, a name mid-sentence).
      if (!is_cjk(prev) && !is_cjk(next))
         return false;
      return true;
   }

   // Check if codepoint is a punctuation
   inline bool is_punctuation(unsigned codepoint)
   {
      return (codepoint < 0x80 && std::ispunct(codepoint))
         || (codepoint >= 0xA0 && codepoint <= 0xBF)
         || (codepoint >= 0x2000 && codepoint <= 0x206F)
         ;
   }

   ////////////////////////////////////////////////////////////////////////////
   // Decoding UTF8
   //
   // Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
   // See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
   ////////////////////////////////////////////////////////////////////////////

   enum
   {
      utf8_accept = 0,
      utf8_reject = 12
   };

   inline unsigned decode_utf8(unsigned& state, unsigned& codepoint, uint8_t byte)
   {
      static constexpr uint8_t utf8d[] =
      {
         // The first part of the table maps bytes to character classes that
         // to reduce the size of the transition table and create bitmasks.
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,       9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
         7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,       7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
         8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,       2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
         10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3,      11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

         // The second part is a transition table that maps a combination
         // of a state of the automaton and a character class to a state.
         0,12,24,36,60,96,84,12,12,12,48,72,    12,12,12,12,12,12,12,12,12,12,12,12,
         12, 0,12,12,12,12,12, 0,12, 0,12,12,   12,24,12,12,12,12,12,24,12,24,12,12,
         12,12,12,12,12,12,12,24,12,12,12,12,   12,24,12,12,12,12,12,12,12,24,12,12,
         12,12,12,12,12,12,12,36,12,36,12,12,   12,36,12,12,12,12,12,36,12,36,12,12,
         12,36,12,12,12,12,12,12,12,12,12,12,
      };

      unsigned type = utf8d[byte];

      codepoint = (state != utf8_accept) ?
         (byte & 0x3fu) | (codepoint << 6) :
         (0xff >> type) & (byte)
         ;

      state = utf8d[256 + state + type];
      return state;
   }

   ////////////////////////////////////////////////////////////////////////////
   // UTF8 Iteration. See A code point iterator adapter for C++ strings in
   // UTF-8 by Ángel José Riesgo: http://www.nubaria.com/en/blog/?p=371
   ////////////////////////////////////////////////////////////////////////////
   struct utf8_mask
   {
      static uint8_t const first    = 128;   // 1000000
      static uint8_t const second   = 64;    // 0100000
      static uint8_t const third    = 32;    // 0010000
      static uint8_t const fourth   = 16;    // 0001000
   };

   inline char const* next_utf8(char const* last, char const* utf8)
   {
      char c = *utf8;
      std::size_t offset = 1;

      if (c & utf8_mask::first)
         offset =
            (c & utf8_mask::third)?
               ((c & utf8_mask::fourth)? 4 : 3) : 2
         ;

      utf8 += offset;
      if (utf8 > last)
         utf8 = last;
      return utf8;
   }

   inline char const* prev_utf8(char const* start, char const* utf8)
   {
      if (*--utf8 & utf8_mask::first)
      {
         if ((*--utf8 & utf8_mask::second) == 0)
         {
            if ((*--utf8 & utf8_mask::second) == 0)
               --utf8;
         }
      }
      if (utf8 < start)
         utf8 = start;
      return utf8;
   }

   ////////////////////////////////////////////////////////////////////////////
   // Extracting codepoints from UTF8
   ////////////////////////////////////////////////////////////////////////////
   inline unsigned codepoint(char const*& utf8)
   {
      unsigned state = 0;
      unsigned cp;
      while (decode_utf8(state, cp, uint8_t(*utf8)))
         utf8++;
      ++utf8; // one past the last byte
      return cp;
   }

   ////////////////////////////////////////////////////////////////////////////
   // Helper for converting char8_t[] string literals to char[]
   ////////////////////////////////////////////////////////////////////////////

#if __cplusplus >= 202002L // C++20
   // see https://developercommunity.visualstudio.com/t/C20-String-literal-operator-template-u/1318552
#ifdef __INTELLISENSE__
   consteval const char* operator""_as_char([[maybe_unused]] const char8_t*, [[maybe_unused]] std::size_t)
   {
      return "";
   }
#else
   template<std::size_t N>
   struct char8_t_string_literal
   {
      char8_t buffer[N];
      static constexpr inline std::size_t size = N;

      template<std::size_t... I>
      constexpr char8_t_string_literal(const char8_t(&r)[N], std::index_sequence<I...>)
         : buffer{r[I]...} {}

      constexpr char8_t_string_literal(const char8_t(&r)[N])
         : char8_t_string_literal(r, std::make_index_sequence<N>()) {}
   };

   template<char8_t_string_literal L, std::size_t... I>
   constexpr inline const char as_char_buffer[sizeof...(I)] = {static_cast<char>(L.buffer[I])...};

   template<char8_t_string_literal L, std::size_t... I>
   constexpr auto& make_as_char_buffer(std::index_sequence<I...>)
   {
      return as_char_buffer<L, I...>;
   }

   constexpr char operator ""_as_char(char8_t c)
   {
      return c;
   }

   template<char8_t_string_literal L>
   constexpr auto& operator""_as_char()
   {
      return make_as_char_buffer<L>(std::make_index_sequence<decltype(L)::size>());
   }
#endif
#else
   constexpr const char* operator""_as_char(const char* src, std::size_t)
   {
      return src;
   }
#endif

}}

#endif

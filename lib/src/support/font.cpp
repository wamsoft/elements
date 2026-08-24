/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman
   Copyright (c) 2020 Michał Urbański

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/font.hpp>
#include <elements/support/glyph_utils.hpp>
#include <elements/support/resource_loader.hpp>
#include <infra/assert.hpp>
#include <infra/filesystem.hpp>

#include <thorvg.h>


#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace cycfi { namespace elements
{
   namespace
   {
      inline void ltrim(std::string& s)
      {
         s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](int ch) { return ch != ' ' && ch != '"'; }
         ));
      }

      inline void rtrim(std::string& s)
      {
         s.erase(std::find_if(s.rbegin(), s.rend(),
            [](int ch) { return ch != ' ' && ch != '"'; }
         ).base(), s.end());
      }

      inline void trim(std::string& s)
      {
         ltrim(s);
         rtrim(s);
      }

      ////////////////////////////////////////////////////////////////////////
      // Font registry — manually populated via register_font()
      ////////////////////////////////////////////////////////////////////////
      struct font_entry
      {
         std::string    file;
         std::uint8_t   weight;
         std::uint8_t   slant;
         std::uint8_t   stretch;
         bool           variable = false;   // fvar を持つ (可変フォント)
      };

      // sfnt テーブルディレクトリに 'fvar' があるか (可変フォント判定)。
      // 同じファミリが static 版と VF 版の両方で登録されたとき、 "#tag=val"
      // サフィックス付き解決で VF 側を選ぶために登録時へ記録しておく。
      bool sniff_fvar(std::uint8_t const* data, std::size_t size)
      {
         auto u32 = [&](std::size_t off) -> std::uint32_t {
            return (std::uint32_t(data[off]) << 24) | (std::uint32_t(data[off + 1]) << 16)
                 | (std::uint32_t(data[off + 2]) << 8) | std::uint32_t(data[off + 3]);
         };
         auto u16 = [&](std::size_t off) -> std::uint32_t {
            return (std::uint32_t(data[off]) << 8) | std::uint32_t(data[off + 1]);
         };
         if (!data || size < 12)
            return false;
         std::size_t base = 0;
         auto tag = u32(0);
         if (tag == 0x74746366)                 // 'ttcf': 先頭フォントで判定
         {
            if (size < 16) return false;
            base = u32(12);
            if (base + 12 > size) return false;
            tag = u32(base);
         }
         if (tag != 0x00010000 && tag != 0x4F54544F && tag != 0x74727565)  // sfnt/OTTO/true
            return false;
         auto num = u16(base + 4);
         for (std::uint32_t i = 0; i < num; ++i)
         {
            auto rec = base + 12 + 16 * std::size_t(i);
            if (rec + 16 > size) return false;
            if (u32(rec) == 0x66766172)         // 'fvar'
               return true;
         }
         return false;
      }

      using font_map_type = std::map<std::string, std::vector<font_entry>>;

      font_map_type& font_map()
      {
         static font_map_type map_;
         return map_;
      }

      std::mutex& font_map_mutex()
      {
         static std::mutex mtx_;
         return mtx_;
      }

      ////////////////////////////////////////////////////////////////////////
      // Variable-font instance suffix
      //
      // A family token may address a variable-font instance as
      // "Family#tag=val[,tag=val...]". The base family is looked up in the
      // font map as usual; the suffix is re-appended to the resolved FILE so
      // the render/measure backends (ThorVG loaders, glyph layout) apply the
      // axes and register the instance as a distinct font.
      ////////////////////////////////////////////////////////////////////////
      std::string split_variation_suffix(std::string family, std::string& out_suffix)
      {
         auto pos = family.find('#');
         if (pos == std::string::npos)
         {
            out_suffix.clear();
            return family;
         }
         out_suffix = family.substr(pos);   // keeps the '#'
         family.erase(pos);
         trim(family);
         return family;
      }

      struct match_result
      {
         font_entry const* entry = nullptr;
         std::string family;    // matched base family (without suffix)
         std::string suffix;    // "#tag=val,..." or empty
      };

      // "wght=700" 形状 (1..4 文字の軸タグ + '=' + 数値) はファミリ名ではなく
      // 可変フォント軸指定 — families のカンマ分割で multi-axis suffix
      // ("Family#wght=700,wdth=75") が千切れたときの復元判定に使う。
      bool is_axis_token(std::string const& s)
      {
         auto eq = s.find('=');
         if (eq == std::string::npos || eq < 1 || eq > 4)
            return false;
         char* end = nullptr;
         std::strtod(s.c_str() + eq + 1, &end);
         return end && *end == '\0' && end != s.c_str() + eq + 1;
      }

      // 既定可変軸 ("tag=val,...")。set_default_variations() で family 単位に
      // 登録し、フォント参照が軸を指定しないとき補われる (suffix が常に勝つ)。
      std::map<std::string, std::string>& default_variations_map()
      {
         static std::map<std::string, std::string> map_;
         return map_;
      }

      // suffix ("#a=1,b=2" or 空) が軸タグを含むか ('#'/',' 直後の "tag=")。
      bool suffix_has_tag(std::string const& suffix, std::string const& tag)
      {
         std::string needle = tag + "=";
         for (std::size_t pos = 0;
              (pos = suffix.find(needle, pos)) != std::string::npos; ++pos)
         {
            if (pos > 0 && (suffix[pos - 1] == '#' || suffix[pos - 1] == ','))
               return true;
         }
         return false;
      }

      // suffix に、まだ含まれない軸だけ extra ("tag=val,...") を補う。
      std::string merge_suffix_axes(std::string suffix, std::string const& extra)
      {
         std::istringstream str(extra);
         std::string tok;
         while (std::getline(str, tok, ','))
         {
            trim(tok);
            auto eq = tok.find('=');
            if (eq == std::string::npos || eq == 0)
               continue;
            if (suffix_has_tag(suffix, tok.substr(0, eq)))
               continue;
            suffix += (suffix.empty() ? "#" : ",") + tok;
         }
         return suffix;
      }

      match_result match_ex(font_descr descr)
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());

         // families をカンマで分割。 ただし軸トークンは直前の '#' 付き
         // トークンの suffix の続きなので連結し直す。
         std::vector<std::string> tokens;
         {
            std::istringstream str(std::string{descr._families});
            std::string piece;
            while (getline(str, piece, ','))
            {
               trim(piece);
               if (!tokens.empty()
                   && tokens.back().find('#') != std::string::npos
                   && is_axis_token(piece))
                  tokens.back() += "," + piece;
               else
                  tokens.push_back(std::move(piece));
            }
         }

         for (auto& family : tokens)
         {
            std::string suffix;
            auto base = split_variation_suffix(std::move(family), suffix);
            if (auto i = font_map().find(base); i != font_map().end())
            {
               // "#tag=val" サフィックス付きは可変フォントのエントリを優先する
               // (同じファミリで static 版と VF 版が両方登録されているとき、
               //  static 側を掴むと軸が黙って効かないため)。
               bool const want_var = !suffix.empty() &&
                  std::any_of(i->second.begin(), i->second.end(),
                              [](font_entry const& e) { return e.variable; });
               int min_diff = 10000;
               font_entry const* best = nullptr;
               for (auto const& entry : i->second)
               {
                  if (want_var && !entry.variable)
                     continue;
                  // Biased score: slant (3.0) > weight (1.0) > stretch (0.25)
                  auto diff =
                     (std::abs(int(descr._weight) - int(entry.weight)) * 1.0) +
                     (std::abs(int(descr._slant) - int(entry.slant)) * 3.0) +
                     (std::abs(int(descr._stretch) - int(entry.stretch)) * 0.25)
                     ;
                  if (diff < min_diff)
                  {
                     min_diff = diff;
                     best = &entry;
                  }
               }
               if (best)
                  return { best, std::move(base), std::move(suffix) };
            }
         }
         return {};
      }

      font_entry const* match(font_descr descr)
      {
         return match_ex(descr).entry;
      }

      std::string find_matched_family(font_descr descr)
      {
         auto m = match_ex(descr);
         if (!m.entry)
            return {};
         return m.suffix.empty() ? m.family : m.family + m.suffix;
      }

      ////////////////////////////////////////////////////////////////////////
      // Derive the ThorVG font name from a file path or buffer key by
      // stripping the directory and extension. Matches the convention used
      // by text_backend_tvg (stem_from_path).
      ////////////////////////////////////////////////////////////////////////
      std::string stem_from_path(std::string const& path)
      {
         auto slash = path.find_last_of("/\\");
         auto start = (slash != std::string::npos) ? slash + 1 : 0;
         auto dot = path.rfind('.');
         auto end = (dot != std::string::npos && dot > start) ? dot : path.size();
         return path.substr(start, end - start);
      }
   }

   namespace
   {
      // Query the font that was just registered into ThorVG and return its
      // embedded family name (from the OTF/TTF name table). Empty if ThorVG
      // could not extract a name. Helper for both register_font variants.
      std::string query_embedded_family(std::string const& thorvg_key)
      {
         tvg::TextInfo tinfo{};
         if (tvg::Text::info(thorvg_key.c_str(), tinfo) == tvg::Result::Success
             && tinfo.family && *tinfo.family)
         {
            return tinfo.family;
         }
         return {};
      }

      // Add an alias entry to font_map under `family_alias`, pointing to the
      // same on-disk file/key as the original registration. Caller-side font
      // matching (label_font etc.) can then resolve either the caller-supplied
      // family name or the embedded one.
      void add_family_alias(std::string const& family_alias,
         std::string const& file_key,
         font_constants::weight_enum weight,
         font_constants::slant_enum slant,
         font_constants::stretch_enum stretch,
         bool variable = false)
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file_key;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         entry.variable = variable;
         font_map()[family_alias].push_back(std::move(entry));
      }
   }

   ////////////////////////////////////////////////////////////////////////////
   // register_font — public API
   //
   // Reads the font bytes through the active resource_loader and registers
   // the result via the in-memory path. This keeps a single code path for
   // font lifetime: the buffer is owned by the FT backend and by ThorVG
   // (via copy=true), and the loader does not need to keep the bytes
   // around.
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font(
      std::string const&                family,
      std::string const&                file,
      font_constants::weight_enum       weight,
      font_constants::slant_enum        slant,
      font_constants::stretch_enum      stretch)
   {
#ifdef ELEMENTS_TVG_GW
      // gw ローダビルド: `file` はホストキー。バイトをここで読まず、ThorVG の
      // path 版 Text::load (gw ローダがブリッジ openFaceByKey でホストの共有
      // バッファから開く) と glyph backend にキーをそのまま渡す (ゼロコピー)。
      // 計測側 (glyph_layout_gw) を先に開き、fvar の有無 (可変フォントか) を
      // ブリッジ経由で教えてもらう — "#tag=val" 解決の VF 優先に使う。
      get_font_backend()->initialize();
      get_font_backend()->register_font(file);
      auto variable = get_font_backend()->is_variable(file);

      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         entry.variable = variable;
         font_map()[family].push_back(std::move(entry));
      }

      if (tvg::Text::load(file.c_str()) != tvg::Result::Success)
         return {};

      auto thorvg_name = stem_from_path(file);
      auto embedded = query_embedded_family(thorvg_name);
      if (!embedded.empty() && embedded != family)
         add_family_alias(embedded, file, weight, slant, stretch, variable);
      if (thorvg_name != family)
         add_family_alias(thorvg_name, file, weight, slant, stretch, variable);

      return embedded;
#else
      auto bytes = get_resource_loader().read(file);
      if (bytes.empty())
         return {};

      auto variable = sniff_fvar(bytes.data(), bytes.size());

      // Register in internal font map. The file path stays the entry key
      // so canvas state and glyph layout (which keys FT_Face by f.file())
      // can find the registered face.
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = file;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         entry.variable = variable;
         font_map()[family].push_back(std::move(entry));
      }

      // ThorVG indexes fonts by name. text_backend_tvg derives that name
      // by stripping the directory and extension from canvas state's
      // font_file, so register the same stem here.
      auto thorvg_name = stem_from_path(file);
      tvg::Text::load(
         thorvg_name.c_str(),
         reinterpret_cast<const char*>(bytes.data()),
         static_cast<uint32_t>(bytes.size()),
         "ttf",
         /*copy=*/true);

      // Ask ThorVG for the font's embedded family name. If it's different
      // from the caller-supplied family, also expose it as a font_map alias
      // so lookups by either name resolve.
      auto embedded = query_embedded_family(thorvg_name);
      if (!embedded.empty() && embedded != family)
         add_family_alias(embedded, file, weight, slant, stretch, variable);

      // ファイル名 stem (= PSD の PostScript 名 "NotoSansJP-Regular" 等と一致し
      // やすい) でも font_descr から引けるよう alias 登録する。
      if (thorvg_name != family)
         add_family_alias(thorvg_name, file, weight, slant, stretch, variable);

      // FreeType side uses the original file string as the cache key, to
      // match glyph_layout_ft.cpp's get_face(f.file()) lookup.
      get_font_backend()->initialize();
      get_font_backend()->register_font_buffer(file, bytes.data(), bytes.size());

      return embedded;
#endif // ELEMENTS_TVG_GW
   }

   ////////////////////////////////////////////////////////////////////////////
   // register_font_buffer — public API (in-memory font registration)
   ////////////////////////////////////////////////////////////////////////////
   std::string register_font_buffer(
      std::string const&                family,
      std::string const&                key,
      std::uint8_t const*               data,
      std::size_t                       size,
      font_constants::weight_enum       weight,
      font_constants::slant_enum        slant,
      font_constants::stretch_enum      stretch)
   {
      if (!data || size == 0 || key.empty())
         return {};

      auto variable = sniff_fvar(data, size);

      // Register in internal font map. `key` plays the role normally taken
      // by the file path, so canvas/text rendering will use it as identifier.
      {
         std::lock_guard<std::mutex> lock(font_map_mutex());
         font_entry entry;
         entry.file = key;
         entry.weight = uint8_t(weight);
         entry.slant = uint8_t(slant);
         entry.stretch = uint8_t(stretch);
         entry.variable = variable;
         font_map()[family].push_back(std::move(entry));
      }

      // ThorVG indexes fonts by name; text_backend_tvg derives that name via
      // stem_from_path(canvas font_file) at draw time. So register with ThorVG
      // under the *stem* of `key` (same convention as the path-based
      // register_font). Registering under the raw `key` would mismatch the
      // lookup (stem_from_path(key) != key when key has a dir/extension) and
      // glyphs would not render even though FreeType layout (keyed by the raw
      // file() == key) still positions text.
      auto thorvg_name = stem_from_path(key);
      tvg::Text::load(
         thorvg_name.c_str(),
         reinterpret_cast<const char*>(data),
         static_cast<uint32_t>(size),
         "ttf",
         /*copy=*/true);

      // Ask ThorVG for the embedded family name. If different from the
      // caller-supplied family, also expose it as a font_map alias. The alias
      // keeps entry.file == key so the FreeType backend lookup stays consistent.
      auto embedded = query_embedded_family(thorvg_name);
      if (!embedded.empty() && embedded != family)
         add_family_alias(embedded, key, weight, slant, stretch, variable);

      // stem 名でも font_descr から引けるよう alias (register_font と同様)。
      if (thorvg_name != family)
         add_family_alias(thorvg_name, key, weight, slant, stretch, variable);

      // Register with the active font backend (FreeType etc.). The backend
      // takes its own copy of the buffer. FreeType side uses the raw `key` as
      // the cache key, to match glyph_layout's get_face(f.file() == key) lookup.
      get_font_backend()->initialize();
      get_font_backend()->register_font_buffer(key, data, size);

      return embedded;
   }

   ////////////////////////////////////////////////////////////////////////////
   // font
   ////////////////////////////////////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////////////
   // Language-aware font family substitution — state and helpers
   // (public setters/getters are at the end of this file)
   ////////////////////////////////////////////////////////////////////////////
   namespace
   {
      struct font_language_state
      {
         font_language_table  table;
         std::string          current;
         std::mutex           mutex;
      };

      font_language_state& font_lang_state()
      {
         static font_language_state state;
         return state;
      }

      font_language_entry const* find_lang_entry_nolock(
         font_language_table const& table, std::string const& lang)
      {
         for (auto const& [key, entry] : table)
            if (key == lang)
               return &entry;
         return nullptr;
      }

      // One reference token: substitute the family part, keep any "#..."
      // variation suffix of the reference.
      std::string substitute_token_nolock(
         font_language_entry const& entry, std::string const& token)
      {
         auto hash = token.find('#');
         std::string base =
            (hash == std::string::npos) ? token : token.substr(0, hash);
         std::string suffix =
            (hash == std::string::npos) ? std::string{} : token.substr(hash);
         rtrim(base);
         for (auto const& [from, to] : entry.map)
            if (from == base)
               return suffix.empty() ? to : to + suffix;
         return token;
      }

      // Substitute every family token of a comma-separated families list
      // for the effective language (descr._lang if set, else current).
      // Returns false when nothing applies (out untouched) so the caller
      // can keep using the original string_view without a copy.
      bool substitute_families(string_view families, string_view lang,
                               std::string& out)
      {
         auto& st = font_lang_state();
         std::lock_guard<std::mutex> lock(st.mutex);
         std::string const use =
            !lang.empty() ? std::string{lang} : st.current;
         if (use.empty() || st.table.empty())
            return false;
         auto const* entry = find_lang_entry_nolock(st.table, use);
         if (!entry || entry->map.empty())
            return false;

         // Same tokenization as match_ex: split on ',' but re-join axis
         // tokens that continue a preceding "#tag=val" suffix.
         std::vector<std::string> tokens;
         {
            std::istringstream str(std::string{families});
            std::string piece;
            while (std::getline(str, piece, ','))
            {
               trim(piece);
               if (!tokens.empty()
                   && tokens.back().find('#') != std::string::npos
                   && is_axis_token(piece))
                  tokens.back() += "," + piece;
               else
                  tokens.push_back(std::move(piece));
            }
         }
         bool changed = false;
         out.clear();
         for (auto const& tok : tokens)
         {
            auto sub = substitute_token_nolock(*entry, tok);
            if (sub != tok)
               changed = true;
            if (!out.empty())
               out += ", ";
            out += sub;
         }
         return changed;
      }
   }

   font::font(font_descr descr)
   {
      // Language-aware family substitution: rewrite the families list for
      // the effective language before matching. The substituted string only
      // needs to outlive match_ex (descr._families is a view into it).
      std::string substituted;
      if (substitute_families(descr._families, descr._lang, substituted))
         descr._families = substituted;
      auto m = match_ex(descr);
      if (m.entry)
      {
#ifndef ELEMENTS_TVG_GW
         // 無指定軸の既定 (FT ビルド): set_default_variations の登録軸 →
         // それでも wght が無ければ wght=400 (CSS の font-weight 既定に合わせ、
         // fvar 既定が Regular でない VF (Noto VF=Thin 等) も無指定で Regular
         // 相当に読めるようにする)。明示 suffix が常にタグ単位で勝つ。wght 軸を
         // 持たない face への wght=400 は各バックエンドが無視する。
         // gw ビルドは正規化も既定もホスト (エンジン) 側で行う。
         if (m.entry->variable)
         {
            {
               std::lock_guard<std::mutex> lock(font_map_mutex());
               auto it = default_variations_map().find(m.family);
               if (it != default_variations_map().end())
                  m.suffix = merge_suffix_axes(std::move(m.suffix), it->second);
            }
            m.suffix = merge_suffix_axes(std::move(m.suffix), "wght=400");
         }
#endif
         // A variation suffix travels on the FILE: the ThorVG loaders and the
         // glyph layout backend split it off, apply the axes, and register
         // the instance under a suffix-aware name.
         _file = m.entry->file + m.suffix;
         _family = m.suffix.empty() ? m.family : m.family + m.suffix;
      }
      _size = descr._size;
   }

   font::font(font const& rhs)
    : _family(rhs._family)
    , _file(rhs._file)
    , _size(rhs._size)
   {
   }

   font& font::operator=(font const& rhs)
   {
      if (&rhs != this)
      {
         _family = rhs._family;
         _file = rhs._file;
         _size = rhs._size;
      }
      return *this;
   }

   font::font(font&& rhs) noexcept
    : _family(std::move(rhs._family))
    , _file(std::move(rhs._file))
    , _size(rhs._size)
   {
   }

   font& font::operator=(font&& rhs) noexcept
   {
      if (&rhs != this)
      {
         _family = std::move(rhs._family);
         _file = std::move(rhs._file);
         _size = rhs._size;
      }
      return *this;
   }

   font::~font()
   {
   }

   ////////////////////////////////////////////////////////////////////////////
   // load_fonts_from_directory — scan and auto-register fonts
   ////////////////////////////////////////////////////////////////////////////
   namespace
   {
      struct font_file_info
      {
         std::string family;
         font_constants::weight_enum weight = font_constants::weight_normal;
         font_constants::slant_enum slant = font_constants::slant_normal;
         font_constants::stretch_enum stretch = font_constants::stretch_normal;
      };

      // Insert spaces before uppercase letters following lowercase:
      // "OpenSans" → "Open Sans", "RobotoMono" → "Roboto Mono"
      std::string expand_camel_case(std::string const& name)
      {
         std::string result;
         for (size_t i = 0; i < name.size(); ++i)
         {
            if (i > 0 && std::isupper(name[i]) && std::islower(name[i - 1]))
               result += ' ';
            result += name[i];
         }
         return result;
      }

      font_file_info parse_font_filename(std::string const& stem)
      {
         font_file_info info;

         // Split on '-' to separate family from style
         auto dash_pos = stem.find('-');
         std::string family_part = (dash_pos != std::string::npos) ? stem.substr(0, dash_pos) : stem;
         std::string style_part = (dash_pos != std::string::npos) ? stem.substr(dash_pos + 1) : "";

         // Expand CamelCase: "OpenSans" → "Open Sans"
         info.family = expand_camel_case(family_part);

         // Remove "Condensed" from family and add stretch
         if (info.family.find("Condensed") != std::string::npos)
         {
            // e.g. "Open Sans Condensed" → family "Open Sans Condensed", stretch condensed
            info.stretch = font_constants::condensed;
         }

         // Parse style part for weight/slant
         // Convert to lowercase for matching
         std::string style_lower = style_part;
         std::transform(style_lower.begin(), style_lower.end(), style_lower.begin(), ::tolower);

         // Check for "variablefont" pattern (e.g. "VariableFont_wght")
         // These are typically regular weight
         if (style_lower.find("variablefont") != std::string::npos)
         {
            // Check if italic is in the style
            if (style_lower.find("italic") != std::string::npos)
               info.slant = font_constants::italic;
            return info;
         }

         // Weight detection
         if (style_lower.find("thin") != std::string::npos)
            info.weight = font_constants::thin;
         else if (style_lower.find("extralight") != std::string::npos ||
                  style_lower.find("extra_light") != std::string::npos)
            info.weight = font_constants::extra_light;
         else if (style_lower.find("semibold") != std::string::npos ||
                  style_lower.find("semi_bold") != std::string::npos ||
                  style_lower.find("demibold") != std::string::npos)
            info.weight = font_constants::semi_bold;
         else if (style_lower.find("extrabold") != std::string::npos)
            info.weight = font_constants::extra_bold;
         else if (style_lower.find("bold") != std::string::npos)
            info.weight = font_constants::bold;
         else if (style_lower.find("black") != std::string::npos)
            info.weight = font_constants::black;
         else if (style_lower.find("medium") != std::string::npos)
            info.weight = font_constants::medium;
         else if (style_lower.find("light") != std::string::npos)
            info.weight = font_constants::light;

         // Slant detection
         if (style_lower.find("italic") != std::string::npos)
            info.slant = font_constants::italic;
         else if (style_lower.find("oblique") != std::string::npos)
            info.slant = font_constants::oblique;

         return info;
      }
   }

   void load_fonts_from_directory(std::string const& dir)
   {
#if defined(ELEMENTS_FILE_IO_SUPPORT)
      fs::path font_dir(dir);
      if (!fs::exists(font_dir) || !fs::is_directory(font_dir))
         return;

      for (auto const& entry : fs::directory_iterator(font_dir))
      {
         if (!entry.is_regular_file())
            continue;

         auto ext = entry.path().extension().string();
         std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
         if (ext != ".ttf" && ext != ".otf")
            continue;

         auto stem = entry.path().stem().string();
         auto file_path = entry.path().string();

         auto info = parse_font_filename(stem);

         // Register through the standard memory-based path. The active
         // resource_loader will read the bytes (the default loader uses
         // an absolute path, so it just reads from disk).
         register_font(info.family, file_path, info.weight, info.slant, info.stretch);
      }
#else
      (void)dir;
#endif
   }

   ////////////////////////////////////////////////////////////////////////////
   // parse_font_name / font_family_available — public wrappers.
   ////////////////////////////////////////////////////////////////////////////
   parsed_font_name parse_font_name(std::string const& name)
   {
      // A variable-font instance suffix ("Family#tag=val,...") is not part of
      // the PSD-style name: parse the base only and re-append the suffix to
      // the family, so font resolution (font_descr -> match) receives it and
      // carries the axes through to the render/measure backends.
      if (auto hash = name.find('#'); hash != std::string::npos)
      {
         auto r = parse_font_name(name.substr(0, hash));
         r.family += name.substr(hash);
         return r;
      }
      auto info = parse_font_filename(name);   // 同一 TU の anon 実装を流用
      parsed_font_name r;
      r.family  = info.family;
      r.weight  = info.weight;
      r.slant   = info.slant;
      r.stretch = info.stretch;
      return r;
   }

   bool font_family_available(std::string const& family)
   {
      if (family.empty())
         return false;
      // Availability is decided on the base family; the variation suffix only
      // selects an instance of it.
      std::string suffix;
      auto base = split_variation_suffix(family, suffix);
      std::lock_guard<std::mutex> lock(font_map_mutex());
      return font_map().find(base) != font_map().end();
   }

   void set_default_variations(std::string const& family,
                               std::string const& axes)
   {
#ifdef ELEMENTS_TVG_GW
      // gw ビルドは既定軸も正規化もホスト (エンジン) 側で管理する。
      (void)family; (void)axes;
#else
      if (family.empty())
         return;
      std::lock_guard<std::mutex> lock(font_map_mutex());
      if (axes.empty())
         default_variations_map().erase(family);
      else
         default_variations_map()[family] = axes;
#endif
   }

   ////////////////////////////////////////////////////////////////////////////
   // Language-aware font family substitution — public API (state/helpers are
   // defined earlier, before font::font, which consumes them per draw)
   ////////////////////////////////////////////////////////////////////////////
   void set_font_language_table(font_language_table table)
   {
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      st.table = std::move(table);
   }

   void set_font_language_entry(std::string const& lang,
                                font_language_entry entry)
   {
      if (lang.empty())
         return;
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      for (auto& [key, e] : st.table)
      {
         if (key == lang)
         {
            e = std::move(entry);
            return;
         }
      }
      st.table.emplace_back(lang, std::move(entry));
   }

   void set_font_language(std::string const& lang)
   {
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      st.current = lang;
   }

   std::string get_font_language()
   {
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      return st.current;
   }

   std::string substitute_font_family(std::string const& name,
                                      std::string const& lang)
   {
      if (name.empty())
         return name;
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      auto const& use = lang.empty() ? st.current : lang;
      if (use.empty())
         return name;
      auto const* entry = find_lang_entry_nolock(st.table, use);
      if (!entry || entry->map.empty())
         return name;
      return substitute_token_nolock(*entry, name);
   }

   std::string font_language_fallback(std::string const& lang)
   {
      auto& st = font_lang_state();
      std::lock_guard<std::mutex> lock(st.mutex);
      auto const& use = lang.empty() ? st.current : lang;
      if (use.empty())
         return {};
      auto const* entry = find_lang_entry_nolock(st.table, use);
      return entry ? entry->fallback : std::string{};
   }
}}

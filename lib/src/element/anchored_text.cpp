/*=============================================================================
   Copyright (c) 2026 wamsoft

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/anchored_text.hpp>
#include <elements/support/text_utils.hpp>
#include <elements/support/theme.hpp>
#include <elements/support/context.hpp>
#include <elements/support/canvas.hpp>
#include <cstdio>
#include <set>
#include <vector>

namespace cycfi::elements
{
   namespace
   {
      // UTF-8 の 1 コードポイントのバイト長。
      int cp_len(unsigned char c)
      {
         return (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2
              : (c & 0xF0) == 0xE0 ? 3 : 4;
      }

      // para を maxw に収まるよう折り返す。 CJK/絵文字 (3-4 byte) は文字単位、
      // Latin は直近の空白で改行。 measure_text で幅を測る (font_descr 指定)。
      std::vector<std::string> wrap_para(
         canvas& cnv, std::string const& para, float maxw, font_descr const& fd)
      {
         std::vector<std::string> out;
         std::string line;
         for (std::size_t i = 0; i < para.size(); )
         {
            int len = cp_len(static_cast<unsigned char>(para[i]));
            std::string ch = para.substr(i, len);
            if (!line.empty() && measure_text(cnv, line + ch, fd).x > maxw)
            {
               auto sp = line.find_last_of(' ');
               bool multibyte = len >= 3;
               if (sp != std::string::npos && sp > 0 && !multibyte)
               {
                  out.push_back(line.substr(0, sp));
                  line = line.substr(sp + 1) + ch;
               }
               else
               {
                  out.push_back(line);
                  line = ch;
               }
            }
            else
            {
               line += ch;
            }
            i += len;
         }
         if (!line.empty())
            out.push_back(line);
         return out;
      }
   }

   anchored_text::anchored_text(
      std::string text, std::string family, float size, color col,
      int halign, point anchor, int tracking, float leading, bool wrap,
      std::string locale)
    : _text(std::move(text))
    , _weight(font_constants::weight_normal)
    , _slant(font_constants::slant_normal)
    , _resolved(true)
    , _size(size)
    , _color(col)
    , _halign(halign)
    , _anchor(anchor)
    , _tracking(tracking)
    , _leading(leading)
    , _wrap(wrap)
    , _locale(std::move(locale))
   {
      // PSD 由来のフォント名 ("NotoSansJP-Medium" 等) を human family + weight/slant
      // に分解し、 登録済みか確認する。 未登録なら一度だけ警告して (フォント追加を
      // 促す) theme 既定へフォールバックする。 空指定は最初から theme 既定。
      if (!family.empty())
      {
         auto pf = parse_font_name(family);
         _family = pf.family;
         _weight = static_cast<unsigned char>(pf.weight);
         _slant  = static_cast<unsigned char>(pf.slant);
         _resolved = font_family_available(_family);
         if (!_resolved)
         {
            static std::set<std::string> warned;
            if (warned.insert(family).second)
               std::fprintf(stderr,
                  "[elements] font \"%s\" (family \"%s\") not available; "
                  "add a matching .ttf/.otf to the fonts directory to use it "
                  "(falling back to the default font).\n",
                  family.c_str(), _family.c_str());
         }
      }
   }

   font_descr anchored_text::make_descr() const
   {
      // family 未指定 or 未登録なら theme 既定フォントを土台に。 いずれも _size 適用。
      // _families は string_view なので描画中だけ生きていればよい (_family はメンバ)。
      if (_family.empty() || !_resolved)
      {
         font_descr fd = get_theme().label_font;
         fd._size = _size;
         return fd;
      }
      font_descr fd{};
      fd._families = _family;
      fd._weight = _weight;
      fd._slant = _slant;
      fd._size = _size;
      return fd;
   }

   view_limits anchored_text::limits(basic_context const& ctx) const
   {
      auto size = measure_text(ctx.canvas, _text, make_descr());
      return {{size.x, size.y}, {size.x, size.y}};
   }

   void anchored_text::draw(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  state = cnv.new_state();

      cnv.fill_style(_color);
      cnv.font(make_descr(), _size);
      if (!_locale.empty())
         cnv.text_locale(_locale);
      // tracking (1/1000 em, 加算) → advance 倍率 (全角で近似的に一致)。
      if (_tracking != 0)
         cnv.letter_spacing(1.0f + _tracking / 1000.0f);

      // 水平 align のみ渡す (垂直ビット無し) → tvg backend の既定 = baseline。
      cnv.text_align(_halign & 0x3);
      float lead = _leading > 0.0f ? _leading : _size * 1.2f;

      // テキストボックス: bounds 幅で word-wrap し、 枠上 + ascent から下へ流す。
      if (_wrap)
      {
         float maxw = ctx.bounds.width();
         auto  fm = cnv.measure_font();
         float ascent = fm.ascent > 0.0f ? fm.ascent : _size * 0.82f;
         float ax = (_halign & 0x3) == canvas::center ? ctx.bounds.left + maxw * 0.5f
                  : (_halign & 0x3) == canvas::right  ? ctx.bounds.right
                  :                                     ctx.bounds.left;
         auto  fd = make_descr();
         int   li = 0;
         std::size_t start = 0;
         for (std::size_t i = 0; i <= _text.size(); ++i)
         {
            if (i == _text.size() || _text[i] == '\n')
            {
               for (auto const& ln : wrap_para(cnv, _text.substr(start, i - start), maxw, fd))
                  cnv.fill_text(ln, point{ax, ctx.bounds.top + ascent + li++ * lead});
               start = i + 1;
            }
         }
         return;
      }

      float bx = ctx.bounds.left + _anchor.x;
      float by = ctx.bounds.top + _anchor.y;
      // 改行で分割し、 各行を baseline + i*leading に描く。 leading 未指定は ~1.2em。
      std::size_t start = 0;
      int line = 0;
      for (std::size_t i = 0; i <= _text.size(); ++i)
      {
         if (i == _text.size() || _text[i] == '\n')
         {
            cnv.fill_text(_text.substr(start, i - start),
                          point{bx, by + line * lead});
            start = i + 1;
            ++line;
         }
      }
   }
}

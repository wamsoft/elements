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

   resolved_font resolve_font_name(std::string const& name)
   {
      resolved_font r;
      if (name.empty())
         return r;                 // ok=false → theme 既定
      auto pf = parse_font_name(name);
      r.family = pf.family;
      r.weight = static_cast<unsigned char>(pf.weight);
      r.slant  = static_cast<unsigned char>(pf.slant);
      r.ok = font_family_available(r.family);
      if (!r.ok)
      {
         static std::set<std::string> warned;
         if (warned.insert(name).second)
            std::fprintf(stderr,
               "[elements] font \"%s\" (family \"%s\") not available; "
               "add a matching .ttf/.otf to the fonts directory to use it "
               "(falling back to the default font).\n",
               name.c_str(), r.family.c_str());
      }
      return r;
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
         auto rf = resolve_font_name(family);
         _family = rf.family;
         _weight = rf.weight;
         _slant  = rf.slant;
         _resolved = rf.ok;
      }
   }

   font_descr anchored_text::run_descr(text_run const& r) const
   {
      float sz = r.size > 0.0f ? r.size : _size;
      if (r.family.empty())        // 未指定/未解決 → theme 既定
      {
         font_descr fd = get_theme().label_font;
         fd._size = sz;
         fd._lang = _locale;       // 言語連動フォント置換の明示言語 (空=現在言語)
         return fd;
      }
      font_descr fd{};
      fd._families = r.family;     // string_view: _runs[i].family (メンバ) は draw 中有効
      fd._weight = r.weight;
      fd._slant = r.slant;
      fd._size = sz;
      fd._lang = _locale;
      return fd;
   }

   font_descr anchored_text::make_descr(float sz) const
   {
      // family 未指定 or 未登録なら theme 既定フォントを土台に。 いずれも sz 適用。
      // _families は string_view なので描画中だけ生きていればよい (_family はメンバ)。
      if (_family.empty() || !_resolved)
      {
         font_descr fd = get_theme().label_font;
         fd._size = sz;
         fd._lang = _locale;       // 言語連動フォント置換の明示言語 (空=現在言語)
         return fd;
      }
      font_descr fd{};
      fd._families = _family;
      fd._weight = _weight;
      fd._slant = _slant;
      fd._size = sz;
      fd._lang = _locale;
      return fd;
   }

   font_descr anchored_text::make_descr() const
   {
      return make_descr(_size);
   }

   // fit 有効時の実描画サイズ。 _size のまま測って bounds 幅に収まればそのまま、
   // はみ出すなら (bounds 幅 / 実測幅) を倍率として縮める。 複数行 (改行区切り) は
   // 最も長い行を基準にする (行ごとにサイズを変えると段が揃わないため)。
   // 下限は _fit_min_scale。 measure_text はフォント実測なので言語・書体差
   // (EN の長い単語 / CJK の全角) をそのまま吸収できる。
   float anchored_text::fit_size(context const& ctx) const
   {
      const float maxw = ctx.bounds.width();
      if (!_fit || maxw <= 0.0f || _text.empty())
         return _size;

      const font_descr fd = make_descr(_size);
      float widest = 0.0f;
      std::size_t start = 0;
      for (std::size_t i = 0; i <= _text.size(); ++i)
      {
         if (i == _text.size() || _text[i] == '\n')
         {
            if (i > start)
            {
               const float w =
                  measure_text(ctx.canvas, std::string_view(_text).substr(start, i - start), fd).x;
               if (w > widest) widest = w;
            }
            start = i + 1;
         }
      }
      if (widest <= maxw || widest <= 0.0f)
         return _size;

      float scale = maxw / widest;
      if (scale < _fit_min_scale) scale = _fit_min_scale;
      return _size * scale;
   }

   view_limits anchored_text::limits(basic_context const& ctx) const
   {
      auto size = measure_text(ctx.canvas, _text, make_descr());
      return {{size.x, size.y}, {size.x, size.y}};
   }

   void anchored_text::draw(context const& ctx)
   {
      if (!_runs.empty())          // rich text (run 別書式) は専用経路へ
      {
         draw_rich(ctx);
         return;
      }
      auto& cnv = ctx.canvas;
      auto  state = cnv.new_state();

      // fit 有効なら bounds 幅に収まるサイズへ落とす (無効時は _size のまま)。
      // wrap は自前で折り返して収めるので fit は効かせない。
      const float dsz = _wrap ? _size : fit_size(ctx);

      cnv.fill_style(_color);
      cnv.font(make_descr(dsz), dsz);
      if (!_locale.empty())
         cnv.text_locale(_locale);
      // tracking (1/1000 em, 加算) → advance 倍率 (全角で近似的に一致)。
      if (_tracking != 0)
         cnv.letter_spacing(1.0f + _tracking / 1000.0f);

      // 水平 align のみ渡す (垂直ビット無し) → tvg backend の既定 = baseline。
      cnv.text_align(_halign & 0x3);
      float lead = _leading > 0.0f ? _leading : dsz * 1.2f;

      // テキストボックス: bounds 幅で word-wrap し、 枠上 + ascent から下へ流す。
      if (_wrap)
      {
         float maxw = ctx.bounds.width();
         auto  fm = cnv.measure_font();
         float ascent = fm.ascent > 0.0f ? fm.ascent : _size * 0.82f;
         float ax = (_halign & 0x3) == canvas::center ? ctx.bounds.left + maxw * 0.5f
                  : (_halign & 0x3) == canvas::right  ? ctx.bounds.right
                  :                                     ctx.bounds.left;
         auto  fd = make_descr(_size);   // wrap は折り返しで収めるので fit 非適用
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

   // run 別書式 (rich text)。 各 span を自身のフォント/サイズ/色で描く。 box なら
   // bounds 幅で word-wrap。 段落 (\n 区切り) ごとに _para_aligns の align を適用。
   // 行のベースライン送りは行内 span の最大サイズ基準。
   void anchored_text::draw_rich(context const& ctx)
   {
      auto& cnv = ctx.canvas;
      auto  state = cnv.new_state();
      if (!_locale.empty())
         cnv.text_locale(_locale);
      float maxw = ctx.bounds.width();

      auto measure = [&](std::string const& t, text_run const& r) -> float {
         return measure_text(cnv, t, run_descr(r)).x;
      };

      struct Span { std::string text; text_run const* r; };
      struct Line { std::vector<Span> spans; int para; };
      std::vector<Line> lines;
      lines.push_back(Line{{}, 0});
      int paraIdx = 0;
      float lineW = 0;
      auto brk = [&](bool hard) {
         if (hard) ++paraIdx;
         lines.push_back(Line{{}, paraIdx});
         lineW = 0;
      };
      auto put = [&](std::string const& ch, text_run const* r) {
         auto& cur = lines.back().spans;
         if (!cur.empty() && cur.back().r == r) cur.back().text += ch;
         else cur.push_back(Span{ch, r});
      };
      for (auto const& run : _runs)
      {
         std::size_t start = 0;
         for (std::size_t i = 0; i <= run.text.size(); ++i)
         {
            if (i == run.text.size() || run.text[i] == '\n')
            {
               std::string part = run.text.substr(start, i - start);
               if (_wrap)
               {
                  for (std::size_t j = 0; j < part.size(); )
                  {
                     int len = cp_len(static_cast<unsigned char>(part[j]));
                     std::string ch = part.substr(j, len);
                     float w = measure(ch, run);
                     bool brkable = len >= 3 || ch == " ";
                     if (lineW > 0.0f && lineW + w > maxw && brkable) brk(false);
                     put(ch, &run);
                     lineW += w;
                     j += len;
                  }
               }
               else if (!part.empty())
               {
                  put(part, &run);
                  lineW += measure(part, run);
               }
               if (i < run.text.size()) brk(true);   // hard 改行 (末尾以外)
               start = i + 1;
            }
         }
      }

      cnv.text_align(canvas::left);
      float y = 0;
      for (std::size_t li = 0; li < lines.size(); ++li)
      {
         auto& line = lines[li];
         float maxSize = 0, lineWidth = 0;
         for (auto& sp : line.spans)
         {
            float sz = sp.r->size > 0.0f ? sp.r->size : _size;
            if (sz > maxSize) maxSize = sz;
            lineWidth += measure(sp.text, *sp.r);
         }
         if (maxSize <= 0.0f) maxSize = _size;
         float lead = _leading > 0.0f ? _leading : maxSize * 1.2f;
         float ascent = maxSize * 0.82f;
         if (li == 0) y = ctx.bounds.top + ascent;
         int la = _halign & 0x3;
         if (!_para_aligns.empty() && line.para < static_cast<int>(_para_aligns.size()))
            la = _para_aligns[line.para] & 0x3;
         float x = la == canvas::center ? ctx.bounds.left + (maxw - lineWidth) * 0.5f
                 : la == canvas::right  ? ctx.bounds.left + maxw - lineWidth
                 :                        ctx.bounds.left;
         for (auto& sp : line.spans)
         {
            cnv.fill_style(sp.r->col);
            cnv.font(run_descr(*sp.r), sp.r->size > 0.0f ? sp.r->size : _size);
            cnv.fill_text(sp.text, point{x, y});
            x += measure(sp.text, *sp.r);
         }
         y += lead;
      }
   }
}

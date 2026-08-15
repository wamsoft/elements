/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/label.hpp>
#include <elements/support/text_utils.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace cycfi::elements
{
   namespace
   {
      // '\n' 区切りで行に分割する ("\r\n" の '\r' は落とす)。
      // 空文字列は「1 行の空行」として扱う (高さ 1 行分を確保するため)。
      std::vector<std::string_view> split_label_lines(std::string_view text)
      {
         std::vector<std::string_view> lines;
         std::size_t start = 0;
         while (true)
         {
            auto pos = text.find('\n', start);
            auto end = (pos == std::string_view::npos) ? text.size() : pos;
            auto line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
               line.remove_suffix(1);
            lines.push_back(line);
            if (pos == std::string_view::npos)
               break;
            start = pos + 1;
         }
         return lines;
      }

      struct label_text_metrics
      {
         std::vector<std::string_view> lines;
         float width = 0;        // 最大行の幅
         float line_height = 0;  // 1 行の高さ (全行で共通)
      };

      // 複数行ラベルの寸法。 行ごとに measure して最大幅と行高を得る。
      label_text_metrics measure_label(canvas& cnv, std::string_view text, font_descr fd)
      {
         label_text_metrics m;
         m.lines = split_label_lines(text);
         for (auto line : m.lines)
         {
            auto size = measure_text(cnv, line, fd);
            m.width = std::max(m.width, size.x);
            m.line_height = std::max(m.line_height, size.y);
         }
         return m;
      }
   }

   view_limits default_label_styler::limits(basic_context const& ctx) const
   {
      // text に改行が含まれる場合、 描画側は行ごとに描くので高さも行数ぶん
      // 確保する。 これをしないと親 (vtile 等) が 1 行分しか場所を空けず、
      // 後続のウィジェットが 2 行目以降に重なる。
      auto m = measure_label(ctx.canvas, get_text(), get_font().size(get_font_size()));
      float height = m.line_height * static_cast<float>(m.lines.size());
      return {{m.width, height}, {m.width, height}};
   }

   void default_label_styler::draw(context const& ctx)
   {
      auto& canvas_ = ctx.canvas;
      auto  state = canvas_.new_state();
      auto  align = get_text_align();

      // default should reflect the theme's vertical label_text_align
      if ((align & 0x1C) == 0)
         align |= get_theme().label_text_align & 0x1C;

      auto text_c = get_font_color();
      if (!ctx.enabled || !is_enabled())
         text_c = text_c.opacity(text_c.alpha * get_theme().disabled_opacity);

      canvas_.fill_style(text_c);
      canvas_.font(get_font(), get_font_size());
      canvas_.text_locale(get_text_locale());

      float cx = ctx.bounds.left + (ctx.bounds.width() / 2);
      switch (align & 0x3)
      {
         case canvas::left:
            cx = ctx.bounds.left;
            break;
         case canvas::center:
            break;
         case canvas::right:
            cx = ctx.bounds.right;
            break;
      }

      std::string const& text = get_text();
      if (text.find('\n') == std::string::npos)
      {
         float cy = ctx.bounds.top + (ctx.bounds.height() / 2);
         switch (align & 0x1C)
         {
            case canvas::top:
               cy = ctx.bounds.top;
               break;
            case canvas::middle:
               break;
            case canvas::bottom:
               cy = ctx.bounds.bottom;
               break;
         }

         canvas_.text_align(align);
         canvas_.fill_text(text.c_str(), point{cx, cy});
         return;
      }

      // 複数行: 行ごとに描く。 バックエンドの fill_text が '\n' をどう扱うかに
      // 依存しないよう、 1 行ずつ明示的に描いて limits と行位置を一致させる。
      auto m = measure_label(canvas_, text, get_font().size(get_font_size()));
      float block_h = m.line_height * static_cast<float>(m.lines.size());
      float y = ctx.bounds.top;   // ブロック上端 (縦アラインで決める)
      switch (align & 0x1C)
      {
         case canvas::top:
            break;
         case canvas::bottom:
            y = ctx.bounds.bottom - block_h;
            break;
         case canvas::middle:
         default:
            y = ctx.bounds.top + (ctx.bounds.height() - block_h) / 2;
            break;
      }

      canvas_.text_align((align & 0x3) | canvas::top);
      for (auto line : m.lines)
      {
         canvas_.fill_text(std::string(line).c_str(), point{cx, y});
         y += m.line_height;
      }
   }

   void default_label_styler::enable(bool state)
   {
      _is_enabled = state;
   }

   bool default_label_styler::is_enabled() const
   {
      return _is_enabled;
   }
}


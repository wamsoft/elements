/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/element/block_text.hpp>
#include <elements/support/context.hpp>
#include <elements/view.hpp>

#include <algorithm>

namespace cycfi::elements
{
   block_text_box::block_text_box(std::string text, font font_, float size, color color_)
    : _text(std::move(text))
    , _font(std::move(font_))
    , _size(size > 0 ? size : _font.size())
    , _color(color_)
   {}

   void block_text_box::relayout(float width, float height) const
   {
      if (!_dirty && width == _laid_out_width && height == _laid_out_height
         && _count == _laid_out_count)
         return;

      block_text_request req;
      req.text = _text;
      req.font_key = _font.file();
      req.font_family = _font.family();
      req.fnt = &_font;
      req.size = _size;
      req.width = width;
      req.height = height;
      req.line_spacing = _line_spacing;
      req.align = _align;
      req.base = _base;
      req.count = _count;

      if (!get_block_text_backend().layout(req, _result))
         _result = block_text_result{};

      _laid_out_width = width;
      _laid_out_height = height;
      _laid_out_count = _count;
      _dirty = false;
   }

   view_limits block_text_box::limits(basic_context const& /* ctx */) const
   {
      // The width is the caller's to give; the height follows from wrapping at
      // whatever width the last layout saw (one line's worth before that).
      // The minimum width matters for fit-to-content parents: reporting 0
      // would let them collapse the block to nothing.
      relayout(_laid_out_width, _laid_out_height);
      float min_height = _result.line_height > 0 ? _result.line_height : _size;
      if (_result.height > min_height)
         min_height = _result.height;
      float min_width = default_min_width;
      if (_result.width > 0 && _result.width < min_width)
         min_width = _result.width;
      return {{min_width, min_height}, {full_extent, full_extent}};
   }

   void block_text_box::layout(context const& ctx)
   {
      relayout(ctx.bounds.width(), ctx.bounds.height());
   }

   void block_text_box::draw(context const& ctx)
   {
      relayout(ctx.bounds.width(), ctx.bounds.height());
      if (_result.lines.empty())
         return;

      auto& cnv = ctx.canvas;
      auto state = cnv.new_state();
      cnv.add_rect(ctx.bounds);
      cnv.clip();
      cnv.fill_style(_color);
      cnv.font(_font, _size);
      cnv.text_align(canvas::top | canvas::left);

      auto const clip_extent = cnv.clip_extent();
      for (auto const& line : _result.lines)
      {
         if (line.reveal_end <= line.start)
            continue;
         float const y = ctx.bounds.top + line.y;
         if (y + _result.descent < clip_extent.top)
            continue;
         if (y - _result.ascent > clip_extent.bottom)
            break;
         cnv.fill_text(
            std::string_view{_text.data() + line.start, line.reveal_end - line.start},
            point{ctx.bounds.left + line.x, y - _result.ascent});
      }
   }

   void block_text_box::set_text(string_view text)
   {
      _text = std::string(text);
      _dirty = true;
   }

   void block_text_box::set_align(int a)
   {
      if (_align == a)
         return;
      _align = a;
      _dirty = true;
   }

   void block_text_box::set_base_direction(int d)
   {
      if (_base == d)
         return;
      _base = d;
      _dirty = true;
   }

   void block_text_box::set_line_spacing(float px)
   {
      if (_line_spacing == px)
         return;
      _line_spacing = px;
      _dirty = true;
   }

   void block_text_box::set_count(int count)
   {
      _count = count;
   }

   int block_text_box::total_count() const
   {
      block_text_request req;
      req.text = _text;
      req.font_key = _font.file();
      req.font_family = _font.family();
      req.fnt = &_font;
      req.size = _size;
      return get_block_text_backend().count_clusters(req);
   }
}

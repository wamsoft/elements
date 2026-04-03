/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/canvas.hpp>
#include <algorithm>
#include <cstring>

namespace cycfi { namespace elements
{
   ///////////////////////////////////////////////////////////////////////////
   // Matrix helpers
   ///////////////////////////////////////////////////////////////////////////
   tvg::Matrix canvas::identity()
   {
      return {1, 0, 0, 0, 1, 0, 0, 0, 1};
   }

   tvg::Matrix canvas::multiply(tvg::Matrix const& a, tvg::Matrix const& b)
   {
      return {
         a.e11*b.e11 + a.e12*b.e21 + a.e13*b.e31,
         a.e11*b.e12 + a.e12*b.e22 + a.e13*b.e32,
         a.e11*b.e13 + a.e12*b.e23 + a.e13*b.e33,

         a.e21*b.e11 + a.e22*b.e21 + a.e23*b.e31,
         a.e21*b.e12 + a.e22*b.e22 + a.e23*b.e32,
         a.e21*b.e13 + a.e22*b.e23 + a.e23*b.e33,

         a.e31*b.e11 + a.e32*b.e21 + a.e33*b.e31,
         a.e31*b.e12 + a.e32*b.e22 + a.e33*b.e32,
         a.e31*b.e13 + a.e32*b.e23 + a.e33*b.e33
      };
   }

   tvg::Matrix canvas::invert(tvg::Matrix const& m)
   {
      float det = m.e11*(m.e22*m.e33 - m.e23*m.e32)
                - m.e12*(m.e21*m.e33 - m.e23*m.e31)
                + m.e13*(m.e21*m.e32 - m.e22*m.e31);

      if (std::abs(det) < 1e-10f)
         return identity();

      float inv_det = 1.0f / det;
      return {
         (m.e22*m.e33 - m.e23*m.e32) * inv_det,
         (m.e13*m.e32 - m.e12*m.e33) * inv_det,
         (m.e12*m.e23 - m.e13*m.e22) * inv_det,

         (m.e23*m.e31 - m.e21*m.e33) * inv_det,
         (m.e11*m.e33 - m.e13*m.e31) * inv_det,
         (m.e13*m.e21 - m.e11*m.e23) * inv_det,

         (m.e21*m.e32 - m.e22*m.e31) * inv_det,
         (m.e12*m.e31 - m.e11*m.e32) * inv_det,
         (m.e11*m.e22 - m.e12*m.e21) * inv_det
      };
   }

   point canvas::transform_point(tvg::Matrix const& m, point p)
   {
      return {
         m.e11*p.x + m.e12*p.y + m.e13,
         m.e21*p.x + m.e22*p.y + m.e23
      };
   }

   ///////////////////////////////////////////////////////////////////////////
   // Constructor / Destructor
   ///////////////////////////////////////////////////////////////////////////
   canvas::canvas(uint32_t* buf, uint32_t w, uint32_t h, float scale_)
    : _buffer(buf)
    , _width(w)
    , _height(h)
    , _scale(scale_)
   {
      // EngineOption::None disables dirty region optimization.
      // Default mode clears changed regions with 0x00000000 before redraw,
      // causing black backgrounds around text glyphs.
      _tvg_canvas = tvg::SwCanvas::gen(tvg::EngineOption::None);
      _tvg_canvas->target(buf, w, w, h, tvg::ColorSpace::ARGB8888);

      _initial_matrix = {scale_, 0, 0, 0, scale_, 0, 0, 0, 1};
      _inv_initial = invert(_initial_matrix);
      _state.matrix = _initial_matrix;
   }

   canvas::canvas(canvas&& rhs)
    : _tvg_canvas(rhs._tvg_canvas)
    , _buffer(rhs._buffer)
    , _width(rhs._width)
    , _height(rhs._height)
    , _scale(rhs._scale)
    , _path_cmds(std::move(rhs._path_cmds))
    , _path_pts(std::move(rhs._path_pts))
    , _state(std::move(rhs._state))
    , _initial_matrix(rhs._initial_matrix)
    , _inv_initial(rhs._inv_initial)
   {
      rhs._tvg_canvas = nullptr;
   }

   canvas::~canvas()
   {
      if (_tvg_canvas)
      {
         flush();
         delete _tvg_canvas;
      }
   }

   ///////////////////////////////////////////////////////////////////////////
   // Flush
   ///////////////////////////////////////////////////////////////////////////
   void canvas::flush()
   {
      flush_shapes();
   }

   void canvas::flush_shapes()
   {
      if (_has_pending && _tvg_canvas)
      {
         _tvg_canvas->update();
         _tvg_canvas->draw(false);
         _tvg_canvas->sync();
         _tvg_canvas->remove();
         _has_pending = false;
      }
   }

   ///////////////////////////////////////////////////////////////////////////
   // Shape creation helpers
   ///////////////////////////////////////////////////////////////////////////
   tvg::Shape* canvas::make_shape() const
   {
      auto* shape = tvg::Shape::gen();
      if (!_path_cmds.empty())
         shape->appendPath(_path_cmds.data(), _path_cmds.size(),
                           _path_pts.data(), _path_pts.size());
      return shape;
   }

   tvg::Shape* canvas::make_clip_shape() const
   {
      if (!_state.clip)
         return nullptr;
      auto* shape = tvg::Shape::gen();
      shape->appendPath(_state.clip->cmds.data(), _state.clip->cmds.size(),
                        _state.clip->pts.data(), _state.clip->pts.size());
      shape->transform(_state.clip->transform);
      shape->fillRule(_state.clip->rule);
      return shape;
   }

   void canvas::apply_transform(tvg::Paint* paint) const
   {
      paint->transform(_state.matrix);
   }

   void canvas::apply_fill_to_shape(tvg::Shape* shape) const
   {
      if (auto* c = std::get_if<color>(&_state.fill_style_data))
      {
         shape->fill(
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255),
            uint8_t(c->alpha * 255)
         );
      }
      else if (auto* gd = std::get_if<gradient_data>(&_state.fill_style_data))
      {
         if (gd->is_linear)
         {
            auto* grad = tvg::LinearGradient::gen();
            grad->linear(gd->start.x, gd->start.y, gd->end.x, gd->end.y);

            std::vector<tvg::Fill::ColorStop> stops;
            for (auto& cs : gd->stops)
            {
               stops.push_back({
                  cs.offset,
                  uint8_t(cs.color.red * 255),
                  uint8_t(cs.color.green * 255),
                  uint8_t(cs.color.blue * 255),
                  uint8_t(cs.color.alpha * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            grad->transform(_state.matrix);
            shape->fill(grad);
         }
         else
         {
            auto* grad = tvg::RadialGradient::gen();
            grad->radial(gd->c2.x, gd->c2.y, gd->c2_radius,
                         gd->c1.x, gd->c1.y, gd->c1_radius);

            std::vector<tvg::Fill::ColorStop> stops;
            for (auto& cs : gd->stops)
            {
               stops.push_back({
                  cs.offset,
                  uint8_t(cs.color.red * 255),
                  uint8_t(cs.color.green * 255),
                  uint8_t(cs.color.blue * 255),
                  uint8_t(cs.color.alpha * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            grad->transform(_state.matrix);
            shape->fill(grad);
         }
      }
   }

   void canvas::apply_stroke_to_shape(tvg::Shape* shape) const
   {
      shape->strokeWidth(_state.line_width_val);

      if (auto* c = std::get_if<color>(&_state.stroke_style_data))
      {
         shape->strokeFill(
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255),
            uint8_t(c->alpha * 255)
         );
      }
      else if (auto* gd = std::get_if<gradient_data>(&_state.stroke_style_data))
      {
         if (gd->is_linear)
         {
            auto* grad = tvg::LinearGradient::gen();
            grad->linear(gd->start.x, gd->start.y, gd->end.x, gd->end.y);

            std::vector<tvg::Fill::ColorStop> stops;
            for (auto& cs : gd->stops)
            {
               stops.push_back({
                  cs.offset,
                  uint8_t(cs.color.red * 255),
                  uint8_t(cs.color.green * 255),
                  uint8_t(cs.color.blue * 255),
                  uint8_t(cs.color.alpha * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            grad->transform(_state.matrix);
            shape->strokeFill(grad);
         }
         else
         {
            auto* grad = tvg::RadialGradient::gen();
            grad->radial(gd->c2.x, gd->c2.y, gd->c2_radius,
                         gd->c1.x, gd->c1.y, gd->c1_radius);

            std::vector<tvg::Fill::ColorStop> stops;
            for (auto& cs : gd->stops)
            {
               stops.push_back({
                  cs.offset,
                  uint8_t(cs.color.red * 255),
                  uint8_t(cs.color.green * 255),
                  uint8_t(cs.color.blue * 255),
                  uint8_t(cs.color.alpha * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            grad->transform(_state.matrix);
            shape->strokeFill(grad);
         }
      }
   }

   ///////////////////////////////////////////////////////////////////////////
   // Transforms
   ///////////////////////////////////////////////////////////////////////////
   void canvas::translate(point p)
   {
      tvg::Matrix t = {1, 0, p.x, 0, 1, p.y, 0, 0, 1};
      _state.matrix = multiply(_state.matrix, t);
   }

   void canvas::rotate(float rad)
   {
      float c = std::cos(rad);
      float s = std::sin(rad);
      tvg::Matrix r = {c, -s, 0, s, c, 0, 0, 0, 1};
      _state.matrix = multiply(_state.matrix, r);
   }

   void canvas::scale(point p)
   {
      tvg::Matrix s = {p.x, 0, 0, 0, p.y, 0, 0, 0, 1};
      _state.matrix = multiply(_state.matrix, s);
   }

   void canvas::skew(float sx, float sy)
   {
      // Match Cairo: cairo_matrix_init(&mat, 1, 0, sx, 1, 0, sy)
      // x' = x + sx*y, y' = y + sy
      tvg::Matrix m = {1, sx, 0, 0, 1, sy, 0, 0, 1};
      _state.matrix = multiply(_state.matrix, m);
   }

   point canvas::device_to_user(point p) const
   {
      // Compute relative transform (current vs initial)
      tvg::Matrix xaf = multiply(_state.matrix, _inv_initial);
      tvg::Matrix inv_xaf = invert(xaf);
      return transform_point(inv_xaf, p);
   }

   point canvas::user_to_device(point p) const
   {
      tvg::Matrix xaf = multiply(_state.matrix, _inv_initial);
      return transform_point(xaf, p);
   }

   ///////////////////////////////////////////////////////////////////////////
   // Paths
   ///////////////////////////////////////////////////////////////////////////
   void canvas::begin_path()
   {
      _path_cmds.clear();
      _path_pts.clear();
   }

   void canvas::close_path()
   {
      _path_cmds.push_back(tvg::PathCommand::Close);
      _current_pt = _subpath_start;
   }

   void canvas::fill()
   {
      auto* shape = make_shape();
      shape->fillRule(_state.fill_rule_val);
      apply_fill_to_shape(shape);
      apply_transform(shape);

      if (auto* clip_shape = make_clip_shape())
         shape->clip(clip_shape);

      _tvg_canvas->add(shape);
      _has_pending = true;

      _path_cmds.clear();
      _path_pts.clear();
   }

   void canvas::fill_preserve()
   {
      auto* shape = make_shape();
      shape->fillRule(_state.fill_rule_val);
      apply_fill_to_shape(shape);
      apply_transform(shape);

      if (auto* clip_shape = make_clip_shape())
         shape->clip(clip_shape);

      _tvg_canvas->add(shape);
      _has_pending = true;
      // Path is preserved (not cleared)
   }

   void canvas::stroke()
   {
      auto* shape = make_shape();
      apply_stroke_to_shape(shape);
      // No fill for stroke-only
      shape->fill(0, 0, 0, 0);
      apply_transform(shape);

      if (auto* clip_shape = make_clip_shape())
         shape->clip(clip_shape);

      _tvg_canvas->add(shape);
      _has_pending = true;

      _path_cmds.clear();
      _path_pts.clear();
   }

   void canvas::stroke_preserve()
   {
      auto* shape = make_shape();
      apply_stroke_to_shape(shape);
      shape->fill(0, 0, 0, 0);
      apply_transform(shape);

      if (auto* clip_shape = make_clip_shape())
         shape->clip(clip_shape);

      _tvg_canvas->add(shape);
      _has_pending = true;
      // Path is preserved
   }

   void canvas::clip()
   {
      // Store current path as clip data
      auto cd = std::make_shared<clip_data>();
      cd->cmds = _path_cmds;
      cd->pts = _path_pts;
      cd->transform = _state.matrix;
      cd->rule = _state.fill_rule_val;
      _state.clip = cd;

      _path_cmds.clear();
      _path_pts.clear();
   }

   rect canvas::clip_extent() const
   {
      if (!_state.clip || _state.clip->pts.empty())
         return {0, 0, float(_width)/_scale, float(_height)/_scale};

      float min_x = 1e30f, min_y = 1e30f;
      float max_x = -1e30f, max_y = -1e30f;
      auto& m = _state.clip->transform;
      for (auto& pt : _state.clip->pts)
      {
         float x = m.e11*pt.x + m.e12*pt.y + m.e13;
         float y = m.e21*pt.x + m.e22*pt.y + m.e23;
         min_x = std::min(min_x, x);
         min_y = std::min(min_y, y);
         max_x = std::max(max_x, x);
         max_y = std::max(max_y, y);
      }
      // Convert from device to user coords
      auto tl = device_to_user(point{min_x, min_y});
      auto br = device_to_user(point{max_x, max_y});
      return {tl.x, tl.y, br.x, br.y};
   }

   bool canvas::hit_test(point p) const
   {
      // Simple point-in-path test using winding/even-odd rule
      // Transform point to path space
      if (_path_pts.size() < 3)
         return false;

      int crossings = 0;
      size_t pt_idx = 0;
      float px = p.x, py = p.y;

      tvg::Point prev = {0, 0};
      bool have_prev = false;

      for (size_t i = 0; i < _path_cmds.size(); ++i)
      {
         switch (_path_cmds[i])
         {
            case tvg::PathCommand::MoveTo:
               prev = _path_pts[pt_idx++];
               have_prev = true;
               break;
            case tvg::PathCommand::LineTo:
            {
               auto cur = _path_pts[pt_idx++];
               if (have_prev)
               {
                  float y0 = prev.y, y1 = cur.y;
                  float x0 = prev.x, x1 = cur.x;
                  if ((y0 <= py && y1 > py) || (y1 <= py && y0 > py))
                  {
                     float t = (py - y0) / (y1 - y0);
                     if (px < x0 + t * (x1 - x0))
                        ++crossings;
                  }
               }
               prev = cur;
               break;
            }
            case tvg::PathCommand::CubicTo:
               // Approximate: skip complex cubic intersection
               pt_idx += 3;
               if (pt_idx <= _path_pts.size())
                  prev = _path_pts[pt_idx - 1];
               break;
            case tvg::PathCommand::Close:
               break;
         }
      }

      if (_state.fill_rule_val == tvg::FillRule::EvenOdd)
         return (crossings & 1) != 0;
      else
         return crossings != 0;
   }

   rect canvas::fill_extent() const
   {
      if (_path_pts.empty())
         return {0, 0, 0, 0};

      float min_x = 1e30f, min_y = 1e30f;
      float max_x = -1e30f, max_y = -1e30f;
      for (auto& pt : _path_pts)
      {
         min_x = std::min(min_x, pt.x);
         min_y = std::min(min_y, pt.y);
         max_x = std::max(max_x, pt.x);
         max_y = std::max(max_y, pt.y);
      }
      return elements::rect(min_x, min_y, max_x, max_y);
   }

   void canvas::move_to(point p)
   {
      _path_cmds.push_back(tvg::PathCommand::MoveTo);
      _path_pts.push_back({p.x, p.y});
      _subpath_start = {p.x, p.y};
      _current_pt = {p.x, p.y};
   }

   void canvas::line_to(point p)
   {
      _path_cmds.push_back(tvg::PathCommand::LineTo);
      _path_pts.push_back({p.x, p.y});
      _current_pt = {p.x, p.y};
   }

   void canvas::arc_to(point /* p1 */, point /* p2 */, float /* radius */)
   {
      assert(false); // unimplemented (same as Cairo version)
   }

   void canvas::arc(
      point p, float radius,
      float start_angle, float end_angle,
      bool ccw
   )
   {
      // Approximate arc with cubic Bezier curves
      // Normalize angles
      if (ccw && end_angle > start_angle)
         end_angle -= 2 * M_PI;
      if (!ccw && end_angle < start_angle)
         end_angle += 2 * M_PI;

      float angle_diff = end_angle - start_angle;
      if (std::abs(angle_diff) < 1e-6f)
         return;

      // Split into segments of at most PI/2
      int num_segments = std::max(1, int(std::ceil(std::abs(angle_diff) / (M_PI / 2))));
      float seg_angle = angle_diff / num_segments;

      for (int i = 0; i < num_segments; ++i)
      {
         float a1 = start_angle + i * seg_angle;
         float a2 = a1 + seg_angle;

         float cos1 = std::cos(a1), sin1 = std::sin(a1);
         float cos2 = std::cos(a2), sin2 = std::sin(a2);

         // Compute control points for cubic Bezier approximation
         float alpha = 4.0f * std::tan((a2 - a1) / 4.0f) / 3.0f;

         float x1 = p.x + radius * cos1;
         float y1 = p.y + radius * sin1;
         float x2 = p.x + radius * cos2;
         float y2 = p.y + radius * sin2;

         float cx1 = x1 - alpha * radius * sin1;
         float cy1 = y1 + alpha * radius * cos1;
         float cx2 = x2 + alpha * radius * sin2;
         float cy2 = y2 - alpha * radius * cos2;

         if (i == 0 && _path_cmds.empty())
         {
            _path_cmds.push_back(tvg::PathCommand::MoveTo);
            _path_pts.push_back({x1, y1});
            _subpath_start = {x1, y1};
         }
         else if (i == 0)
         {
            // Line to start of arc if path already has content
            _path_cmds.push_back(tvg::PathCommand::LineTo);
            _path_pts.push_back({x1, y1});
         }

         _path_cmds.push_back(tvg::PathCommand::CubicTo);
         _path_pts.push_back({cx1, cy1});
         _path_pts.push_back({cx2, cy2});
         _path_pts.push_back({x2, y2});
         _current_pt = {x2, y2};
      }
   }

   void canvas::rect(struct rect r)
   {
      add_rect(r);
   }

   void canvas::add_rect(struct rect r)
   {
      _path_cmds.push_back(tvg::PathCommand::MoveTo);
      _path_pts.push_back({r.left, r.top});

      _path_cmds.push_back(tvg::PathCommand::LineTo);
      _path_pts.push_back({r.right, r.top});

      _path_cmds.push_back(tvg::PathCommand::LineTo);
      _path_pts.push_back({r.right, r.bottom});

      _path_cmds.push_back(tvg::PathCommand::LineTo);
      _path_pts.push_back({r.left, r.bottom});

      _path_cmds.push_back(tvg::PathCommand::Close);

      _subpath_start = {r.left, r.top};
      _current_pt = {r.left, r.top};
   }

   void canvas::add_round_rect(struct rect r_, float radius)
   {
      auto x = r_.left;
      auto y = r_.top;
      auto r = r_.right;
      auto b = r_.bottom;
      radius = std::min(radius, std::min(r_.width(), r_.height()) / 2);

      // Build round rect using arcs at corners
      // Start at top-right, going clockwise
      move_to({r - radius, y});
      arc({r - radius, y + radius}, radius, -M_PI/2, 0);          // top-right
      arc({r - radius, b - radius}, radius, 0, M_PI/2);           // bottom-right
      arc({x + radius, b - radius}, radius, M_PI/2, M_PI);        // bottom-left
      arc({x + radius, y + radius}, radius, M_PI, M_PI*1.5);      // top-left
      close_path();
   }

   void canvas::round_rect(struct rect r, float radius)
   {
      add_round_rect(r, radius);
   }

   ///////////////////////////////////////////////////////////////////////////
   // Styles
   ///////////////////////////////////////////////////////////////////////////
   void canvas::fill_style(color c)
   {
      _state.fill_style_data = c;
   }

   void canvas::stroke_style(color c)
   {
      _state.stroke_style_data = c;
   }

   void canvas::line_width(float w)
   {
      _state.line_width_val = w;
   }

   void canvas::fill_style(linear_gradient const& gr)
   {
      gradient_data gd;
      gd.is_linear = true;
      gd.start = gr.start;
      gd.end = gr.end;
      gd.stops = gr.space;
      _state.fill_style_data = gd;
   }

   void canvas::fill_style(radial_gradient const& gr)
   {
      gradient_data gd;
      gd.is_linear = false;
      gd.c1 = gr.c1;
      gd.c1_radius = gr.c1_radius;
      gd.c2 = gr.c2;
      gd.c2_radius = gr.c2_radius;
      gd.stops = gr.space;
      _state.fill_style_data = gd;
   }

   void canvas::fill_rule(fill_rule_enum rule)
   {
      _state.fill_rule_val = (rule == fill_winding)
         ? tvg::FillRule::NonZero
         : tvg::FillRule::EvenOdd;
   }

   ///////////////////////////////////////////////////////////////////////////
   // Font
   ///////////////////////////////////////////////////////////////////////////
   namespace
   {
      // ThorVG internally converts font size from points to pixels
      // using 96/72 DPI factor. Elements expects metrics in user-space
      // (like Cairo), so we cancel out ThorVG's DPI conversion.
      constexpr float tvg_font_scale = 72.0f / 96.0f;

      // Extract filename without extension from a path.
      // ThorVG uses this as the font name key internally.
      std::string stem_from_path(std::string const& path)
      {
         auto slash = path.find_last_of("/\\");
         auto start = (slash != std::string::npos) ? slash + 1 : 0;
         auto dot = path.rfind('.');
         auto end = (dot != std::string::npos && dot > start) ? dot : path.size();
         return path.substr(start, end - start);
      }
   }

   void canvas::font(elements::font const& font_)
   {
      _state.font_file = font_.file();
      // ThorVG registers fonts by filename stem (no extension)
      _state.font_family = _state.font_file.empty()
         ? font_.family()
         : stem_from_path(_state.font_file);
      _state.font_size = font_.size();
   }

   void canvas::font(elements::font const& font_, float size)
   {
      font(font_);
      _state.font_size = size;
   }

   void canvas::font_size(float size)
   {
      _state.font_size = size;
   }

   ///////////////////////////////////////////////////////////////////////////
   // Text
   ///////////////////////////////////////////////////////////////////////////
   void canvas::fill_text(point p, char const* utf8)
   {
      fill_text(std::string_view(utf8), p);
   }

   void canvas::fill_text(std::string_view utf8_, point p)
   {
      // Flush pending shapes so text appears in correct order
      flush_shapes();

      std::string utf8(utf8_);

      // Use richtext for text rendering
      // For now, use ThorVG Text as fallback
      auto* text = tvg::Text::gen();
      if (!_state.font_file.empty())
         tvg::Text::load(_state.font_file.c_str());

      text->font(_state.font_family.c_str());
      text->size(_state.font_size * tvg_font_scale);
      text->text(utf8.c_str());

      // Get metrics for alignment
      tvg::TextMetrics tm;
      text->metrics(tm);

      float ascent = tm.ascent;
      float descent = -tm.descent; // ThorVG descent is negative

      // Apply alignment
      float dx = 0, dy = 0;
      switch (_state.align & 0x3)
      {
         case text_alignment::right:
         {
            // Measure text width using glyph metrics
            float width = 0;
            for (const char* c = utf8.c_str(); *c; )
            {
               tvg::GlyphMetrics gm;
               int len = 1;
               // Simple UTF-8 lead byte detection
               if ((*c & 0x80) == 0) len = 1;
               else if ((*c & 0xE0) == 0xC0) len = 2;
               else if ((*c & 0xF0) == 0xE0) len = 3;
               else len = 4;
               std::string ch(c, len);
               if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
                  width += gm.advance;
               c += len;
            }
            dx = -width;
            break;
         }
         case text_alignment::center:
         {
            float width = 0;
            for (const char* c = utf8.c_str(); *c; )
            {
               tvg::GlyphMetrics gm;
               int len = 1;
               if ((*c & 0x80) == 0) len = 1;
               else if ((*c & 0xE0) == 0xC0) len = 2;
               else if ((*c & 0xF0) == 0xE0) len = 3;
               else len = 4;
               std::string ch(c, len);
               if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
                  width += gm.advance;
               c += len;
            }
            dx = -width / 2;
            break;
         }
         default:
            break;
      }

      switch (_state.align & 0x1C)
      {
         case text_alignment::top:
            dy = 0; // ThorVG text origin is top-left
            break;
         case text_alignment::middle:
            dy = -(ascent + descent) / 2;
            break;
         case text_alignment::bottom:
            dy = -(ascent + descent);
            break;
         default: // baseline
            dy = -ascent;
            break;
      }

      // Apply fill color
      if (auto* c = std::get_if<color>(&_state.fill_style_data))
      {
         text->fill(
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255)
         );
      }

      // Position: apply current matrix translation + alignment offset
      tvg::Matrix tm2 = _state.matrix;
      tvg::Matrix offset = {1, 0, p.x + dx, 0, 1, p.y + dy, 0, 0, 1};
      text->transform(multiply(tm2, offset));

      _tvg_canvas->add(text);
      _tvg_canvas->update();
      _tvg_canvas->draw(false);
      _tvg_canvas->sync();
      _tvg_canvas->remove();
   }

   void canvas::stroke_text(point p, char const* utf8)
   {
      stroke_text(std::string_view(utf8), p);
   }

   void canvas::stroke_text(std::string_view utf8_, point p)
   {
      flush_shapes();

      std::string utf8(utf8_);

      auto* text = tvg::Text::gen();
      if (!_state.font_file.empty())
         tvg::Text::load(_state.font_file.c_str());

      text->font(_state.font_family.c_str());
      text->size(_state.font_size * tvg_font_scale);
      text->text(utf8.c_str());

      tvg::TextMetrics tm;
      text->metrics(tm);

      float dy = -tm.ascent; // baseline alignment default

      // Apply stroke
      if (auto* c = std::get_if<color>(&_state.stroke_style_data))
      {
         text->outline(_state.line_width_val,
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255)
         );
      }
      text->fill(0, 0, 0); // transparent fill for stroke-only

      tvg::Matrix offset = {1, 0, p.x, 0, 1, p.y + dy, 0, 0, 1};
      text->transform(multiply(_state.matrix, offset));

      _tvg_canvas->add(text);
      _tvg_canvas->update();
      _tvg_canvas->draw(false);
      _tvg_canvas->sync();
      _tvg_canvas->remove();
   }

   canvas::text_metrics canvas::measure_text(char const* utf8)
   {
      auto* text = tvg::Text::gen();
      if (!_state.font_file.empty())
         tvg::Text::load(_state.font_file.c_str());

      text->font(_state.font_family.c_str());
      text->size(_state.font_size * tvg_font_scale);
      text->text(utf8);

      tvg::TextMetrics tm = {};
      text->metrics(tm);

      // Measure text width
      float width = 0;
      for (const char* c = utf8; *c; )
      {
         tvg::GlyphMetrics gm;
         int len = 1;
         if ((*c & 0x80) == 0) len = 1;
         else if ((*c & 0xE0) == 0xC0) len = 2;
         else if ((*c & 0xF0) == 0xE0) len = 3;
         else len = 4;
         std::string ch(c, len);
         if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
            width += gm.advance;
         c += len;
      }

      float ascent = tm.ascent;
      float descent = -tm.descent;
      float leading = tm.linegap;

      tvg::Paint::rel(text);

      return {
         ascent,
         descent,
         leading,
         {width, ascent + descent}
      };
   }

   canvas::font_metrics canvas::measure_font()
   {
      auto* text = tvg::Text::gen();
      if (!_state.font_file.empty())
         tvg::Text::load(_state.font_file.c_str());

      text->font(_state.font_family.c_str());
      text->size(_state.font_size * tvg_font_scale);
      text->text(" "); // need some text to get metrics

      tvg::TextMetrics tm;
      text->metrics(tm);

      float ascent = tm.ascent;
      float descent = -tm.descent;
      float height = tm.advance;
      float leading = tm.linegap;

      tvg::Paint::rel(text);

      return {
         ascent,
         descent,
         height,
         leading
      };
   }

   ///////////////////////////////////////////////////////////////////////////
   // Pixmaps
   ///////////////////////////////////////////////////////////////////////////
   void canvas::draw(pixmap const& pm, elements::rect /*src*/, elements::rect dest)
   {
      auto state_ = new_state();
      auto w = dest.width();
      auto h = dest.height();

      auto* base = pm.picture();
      if (!base)
         return;

      auto* paint = base->duplicate();
      if (!paint)
         return;

      auto* pic = static_cast<tvg::Picture*>(paint);
      pic->size(w, h);

      tvg::Matrix offset = {1, 0, dest.left, 0, 1, dest.top, 0, 0, 1};
      pic->transform(multiply(_state.matrix, offset));

      if (auto* clip_shape = make_clip_shape())
         pic->clip(clip_shape);

      _tvg_canvas->add(pic);
      _has_pending = true;
   }

   ///////////////////////////////////////////////////////////////////////////
   // Save / Restore
   ///////////////////////////////////////////////////////////////////////////
   void canvas::save()
   {
      _state_stack.push(_state);
   }

   void canvas::restore()
   {
      if (!_state_stack.empty())
      {
         _state = _state_stack.top();
         _state_stack.pop();
      }
   }
}}

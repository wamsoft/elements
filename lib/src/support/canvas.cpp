/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/canvas.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace cycfi { namespace elements
{
   // Default text backend (ThorVG). Can be replaced via set_text_backend().
   std::shared_ptr<elements::text_backend> canvas::_text_backend;

   // Process-wide flush counter (see canvas::flush_generation).
   std::uint64_t canvas::_flush_gen = 0;

   void canvas::set_text_backend(std::shared_ptr<elements::text_backend> b)
   {
      _text_backend = std::move(b);
   }

   std::shared_ptr<elements::text_backend> canvas::get_text_backend()
   {
      if (!_text_backend)
         _text_backend = create_tvg_text_backend();
      return _text_backend;
   }

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
   // Viewport (partial redraw)
   ///////////////////////////////////////////////////////////////////////////
   void canvas::viewport(int x, int y, int w, int h)
   {
      // ThorVG allows changing the viewport only at the start of a rendering
      // sequence (before the first Canvas::add), which is where an offscreen
      // host calls this — right after constructing the canvas for the frame.
      if (_tvg_canvas)
         _tvg_canvas->viewport(x, y, w, h);
   }

   ///////////////////////////////////////////////////////////////////////////
   // Flush
   ///////////////////////////////////////////////////////////////////////////
   void canvas::flush()
   {
      flush_shapes();
   }

   void canvas::add_pending(tvg::Paint* paint)
   {
      if (!paint || !_tvg_canvas)
         return;
      _tvg_canvas->add(paint);
      _has_pending = true;
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
         ++_flush_gen;   // 以後、 キャッシュ済 paint を再び add してよい
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
      const float ga = _state.global_alpha;
      if (auto* c = std::get_if<color>(&_state.fill_style_data))
      {
         shape->fill(
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255),
            uint8_t(c->alpha * ga * 255)
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
                  uint8_t(cs.color.alpha * ga * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            // No grad->transform() — gradient coords are in the same user space
            // as the shape path; shape->transform() moves both together.
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
                  uint8_t(cs.color.alpha * ga * 255)
               });
            }
            grad->colorStops(stops.data(), stops.size());
            shape->fill(grad);
         }
      }
   }

   void canvas::apply_stroke_to_shape(tvg::Shape* shape) const
   {
      shape->strokeWidth(_state.line_width_val);

      const float ga = _state.global_alpha;
      if (auto* c = std::get_if<color>(&_state.stroke_style_data))
      {
         shape->strokeFill(
            uint8_t(c->red * 255),
            uint8_t(c->green * 255),
            uint8_t(c->blue * 255),
            uint8_t(c->alpha * ga * 255)
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
                  uint8_t(cs.color.alpha * ga * 255)
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
                  uint8_t(cs.color.alpha * ga * 255)
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
      // Compute the bounding box of the new clip path in device coordinates
      auto transform_pt = [](tvg::Matrix const& m, tvg::Point const& p) -> tvg::Point {
         return {m.e11*p.x + m.e12*p.y + m.e13,
                 m.e21*p.x + m.e22*p.y + m.e23};
      };

      float new_min_x = 1e30f, new_min_y = 1e30f;
      float new_max_x = -1e30f, new_max_y = -1e30f;
      for (auto& pt : _path_pts)
      {
         auto dp = transform_pt(_state.matrix, pt);
         new_min_x = std::min(new_min_x, dp.x);
         new_min_y = std::min(new_min_y, dp.y);
         new_max_x = std::max(new_max_x, dp.x);
         new_max_y = std::max(new_max_y, dp.y);
      }

      // If there is an existing clip, intersect bounding boxes
      if (_state.clip && !_state.clip->pts.empty())
      {
         float old_min_x = 1e30f, old_min_y = 1e30f;
         float old_max_x = -1e30f, old_max_y = -1e30f;
         for (auto& pt : _state.clip->pts)
         {
            auto dp = transform_pt(_state.clip->transform, pt);
            old_min_x = std::min(old_min_x, dp.x);
            old_min_y = std::min(old_min_y, dp.y);
            old_max_x = std::max(old_max_x, dp.x);
            old_max_y = std::max(old_max_y, dp.y);
         }

         // Intersect the two bounding boxes in device space
         new_min_x = std::max(new_min_x, old_min_x);
         new_min_y = std::max(new_min_y, old_min_y);
         new_max_x = std::min(new_max_x, old_max_x);
         new_max_y = std::min(new_max_y, old_max_y);

         // Clamp to non-negative area
         if (new_max_x < new_min_x) new_max_x = new_min_x;
         if (new_max_y < new_min_y) new_max_y = new_min_y;
      }

      // Store intersected clip as an axis-aligned rect in device space
      // (identity transform since coordinates are already in device space)
      auto cd = std::make_shared<clip_data>();
      cd->cmds = {
         tvg::PathCommand::MoveTo,
         tvg::PathCommand::LineTo,
         tvg::PathCommand::LineTo,
         tvg::PathCommand::LineTo,
         tvg::PathCommand::Close
      };
      cd->pts = {
         {new_min_x, new_min_y},
         {new_max_x, new_min_y},
         {new_max_x, new_max_y},
         {new_min_x, new_max_y}
      };
      cd->transform = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // identity
      cd->rule = tvg::FillRule::NonZero;
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
      // Clip points are in absolute device (pixel) space.
      // Convert back to user coords using inverse of current state matrix.
      auto inv = invert(_state.matrix);
      auto tl = transform_point(inv, point{min_x, min_y});
      auto br = transform_point(inv, point{max_x, max_y});
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
   void canvas::font(elements::font const& font_)
   {
      _state.font_file = font_.file();
      _state.font_family = font_.family();
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

   void canvas::text_locale(std::string locale)
   {
      _state.text_locale = std::move(locale);
   }

   void canvas::letter_spacing(float scale)
   {
      _state.letter_spacing = scale;
   }

   ///////////////////////////////////////////////////////////////////////////
   // Text — dispatched through text_backend interface
   ///////////////////////////////////////////////////////////////////////////
   void canvas::fill_text(point p, char const* utf8)
   {
      fill_text(std::string_view(utf8), p);
   }

   void canvas::fill_text(std::string_view utf8_, point p)
   {
      if (utf8_.empty()) return;
      get_text_backend()->fill_text(*this, utf8_, p);
   }

   void canvas::stroke_text(point p, char const* utf8)
   {
      stroke_text(std::string_view(utf8), p);
   }

   void canvas::stroke_text(std::string_view utf8_, point p)
   {
      if (utf8_.empty()) return;
      get_text_backend()->stroke_text(*this, utf8_, p);
   }

   canvas::text_metrics canvas::measure_text(char const* utf8)
   {
      // 測定メモ化: limits() はツリー探索 (フォーカスナビ / refresh / レイアウト)
      // のたびに同じ文字列を測り直し、その都度シェーピングが走る。文字列 +
      // フォント状態をキーに結果を記憶して 2 回目以降を O(1) にする。
      // フォント登録は起動時に済む前提 (後から register_font しても既存エントリ
      // の測定値は既に描画に使われた値なので実害は薄い)。肥大化時は全クリア。
      auto const& s = get_state();
      std::string key;
      key.reserve(s.font_family.size() + s.font_file.size()
                  + s.text_locale.size() + std::strlen(utf8) + 40);
      key += s.font_family;
      key += '\x01';
      key += s.font_file;
      key += '\x01';
      char num[48];
      std::snprintf(num, sizeof(num), "%.3f|%.4f|", s.font_size, s.letter_spacing);
      key += num;
      key += s.text_locale;
      key += '\x01';
      key += utf8;

      static std::unordered_map<std::string, text_metrics> cache;
      auto it = cache.find(key);
      if (it != cache.end())
         return it->second;

      auto m = get_text_backend()->measure_text(*this, utf8);
      if (cache.size() > 4096)
         cache.clear();
      cache.emplace(std::move(key), m);
      return m;
   }

   canvas::font_metrics canvas::measure_font()
   {
      return get_text_backend()->measure_font(*this);
   }

   ///////////////////////////////////////////////////////////////////////////
   // Pixmaps
   ///////////////////////////////////////////////////////////////////////////
   void canvas::draw(pixmap const& pm, elements::rect src, elements::rect dest)
   {
      auto state_ = new_state();

      if (src.width() <= 0 || src.height() <= 0 ||
          dest.width() <= 0 || dest.height() <= 0)
         return;

      auto* base = pm.picture();
      if (!base)
         return;

      auto* paint = base->duplicate();
      if (!paint)
         return;

      auto* pic = static_cast<tvg::Picture*>(paint);

      // The picture has pixel dimensions (pic_pw × pic_ph).
      // pixmap::size() returns logical coordinates: pixels * scale.
      // src is in logical coordinates (a sub-rect of the logical image).
      // We build a matrix that maps picture pixel-space → screen-space
      // such that the src region maps exactly onto dest.
      //
      // Logical coord → pixel coord: divide by scale
      // pixel (px,py) → screen: sx*px + tx, sy*py + ty
      //   where sx = scale * dest.width / src.width
      //         tx = dest.left - src.left * dest.width / src.width
      float scale = pm.scale();
      float sx = scale * dest.width()  / src.width();
      float sy = scale * dest.height() / src.height();
      float tx = dest.left - src.left * dest.width()  / src.width();
      float ty = dest.top  - src.top  * dest.height() / src.height();

      tvg::Matrix mat = {sx, 0, tx, 0, sy, ty, 0, 0, 1};
      pic->transform(multiply(_state.matrix, mat));

      // Clip to dest rectangle (in device coords), intersected with state clip
      auto& m = _state.matrix;
      auto tx_pt = [&m](float x, float y) -> tvg::Point {
         return {m.e11*x + m.e12*y + m.e13, m.e21*x + m.e22*y + m.e23};
      };
      // Transform dest corners to device space
      auto d0 = tx_pt(dest.left,  dest.top);
      auto d1 = tx_pt(dest.right, dest.top);
      auto d2 = tx_pt(dest.right, dest.bottom);
      auto d3 = tx_pt(dest.left,  dest.bottom);
      float cl = std::min({d0.x, d1.x, d2.x, d3.x});
      float ct = std::min({d0.y, d1.y, d2.y, d3.y});
      float cr = std::max({d0.x, d1.x, d2.x, d3.x});
      float cb = std::max({d0.y, d1.y, d2.y, d3.y});

      // Intersect with state clip if present
      if (_state.clip && !_state.clip->pts.empty())
      {
         auto& cm = _state.clip->transform;
         float ol = 1e30f, ot = 1e30f, or_ = -1e30f, ob = -1e30f;
         for (auto& pt : _state.clip->pts)
         {
            float x = cm.e11*pt.x + cm.e12*pt.y + cm.e13;
            float y = cm.e21*pt.x + cm.e22*pt.y + cm.e23;
            ol = std::min(ol, x); ot = std::min(ot, y);
            or_ = std::max(or_, x); ob = std::max(ob, y);
         }
         cl = std::max(cl, ol); ct = std::max(ct, ot);
         cr = std::min(cr, or_); cb = std::min(cb, ob);
         if (cr < cl) cr = cl;
         if (cb < ct) cb = ct;
      }

      auto* clip = tvg::Shape::gen();
      clip->appendRect(cl, ct, cr - cl, cb - ct);
      // Already in device coords — identity transform
      pic->clip(clip);

      // Group opacity (fade): multiply the picture's alpha by global_alpha.
      if (_state.global_alpha < 1.0f)
      {
         float ga = _state.global_alpha < 0.0f ? 0.0f : _state.global_alpha;
         pic->opacity(uint8_t(ga * 255));
      }

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

   ///////////////////////////////////////////////////////////////////////////
   // Global alpha (group opacity)
   ///////////////////////////////////////////////////////////////////////////
   float canvas::global_alpha() const
   {
      return _state.global_alpha;
   }

   void canvas::global_alpha(float a)
   {
      if (a < 0.0f) a = 0.0f;
      if (a > 1.0f) a = 1.0f;
      _state.global_alpha = a;
   }
}}

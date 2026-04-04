/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_CANVAS_MAY_3_2016)
#define ELEMENTS_CANVAS_MAY_3_2016

#include <elements/support/color.hpp>
#include <elements/support/rect.hpp>
#include <elements/support/circle.hpp>
#include <elements/support/pixmap.hpp>
#include <elements/support/font.hpp>
#include <infra/filesystem.hpp>

#include <thorvg.h>

#include <vector>
#include <functional>
#include <stack>
#include <cmath>
#include <cassert>
#include <variant>
#include <memory>

namespace richtext { class TextRenderer; }

namespace cycfi { namespace elements
{
   class canvas
   {
   public:

      enum class text_backend { richtext, thorvg };

      explicit          canvas(uint32_t* buf, uint32_t w, uint32_t h, float scale = 1.0f);
                        canvas(canvas&& rhs);
                        ~canvas();

                        canvas(canvas const& rhs) = delete;
      canvas&           operator=(canvas const& rhs) = delete;

      // Switch text rendering backend at runtime
      static void       set_text_backend(text_backend b) { _text_backend = b; }
      static text_backend get_text_backend()              { return _text_backend; }

      // Access to underlying ThorVG canvas (for advanced use)
      tvg::Canvas&      tvg_canvas() const;

      // Finalize rendering (must call at end of frame)
      void              flush();

      ///////////////////////////////////////////////////////////////////////////////////
      // Transforms
      void              translate(point p);
      void              rotate(float rad);
      void              scale(point p);
      void              skew(float sx, float sy);
      point             device_to_user(point p) const;
      point             user_to_device(point p) const;

      ///////////////////////////////////////////////////////////////////////////////////
      // Paths
      void              begin_path();
      void              close_path();
      void              fill();
      void              fill_preserve();
      void              stroke();
      void              stroke_preserve();
      void              clip();
      elements::rect    clip_extent() const;
      bool              hit_test(point p) const;
      elements::rect    fill_extent() const;

      void              move_to(point p);
      void              line_to(point p);
      void              arc_to(point p1, point p2, float radius);
      void              arc(
                           point p, float radius,
                           float start_angle, float end_angle,
                           bool ccw = false
                        );

                        [[deprecated("Use add_rect(r) instead following artist API.")]]
      void              rect(elements::rect r);
      void              add_rect(elements::rect r);

                        [[deprecated("Use round_rect(r, radius) instead following artist API.")]]
      void              round_rect(elements::rect r, float radius);
      void              add_round_rect(elements::rect r, float radius);

                        [[deprecated("Use circle(c) instead following artist API.")]]
      void              circle(elements::circle c);
      void              add_circle(elements::circle c);

      ///////////////////////////////////////////////////////////////////////////////////
      // Styles
      void              fill_style(color c);
      void              stroke_style(color c);
      void              line_width(float w);

      ///////////////////////////////////////////////////////////////////////////////////
      // Gradients
      struct color_stop
      {
         float             offset;
         elements::color   color;
      };

      struct linear_gradient
      {
         point start = {};
         point end = {};

         void  add_color_stop(color_stop cs);
         std::vector<color_stop> space = {};
      };

      struct radial_gradient
      {
         point c1 = {};
         float c1_radius = {};
         point c2 = c1;
         float c2_radius = c1_radius;

         void  add_color_stop(color_stop cs);
         std::vector<color_stop> space = {};
      };

      void              fill_style(linear_gradient const& gr);
      void              fill_style(radial_gradient const& gr);

      enum fill_rule_enum
      {
         fill_winding,
         fill_odd_even
      };

      void              fill_rule(fill_rule_enum rule);

      ///////////////////////////////////////////////////////////////////////////////////
      // Rectangles
      void              fill_rect(elements::rect r);
      void              fill_round_rect(elements::rect r, float radius);
      void              stroke_rect(elements::rect r);
      void              stroke_round_rect(elements::rect r, float radius);

      ///////////////////////////////////////////////////////////////////////////////////
      // Font
      void              font(elements::font const& font_);
      void              font(elements::font const& font_, float size);
      void              font_size(float size);

      ///////////////////////////////////////////////////////////////////////////////////
      // Text
      enum text_alignment
      {
         // Horizontal align
         left     = 0,        // Default, align text horizontally to left.
         center   = 1,        // Align text horizontally to center.
         right    = 2,        // Align text horizontally to right.

         // Vertical align
         baseline = 4,        // Default, align text vertically to baseline.
         top      = 8,        // Align text vertically to top.
         middle   = 12,       // Align text vertically to middle.
         bottom   = 16        // Align text vertically to bottom.
      };

      struct text_metrics
      {
         float       ascent;
         float       descent;
         float       leading;
         point       size;
      };

      struct font_metrics
      {
         float       ascent;
         float       descent;
         float       height;
         float       leading;
      };

                        [[deprecated("Use fill_text(utf8, p) instead following artist API.")]]
      void              fill_text(point p, char const* utf8);
      void              fill_text(std::string_view utf8, point p);

                        [[deprecated("Use fill_text(utf8, p) instead following artist API.")]]
      void              stroke_text(point p, char const* utf8);
      void              stroke_text(std::string_view utf8, point p);

      text_metrics      measure_text(char const* utf8);
      void              text_align(int align);

      font_metrics      measure_font();

      ///////////////////////////////////////////////////////////////////////////////////
      // Pixmaps

      void              draw(pixmap const& pm, elements::rect src, elements::rect dest);
      void              draw(pixmap const& pm, elements::rect dest);
      void              draw(pixmap const& pm, point pos);

      ///////////////////////////////////////////////////////////////////////////////////
      // States
      class state
      {
      public:
                        state(canvas& cnv_);
                        state(state&& rhs) noexcept;
                        state(state const&) = delete;
                        ~state();

         state&         operator=(state const&) = delete;
         state&         operator=(state&& rhs) noexcept;

      private:

         canvas* cnv;
      };

      state             new_state()   {return state{ *this}; }
      void              save();
      void              restore();

   private:

      friend class glyphs;

      void              apply_fill_style();
      void              apply_stroke_style();

      // Create a ThorVG Shape from current accumulated path
      tvg::Shape*       make_shape() const;

      // Create a clip Shape clone from current clip data
      tvg::Shape*       make_clip_shape() const;

      // Apply current matrix to a paint
      void              apply_transform(tvg::Paint* paint) const;

      // Flush pending shapes to render
      void              flush_shapes();

      // Get/create richtext TextRenderer
      richtext::TextRenderer& text_renderer();

      // Backend-specific implementations
      void              fill_text_tvg(std::string_view utf8, point p);
      void              fill_text_rt(std::string_view utf8, point p);
      void              stroke_text_tvg(std::string_view utf8, point p);
      void              stroke_text_rt(std::string_view utf8, point p);
      text_metrics      measure_text_tvg(char const* utf8);
      text_metrics      measure_text_rt(char const* utf8);
      font_metrics      measure_font_tvg();
      font_metrics      measure_font_rt();

      // Matrix helpers
      static tvg::Matrix multiply(tvg::Matrix const& a, tvg::Matrix const& b);
      static tvg::Matrix invert(tvg::Matrix const& m);
      static point       transform_point(tvg::Matrix const& m, point p);
      static tvg::Matrix identity();

      ///////////////////////////////////////////////////////////////////////////////////
      // Fill/stroke style variant
      struct gradient_data
      {
         bool is_linear; // true = linear, false = radial
         // linear
         point start, end;
         // radial
         point c1, c2;
         float c1_radius, c2_radius;
         // color stops
         std::vector<color_stop> stops;
      };

      using style_variant = std::variant<color, gradient_data>;

      // Apply style to shape fill
      void              apply_fill_to_shape(tvg::Shape* shape) const;
      // Apply style to shape stroke
      void              apply_stroke_to_shape(tvg::Shape* shape) const;

      ///////////////////////////////////////////////////////////////////////////////////
      // Clip data (stored as path so we can create new Shape clones)
      struct clip_data
      {
         std::vector<tvg::PathCommand> cmds;
         std::vector<tvg::Point>       pts;
         tvg::Matrix                   transform;
         tvg::FillRule                 rule = tvg::FillRule::NonZero;
      };

      ///////////////////////////////////////////////////////////////////////////////////
      // Canvas state (save/restore)
      struct canvas_state
      {
         style_variant      fill_style_data{colors::black};
         style_variant      stroke_style_data{colors::black};
         float              line_width_val = 1.0f;
         int                align          = 0;
         tvg::FillRule      fill_rule_val  = tvg::FillRule::NonZero;
         tvg::Matrix        matrix;
         std::shared_ptr<clip_data> clip;

         // Font state
         std::string        font_family;
         std::string        font_file;
         float              font_size = 12;
      };

      // ThorVG canvas and buffer
      tvg::SwCanvas*                _tvg_canvas = nullptr;
      uint32_t*                     _buffer;
      uint32_t                      _width;
      uint32_t                      _height;
      float                         _scale;

      // Path accumulation
      std::vector<tvg::PathCommand> _path_cmds;
      std::vector<tvg::Point>       _path_pts;

      // Current subpath start (for close_path)
      tvg::Point                    _subpath_start = {0, 0};
      tvg::Point                    _current_pt = {0, 0};

      // State
      canvas_state                  _state;
      std::stack<canvas_state>      _state_stack;

      // Initial matrix (includes DPI scale)
      tvg::Matrix                   _initial_matrix;
      tvg::Matrix                   _inv_initial;

      // Track whether shapes have been added since last flush
      bool                          _has_pending = false;

      // Richtext renderer (uses _tvg_canvas directly)
      std::unique_ptr<richtext::TextRenderer> _text_renderer;

      // Text backend selection
      static text_backend _text_backend;
   };
}}

#include <elements/support/detail/canvas_impl.hpp>
#endif

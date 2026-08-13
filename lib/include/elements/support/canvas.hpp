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

#include <elements/support/text_backend.hpp>
#include <thorvg.h>

#include <vector>
#include <functional>
#include <stack>
#include <cmath>
#include <cassert>
#include <variant>
#include <memory>

namespace cycfi { namespace elements
{
   class canvas
   {
   public:

      explicit          canvas(uint32_t* buf, uint32_t w, uint32_t h, float scale = 1.0f);
                        canvas(canvas&& rhs);
                        ~canvas();

                        canvas(canvas const& rhs) = delete;
      canvas&           operator=(canvas const& rhs) = delete;

      // Text rendering backend (plugin)
      static void       set_text_backend(std::shared_ptr<elements::text_backend> b);
      static std::shared_ptr<elements::text_backend> get_text_backend();

      // Access to underlying ThorVG canvas (for advanced use)
      tvg::Canvas&      tvg_canvas() const;

      // Restrict rasterization to the given device-pixel rectangle for the
      // whole canvas (ThorVG Canvas::viewport). Unlike clip(), this costs
      // nothing per shape — it narrows the raster region itself, so it is the
      // right tool for partial redraw (an offscreen host that keeps the
      // previous frame in its buffer and re-renders only a dirty rectangle).
      // Must be called before anything is drawn on this canvas.
      void              viewport(int x, int y, int w, int h);

      // Finalize rendering (must call at end of frame)
      void              flush();

      // Access for text backends
      void              flush_shapes();
      tvg::Shape*       make_clip_shape() const;
      struct canvas_state;
      canvas_state const& get_state() const;
      static tvg::Matrix multiply(tvg::Matrix const& a, tvg::Matrix const& b);

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

      // BCP47 locale tag (e.g. "ja-JP", "zh-TW"). Pass empty string to clear.
      // Used by the FT text loader for HarfBuzz language-sensitive glyph
      // selection (locl GSUB feature). Other backends silently ignore.
      void              text_locale(std::string locale);

      // Letter spacing (tracking) as a scale factor on each glyph advance
      // (1.0 = normal, <1 tighter, >1 looser). See canvas_state::letter_spacing.
      void              letter_spacing(float scale);

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

      // Text metrics — defined in text_backend.hpp
      using text_metrics = elements::text_metrics;
      using font_metrics = elements::font_metrics;

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

      ///////////////////////////////////////////////////////////////////////////////////
      // Global alpha (group opacity)
      //
      // 0..1 の係数で、 アクティブな間に描画される fill / stroke / text / image の
      // 不透明度に乗算される。 state スタックで save/restore される。 オフスクリーン
      // 合成なしに要素単位のフェード (opacity) を行うために使う (重なり部分の厳密な
      // 合成ではなく、 各シェイプ独立の alpha 乗算)。
      float             global_alpha() const;
      void              global_alpha(float a);

      ///////////////////////////////////////////////////////////////////////////////////
      // Types used by text backends (public for plugin access)

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

      struct clip_data
      {
         std::vector<tvg::PathCommand> cmds;
         std::vector<tvg::Point>       pts;
         tvg::Matrix                   transform;
         tvg::FillRule                 rule = tvg::FillRule::NonZero;
      };

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

         // BCP47 locale tag for HarfBuzz language-sensitive shaping
         // (e.g. "ja-JP", "zh-CN", "zh-TW"). Empty = no locale hint.
         // Honored by FT loader builds (TVG_LOADER_FT=ON); ignored otherwise.
         std::string        text_locale;

         // Letter spacing (tracking) as a scale factor on each glyph's advance
         // width (ThorVG Text::spacing convention). 1.0 = normal. Source formats
         // with additive tracking (PSD: 1/1000 em) map to 1 + tracking/1000,
         // which is exact for full-width (advance≈em) glyphs. See
         // canvas::letter_spacing().
         float              letter_spacing = 1.0f;

         // Group opacity (0..1) multiplied into fill/stroke/text/image alpha.
         // Default 1 (opaque). See canvas::global_alpha().
         float              global_alpha = 1.0f;
      };

   private:

      friend class glyphs;

      void              apply_fill_style();
      void              apply_stroke_style();

      // Create a ThorVG Shape from current accumulated path
      tvg::Shape*       make_shape() const;

      // Apply current matrix to a paint
      void              apply_transform(tvg::Paint* paint) const;

      // Matrix helpers (multiply is public, others are internal)
      static tvg::Matrix invert(tvg::Matrix const& m);
      static point       transform_point(tvg::Matrix const& m, point p);
      static tvg::Matrix identity();

      // Apply style to shape fill
      void              apply_fill_to_shape(tvg::Shape* shape) const;
      // Apply style to shape stroke
      void              apply_stroke_to_shape(tvg::Shape* shape) const;

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

      // Text backend (static, shared across all canvas instances)
      static std::shared_ptr<elements::text_backend> _text_backend;
   };
}}

#include <elements/support/detail/canvas_impl.hpp>
#endif

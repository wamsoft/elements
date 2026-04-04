/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_GLYPH_UTILS_RICHTEXT_HPP)
#define ELEMENTS_GLYPH_UTILS_RICHTEXT_HPP

#include <elements/support/glyph_utils.hpp>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Create richtext-based glyph layout and font backends.
   // These are the only public API — richtext headers are not exposed.
   ////////////////////////////////////////////////////////////////////////////
   std::shared_ptr<glyph_layout_backend> create_richtext_glyph_layout_backend();
   std::shared_ptr<font_backend>         create_richtext_font_backend();
}}

#endif

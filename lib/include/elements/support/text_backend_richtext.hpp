/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_TEXT_BACKEND_RICHTEXT_HPP)
#define ELEMENTS_TEXT_BACKEND_RICHTEXT_HPP

#include <elements/support/text_backend.hpp>

namespace cycfi { namespace elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Create a richtext-based text backend.
   // This is the only public API — richtext headers are not exposed.
   ////////////////////////////////////////////////////////////////////////////
   std::shared_ptr<text_backend> create_richtext_text_backend();
}}

#endif

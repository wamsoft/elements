/*=============================================================================
   Copyright (c) 2026 Cycfi Research. All rights reserved.

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(CYCFI_ELEMENTS_FOCUS_2026_06_06)
#define CYCFI_ELEMENTS_FOCUS_2026_06_06

#include <elements/element/proxy.hpp>
#include <elements/view.hpp>

namespace cycfi::elements
{
   /**
    * \class initial_focus_element
    * \brief
    *    Marks its subject as the desired initial keyboard focus target.
    *
    *    On the first layout pass, this proxy asks the view to set focus to
    *    itself. Because proxy_base delegates wants_focus / begin_focus /
    *    end_focus to its subject, the wrapped element ends up holding the
    *    focus. Subsequent layouts are no-ops with respect to focus.
    */
   class initial_focus_element : public proxy_base
   {
   public:

      using proxy_base::proxy_base;

      void layout(context const& ctx) override
      {
         proxy_base::layout(ctx);
         if (!_requested)
         {
            _requested = true;
            ctx.view.focus(shared_from_this());
         }
      }

   private:

      bool _requested = false;
   };

   /**
    * \brief
    *    Wrap an element to declare it as the initial keyboard focus.
    *
    *    Use this where the element is constructed in `view.content(...)`:
    *    \code
    *    view_.content(
    *       initial_focus(button("OK")),
    *       background
    *    );
    *    \endcode
    *
    *    For a programmatic alternative, use view::focus(element_ptr).
    */
   template <concepts::Element Subject>
   inline proxy<remove_cvref_t<Subject>, initial_focus_element>
   initial_focus(Subject&& subject)
   {
      return {std::forward<Subject>(subject)};
   }

   /**
    * \class focus_unit_element
    * \brief
    *    Presents its whole subtree (typically a composite) to the view's
    *    focus machinery as ONE atomic focusable unit.
    *
    *    The directional focus navigation (view::arrow_focus_navigation)
    *    normally descends into composites and collects the individual
    *    focusable children. A focus_unit_element stops that descent: the
    *    unit itself is collected instead, and begin_focus / end_focus are
    *    NOT forwarded to the subject (the unit holds the focus flag).
    *
    *    Derive from this and override key() / pad_axis() to give the unit
    *    its behavior (e.g. a choice group where Left/Right move the
    *    selection between member buttons). Mouse clicks pass through to
    *    the children as usual (proxy_base default).
    */
   class focus_unit_element : public proxy_base
   {
   public:

      bool                    wants_control() const override { return true; }
      bool                    wants_focus() const override   { return true; }
      void                    begin_focus(focus_request) override { _has_focus = true; }
      bool                    end_focus() override { _has_focus = false; return true; }
      bool                    focused() const { return _has_focus; }

   private:

      bool                    _has_focus = false;
   };
}

#endif

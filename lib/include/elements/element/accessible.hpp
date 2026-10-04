/*=============================================================================
   Copyright (c) 2026 Go Watanabe

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(ELEMENTS_ACCESSIBLE_OCTOBER_4_2026)
#define ELEMENTS_ACCESSIBLE_OCTOBER_4_2026

#include <elements/element/proxy.hpp>
#include <elements/support/a11y.hpp>
#include <functional>
#include <string>
#include <utility>

namespace cycfi::elements
{
   ////////////////////////////////////////////////////////////////////////////
   // Declarative accessibility overrides
   //
   // The wrapper does not become a node itself. Its non-empty fields are
   // applied to the first node found inside it — so a11y_label("Volume",
   // slider) names the slider — and when nothing inside becomes a node (an
   // image, a decoration), the wrapper stands for it:
   //
   //    a11y_label("Volume", hold(volume_slider))
   //    a11y_label("Logo", image{"logo.png"})        // becomes an image node
   //    a11y_role(a11y::role::heading, label("Settings"))
   //    a11y_value_fn([&]{ return fmt_db(gain); }, gain_knob)
   //    a11y_live(a11y::live::polite, status_label)
   //    a11y_hidden(background_art)
   ////////////////////////////////////////////////////////////////////////////
   template <concepts::Element Subject>
   class a11y_props_element : public proxy<Subject>
   {
   public:

      using base_type = proxy<Subject>;
      using value_function = std::function<std::string()>;

                              a11y_props_element(Subject subject, a11y::info props,
                                 value_function value_fn = {})
                               : base_type(std::move(subject))
                               , _props(std::move(props))
                               , _value_fn(std::move(value_fn))
                              {}

      void                    accessible(context const& /* ctx */, a11y::info& out) const override
                              {
                                 out = _props;
                                 out.is_override = true;
                                 if (_value_fn)
                                    out.value = _value_fn();
                              }

      a11y::info&             props() { return _props; }

   private:

      a11y::info              _props;
      value_function          _value_fn;
   };

   template <concepts::Element Subject>
   inline a11y_props_element<remove_cvref_t<Subject>>
   a11y_props(a11y::info props, Subject&& subject)
   {
      return {std::forward<Subject>(subject), std::move(props)};
   }

   template <concepts::Element Subject>
   inline auto a11y_label(std::string name, Subject&& subject)
   {
      a11y::info i;
      i.name = std::move(name);
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   template <concepts::Element Subject>
   inline auto a11y_description(std::string text, Subject&& subject)
   {
      a11y::info i;
      i.description = std::move(text);
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   template <concepts::Element Subject>
   inline auto a11y_role(a11y::role r, Subject&& subject)
   {
      a11y::info i;
      i.role = r;
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   // A stable id: survives rebuilds and is what dumps and REPL commands
   // show (instead of "#<hex>").
   template <concepts::Element Subject>
   inline auto a11y_id(std::string id, Subject&& subject)
   {
      a11y::info i;
      i.id = std::move(id);
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   template <concepts::Element Subject>
   inline auto a11y_live(a11y::live priority, Subject&& subject)
   {
      a11y::info i;
      i.live = priority;
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   template <concepts::Element Subject>
   inline auto a11y_hidden(Subject&& subject)
   {
      a11y::info i;
      i.hidden = true;
      return a11y_props(std::move(i), std::forward<Subject>(subject));
   }

   template <typename F, concepts::Element Subject>
   inline a11y_props_element<remove_cvref_t<Subject>>
   a11y_value_fn(F&& f, Subject&& subject)
   {
      return {std::forward<Subject>(subject), a11y::info{}, std::forward<F>(f)};
   }
}

#endif

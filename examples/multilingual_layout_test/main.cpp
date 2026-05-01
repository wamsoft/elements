/*=============================================================================
   Multilingual layout isolation test.

   The widget frames in examples/multilingual_widgets/ size themselves from
   measure_text/measure_font which (at the ThorVG FT loader) read ascent &
   descent from the *primary* face only. When a fallback face (e.g. Noto Sans
   JP) is taller than the primary (e.g. Open Sans), the actually-rendered
   glyphs overflow the box vertically.

   This sample drops a label inside a tight frame so that the box edges line
   up with measure_text's output. If the case overflows the frame, the box
   was too short.

     [A] Latin only / Open Sans primary
         primary covers the text. Should be tight — sanity check.
     [B] CJK only / Open Sans primary (fallback to Noto Sans JP)
         box uses Open Sans metrics, glyphs come from Noto Sans JP. Expected
         to overflow if the bug is in FtLoader::metrics(fm, TextMetrics&)
         (primary-only) OR in text_backend baseline placement.
     [C] CJK only / Noto Sans JP primary (no fallback needed)
         box and glyphs both from Noto Sans JP. Should be tight. If [C]
         still overflows, the bug is on the backend baseline side, not in
         FtLoader's metrics.
     [D] Mixed / Open Sans primary
         reproduces the original case from multilingual_widgets.
=============================================================================*/
#include <elements.hpp>
#include <utility>

using namespace cycfi::elements;

auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

auto framed_label(char const* text, char const* family, char const* locale_tag = "")
{
   auto lbl =
      label(text)
         .font(font_descr{family})
         .font_color(colors::antique_white)
         .font_size(28)
         .text_align(canvas::left)
         .locale(locale_tag);
   return layer(margin({4, 0, 4, 0}, std::move(lbl)), frame{});
}

auto caption_label(char const* text)
{
   return
      label(text)
         .font(font_descr{"Open Sans"}.semi_bold())
         .font_color(colors::light_gray)
         .font_size(12)
         .text_align(canvas::left);
}

auto make_case(char const* tag, char const* text, char const* family,
               char const* locale_tag = "")
{
   return
      margin({0, 8, 0, 8},
         vtile(
            caption_label(tag),
            margin_top(3, framed_label(text, family, locale_tag))
         )
      );
}

auto make_content()
{
   auto title =
      label("Multilingual layout isolation test")
         .font(font_descr{"Open Sans"}.bold())
         .font_color(colors::white)
         .font_size(20)
         .text_align(canvas::left);

   return
      margin({30, 24, 30, 24},
         vtile(
            std::move(title),
            margin_top(14,
               make_case(
                  "[A] Latin only / Open Sans primary  (sanity check)",
                  "The quick brown fox jumps over the lazy dog.",
                  "Open Sans")),
            make_case(
               "[B] CJK only / Open Sans primary (falls back to Noto Sans JP)",
               "今日もコードを書こう。",
               "Open Sans"),
            make_case(
               "[C] CJK only / Noto Sans JP primary (no fallback)",
               "今日もコードを書こう。",
               "Noto Sans JP", "ja-JP"),
            make_case(
               "[D] Mixed / Open Sans primary (original bug case)",
               "Hello 今日もコードを書こう World",
               "Open Sans")
         )
      );
}

int main(int argc, char* argv[])
{
   app _app("Multilingual Layout Test");
   window _win(_app.name(), window::standard, {50, 50, 820, 560});
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);

   view_.content(
      make_content(),
      background
   );

   _app.run();
   return 0;
}

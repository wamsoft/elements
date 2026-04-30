/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements.hpp>

using namespace cycfi::elements;

// Main window background color
auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

// Demonstrates the FT loader's per-codepoint fallback. Open Sans is the
// primary font; CJK and emoji codepoints are resolved through the chain
// (Noto Sans JP, Noto Sans TC, Noto Emoji).
struct sample_row
{
   char const* caption;
   char const* text;
};

sample_row const fallback_samples[] =
{
   { "English (primary)",
     "The quick brown fox jumps over the lazy dog." },

   { "Japanese only — falls back to Noto Sans JP",
     "いろはにほへと ちりぬるを わかよたれそ つねならむ" },

   { "Japanese + English mix",
     "Elements は C++ の軽量 GUI ライブラリです。" },

   { "Text + monochrome emoji — falls back to Noto Emoji",
     "Wave \U0001F44B  Laptop \U0001F4BB  Party \U0001F389  Rocket \U0001F680" },

   { "Full mix (English + Japanese + emoji)",
     "Hi \U0001F44B  今日も Elements でコードを書こう \U0001F4BB \U0001F338" },
};

// Side-by-side comparison: identical CJK Unified Ideograph string rendered
// with two different primary fonts and BCP47 locale tags. Noto Sans JP +
// "ja-JP" picks Japanese glyph variants; Noto Sans TC + "zh-TW" picks
// Traditional Chinese variants. The shapes of 直, 骨, 次, 海 etc. visibly differ.
char const* const cjk_compare_text = "直 骨 次 海 雪 公 — 同じ Unicode コードポイント";

auto make_label(char const* text, char const* family, float size, std::string locale_tag = {})
{
   return label(text)
      .font(font_descr{family})
      .font_color(colors::antique_white)
      .font_size(size)
      .text_align(canvas::left)
      .locale(std::move(locale_tag));
}

auto make_row(sample_row const& s)
{
   auto caption =
      label(s.caption)
         .font(font_descr{"Open Sans"}.semi_bold())
         .font_color(colors::light_gray)
         .font_size(12)
         .text_align(canvas::left)
      ;

   auto body = make_label(s.text, "Open Sans", 20);

   return
      margin({0, 6, 0, 6},
         vtile(
            std::move(caption),
            margin_top(2, std::move(body))
         )
      );
}

auto make_locale_compare()
{
   auto caption =
      label("Locale-sensitive glyph variants (same codepoints, different fonts/locales)")
         .font(font_descr{"Open Sans"}.semi_bold())
         .font_color(colors::light_gray)
         .font_size(12)
         .text_align(canvas::left)
      ;

   auto jp_caption =
      label("ja-JP / Noto Sans JP")
         .font(font_descr{"Open Sans"})
         .font_color(colors::light_sky_blue)
         .font_size(11)
         .text_align(canvas::left)
      ;

   auto sc_caption =
      label("zh-CN / Noto Sans SC")
         .font(font_descr{"Open Sans"})
         .font_color(colors::light_sky_blue)
         .font_size(11)
         .text_align(canvas::left)
      ;

   auto tc_caption =
      label("zh-TW / Noto Sans TC")
         .font(font_descr{"Open Sans"})
         .font_color(colors::light_sky_blue)
         .font_size(11)
         .text_align(canvas::left)
      ;

   auto jp_body = make_label(cjk_compare_text, "Noto Sans JP", 20, "ja-JP");
   auto sc_body = make_label(cjk_compare_text, "Noto Sans SC", 20, "zh-CN");
   auto tc_body = make_label(cjk_compare_text, "Noto Sans TC", 20, "zh-TW");

   return
      margin({0, 6, 0, 6},
         vtile(
            std::move(caption),
            margin_top(6, std::move(jp_caption)),
            margin_top(2, std::move(jp_body)),
            margin_top(6, std::move(sc_caption)),
            margin_top(2, std::move(sc_body)),
            margin_top(6, std::move(tc_caption)),
            margin_top(2, std::move(tc_body))
         )
      );
}

auto make_content()
{
   auto rows = vtile(
      make_row(fallback_samples[0]),
      make_row(fallback_samples[1]),
      make_row(fallback_samples[2]),
      make_row(fallback_samples[3]),
      make_row(fallback_samples[4])
   );

   auto title =
      label("Multilingual text — ThorVG FT loader")
         .font(font_descr{"Open Sans"}.bold())
         .font_color(colors::white)
         .font_size(22)
         .text_align(canvas::left)
      ;

   return
      margin({30, 24, 30, 24},
         vtile(
            std::move(title),
            margin_top(18, std::move(rows)),
            margin_top(20, make_locale_compare())
         )
      );
}

int main(int argc, char* argv[])
{
   app _app("Multilingual Text");
   window _win(_app.name(), window::standard, {50, 50, 900, 800});
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);

   view_.content(
      make_content(),
      background
   );

   _app.run();
   return 0;
}

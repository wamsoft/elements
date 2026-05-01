/*=============================================================================
   Copyright (c) 2016-2023 Joel de Guzman

   Distributed under the MIT License (https://opensource.org/licenses/MIT)
=============================================================================*/
#include <elements.hpp>

using namespace cycfi::elements;

// Validation sample for multilingual widget layout. Every widget label below
// mixes Latin, Japanese, and monochrome emoji so that ThorVG's FT loader has
// to fall back across Open Sans → NotoSansJP → NotoEmoji. If per-glyph
// fallback metrics or vertical line metrics are wrong, frame and content
// disagree visibly.

auto constexpr bkd_color = rgba(35, 35, 37, 255);
auto background = box(bkd_color);

constexpr auto bred   = colors::red.opacity(0.4);
constexpr auto bgreen = colors::green.level(0.7).opacity(0.4);
constexpr auto bblue  = colors::blue.opacity(0.4);
constexpr auto brblue = colors::royal_blue.opacity(0.4);

auto make_button_row()
{
   auto m  = button("Save \U0001F4BE 保存");
   auto t  = toggle_button("Mute \U0001F507 ミュート", 1.0, bred);
   auto l  = latching_button("Lock \U0001F512 ロック", 1.0, bgreen);
   auto ic = button(icons::cog, "Settings 設定 \U0001F527", 1.0, brblue);

   auto styled_left =
      momentary_button(
         button_styler{"Left 左 \U00002B05"}
            .align_left()
            .icon(icons::left_circled)
            .icon_left()
            .body_color(bred)
            .rounded_left(10)
      );

   auto styled_center =
      momentary_button(
         button_styler{"Center 中央 \U0001F3AF"}
            .body_color(bblue)
            .corner_radius(0)
      );

   auto styled_right =
      momentary_button(
         button_styler{"Right 右 \U000027A1"}
            .align_right()
            .icon(icons::right_circled)
            .body_color(bgreen)
            .corner_radius(0, 10, 10, 0)
      );

   return
      group("Buttons / ボタン \U0001F518",
         margin({10, 35, 10, 10},
            vtile_spaced(10.0,
               m,
               t,
               l,
               ic,
               hgrid(styled_left, styled_center, styled_right)
            )
         )
      );
}

auto make_check_radio_row()
{
   auto cb1 = check_box("Enable notifications \U0001F514 通知");
   auto cb2 = check_box("Save on exit 終了時に保存 \U0001F4BE");
   auto cb3 = check_box("Dark mode 暗色モード \U0001F319");
   cb1.value(true);
   cb3.value(true);

   auto check_boxes =
      group("Check Boxes / チェック \U00002705",
         margin({10, 35, 10, 10},
            vtile_spaced(8.0,
               align_left(cb1),
               align_left(cb2),
               align_left(cb3)
            )
         )
      );

   auto rb1 = radio_button("English (英語) \U0001F1FA\U0001F1F8");
   auto rb2 = radio_button("Japanese (日本語) \U0001F1EF\U0001F1F5");
   auto rb3 = radio_button("Mix 混在 \U0001F30F");
   rb2.select(true);

   auto radios =
      group("Radios / ラジオ \U0001F4FB",
         margin({10, 35, 10, 10},
            vtile_spaced(8.0,
               align_left(rb1),
               align_left(rb2),
               align_left(rb3)
            )
         )
      );

   return htile_spaced(15.0, check_boxes, radios);
}

auto make_label_row()
{
   auto title =
      label("Title 見出し \U0001F4DA")
         .font(font_descr{"Open Sans"}.bold())
         .font_color(colors::antique_white)
         .font_size(22)
         .text_align(canvas::left);

   auto sub =
      label("Subtitle — 副題 \U0001F4D6 mixed Latin / 日本語 / emoji")
         .font(font_descr{"Open Sans"}.semi_bold())
         .font_color(colors::light_sky_blue)
         .font_size(14)
         .text_align(canvas::left);

   auto body =
      label("Hi \U0001F44B  今日も Elements でコードを書こう \U0001F4BB \U0001F338")
         .font(font_descr{"Open Sans"})
         .font_color(colors::light_gray)
         .font_size(16)
         .text_align(canvas::left);

   return
      group("Labels / ラベル \U0001F3F7",
         margin({10, 35, 10, 10},
            vtile_spaced(6.0, title, sub, body)
         )
      );
}

auto make_input_row()
{
   auto in1 = input_box("Type here / ここに入力 \U0001F4DD").first;
   auto in2 = input_box("Search 検索 \U0001F50D").first;

   return
      group("Inputs / 入力 \U0001F4E5",
         margin({10, 35, 10, 10},
            vtile_spaced(10.0,
               in1,
               in2
            )
         )
      );
}

auto make_content()
{
   return
      margin({20, 20, 20, 20},
         vtile_spaced(15.0,
            make_label_row(),
            make_button_row(),
            make_check_radio_row(),
            make_input_row()
         )
      );
}

int main(int argc, char* argv[])
{
   app _app("Multilingual Widgets");
   window _win(_app.name(), window::standard, {50, 50, 760, 820});
   _win.on_close = [&_app]() { _app.stop(); };

   view view_(_win);

   view_.content(
      make_content(),
      background
   );

   _app.run();
   return 0;
}

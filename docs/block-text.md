# Block text — rectangle flow with a host-supplied line breaker

`static_text_box` wraps text with Elements' own width-greedy word wrap
(`master_glyphs::break_lines`). That is fine for a scrolling text pane, but not
for a caption: it has no script awareness, no Japanese 行頭/行末禁則, and no way
to reveal the text a character at a time. And when the application *also* draws
the same string with its own text engine, the two disagree about where the lines
break.

Block text fixes both by making the line breaking replaceable:

- `block_text_backend` (`support/block_text.hpp`) — the injection seam. A host
  installs its own implementation; without one a built-in fallback keeps the
  widget working.
- `block_text_box` (`element/block_text.hpp`) — the widget: alignment, extra
  line spacing, base direction, and a cluster `count` limit for a typewriter
  reveal. Exposed to JSON layouts as `text_area` (see
  `external/elements_modal/README.md`).

Glyph rendering is untouched: lines are drawn through the normal `tvg::Text`
path, so a `text_area` looks like every other label in the same view.

## What the backend answers

```cpp
struct block_text_request {
    std::string_view  text;
    std::string       font_key;      // font::file() — whatever key the host registered
    std::string       font_family;
    font const*       fnt;           // for the built-in fallback
    float size, width, height, line_spacing;
    int   align;   // left / center / right
    int   base;    // auto / ltr / rtl
    int   count;   // cluster limit; < 0 = no limit
};

struct block_text_line {
    std::size_t start, end;     // the line's range in the source text
    std::size_t reveal_end;     // where `count` cuts it (== end when nothing is hidden)
    float x, y;                 // alignment offset, baseline offset
    float width;                // full line width, ignoring `count`
    int   clusters, total_clusters;
};
```

So the backend decides **where the lines break** and **where the reveal cuts**,
nothing else. It never produces glyphs. That is what keeps the seam small: a
host that already owns a shaper can answer this from data it has, and the widget
still renders through ThorVG.

`count` is measured in *clusters* — one shaped draw unit (a ligature, a base
plus its combining marks, an emoji ZWJ sequence) — not codepoints, so a reveal
never splits a grapheme. `count_clusters()` reports the total the reveal counts
up to.

## Rules the widget relies on

- **Wrapping is resolved for the whole text before `count` applies.** Advancing
  a reveal must not reflow what is already on screen, so a backend must not let
  the visible prefix decide the break positions.
- **`block_text_line::width` is the full line width**, not the revealed part.
  Alignment uses it, so a centered caption does not drift as it types out.
- Lines are returned in order and cover the text without overlapping.
- A backend that cannot serve a request (unknown font key, say) returns `false`;
  the widget then draws nothing rather than falling back silently to a different
  wrap, which would be worse — a caption that quietly re-breaks is harder to
  notice than one that is missing.

## Built-in fallback

`get_block_text_backend()` never returns null. With no host backend installed it
returns an implementation over `master_glyphs::break_lines`, counting codepoints
as clusters. Alignment, line spacing, the height bound and the `count` reveal all
work; only the break quality differs. This keeps `text_area` usable in plain
Elements applications.

## Host example: 吉里吉里Z

The engine implements the backend with **glyphware**, its unified font engine
(FreeType + HarfBuzz + SheenBidi), so `text_area` and the engine's own
`Layer.drawShapedTextArea` share one `layoutBlock()` implementation: paragraph
splitting, word / per-character (CJK) wrapping, 行頭/行末禁則, alignment and the
cluster limit. The same string at the same width breaks identically whether the
engine draws it into a layer or an Elements widget draws it — which is the point
of the seam.

The font chain is rebuilt from the widget's own key plus the theme families, in
theme order, so the faces used for measuring match the ones ThorVG falls back to
per codepoint when drawing.

## Notes

- `limits()` reports a minimum width (200px, matching `static_text_box`, or the
  block's own width when it is narrower). Reporting 0 would let a
  fit-to-content parent collapse the block to nothing.
- The layout is cached per (width, height, count); `set_text` / `set_align` /
  `set_base_direction` / `set_line_spacing` mark it dirty.

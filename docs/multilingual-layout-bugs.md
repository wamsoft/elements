# 多言語テキストでレイアウトが崩れる件 — 原因 2 つ

`TVG_LOADER_FT=ON` (FreeType + HarfBuzz) で日本語等を含むテキストを描いたとき、
Latin プライマリ + CJK fallback の構成 (例: `font_descr{"Open Sans"}` で
`"こんにちは"` を描く) で、ボタン / ラベル / グループ等の **見た目上のサイズ
と中身のテキストが噛み合わない**。具体的には:

- ボタンの幅が日本語テキストより明らかに狭く、文字が左右にはみ出す
- 上下方向にも余白の取り方がおかしい (テキストが浮いて見える / 切れる)

原因は **ThorVG fork (`wtnbgo/thorvg cmake` ブランチ) 側の 2 つの独立したバグ**
で、Elements 側のコードは正しい。

## 再現

`examples/multilingual_text/` の 1 行目「English (primary)」用ボタン
(`make_label` で生成) を **`label` ではなく `button` に変更** すると分かりやすい。
Latin のみの行は中身ピッタリで描画されるが、日本語混在の行はボタン枠が狭くて
はみ出す。同様に `examples/buttons/` の `text` を `"こんにちは"` 等に書き換える
だけで再現する。

検証用スニペット (任意の example の中で):

```cpp
auto btn = button("こんにちは");        // Open Sans プライマリ + NotoSansJP fallback
// → btn の view_limits が "こんにちは" の見た目幅より大きく狭い
```

---

## バグ 1: per-glyph metrics でフォールバックが効かない

**ファイル**: `src/loaders/ft/tvgFtLoader.cpp`
**関数**: `FtLoader::metrics(const FontMetrics& fm, const char* ch, GlyphMetrics& out)` (現状 line 512〜528)

### 現状コード

```cpp
bool FtLoader::metrics(const FontMetrics& fm, const char* ch, GlyphMetrics& out)
{
    if (!ch || !ftFace.face || fm.fontSize <= 0.0f) return false;
    auto p = ch;
    auto code = decodeUtf8(&p, ch + strlen(ch));
    auto gid = ftFace.glyphIndex(code);
    if (gid == 0) return false;                // ← プライマリに無いと即 false

    auto scale = (fm.fontSize * FontLoader::DPI) / static_cast<float>(ftFace.unitsPerEm());
    out.advance = static_cast<float>(ftFace.advance(gid)) * scale;
    out.bearing = 0.0f;
    out.min = {0, 0};
    out.max = {out.advance, 0};
    return true;
}
```

### 問題

フルテキスト shaping 経路 (`generate()` 内 line 206〜212) は
`FtFontManager::fallback(cp, primary)` を介して fallback face で必ずグリフを
解決する:

```cpp
auto resolve = [&](uint32_t cp) -> FtFace* {
    if (ftFace.glyphIndex(cp) != 0) return &ftFace;
    if (auto* fb = mgr.fallback(cp, &ftFace)) return fb;
    return &ftFace;
};
```

ところが **per-glyph `metrics(ch, out)` は primary face しか引かない**。
プライマリに無い codepoint (CJK / 絵文字) に対しては `gid == 0` で `false`
を返す。

### 影響

呼び側 (Elements `lib/src/support/text_backend_tvg.cpp::measure_text`) は
codepoint ごとに `text->metrics(ch, gm)` を呼んで advance を加算しており、
fallback で描画されるはずの文字は **全部 advance=0 として集計される**。結果、
ボタン等の見た目の幅が日本語テキストの実描画幅より大幅に狭くなり、文字がはみ出す。

```cpp
// elements/lib/src/support/text_backend_tvg.cpp の measure_text:
float width = 0;
for (const char* c = utf8; *c; ) {
    tvg::GlyphMetrics gm;
    int len = ...;
    std::string ch(c, len);
    if (text->metrics(ch.c_str(), gm) == tvg::Result::Success)
        width += gm.advance;     // ← fallback のはずの文字が来ない
    c += len;
}
```

### 修正案

フルテキスト経路と同じ resolve ロジックを per-glyph 版にも入れる:

```cpp
bool FtLoader::metrics(const FontMetrics& fm, const char* ch, GlyphMetrics& out)
{
    if (!ch || !ftFace.face || fm.fontSize <= 0.0f) return false;
    auto p = ch;
    auto code = decodeUtf8(&p, ch + strlen(ch));

    //Apply the same primary→fallback resolve order as the full-text shaping
    //path (resolve() in generate()). Without this, callers that compute a
    //string's total width by summing per-glyph advances underestimate widths
    //whenever the primary font lacks the glyph (CJK / emoji), causing layout
    //overflow.
    auto* face = &ftFace;
    auto gid = face->glyphIndex(code);
    if (gid == 0) {
        if (auto* fb = FtFontManager::instance().fallback(code, &ftFace)) {
            face = fb;
            gid = face->glyphIndex(code);
        }
    }
    if (gid == 0) return false;

    auto upem = face->unitsPerEm();
    if (upem == 0) return false;
    auto scale = (fm.fontSize * FontLoader::DPI) / static_cast<float>(upem);
    out.advance = static_cast<float>(face->advance(gid)) * scale;
    out.bearing = 0.0f;
    out.min = {0, 0};
    out.max = {out.advance, 0};
    return true;
}
```

ポイント:

- `face` を可変参照に変えて、必要なら `mgr.fallback()` 結果に差し替える
- スケール計算は **採用された face の unitsPerEm** で行う (プライマリの upem
  ではない)。`face->advance(gid) * scale` でピクセル advance が出る

---

## バグ 2: TextMetrics の linegap が 2×|descent| ぶん大きく評価される

**ファイル**: `src/loaders/ft/tvgFtLoader.cpp`
**関数**: `FtLoader::metrics(const FontMetrics& fm, TextMetrics& out)` (現状 line 499〜509)

### 現状コード

```cpp
void FtLoader::metrics(const FontMetrics& fm, TextMetrics& out)
{
    auto scale = (fm.fontSize * FontLoader::DPI) / static_cast<float>(ftFace.unitsPerEm());
    out.advance = static_cast<float>(ftFace.lineHeight()) * scale;
    out.ascent = static_cast<float>(ftFace.ascent()) * scale;
    out.descent = static_cast<float>(ftFace.descent()) * scale;
    //FreeType doesn't surface linegap directly via the public face fields;
    //hhea.lineGap is roughly height - (ascender - descender). Approximate it.
    out.linegap = out.advance - (out.ascent + out.descent);   // ← 符号誤り
    if (out.linegap < 0.0f) out.linegap = 0.0f;
}
```

### 問題

FreeType の規約:

- `face->ascender` は正値
- `face->descender` は **負値**
- `face->height` (= lineHeight) = `ascender - descender + lineGap`
  (descender が負なので `ascender + |descender| + lineGap`)

したがって `lineGap = height - (ascender - descender)` 。コードに置き換えると
`descent` が負値で来るので:

```
lineGap = advance - ascent + descent   // ← descent は負値、つまり |descent| を引く
```

ところが現状コードは `advance - (ascent + descent)` と括っているので、
descent の符号が逆に効いて `advance - ascent - descent` (= `advance - ascent +
|descent|`) になる。コメントの直感 (「`height - (ascender - descender)`」) と
**逆符号**。

具体例 (Open Sans 16px、UPM=1000、ascent=800、descent=-200、height=1100、
真の lineGap=100、scale=0.016):

| 値 | 真値 (px) | 現状コード (px) |
|---|---|---|
| out.ascent | 12.8 | 12.8 |
| out.descent | -3.2 | -3.2 |
| out.advance (lineHeight) | 17.6 | 17.6 |
| out.linegap | 1.6 | **8.0** ← +6.4 過剰 |

### 影響

呼び側 (Elements `text_utils.cpp::measure_text`) はテキスト 1 行の高さを
`ascent + descent + leading` で計算している:

```cpp
auto info = cnv.measure_text(text);
auto height = info.ascent + info.descent + info.leading;
return {info.size.x, height};
```

`info.descent` は `text_backend_tvg.cpp` で `-tm.descent` と符号反転して正値で
返るので、`ascent + descent` は典ascenderHeight。これに過大な leading が乗る
ので、**ボタンの最小高さが本来より大幅に大きくなる** + テキスト中央配置で
余計に下に寄る等の症状が出る。

### 修正案

```cpp
void FtLoader::metrics(const FontMetrics& fm, TextMetrics& out)
{
    auto scale = (fm.fontSize * FontLoader::DPI) / static_cast<float>(ftFace.unitsPerEm());
    out.advance = static_cast<float>(ftFace.lineHeight()) * scale;
    out.ascent = static_cast<float>(ftFace.ascent()) * scale;
    out.descent = static_cast<float>(ftFace.descent()) * scale;   //FT 規約で負値
    //hhea: face->height = ascender - descender + lineGap (descender < 0).
    //So lineGap = height - (ascender - descender) = advance - ascent + descent.
    out.linegap = out.advance - out.ascent + out.descent;
    if (out.linegap < 0.0f) out.linegap = 0.0f;
}
```

差分は `(out.ascent + out.descent)` → `out.ascent - out.descent` に相当
(算術的には `+ out.descent` の符号反転で同じ)。

---

## バグ 3: `FtFace::descent()` が符号反転していて TextMetrics の規約を破る

**ファイル**: `src/loaders/ft/tvgFtFace.cpp`
**関数**: `FtFace::descent()` (修正前 line 270〜275)

### 現状コード (修正前)

```cpp
int16_t FtFace::descent() const
{
    if (!face) return 0;
    //FreeType reports descender as a negative value; thorvg uses positive.
    return static_cast<int16_t>(-face->descender);
}
```

### 問題

ThorVG-wide の規約では `TextMetrics::descent` は **負値**。これは
`src/loaders/ttf/tvgTtfReader.cpp` の `metrics.hhea.descent = _i16(...)`
(signed そのまま渡す) と、`tvgFtLoader.cpp::metrics()` の
`out.descent = ftFace.descent() * scale; //negative per FT convention`
というコメントに整合する。

ところが `FtFace::descent()` は FT の `face->descender` (負) を **反転して
正にして** 返している。コメント (`thorvg uses positive`) は誤り。結果、
`FtLoader::metrics(TextMetrics&)` 経由で出力される `tm.descent` は **正値**
になってしまう。

### 影響

呼び側 (Elements `text_backend_tvg.cpp::measure_text` 等) は
TextMetrics 規約 (descent は負) を信じて符号を反転している:

```cpp
float ascent = tm.ascent, descent = -tm.descent, leading = tm.linegap;
return { ascent, descent, leading, {width, ascent + descent} };
```

`tm.descent` が正値 (バグ) のため、`descent = -tm.descent` は **負値** になり、
`text_metrics.size.y = ascent + descent = ascent - |descent|` で descender
分が引かれる。

さらに `text_utils.cpp::measure_text` の高さ計算でも:

```cpp
auto height = info.ascent + info.descent + info.leading;
```

`info.descent` が負なので、結局 **descender 分の高さがフレームに含まれない**。
症状として `examples/multilingual_layout_test/` の Latin オンリー sanity case
[A] で `y` / `j` の descender がフレーム下からはみ出す現象が出る (CJK 関係なし)。

CJK fallback と組み合わさると、フレーム高は ascent ベースで小さく見積もら
れた状態のまま、描画は (FtLoader::transform で primary ascent ぶん下げてから)
fallback face のグリフが本来の descender ぶん下に伸びるため、結果 baseline
直下〜数 px のはみ出しになる。Bug 1 の per-glyph fallback 不在とは独立。

### 修正

```cpp
int16_t FtFace::descent() const
{
    if (!face) return 0;
    return face->descender;     //FT 規約 (負) のまま返す
}
```

`FtLoader::metrics()` 側はコメント通り `negative per FT convention` でそのまま
動く。bug 2 の linegap 計算 (`advance - ascent + descent`) も負 descent 前提で
正しい (TtfLoader と同じ式)。

`FtFace::descent()` の他の呼び出し箇所は `FtLoader::metrics()` の一箇所のみ
(`grep -n 'ftFace\.descent()' src/`) なので副作用なし。

### 検証

`examples/multilingual_layout_test/` の 4 ケース:

| ケース | primary | テキスト | 修正前 | 修正後 期待 |
|---|---|---|---|---|
| [A] | Open Sans | Latin (`y`/`j` 含む) | descender はみ出し | 収まる |
| [B] | Open Sans | CJK 100% (fallback) | はみ出し | (Bug 任意 B の対象) |
| [C] | Noto Sans JP | CJK 100% (no fallback) | はみ出し | 収まる |
| [D] | Open Sans | mixed | 日本語のみはみ出し | (Bug 任意 B の対象) |

[A] と [C] が「修正後 収まる」ことが確認できれば本バグは解消。
[B] と [D] のわずかなはみ出しは、primary より tall な fallback face の
descent を反映できていない問題 (バグ 任意 B 案) のみの状態になる。

---

## 任意: バグ 1 の延長としての TextMetrics fallback (検討事項)

`metrics(fm, TextMetrics& out)` も **プライマリ face のみ** を見ているので、
プライマリより背の高い fallback 字 (例: 漢字) を含む文章では ascent / descent
が小さく見積もられ、ボタン等の box に対してテキストが上下にはみ出す可能性が
ある。ただしこれは「テキストの内容を見ないと判断できない」ので、
`TextMetrics` 単独の API では限界がある。

可能な対応:

A. **元のテキスト依存にしない (現状ママ)**: line height は primary 基準。
   過小評価のリスクはあるが、Noto 系は Latin/CJK で metrics が近いので実害が
   出ないケースが多い。ホスト側 (Elements) で box の高さに余裕を持たせるのが
   正攻法。

B. **登録済み全 face の max を取る**: `FtFontManager` の全 face を走査して
   ascent / descent / lineHeight の最大値を返す。Latin のみのテキストでも
   過大評価 (= 余白多め) になる副作用あり。ただし「fallback を登録した時点で
   ある程度のサイズ余裕は許容する」という割り切りができれば実用的。

C. **新 API 追加**: `Text::metrics(TextMetrics&)` を「既に `text(utf8)` で
   セット済みのテキストに対して、実際に shaping で採用される faces の union
   metrics を返す」セマンティクスに変える、または別 API として追加する。
   ホスト側にとっては最も使いやすいが、API surface が増える。

A が現状、B が低工数で改善、C が理想形。どれを採用するかは upstream 取り込み時
の議論次第。

---

## 検証手順

1. `examples/multilingual_text/main.cpp` のうち `make_label(s.text, ...)` を
   一旦 button にしてビルド (`make build`)
2. 修正前: 日本語混在行のボタン枠から文字がはみ出す + 上下に違和感
3. 上記バグ 1 / 2 を `tvgFtLoader.cpp` に当てて再ビルド
4. 修正後: 枠と中身が一致、上下も自然に収まる

upstream PR にする際は両バグを別コミットに分けるとレビューしやすい
(独立したバグなので)。

---

## 参考: 関連ファイル

- ThorVG fork: `src/loaders/ft/tvgFtLoader.cpp` (バグ箇所両方)
- ThorVG fork: `src/loaders/ft/tvgFtFontManager.{h,cpp}` (`fallback()` API)
- Elements: `lib/src/support/text_backend_tvg.cpp::measure_text` (per-glyph 集計の呼び側)
- Elements: `lib/src/support/text_utils.cpp::measure_text` (高さ算出の呼び側)

#!/usr/bin/env bash
#-----------------------------------------------------------------------------
# copy_kenney_assets.sh
#
# Kenney Input Prompts pack の必要ファイルだけを elements_modal の pad_icon
# が探しに行くレイアウトでコピーするスクリプト。 元 pack は再配布不可なので、
# 各環境でローカルにダウンロード → このスクリプトで配置 → ホスト側で
# set_pad_icon_base_dir() に dst を渡す。
#
# 元 pack: https://kenney.nl/assets/input-prompts (CC0)
#
# 使い方 (consuming repo の root で実行する想定):
#   ./external/elements_modal/scripts/copy_kenney_assets.sh
#   ./external/elements_modal/scripts/copy_kenney_assets.sh <src> [<dst>]
#
# デフォルト (実行時の CWD 基準):
#   src = ./kenney_input-prompts            (Kenney zip を展開した directory)
#   dst = ./resources/kenny_input_prompts   (pad_icon が読みに行く directory)
#
# 対応テーマ:
#   "Xbox Series"        → xbox/
#   "PlayStation Series" → ps/
#   "Nintendo Switch"    → switch/
#   "Keyboard & Mouse"   → keyboard/
#
# 各テーマで <theme>/vector/*.svg と <theme>/<font.ttf|.otf|_map.txt> を配置。
#-----------------------------------------------------------------------------
set -euo pipefail

base_dir="$(pwd)"

src="${1:-$base_dir/kenney_input-prompts}"
dst="${2:-$base_dir/resources/kenny_input_prompts}"

if [ ! -d "$src" ]; then
   echo "error: source directory not found: $src" >&2
   echo "       download from https://kenney.nl/assets/input-prompts and unzip there," >&2
   echo "       or pass an explicit source path as the first argument." >&2
   exit 1
fi

# theme display name → destination subdir
declare -a themes=(
   "Xbox Series:xbox"
   "PlayStation Series:ps"
   "Nintendo Switch:switch"
   "Keyboard & Mouse:keyboard"
)

mkdir -p "$dst"

copied_total=0
for pair in "${themes[@]}"; do
   theme_name="${pair%%:*}"
   theme_dir="${pair##*:}"

   src_theme="$src/$theme_name"
   dst_theme="$dst/$theme_dir"

   if [ ! -d "$src_theme" ]; then
      echo "  skip: $theme_name (not found in source)" >&2
      continue
   fi

   mkdir -p "$dst_theme/vector"

   # SVGs (vector). cp -u だと msys2 で挙動不安定なので、 全コピー上書き。
   svg_count=0
   if [ -d "$src_theme/Vector" ]; then
      shopt -s nullglob
      svgs=( "$src_theme/Vector"/*.svg )
      if [ ${#svgs[@]} -gt 0 ]; then
         cp -f "${svgs[@]}" "$dst_theme/vector/"
         svg_count=${#svgs[@]}
      fi
      shopt -u nullglob
   fi

   # Fonts + map.txt
   font_count=0
   if [ -d "$src_theme/Fonts" ]; then
      shopt -s nullglob
      fonts=( "$src_theme/Fonts"/* )
      if [ ${#fonts[@]} -gt 0 ]; then
         cp -f "${fonts[@]}" "$dst_theme/"
         font_count=${#fonts[@]}
      fi
      shopt -u nullglob
   fi

   echo "  $theme_name → $theme_dir/ : $svg_count svgs, $font_count font files"
   copied_total=$((copied_total + svg_count + font_count))
done

# License (CC0) — pack 同梱の License.txt を配布物へそのまま同梱できるようコピー
if [ -f "$src/License.txt" ]; then
   cp -f "$src/License.txt" "$dst/License.txt"
   echo "  license: License.txt → $dst/"
else
   echo "  note: License.txt not found in source pack" >&2
   echo "        (Input Prompts by Kenney, CC0: https://kenney.nl/assets/input-prompts)" >&2
fi

echo
echo "done. total files copied: $copied_total"
echo "destination: $dst"

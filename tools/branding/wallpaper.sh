#!/usr/bin/env bash
# Compose the AtriOS wallpaper (1600x900) and the U-Boot boot splash BMP
# from the canonical mark (docs/assets/atrios-mark.png, rendered by
# tools/branding/raster.py).
#
# Usage: wallpaper.sh [mark.png] [out-dir]
#   out-dir receives: atrios-logo.png (1600x900) — commit to docs/assets/
#                     atrios-station-720p.bmp — commit to packages/blobs/splash/
set -euo pipefail

MARK="${1:-docs/assets/atrios-mark.png}"
OUT="${2:-.}"
FONT="$(fc-match -f '%{file}' 'DejaVu Sans:style=Bold')"

# dark gradient background
magick -size 1600x900 gradient:"#141a2e"-"#05070d" "$OUT/bg.png"

# mark centered, slightly above middle
magick "$OUT/bg.png" \
  \( "$MARK" -resize 780x780 \) -gravity center -geometry +0-40 -composite \
  -font "$FONT" -pointsize 150 -fill "#f5f8fc" -gravity center \
      -annotate +0+120 "ATRIOS" \
  -pointsize 38 -fill "#7c8ba1" \
      -annotate +0+235 "A T R I S T A T I O N" \
  "$OUT/atrios-logo.png"

rm -f "$OUT/bg.png"

# boot splash: 1280x720 24bpp BMP (U-Boot 'bmp display' format)
magick "$OUT/atrios-logo.png" -resize 1280x720! -depth 8 -type TrueColor \
  BMP3:"$OUT/atrios-station-720p.bmp"

echo "done: $OUT/atrios-logo.png + $OUT/atrios-station-720p.bmp"

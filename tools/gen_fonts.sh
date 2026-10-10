#!/bin/sh
# Regenerates the Vietnamese-capable LVGL fonts used by the HUD.
# Needs: python3 + Pillow (pip install pillow) and a TTF with Vietnamese glyphs.
# DejaVu Sans is used by default (free licence, redistribution allowed).
#
# Usage: tools/gen_fonts.sh [/path/to/font.ttf]
set -e
cd "$(dirname "$0")/.."
TTF="${1:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}"
VN="20-7E C0-FF 102,103,110,111,128,129,168,169,1A0,1A1,1AF,1B0 1EA0-1EF9"
python3 tools/gen_lv_font.py "$TTF" 14 vn_font_14 src/vn_font_14.c $VN
python3 tools/gen_lv_font.py "$TTF" 18 vn_font_18 src/vn_font_18.c $VN
# Turn arrows for the "Sau:" preview: ↑ ↖ ↗ ↰ ↱ ↶ ↻ ●  (needs a font that has them)
python3 tools/gen_lv_font.py "$TTF" 22 vn_arrows_22 src/vn_arrows_22.c 2191,2196,2197,21B0,21B1,21B6,21BB,25CF
# Arduino IDE builds the sketch folder, so keep copies next to the .ino.
cp src/vn_font_14.c src/vn_font_18.c src/vn_arrows_22.c .

# Alternative with the original Montserrat look (needs node + lv_font_conv):
#   lv_font_conv --font Montserrat-Medium.ttf --size 18 --bpp 4 --no-compress \
#     -r 0x20-0x7E -r 0xC0-0xFF -r 0x102-0x103 -r 0x110-0x111 -r 0x128-0x129 \
#     -r 0x168-0x169 -r 0x1A0-0x1A1 -r 0x1AF-0x1B0 -r 0x1EA0-0x1EF9 \
#     --format lvgl --lv-font-name vn_font_18 -o src/vn_font_18.c

#!/usr/bin/env python3
"""Convert approved art and the OFL font to native embedded-display assets."""
from pathlib import Path
import argparse
import struct
import sys
sys.dont_write_bytecode = True
from PIL import Image
from pack_cards import convert_font

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    font = ROOT / 'assets/fonts/NotoSansSC-SemiBold.ttf'
    if not font.exists():
        raise FileNotFoundError('Restore assets/fonts/NotoSansSC-SemiBold.ttf and its OFL license before preparing assets')
    ui = (ROOT / 'assets/fonts/ui-24.txt').read_text(encoding='utf-8')
    ui += ''.join(chr(n) for n in range(32, 127))
    convert_font(font, args.converter.resolve(), ui, 24, ROOT / 'assets/fonts/travel_ui_font_24.c', 'lvgl', 'travel_ui_font_24')
    convert_font(font, args.converter.resolve(), '家长设置蓝牙配网目的地忘记联网确认内容包', 36,
                 ROOT / 'assets/fonts/travel_ui_font_36.c', 'lvgl', 'travel_ui_font_36')
    image = Image.open(ROOT / 'assets/images/koala-sprite-source.png').convert('RGBA')
    alpha = image.getchannel('A')
    if alpha.getextrema()[0] != 0 or alpha.getextrema()[1] != 255:
        raise ValueError('the mascot must have actual transparency and opaque pixels')
    image = image.crop(alpha.getbbox())
    image.thumbnail((144, 144), Image.Resampling.LANCZOS)
    sprite = Image.new('RGBA', (144, 144)); sprite.alpha_composite(image, ((144 - image.width) // 2, (144 - image.height) // 2))
    pixels = list(sprite.getdata())
    rgb = b''.join(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b, a in pixels)
    data = rgb + bytes(a for r, g, b, a in pixels)
    values = '\n'.join('    ' + ','.join(f'0x{v:02x}' for v in data[i:i+24]) + ',' for i in range(0, len(data), 24))
    (ROOT / 'assets/images/koala_sprite.c').write_text(
        '#include "lvgl.h"\nstatic const uint8_t pixels[] = {\n' + values + '\n};\n'
        'const lv_image_dsc_t koala_sprite = {\n'
        '    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = 144, .h = 144, .stride = 288},\n'
        '    .data_size = sizeof(pixels), .data = pixels,\n};\n', encoding='utf-8')
    print('Native sprite and 24/36 px semibold UI font subsets prepared.')

if __name__ == '__main__': main()

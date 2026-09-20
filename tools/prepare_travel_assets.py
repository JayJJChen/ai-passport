#!/usr/bin/env python3
"""Convert approved art and the OFL font to native embedded-display assets."""
from pathlib import Path
import argparse
import sys
sys.dont_write_bytecode = True
from pack_cards import convert_font
from pack_koala_frames import build as build_koala_atlas

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
    convert_font(font, args.converter.resolve(), '探索手记网络设置家长设置蓝牙配网目的地忘记联网确认内容包', 36,
                 ROOT / 'assets/fonts/travel_ui_font_36.c', 'lvgl', 'travel_ui_font_36')
    build_koala_atlas()
    print('Native koala animation atlas and 24/36 px semibold UI font subsets prepared.')

if __name__ == '__main__': main()

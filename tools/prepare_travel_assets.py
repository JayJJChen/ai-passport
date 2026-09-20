#!/usr/bin/env python3
"""Convert approved art and the OFL font to native embedded-display assets."""
from pathlib import Path
import argparse
import json
import sys
sys.dont_write_bytecode = True
from pack_cards import convert_font, validate_manifest
from pack_koala_frames import build as build_koala_atlas

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    font = ROOT / 'assets/fonts/NotoSansSC-SemiBold.ttf'
    if not font.exists():
        raise FileNotFoundError('Restore assets/fonts/NotoSansSC-SemiBold.ttf and its OFL license before preparing assets')
    manifest = json.loads((ROOT / 'assets/packs/western-australia/cards.json').read_text(encoding='utf-8'))
    body, title = validate_manifest(manifest, font)
    body += (ROOT / 'assets/fonts/ui-24.txt').read_text(encoding='utf-8')
    body += ''.join(chr(n) for n in range(32, 127))
    convert_font(font, args.converter.resolve(), body, 26, ROOT / 'assets/fonts/travel_ui_font_24.c', 'lvgl', 'travel_ui_font_24')
    convert_font(font, args.converter.resolve(), title, 26,
                 ROOT / 'assets/fonts/travel_ui_font_36.c', 'lvgl', 'travel_ui_font_36')
    build_koala_atlas()
    print('Native koala animation atlas and fixed-itinerary 26 px semibold font subsets prepared.')

if __name__ == '__main__': main()

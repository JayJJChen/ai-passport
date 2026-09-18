#!/usr/bin/env python3
"""Build a self-contained, USB-replaceable travel card pack; no C compiler needed."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

CAPACITY = 0x100000
HEADER = struct.Struct('<8s6I')
ENTRY = struct.Struct('<24s4I')
ROOT = Path(__file__).resolve().parents[1]

def validate_manifest(manifest: dict) -> tuple[str, str]:
    if not isinstance(manifest, dict) or manifest.get('version') != 1 or not isinstance(manifest.get('places'), list) or not 1 <= len(manifest['places']) <= 6:
        raise ValueError('version must be 1; provide 1..6 places')
    bodies, titles, ids = [], [], set()
    for place in manifest['places']:
        if not isinstance(place, dict): raise ValueError('each place must be an object')
        identifier = place.get('id', '')
        if not isinstance(identifier, str) or not re.fullmatch(r'[a-z0-9_]{1,20}', identifier) or identifier in ids:
            raise ValueError('place id must be unique lowercase ASCII, at most 20 characters')
        ids.add(identifier)
        title = place.get('title', '')
        if not isinstance(title, str) or not 1 <= len(title) <= 4 or any(ord(c) < 32 for c in title):
            raise ValueError('title must contain 1..4 printable characters')
        titles.append(title)
        text = [place.get('home'), place.get('reply')]
        for key in ('greetings', 'tasks'):
            values = place.get(key)
            if not isinstance(values, list) or not 1 <= len(values) <= 20:
                raise ValueError(f'{key} must contain 1..20 cards')
            text.extend(values)
        for value in text:
            if not isinstance(value, str) or not value:
                raise ValueError('card text must be a nonempty string')
            lines = value.split('\n')
            if len(lines) > 2 or any(not line or len(line) > 5 or any(ord(c) < 32 for c in line) for line in lines):
                raise ValueError('use at most 2 lines, 5 printable characters per line; do not shrink fonts')
            bodies.append(value)
        if not isinstance(place.get('background'), str):
            raise ValueError('each place requires a background filename')
    return ''.join(bodies).replace('\n', ''), ''.join(titles)

def make_archive(files: dict[str, bytes]) -> bytes:
    if not 1 <= len(files) <= 12:
        raise ValueError('too many files')
    entries, payload = [], bytearray()
    data_start = HEADER.size + len(files) * ENTRY.size
    for name, data in files.items():
        if not re.fullmatch(r'[a-z0-9_.]{1,23}', name) or not data:
            raise ValueError('invalid filename or empty file')
        while len(payload) % 4: payload.append(0)
        entries.append(ENTRY.pack(name.encode(), data_start + len(payload), len(data), 0, 0))
        payload.extend(data)
    body = b''.join(entries) + payload
    length = HEADER.size + len(body)
    if length > CAPACITY: raise ValueError('card pack exceeds the 1 MiB content partition')
    return HEADER.pack(b'KTRVPK01', 1, length, zlib.crc32(body), len(files), ENTRY.size, 0) + body

def read_archive(data: bytes) -> dict[str, bytes]:
    if len(data) < HEADER.size: raise ValueError('truncated header')
    magic, version, size, crc, count, entry_size, reserved = HEADER.unpack_from(data)
    if magic != b'KTRVPK01' or version != 1 or entry_size != ENTRY.size or reserved or not 1 <= count <= 12:
        raise ValueError('unsupported header')
    start = HEADER.size + count * ENTRY.size
    if not start <= size <= min(len(data), CAPACITY) or zlib.crc32(data[HEADER.size:size]) != crc:
        raise ValueError('size/CRC mismatch')
    result, ranges = {}, []
    for i in range(count):
        raw, offset, length, flags, extra = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        if b'\0' not in raw: raise ValueError('unterminated name')
        name = raw.split(b'\0', 1)[0].decode('ascii')
        if not re.fullmatch(r'[a-z0-9_.]{1,23}', name) or name in result or flags or extra:
            raise ValueError('invalid entry')
        if offset % 4 or offset < start or not length or offset + length > size or any(offset < b and a < offset + length for a, b in ranges):
            raise ValueError('invalid/overlapping range')
        ranges.append((offset, offset + length)); result[name] = data[offset:offset + length]
    return result

def verify_pack(data: bytes) -> dict[str, bytes]:
    files = read_archive(data)
    raw = files.get('manifest.json', b'')
    if not raw or len(raw) > 8192 or raw[-1:] != b'\0' or b'\0' in raw[:-1]:
        raise ValueError('manifest must fit 8 KiB and end with exactly one NUL')
    manifest = json.loads(raw[:-1])
    validate_manifest(manifest)
    for name, budget in [('font28.bin', 16384), ('font36.bin', 4096)]:
        if not 1 <= len(files.get(name, b'')) <= budget: raise ValueError('missing font or font budget exceeded')
    for place in manifest['places']:
        if len(files.get(place['background'], b'')) != 240 * 320 * 2:
            raise ValueError('missing background or invalid dimensions')
    return files

def rgb565(image_path: Path) -> bytes:
    from PIL import Image
    image = Image.open(image_path).convert('RGB').resize((240, 320), Image.Resampling.LANCZOS)
    return b''.join(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in image.getdata())

def convert_font(font: Path, converter: Path, symbols: str, size: int, output: Path, fmt='bin', name=None) -> None:
    from fontTools.ttLib import TTFont
    source = TTFont(font)
    available = source.getBestCmap(); source.close()
    missing = sorted({ord(c) for c in symbols if c not in '\r\n'} - available.keys())
    if missing: raise ValueError('source font lacks: ' + ', '.join(f'U+{c:04X}' for c in missing))
    args = ['node', str(converter), '--font', str(font), '--symbols', ''.join(sorted(set(symbols) - {'\r', '\n'})),
            '--size', str(size), '--bpp', '2', '--format', fmt, '--no-compress', '--no-kerning', '--output', str(output)]
    if name: args += ['--lv-font-name', name, '--lv-include', 'lvgl.h']
    subprocess.run(args, check=True)
    if fmt == 'lvgl':
        # Converter comments must not retain the developer's absolute paths.
        generated = output.read_text(encoding='utf-8')
        generated = generated.replace(str(font), font.name).replace(str(output), output.name)
        output.write_text(generated.rstrip() + '\n', encoding='utf-8')

def build(manifest_path: Path, output: Path, font: Path, converter: Path) -> None:
    if output.resolve() == manifest_path.resolve(): raise ValueError('output must not replace the authoring manifest')
    source = json.loads(manifest_path.read_text(encoding='utf-8'))
    body, title = validate_manifest(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='koala-fonts-', dir=output.parent) as temporary:
        font28 = Path(temporary) / 'font28.bin'; font36 = Path(temporary) / 'font36.bin'
        convert_font(font, converter, body, 28, font28)
        convert_font(font, converter, title, 36, font36)
        if font28.stat().st_size > 16384 or font36.stat().st_size > 4096:
            raise ValueError('font subset exceeds RAM budget; split the content into smaller packs')
        files = {'font28.bin': font28.read_bytes(), 'font36.bin': font36.read_bytes()}
    manifest = json.loads(json.dumps(source))
    for i, place in enumerate(manifest['places']):
        image_path = (manifest_path.parent / place['background']).resolve()
        if not image_path.is_relative_to(manifest_path.parent.resolve()): raise ValueError('background must stay inside pack folder')
        name = f'bg{i}.rgb565'; files[name] = rgb565(image_path); place['background'] = name
    files['manifest.json'] = json.dumps(manifest, ensure_ascii=False, separators=(',', ':')).encode() + b'\0'
    if len(files['manifest.json']) > 8192: raise ValueError('manifest exceeds 8 KiB')
    archive = make_archive(files)
    verify_pack(archive)
    output.write_bytes(archive)
    identity = {'format': 1, 'size': len(archive), 'sha256': hashlib.sha256(archive).hexdigest(),
                'content_offset': '0x700000', 'font_converter': 'lv_font_conv 1.5.3',
                'source_font_sha256': hashlib.sha256(font.read_bytes()).hexdigest(),
                'body_glyphs': sorted(set(body)), 'title_glyphs': sorted(set(title))}
    output.with_name(output.name + '.manifest.json').write_text(json.dumps(identity, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Card pack: {output} ({len(archive)} bytes), SHA256 {identity["sha256"]}')

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    create = sub.add_parser('build'); create.add_argument('manifest', type=Path); create.add_argument('output', type=Path)
    create.add_argument('--font', type=Path, default=ROOT / 'assets/fonts/NotoSansSC-SemiBold.ttf')
    create.add_argument('--converter', type=Path, required=True)
    verify = sub.add_parser('verify'); verify.add_argument('pack', type=Path)
    args = parser.parse_args()
    if args.command == 'build': build(args.manifest.resolve(), args.output.resolve(), args.font.resolve(), args.converter.resolve())
    else:
        data = args.pack.read_bytes(); files = verify_pack(data)
        print(f'Card pack verification: PASS, {len(files)} files, SHA256 {hashlib.sha256(data).hexdigest()}')

if __name__ == '__main__': main()

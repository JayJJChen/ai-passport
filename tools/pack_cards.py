#!/usr/bin/env python3
"""Build a self-contained, USB-replaceable travel card pack; no C compiler needed."""
from __future__ import annotations
import argparse
import functools
import hashlib
import json
import re
import struct
import subprocess
import zlib
from pathlib import Path

CAPACITY = 0x100000
HEADER = struct.Struct('<8s6I')
ENTRY = struct.Struct('<24s4I')
ROOT = Path(__file__).resolve().parents[1]
MAX_DAYS = 11
MAX_REMINDERS = 4
BODY_MAX_PX = 168
TITLE_MAX_PX = 108
SYSTEM_BODY_TEXT = '今天提醒完成住宿活动自动预览出发啦旅程完成下一件事事项都完成啦收到已经完成第天选择日期自动模式月日返回上一下一确定路线按显示0123456789:/'

@functools.lru_cache(maxsize=4)
def _ttf_metrics(font_path: str):
    """Read the format-12 cmap and horizontal advances without optional Python packages."""
    data = Path(font_path).read_bytes()
    num_tables = struct.unpack_from('>H', data, 4)[0]
    tables = {}
    for index in range(num_tables):
        tag, _, offset, length = struct.unpack_from('>4sIII', data, 12 + index * 16)
        tables[tag.decode('ascii')] = (offset, length)
    head, _ = tables['head']; hhea, _ = tables['hhea']; maxp, _ = tables['maxp']; hmtx, _ = tables['hmtx']; cmap, _ = tables['cmap']
    units = struct.unpack_from('>H', data, head + 18)[0]
    glyphs = struct.unpack_from('>H', data, maxp + 4)[0]
    metrics = struct.unpack_from('>H', data, hhea + 34)[0]
    advances = [struct.unpack_from('>H', data, hmtx + index * 4)[0] for index in range(metrics)]
    advances.extend([advances[-1]] * (glyphs - metrics))
    cmap_count = struct.unpack_from('>H', data, cmap + 2)[0]
    groups = None
    for index in range(cmap_count):
        _, _, relative = struct.unpack_from('>HHI', data, cmap + 4 + index * 8)
        subtable = cmap + relative
        if struct.unpack_from('>H', data, subtable)[0] == 12:
            count = struct.unpack_from('>I', data, subtable + 12)[0]
            groups = [struct.unpack_from('>III', data, subtable + 16 + group * 12) for group in range(count)]
            break
    if groups is None: raise ValueError('font needs a Unicode format-12 cmap')
    return units, advances, groups

def _line_width(line: str, font_path: Path, size: int) -> float:
    units, advances, groups = _ttf_metrics(str(font_path.resolve()))
    total = 0
    for char in line:
        codepoint = ord(char)
        glyph = None
        lo, hi = 0, len(groups)
        while lo < hi:
            mid = (lo + hi) // 2
            start, end, first = groups[mid]
            if codepoint < start: hi = mid
            elif codepoint > end: lo = mid + 1
            else:
                glyph = first + codepoint - start
                break
        if glyph is None or glyph >= len(advances): raise ValueError(f'source font lacks U+{codepoint:04X}')
        total += advances[glyph]
    return total * size / units

def _fits(text: str, font_path: Path, size: int, max_pixels: int, max_lines: int) -> bool:
    lines = text.split('\n')
    return len(lines) <= max_lines and all(line and _line_width(line, font_path, size) <= max_pixels for line in lines)

def validate_manifest(manifest: dict, font_path: Path | None = None) -> tuple[str, str]:
    font_path = font_path or ROOT / 'assets/fonts/NotoSansSC-SemiBold.ttf'
    if not isinstance(manifest, dict) or manifest.get('version') != 2 or not isinstance(manifest.get('days'), list) or len(manifest['days']) != MAX_DAYS:
        raise ValueError('version must be 2; provide exactly 11 days')
    bodies, titles, ids = [], [], set()
    expected_dates = [f'2026-10-{day:02d}' for day in range(2, 13)]
    for index, day in enumerate(manifest['days']):
        if not isinstance(day, dict): raise ValueError('each day must be an object')
        identifier = day.get('id', '')
        if not isinstance(identifier, str) or not re.fullmatch(r'[a-z0-9_]{1,20}', identifier) or identifier in ids:
            raise ValueError('day id must be unique lowercase ASCII, at most 20 characters')
        ids.add(identifier)
        if day.get('date') != expected_dates[index]: raise ValueError('days must cover 2026-10-02 through 2026-10-12 in order')
        title = day.get('title', '')
        if not isinstance(title, str) or not _fits(title, font_path, 26, TITLE_MAX_PX, 1):
            raise ValueError('title exceeds the one-line pixel budget')
        titles.append(title)
        text = [day.get('home'), day.get('reply')]
        schedule = day.get('schedule')
        if not isinstance(schedule, list) or len(schedule) != 3: raise ValueError('schedule must contain route, activity, and lodging')
        text.extend(schedule)
        reminders = day.get('reminders')
        if not isinstance(reminders, list) or not 1 <= len(reminders) <= MAX_REMINDERS:
            raise ValueError('reminders must contain 1..4 items')
        for reminder in reminders:
            if not isinstance(reminder, dict) or not isinstance(reminder.get('text'), str): raise ValueError('each reminder needs text')
            at = reminder.get('at')
            if at is not None and (not isinstance(at, str) or not re.fullmatch(r'(?:[01]\d|2[0-3]):[0-5]\d', at)):
                raise ValueError('reminder time must be HH:MM')
            text.append(reminder['text'])
        for value in text:
            if not isinstance(value, str) or not value:
                raise ValueError('card text must be a nonempty string')
            if any(ord(c) < 32 and c != '\n' for c in value) or not _fits(value, font_path, 26, BODY_MAX_PX, 2):
                raise ValueError(f'card text exceeds the two-line pixel budget: {value!r}')
            bodies.append(value)
        if not isinstance(day.get('background'), str):
            raise ValueError('each day requires a background filename')
    return ''.join(bodies).replace('\n', '') + SYSTEM_BODY_TEXT, ''.join(titles)

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
    if not raw or len(raw) > 32768 or raw[-1:] != b'\0' or b'\0' in raw[:-1]:
        raise ValueError('manifest must fit 32 KiB and end with exactly one NUL')
    manifest = json.loads(raw[:-1])
    validate_manifest(manifest)
    for day in manifest['days']:
        if len(files.get(day['background'], b'')) != 240 * 320 * 2:
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

def build(manifest_path: Path, output: Path, font: Path) -> None:
    if output.resolve() == manifest_path.resolve(): raise ValueError('output must not replace the authoring manifest')
    source = json.loads(manifest_path.read_text(encoding='utf-8'))
    body, title = validate_manifest(source, font)
    output.parent.mkdir(parents=True, exist_ok=True)
    files = {}
    manifest = json.loads(json.dumps(source))
    backgrounds: dict[Path, str] = {}
    for day in manifest['days']:
        image_path = (manifest_path.parent / day['background']).resolve()
        if not image_path.is_relative_to(manifest_path.parent.resolve()): raise ValueError('background must stay inside pack folder')
        if image_path not in backgrounds:
            name = f'bg{len(backgrounds)}.rgb565'
            files[name] = rgb565(image_path)
            backgrounds[image_path] = name
        day['background'] = backgrounds[image_path]
    files['manifest.json'] = json.dumps(manifest, ensure_ascii=False, separators=(',', ':')).encode() + b'\0'
    if len(files['manifest.json']) > 32768: raise ValueError('manifest exceeds 32 KiB')
    archive = make_archive(files)
    verify_pack(archive)
    output.write_bytes(archive)
    identity = {'format': 1, 'size': len(archive), 'sha256': hashlib.sha256(archive).hexdigest(),
                'content_offset': '0x700000', 'font_storage': 'firmware compile-time subsets',
                'source_font_sha256': hashlib.sha256(font.read_bytes()).hexdigest(),
                'body_glyphs': sorted(set(body)), 'title_glyphs': sorted(set(title))}
    output.with_name(output.name + '.manifest.json').write_text(json.dumps(identity, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Card pack: {output} ({len(archive)} bytes), SHA256 {identity["sha256"]}')

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    create = sub.add_parser('build'); create.add_argument('manifest', type=Path); create.add_argument('output', type=Path)
    create.add_argument('--font', type=Path, default=ROOT / 'assets/fonts/NotoSansSC-SemiBold.ttf')
    verify = sub.add_parser('verify'); verify.add_argument('pack', type=Path)
    args = parser.parse_args()
    if args.command == 'build': build(args.manifest.resolve(), args.output.resolve(), args.font.resolve())
    else:
        data = args.pack.read_bytes(); files = verify_pack(data)
        print(f'Card pack verification: PASS, {len(files)} files, SHA256 {hashlib.sha256(data).hexdigest()}')

if __name__ == '__main__': main()

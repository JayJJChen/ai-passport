#!/usr/bin/env python3
"""Verify/describe a card update by default; --flash explicitly writes only its data partition."""
import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path
sys.dont_write_bytecode = True
from pack_cards import verify_pack, CAPACITY
from verify_firmware import parse_partition_table

OFFSET = 0x700000

def check_layout(table: bytes) -> None:
    partitions, md5 = parse_partition_table(table, 0x9000)
    matching = [p for p in partitions if p.label == 'travel_cards' and p.kind == 1 and p.subtype == 0x40
                and p.offset == OFFSET and p.size == CAPACITY]
    if not md5 or len(matching) != 1:
        raise ValueError('device does not have the version-1 Koala content partition; do not write it')
    for p in partitions:
        if p.label != 'travel_cards' and p.offset < OFFSET + CAPACITY and OFFSET < p.end:
            raise ValueError('content overlaps another device partition')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pack', type=Path)
    parser.add_argument('--port', help='explicitly identify the intended local USB port')
    parser.add_argument('--flash', action='store_true', help='write after layout validation; requires user-authorized device testing')
    args = parser.parse_args()
    data = args.pack.read_bytes(); verify_pack(data)
    print(f'Pack SHA256: {hashlib.sha256(data).hexdigest()}')
    print(f'Content only: 0x{OFFSET:x}, {len(data)} bytes; application/NVS are not written.')
    if not args.flash:
        print('Verification only. No USB port was opened and no device was changed.')
        return
    if not args.port: parser.error('--flash requires an explicit --port')
    base = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port]
    with tempfile.TemporaryDirectory(prefix='koala-pack-') as directory:
        table = Path(directory) / 'partition-table.bin'
        subprocess.run(base + ['read_flash', '0x8000', '0x1000', str(table)], check=True)
        check_layout(table.read_bytes())
        subprocess.run(base + ['write_flash', hex(OFFSET), str(args.pack.resolve())], check=True)
        subprocess.run(base + ['verify_flash', hex(OFFSET), str(args.pack.resolve())], check=True)
    print('Content write/readback verification: PASS. Observe restart and on-device rendering separately.')

if __name__ == '__main__': main()

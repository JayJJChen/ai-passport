"""Content-only USB layout guards; these tests never access a device."""
import hashlib
import struct
import subprocess
import sys
import unittest
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from update_card_pack import check_layout
from verify_firmware import ENTRY, PARTITION_TABLE_SIZE

def table(entries):
    raw = b''.join(ENTRY.pack(0x50aa, kind, subtype, offset, size, label.encode().ljust(16, b'\0'), 0)
                   for kind, subtype, offset, size, label in entries)
    marker = struct.pack('<H', 0xebeb) + b'\xff' * 14 + hashlib.md5(raw).digest()
    return (raw + marker).ljust(PARTITION_TABLE_SIZE, b'\xff')

class ContentUpdateTests(unittest.TestCase):
    def setUp(self):
        self.entries = [(1, 2, 0x9000, 0x6000, 'nvs'), (1, 1, 0xf000, 0x1000, 'phy_init'),
                        (0, 0, 0x10000, 0x6f0000, 'factory'), (1, 0x40, 0x700000, 0x100000, 'travel_cards')]
    def test_accepts_exact_content_partition(self): check_layout(table(self.entries))
    def test_stock_app_cannot_be_overwritten(self):
        with self.assertRaises(ValueError): check_layout(table(self.entries[:-2] + [(0, 0, 0x10000, 0x7f0000, 'factory')]))
    def test_overlapping_app_is_rejected(self):
        entries = list(self.entries); entries[2] = (0, 0, 0x10000, 0x7f0000, 'factory')
        with self.assertRaises(ValueError): check_layout(table(entries))
    def test_wrong_offset_and_bad_md5_are_rejected(self):
        entries = list(self.entries); entries[3] = (1, 0x40, 0x600000, 0x100000, 'travel_cards')
        with self.assertRaises(ValueError): check_layout(table(entries))
        raw = bytearray(table(self.entries)); raw[20] ^= 1
        with self.assertRaises(ValueError): check_layout(bytes(raw))
    def test_default_cli_does_not_open_even_an_explicit_port(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/update_card_pack.py'),
            str(ROOT / 'assets/packs/shanghai/cards.klp'), '--port', 'NOT_A_PORT'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('No USB port was opened', result.stdout)

if __name__ == '__main__': unittest.main()

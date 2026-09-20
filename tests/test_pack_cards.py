import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack_cards', ROOT / 'tools/pack_cards.py')
pack_cards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack_cards)


class CardPackTests(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / 'assets/packs/western-australia/cards.json'
        self.manifest = json.loads(self.path.read_text(encoding='utf-8'))

    def test_fixed_itinerary_schema(self):
        body, title = pack_cards.validate_manifest(self.manifest)
        self.assertTrue(body and title)
        self.assertEqual(len(self.manifest['days']), 11)
        self.assertEqual(self.manifest['days'][0]['date'], '2026-10-02')
        self.assertEqual(self.manifest['days'][-1]['date'], '2026-10-12')
        self.assertTrue(all(len(day['schedule']) == 3 for day in self.manifest['days']))
        self.assertTrue(all(1 <= len(day['reminders']) <= 4 for day in self.manifest['days']))

    def test_changed_content_needs_no_c_compiler(self):
        self.manifest['days'][0]['home'] = '你好，\n一起看看！'
        pack_cards.validate_manifest(self.manifest)
        blob = pack_cards.make_archive({'manifest.json': json.dumps(self.manifest).encode(), 'bg0.rgb565': b'changed background'})
        self.assertEqual(json.loads(pack_cards.read_archive(blob)['manifest.json']), self.manifest)

    def test_pixel_readability_and_time_format(self):
        invalid = ['字太多不能放进这一行', '一\n二\n三', '一\n', '一\x00二']
        for text in invalid:
            with self.subTest(text=text):
                changed = copy.deepcopy(self.manifest)
                changed['days'][0]['home'] = text
                with self.assertRaises(ValueError):
                    pack_cards.validate_manifest(changed)
        for value in ['24:00', '9:30', '12:60']:
            changed = copy.deepcopy(self.manifest)
            changed['days'][0]['reminders'][0]['at'] = value
            with self.assertRaises(ValueError):
                pack_cards.validate_manifest(changed)
        changed = copy.deepcopy(self.manifest)
        changed['days'][0]['reminders'][0]['at'] = '09:30'
        pack_cards.validate_manifest(changed)

    def test_duplicate_day_ids(self):
        self.manifest['days'][1]['id'] = self.manifest['days'][0]['id']
        with self.assertRaises(ValueError):
            pack_cards.validate_manifest(self.manifest)

    def test_roundtrip_and_corruption(self):
        blob = pack_cards.make_archive({'a.bin': b'123', 'b.bin': b'456789'})
        self.assertEqual(pack_cards.read_archive(blob), {'a.bin': b'123', 'b.bin': b'456789'})
        for invalid in [blob[:-1], blob[:30], blob[:-1] + bytes([blob[-1] ^ 1])]:
            with self.assertRaises(ValueError):
                pack_cards.read_archive(invalid)

    def test_runtime_file_budgets_and_background_deduplication(self):
        pack_path = ROOT / 'assets/packs/western-australia/cards.klp'
        self.assertLess(pack_path.stat().st_size, pack_cards.CAPACITY)
        files = pack_cards.verify_pack(pack_path.read_bytes())
        manifest = json.loads(files['manifest.json'][:-1])
        backgrounds = {day['background'] for day in manifest['days']}
        self.assertEqual(len(backgrounds), 5)
        self.assertEqual(len(files), 6)
        for name, replacement in [
            ('bg0.rgb565', b'x'),
            ('manifest.json', files['manifest.json'][:-1]),
        ]:
            with self.subTest(name=name):
                invalid = dict(files)
                invalid[name] = replacement
                with self.assertRaises(ValueError):
                    pack_cards.verify_pack(pack_cards.make_archive(invalid))

    def test_build_cannot_overwrite_authoring_json(self):
        with self.assertRaises(ValueError):
            pack_cards.build(self.path, self.path, Path('unused'))


if __name__ == '__main__':
    unittest.main()

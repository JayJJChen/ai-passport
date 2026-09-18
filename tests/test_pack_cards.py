import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack_cards', ROOT / 'tools/pack_cards.py')
pack_cards = importlib.util.module_from_spec(spec); spec.loader.exec_module(pack_cards)

class CardPackTests(unittest.TestCase):
    def setUp(self): self.manifest = json.loads((ROOT / 'assets/packs/shanghai/cards.json').read_text(encoding='utf-8'))
    def test_changed_content_needs_no_c_compiler(self):
        self.manifest['places'][0]['home'] = '你好，\n一起看看！'
        pack_cards.validate_manifest(self.manifest)
        blob = pack_cards.make_archive({'manifest.json': json.dumps(self.manifest).encode(), 'bg0.rgb565': b'changed background'})
        self.assertEqual(json.loads(pack_cards.read_archive(blob)['manifest.json']), self.manifest)
    def test_readability_limits(self):
        for text in ['字太多不能塞进去', '一\n二\n三', '一\n', '一\x00二']:
            with self.subTest(text=text):
                self.manifest['places'][0]['home'] = text
                with self.assertRaises(ValueError): pack_cards.validate_manifest(self.manifest)
    def test_duplicate_place_ids(self):
        self.manifest['places'].append(copy.deepcopy(self.manifest['places'][0]))
        with self.assertRaises(ValueError): pack_cards.validate_manifest(self.manifest)
    def test_roundtrip_and_corruption(self):
        blob = pack_cards.make_archive({'a.bin': b'123', 'b.bin': b'456789'})
        self.assertEqual(pack_cards.read_archive(blob), {'a.bin': b'123', 'b.bin': b'456789'})
        for invalid in [blob[:-1], blob[:30], blob[:-1] + bytes([blob[-1] ^ 1])]:
                with self.assertRaises(ValueError): pack_cards.read_archive(invalid)

    def test_runtime_file_budgets(self):
        files = pack_cards.verify_pack((ROOT / 'assets/packs/shanghai/cards.klp').read_bytes())
        for name, replacement in [('font28.bin', b'x' * 16385), ('font36.bin', b'x' * 4097),
                                  ('bg0.rgb565', b'x'), ('manifest.json', files['manifest.json'][:-1])]:
            with self.subTest(name=name):
                invalid = dict(files); invalid[name] = replacement
                with self.assertRaises(ValueError): pack_cards.verify_pack(pack_cards.make_archive(invalid))

    def test_build_cannot_overwrite_authoring_json(self):
        path = ROOT / 'assets/packs/shanghai/cards.json'
        with self.assertRaises(ValueError): pack_cards.build(path, path, Path('unused'), Path('unused'))

if __name__ == '__main__': unittest.main()

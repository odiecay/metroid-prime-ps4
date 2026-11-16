import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import hashlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_assets', ROOT/'tools/package_assets.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class InstalledAssetTests(unittest.TestCase):
    def test_build_sources_survive_workspace_changes_and_detect_archive_damage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'source';root.mkdir()
            (root/'platform').mkdir();source=root/'platform/backend.cpp'
            source.write_bytes(b'exact compiled source')
            hashes={'platform/backend.cpp':hashlib.sha256(source.read_bytes()).hexdigest()}
            archive=Path(tmp)/'archive'
            self.assertEqual(m.archive_sources(root,archive,hashes),hashes)
            source.write_bytes(b'next candidate')
            self.assertEqual(m.archive_sources(root,archive,hashes,verify=True),hashes)
            (archive/'platform/backend.cpp').write_bytes(b'damaged')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                m.archive_sources(root,archive,hashes,verify=True)

    def test_changed_source_is_rejected_before_archival(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'backend.cpp').write_bytes(b'changed')
            archive=root/'archive'
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                m.archive_sources(root,archive,{'backend.cpp':'0'*64})
            self.assertFalse(archive.exists())

    def fixture(self, base):
        source, stage = base/'source', base/'stage'
        source.mkdir(); stage.mkdir()
        gp4 = stage/'pkg.gp4'
        gp4.write_text('<psproject><volume><package content_id="PRESERVED" /></volume>'
                       '<files img_no="0"><file targ_path="sce_sys/param.sfo" orig_path="sce_sys/param.sfo" />'
                       '</files><rootdir><dir targ_name="sce_sys" /></rootdir></psproject>')
        return source, stage, gp4

    def test_installed_tree_preserves_recipe_and_nested_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, stage, gp4 = self.fixture(Path(tmp))
            name = 'mods/remastered-models/roomenv/brdf.lut'
            asset = source/name; asset.parent.mkdir(parents=True); asset.write_bytes(b'private')
            mapping = m.add_asset_tree(gp4, source, [name])
            root = ET.parse(gp4).getroot()
            self.assertEqual(root.find('volume/package').get('content_id'), 'PRESERVED')
            self.assertEqual((stage/'remastered'/name).read_bytes(), b'private')
            self.assertEqual(mapping, {'remastered/'+name: asset})
            self.assertIsNotNone(root.find('rootdir/dir[@targ_name="remastered"]/dir[@targ_name="mods"]/'
                                           'dir[@targ_name="remastered-models"]/dir[@targ_name="roomenv"]'))
            self.assertEqual(len(root.findall('rootdir/dir[@targ_name="sce_sys"]')), 1)

    def test_unsafe_missing_duplicate_and_symlink_inputs_are_rejected_before_copy(self):
        for case in ('../outside', '/absolute', 'a/../bad', 'missing', 'duplicate', 'symlink'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                source, stage, gp4 = self.fixture(Path(tmp))
                (source/'valid').write_bytes(b'asset')
                names = ['valid', 'valid'] if case == 'duplicate' else [case]
                if case == 'symlink': (source/'symlink').symlink_to(source/'valid')
                with self.assertRaises(ValueError): m.add_asset_tree(gp4, source, names)
                self.assertFalse((stage/'remastered').exists())

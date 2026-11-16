"""Dependency receipts reject changed bytes/names and escaping source symlinks."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from source_catalog import tree_digest,verify_populated
class SourceCatalogTests(unittest.TestCase):
    def test_changed_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=root/'dep';source.mkdir();p=source/'source.c';p.write_text('original')
            catalog=[{'name':'fixture','source_path':'dep','source_tree_sha256':tree_digest(source)}]
            self.assertEqual(verify_populated(root,catalog),1)
            p.write_text('changed')
            with self.assertRaisesRegex(ValueError,'differs'):verify_populated(root,catalog)
    def test_path_names_are_part_of_the_receipt(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p=root/'one.h';p.write_text('same');before=tree_digest(root)
            p.rename(root/'two.h');self.assertNotEqual(before,tree_digest(root))
    def test_timestamps_and_python_cache_are_not_source(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p=root/'one.h';p.write_text('same');before=tree_digest(root)
            p.touch();(root/'__pycache__').mkdir();(root/'__pycache__/ignored.pyc').write_bytes(b'cache')
            self.assertEqual(before,tree_digest(root))
    def test_escaping_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'outside.h').symlink_to('/etc/hosts')
            with self.assertRaisesRegex(ValueError,'escapes'):tree_digest(root)
if __name__=='__main__':unittest.main()

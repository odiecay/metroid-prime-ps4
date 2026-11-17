"""Exact-object and input-provenance guards; no console or SDK mutations."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('radv_overlay', ROOT/'tools/build_radv_command_overlay.py')
overlay = importlib.util.module_from_spec(spec); spec.loader.exec_module(overlay)


class RadvCommandOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(); cls.root = Path(cls.temporary.name)
        cls.source = cls.root/'fixture.c'; cls.source.write_text('int marker=3; int read_marker(int x){return x+marker;}\n')
        cls.base = cls.root/'base.o'
        subprocess.run(['/usr/bin/clang-18','-c','-O2',cls.source,'-o',cls.base], check=True, capture_output=True)
        cls.objcopy = '/usr/lib/llvm-19/bin/llvm-objcopy'

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_exact_object_passes(self):
        self.assertTrue(overlay.require_baseline(self.base,self.base)['exact_except_comment'])

    def test_compiler_comment_difference_only_is_allowed(self):
        comment=self.root/'compiler-comment'; comment.write_bytes(b'fixture compiler\0')
        obj=self.root/'comment.o'
        subprocess.run([self.objcopy,'--update-section',f'.comment={comment}',self.base,obj],check=True,capture_output=True)
        result=overlay.require_baseline(self.base,obj)
        self.assertEqual(result['differing_sections'],['.comment'])

    def test_allocated_data_difference_is_rejected(self):
        path=self.root/'changed.c';path.write_text('int marker=4; int read_marker(int x){return x+marker;}\n')
        obj=self.root/'changed.o'
        subprocess.run(['/usr/bin/clang-18','-c','-O2',path,'-o',obj],check=True,capture_output=True)
        self.assertIn('.data',overlay.compare_elf(self.base,obj)['differing_sections'])
        with self.assertRaisesRegex(ValueError,'code/ABI reproduction failed'):
            overlay.require_baseline(self.base,obj)

    def test_relocation_symbol_identity_change_is_rejected(self):
        obj=self.root/'relocation.o'
        subprocess.run([self.objcopy,'--redefine-sym','marker=other_marker',self.base,obj],check=True,capture_output=True)
        result=overlay.compare_elf(self.base,obj)
        self.assertFalse(result['same_symbol_records'])
        with self.assertRaises(ValueError):overlay.require_baseline(self.base,obj)

    def test_extra_allocated_section_is_rejected(self):
        data=self.root/'extra-data';data.write_bytes(b'new driver state')
        obj=self.root/'extra.o'
        subprocess.run([self.objcopy,'--add-section',f'.extra={data}',
                        '--set-section-flags','.extra=alloc,readonly',self.base,obj],check=True,capture_output=True)
        with self.assertRaises(ValueError):overlay.require_baseline(self.base,obj)

    def test_comment_cannot_become_allocated(self):
        obj=self.root/'allocated-comment.o'
        subprocess.run([self.objcopy,'--set-section-flags','.comment=alloc,readonly',self.base,obj],
                       check=True,capture_output=True)
        with self.assertRaises(ValueError):overlay.require_baseline(self.base,obj)

    def test_manifest_path_escape_is_rejected(self):
        for name in ('/absolute','../parent','src/../../parent','src\\escape'):
            with self.subTest(name=name),self.assertRaises(ValueError):overlay.relative(name)
        self.assertEqual(overlay.relative('src/amd/test.h'),Path('src/amd/test.h'))

    def test_pinned_header_changes_and_symlinks_are_rejected(self):
        base=self.root/'headers';base.mkdir(exist_ok=True);header=base/'generated.h';header.write_text('original')
        entry={'path':'generated.h','sha256':hashlib.sha256(header.read_bytes()).hexdigest()}
        overlay.verify_files(base,[entry]);header.write_text('edited')
        with self.assertRaisesRegex(ValueError,'hash differs'):overlay.verify_files(base,[entry])
        external=self.root/'external-header';external.write_text('original');header.unlink();header.symlink_to(external)
        with self.assertRaisesRegex(ValueError,'escaping'):overlay.verify_files(base,[entry])

    def test_materialisation_reads_pinned_git_blobs_not_dirty_files(self):
        repo=self.root/'git-fixture';repo.mkdir();subprocess.run(['git','init','-q',repo],check=True)
        path=repo/'value.c';original=b'int original_value=7;\n';path.write_bytes(original)
        subprocess.run(['git','-C',repo,'add','value.c'],check=True)
        subprocess.run(['git','-C',repo,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid',
                        '-c','commit.gpgsign=false','commit','-qm','fixture'],check=True)
        commit=subprocess.check_output(['git','-C',repo,'rev-parse','HEAD'],text=True).strip()
        blob=subprocess.check_output(['git','-C',repo,'rev-parse','HEAD:value.c'],text=True).strip()
        entry={'path':'value.c','git_blob':blob,'sha256':hashlib.sha256(original).hexdigest()}
        path.write_text('THIS IS A DIRTY WORKTREE\n');out=self.root/'materialised'
        overlay.materialise(repo,commit,[entry],out)
        self.assertEqual((out/'value.c').read_bytes(),original)
        bad={**entry,'sha256':'0'*64}
        with self.assertRaisesRegex(ValueError,'blob hash differs'):overlay.materialise(repo,commit,[bad],out)
        with self.assertRaisesRegex(ValueError,'Missing pinned Mesa blob'):
            overlay.materialise(repo,commit,[{**entry,'path':'missing.c'}],out)


if __name__ == '__main__':
    unittest.main()

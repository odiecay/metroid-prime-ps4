from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('graphics_profiles',ROOT/'tools/prepare_graphics_profiles.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def model(textures):
    head=bytearray(64)
    struct.pack_into('>II',head,0,0xDEADBABE,2)
    struct.pack_into('>III',head,0x24,1,1,4+len(textures)*4)
    return bytes(head)+struct.pack('>I',len(textures))+b''.join(struct.pack('>I',t) for t in textures)


class GraphicsProfilesTests(unittest.TestCase):
    def test_material_dependencies_and_skin_closure_are_typed(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)
            (src/'0000000A.CMDL').write_bytes(model([1]))
            (src/'0000000C.CMDL').write_bytes(model([2]))
            (src/'00000099.CMDL').write_bytes(model([3]))
            for i in (0xB,0xD,0xE): (src/f'{i:08X}.CSKR').write_bytes(b'skin')
            # Unaligned character bindings, matching upstream's actual rebind method.
            (src/'00000020.ANCS').write_bytes(b'x'+struct.pack('>III',0xA,0xB,0xF)+struct.pack('>III',0xC,0xD,0xF))
            for i in (1,2,3):
                (src/f'{i:08X}.TXTR').write_bytes(b'fallback')
                (src/f'{i:08X}.dds').write_bytes(b'native')
            (src/'00000055.PART').write_bytes(b'effect not selected')
            paths,coverage=m.select_resources(src,{0xA},skin_table={0xA:{0xE}},seed_ancs=set())
            names={p.name for p in paths}
            self.assertEqual(coverage['models'],2)
            self.assertEqual(coverage['skins'],3)
            self.assertIn('0000000E.CSKR',names)  # unchanged ANCS bindings still need replacement skin
            self.assertIn('00000020.ANCS',names)
            self.assertIn('00000002.dds',names)
            self.assertNotIn('00000099.CMDL',names)
            self.assertNotIn('00000003.dds',names)
            self.assertNotIn('00000055.PART',names)

    def test_explicit_beam_character_seed_resolves_new_model_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)
            (src/'12345678.CMDL').write_bytes(model([]))
            (src/'11111111.CSKR').write_bytes(b'skin')
            (src/'22222222.ANCS').write_bytes(struct.pack('>III',0x12345678,0x11111111,0x99999999))
            paths,_=m.select_resources(src,set(),skin_table={},seed_ancs={0x22222222})
            self.assertEqual({p.suffix for p in paths},{'.CMDL','.CSKR','.ANCS'})

    def test_corrupt_model_sections_and_truncated_tables_are_rejected(self):
        with self.assertRaises(ValueError): m.cmdl_textures(b'bad')
        broken=bytearray(model([1]));struct.pack_into('>I',broken,0x2c,999999)
        with self.assertRaises(ValueError): m.cmdl_textures(broken)
        self.assertEqual(m.cmdl_textures(model([1,2])),{1,2})

    def test_key_files_and_symlinks_cannot_enter_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp);(src/'prod.keys').write_text('private test fixture')
            self.assertEqual(m.resource_index(src),{})
            (src/'12345678.CMDL').symlink_to(src/'prod.keys')
            with self.assertRaises(ValueError): m.resource_index(src)

    def test_destinations_outside_private_and_existing_outputs_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'source';src.mkdir()
            existing=root/'existing';existing.mkdir()
            with self.assertRaisesRegex(ValueError,'private'):
                m.prepare(src,None,root.parent/'public-output',None,private_root=root)
            with self.assertRaisesRegex(ValueError,'overwrite'):
                m.prepare(src,None,existing,None,private_root=root)

    def test_pinned_import_version_and_owned_body_skins(self):
        self.assertEqual(m.importer_version(ROOT/'game/MetroidPrimePort/platform/include/port_remastered_import.h'),520)
        self.assertIn(0xA70A4FD4,m.table_skins()[0xCC25E002])

    def test_material_cube_dependency_and_shared_lut_survive_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)
            (src/'0000000A.CMDL').write_bytes(model([])+struct.pack('>I',0x12345678)+b'PBR7')
            (src/'12345678.envcube').write_bytes(b'cube')
            (src/'roomenv').mkdir();(src/'roomenv/brdf.lut').write_bytes(bytes(256))
            (src/'roomenv/notselected.roomenv').write_bytes(b'room lighting')
            paths,_=m.select_resources(src,{0xA},skin_table={},seed_ancs=set())
            names={p.relative_to(src).as_posix() for p in paths}
            self.assertIn('12345678.envcube',names)
            self.assertIn('roomenv/brdf.lut',names)
            self.assertNotIn('roomenv/notselected.roomenv',names)

    def test_failed_footprint_limit_leaves_input_and_destination_untouched(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as tmp:
            private=Path(tmp);source=private/'conversion';source.mkdir()
            (source/'.import-version').write_text('520\nfixture\n')
            payload=model([]);(source/'0000000A.CMDL').write_bytes(payload)
            converter=private/'converter';converter.write_bytes(b'fixture binary identity')
            output=private/'profiles'
            with patch.object(m,'samus_gun_models',return_value={0xA}):
                with self.assertRaisesRegex(ValueError,'budget'):
                    m.prepare(source,None,output,converter,private_root=private,max_lite_bytes=0)
            self.assertFalse(output.exists())
            self.assertEqual((source/'0000000A.CMDL').read_bytes(),payload)
            self.assertEqual(list(private.glob('.profiles-*')),[])

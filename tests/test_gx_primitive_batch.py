"""Compare the real decoder with batching disabled/enabled, including failures."""
import importlib.util
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/pc-asset-importer'

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))

def data_rows(text,prefix):
    return [dict(token.split('=',1) for token in line.split()[1:])
            for line in text.splitlines() if line.startswith(prefix+' ')]

def source_body(text,signature):
    start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

class GxPrimitiveBatchTests(unittest.TestCase):
    def test_actual_strip_writer_matches_reference_bytes(self):
        aurora=ROOT/'game/MetroidPrimePort/extern/aurora'
        source=(aurora/'lib/gx/command_processor.cpp').read_text()
        buffers=(aurora/'lib/internal.hpp').read_text()
        out=Path(os.environ.get('GX_PRIMITIVE_BATCH_TEST_BUILD',str(ROOT/'build/gx-primitive-batch'))).resolve()/'index-writer'
        out.mkdir(parents=True,exist_ok=True)
        (out/'actual_reference_index.inc').write_text(source_body(source,'u16 prepare_idx_buffer('))
        (out/'actual_candidate_index.inc').write_text(source_body(source,'static u16* write_prepared_strip('))
        (out/'actual_index_buffers.inc').write_text(
            source_body(buffers,'template <typename T>\nclass ArrayRef')+';\n'+
            source_body(buffers,'class ByteBuffer')+';\n')
        compiler=shutil.which('clang++-19') or shutil.which('g++')
        flags=['-std=c++20','-O2','-DTARGET_PC','-I'+str(out),'-I'+str(aurora/'include')]
        if os.environ.get('GX_DRAW_SANITIZE')=='1':
            compiler=shutil.which('g++-14') or shutil.which('g++')
            flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        run=subprocess.run([compiler,*flags,str(ROOT/'tests/gx_strip_index_writer.cpp'),'-o',str(out/'run')],
                           capture_output=True,text=True,timeout=60)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run=subprocess.run([out/'run'],env=env,capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('cases=8224 complete_index_bytes_equal=1 exact_end_pointer=1 canaries_intact=1',run.stdout)
        print(run.stdout,end='')

    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing renderer dependencies required')
    def test_exact_draws_winding_and_failure_boundaries(self):
        spec=importlib.util.spec_from_file_location('gx_fixture',ROOT/'tests/test_gx_draw_preparation.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        out=Path(os.environ.get('GX_PRIMITIVE_BATCH_TEST_BUILD',str(ROOT/'build/gx-primitive-batch'))).resolve()
        with patch.dict(os.environ,{
            'GX_DRAW_BUILD_DIR':str(out),
            'GX_DRAW_FIXTURE_SOURCE':str(ROOT/'tests/gx_primitive_batch.cpp'),
            'GX_DRAW_VERIFIED_ARRAYS':'2700',
            'MP_GX_DRAW_PREP_PROFILE':'0',
            'MP_GX_PRIMITIVE_BATCH':'1',
        }):
            fixture.GxDrawPreparationTests('test_actual_draw_helpers_and_fetched_array_bytes').test_actual_draw_helpers_and_fetched_array_bytes()
        env=dict(os.environ,MP_GX_DRAW_PREP_PROFILE='0')
        if env.get('GX_DRAW_SANITIZE')=='1':
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        captures={}
        for value in ('0','1'):
            env['MP_GX_PRIMITIVE_BATCH']=value
            run=subprocess.run([out/'run'],env=env,capture_output=True,text=True,timeout=45)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            captures[value]=run.stdout
            maps=data_rows(run.stdout,'INLINE_MAP_CONTRACT')
            self.assertEqual(maps,[{'cases':'35','successes':'7','failures':'28',
                                  'atomic_failure':'1','frame_slot_owned':'1','no_reallocation':'1'}])
        for prefix,expected in [('DRAW_PREP',6),('ADVERSARIAL',22)]:
            a,b=(data_rows(captures[value],prefix) for value in ('0','1'))
            self.assertEqual(len(a),expected);self.assertEqual(len(b),expected)
            for before,after in zip(a,b):
                for key in ('median_ms','min_ms','max_ms'):
                    before.pop(key,None);after.pop(key,None)
                self.assertEqual(before,after)
        for value,expected in [('0',16200),('1',900)]:
            stages=data_rows(captures[value],'DRAW_STAGE')
            for scenario in ('material_uniform_strips','raster_uniform_strips'):
                for stage in ('vertex_staging','index_staging'):
                    row=next(r for r in stages if r['scenario']==scenario and r['stage']==stage)
                    self.assertEqual(int(row['calls']),expected)
        # Batch lookahead must leave malformed next records to the original
        # decoder validation. They must fail there, without an ASan overread.
        for value in ('0','1'):
            env['MP_GX_PRIMITIVE_BATCH']=value
            for kind,message in [('header','Reader overrun'),('payload','draw vertex data overrun')]:
                env['GX_TINY_MALFORMED']=kind
                run=subprocess.run([out/'run'],env=env,capture_output=True,text=True,
                                   preexec_fn=no_core,timeout=10)
                self.assertLess(run.returncode,0,run.stdout+run.stderr)
                self.assertIn(message,run.stdout+run.stderr)
                self.assertNotIn('MALFORMED_ACCEPTED',run.stdout+run.stderr)
                self.assertNotIn('ERROR: AddressSanitizer',run.stdout+run.stderr)
                self.assertNotIn('runtime error:',run.stdout+run.stderr)
        print('Primitive batching: six 16,200-primitive workloads and 22 state/format/partial/spill/overflow/validation cases match exact commands, vertex/index/uniform bytes and winding; malformed next records preserve decoder failures.')

if __name__=='__main__':unittest.main()

"""Compare the real decoder with batching disabled/enabled, including failures."""
import importlib.util
import json
import os
from pathlib import Path
import resource
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

class GxPrimitiveBatchTests(unittest.TestCase):
    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing renderer dependencies required')
    def test_exact_draws_winding_and_failure_boundaries(self):
        spec=importlib.util.spec_from_file_location('gx_fixture',ROOT/'tests/test_gx_draw_preparation.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        out=Path(os.environ.get('GX_PRIMITIVE_BATCH_TEST_BUILD',str(ROOT/'build/gx-primitive-batch')))
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
        for prefix,expected in [('DRAW_PREP',6),('ADVERSARIAL',22)]:
            a,b=(data_rows(captures[value],prefix) for value in ('0','1'))
            self.assertEqual(len(a),expected);self.assertEqual(len(b),expected)
            for before,after in zip(a,b):
                for key in ('median_ms','min_ms','max_ms'):
                    before.pop(key,None);after.pop(key,None)
                self.assertEqual(before,after)
        for value,expected in [('0',16200),('1',1800)]:
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

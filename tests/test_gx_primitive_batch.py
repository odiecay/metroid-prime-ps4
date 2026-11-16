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

def instrument_triangle_scan(source):
    """Count work in the actual private helper; do not replace its algorithm."""
    signature='[[gnu::noinline]] static bool try_batch_triangles('
    original=source_body(source,signature)
    measured=original.replace('while (count >= 3) {',
                              'while (count >= 3) {\n    ++testTriangleVisits;',1)
    measured=measured.replace('  if (!gfx::map_inline_vertices',
                              '  ++testTriangleMaps;\n  if (!gfx::map_inline_vertices',1)
    if measured==original or '++testTriangleVisits' not in measured or '++testTriangleMaps' not in measured:
        raise ValueError('triangle helper no longer matches the operation-count seam')
    source=source.replace(original,measured,1)
    source=source.replace('namespace aurora::gx::fifo {',
                          'namespace aurora::gx::fifo {\nuint64_t testTriangleVisits=0,testTriangleMaps=0;',1)
    wrapper=('bool test_try_batch_triangles(GXVtxFmt fmt,u16 count,u32 size,ByteReader& reader,DrawData* draw) noexcept '
             '{ return try_batch_triangles(fmt,count,size,reader,draw); }\n')
    return source.replace('} // namespace aurora::gx::fifo',wrapper+'} // namespace aurora::gx::fifo',1)

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
        out.mkdir(parents=True,exist_ok=True)
        processor=Path(os.environ.get('GX_DRAW_PROCESSOR_SOURCE',str(
            ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/command_processor.cpp')))
        measured=out/'measured_processor.cpp'
        measured.write_text(instrument_triangle_scan(processor.read_text()))
        wrapper=out/'primitive_batch_fixture.cpp'
        wrapper.write_text('#define GX_PRIMITIVE_BATCH_SCAN_TESTS 1\n#include '+
                           json.dumps(str(ROOT/'tests/gx_primitive_batch.cpp'))+'\n')
        with patch.dict(os.environ,{
            'GX_DRAW_BUILD_DIR':str(out),
            'GX_DRAW_FIXTURE_SOURCE':str(wrapper),
            'GX_DRAW_PROCESSOR_SOURCE':str(measured),
            'GX_DRAW_VERIFIED_ARRAYS':'2700',
            'MP_GX_DRAW_PREP_PROFILE':'0',
            'MP_GX_PRIMITIVE_BATCH':'1',
            'MP_GX_TRIANGLE_BATCH':'1',
        }):
            fixture.GxDrawPreparationTests('test_actual_draw_helpers_and_fetched_array_bytes').test_actual_draw_helpers_and_fetched_array_bytes()
        env=dict(os.environ,MP_GX_DRAW_PREP_PROFILE='0')
        for key in ('GX_TINY_MALFORMED','GX_TINY_MALFORMED_TRIANGLES','GX_TINY_MALFORMED_PREFIX','GX_TINY_TRIANGLE_CASES'):
            env.pop(key,None)
        if env.get('GX_DRAW_SANITIZE')=='1':
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        configs={'disabled':('0','1'),'enabled':('1','1'),'strips_only':('1','0')}
        for triangles in (False,True):
            captures={}
            for label,(master,triangle) in configs.items():
                selected=dict(env,MP_GX_PRIMITIVE_BATCH=master,MP_GX_TRIANGLE_BATCH=triangle)
                if triangles:selected['GX_TINY_TRIANGLE_CASES']='1'
                run=subprocess.run([out/'run'],env=selected,capture_output=True,text=True,timeout=45)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                captures[label]=run.stdout
                self.assertEqual(data_rows(run.stdout,'INLINE_MAP_CONTRACT'),[{
                    'cases':'40','successes':'12','failures':'28','atomic_failure':'1',
                    'frame_slot_owned':'1','no_reallocation':'1'}])
                self.assertEqual(data_rows(run.stdout,'VERTEX_MAP_CONTRACT'),[{
                    'cases':'28','successes':'12','failures':'16','transactional':'1',
                    'frame_slot_owned':'1','no_reallocation':'1','index_unchanged':'1'}])
                scan=data_rows(run.stdout,'TRIANGLE_SCAN_CONTRACT')
                self.assertEqual(len(scan),1)
                on=label=='enabled'
                self.assertEqual({k:v for k,v in scan[0].items() if k not in ('zero_scan_cases','loop_visits')},{
                    'cases':'15','enabled':str(int(on)),'successes':'5' if on else '0',
                    'failures':'10' if on else '15','maps':'5' if on else '0','bounded_fallback':'1'})
                self.assertGreaterEqual(int(scan[0]['zero_scan_cases']),8 if on else 15)
                self.assertLessEqual(int(scan[0]['loop_visits']),18 if on else 0)
            for prefix,expected in [('DRAW_PREP',6),('ADVERSARIAL',25)]:
                baseline=data_rows(captures['disabled'],prefix)
                for row in baseline:
                    for key in ('median_ms','min_ms','max_ms'):row.pop(key,None)
                self.assertEqual(len(baseline),expected)
                for label in ('enabled','strips_only'):
                    candidate=data_rows(captures[label],prefix)
                    for row in candidate:
                        for key in ('median_ms','min_ms','max_ms'):row.pop(key,None)
                    self.assertEqual(candidate,baseline)
            for label in configs:
                stages=data_rows(captures[label],'DRAW_STAGE')
                scenes={row['scenario'] for row in stages}
                self.assertEqual(len(scenes),6)
                for scenario in scenes:
                    strip=scenario.endswith('_strips')
                    batching=label=='enabled' or (label=='strips_only' and strip)
                    for stage in ('vertex_staging','index_staging'):
                        expected=0 if stage=='index_staging' and not strip else (900 if batching else 16200)
                        row=next(r for r in stages if r['scenario']==scenario and r['stage']==stage)
                        self.assertEqual(int(row['calls']),expected,(label,scenario,stage,row))
        # Batch lookahead must leave malformed next records to the original
        # decoder validation. They must fail there, without an ASan overread.
        for triangles in (False,True):
            for prefix in (2,3,17):
                for kind,message in [('header','Reader overrun'),('payload','draw vertex data overrun')]:
                    for label,(master,triangle) in configs.items():
                        selected=dict(env,MP_GX_PRIMITIVE_BATCH=master,MP_GX_TRIANGLE_BATCH=triangle,
                                      GX_TINY_MALFORMED=kind,GX_TINY_MALFORMED_PREFIX=str(prefix),
                                      GX_TINY_MALFORMED_TRIANGLES=str(int(triangles)))
                        run=subprocess.run([out/'run'],env=selected,capture_output=True,text=True,
                                           preexec_fn=no_core,timeout=10)
                        self.assertLess(run.returncode,0,run.stdout+run.stderr)
                        self.assertIn(message,run.stdout+run.stderr)
                        self.assertNotIn('MALFORMED_ACCEPTED',run.stdout+run.stderr)
                        self.assertNotIn('ERROR: AddressSanitizer',run.stdout+run.stderr)
                        self.assertNotIn('runtime error:',run.stdout+run.stderr)
        print('Primitive batching: 40 joint-map, 28 vertex-map, 15 bounded-lookahead cases; '
              'six 16,200-primitive workloads and 25 triangle/25 strip adversarial cases match '
              'exact commands, geometry/uniform bytes and merged counts under all three configurations; '
              '36 malformed next records preserve decoder failures.')

if __name__=='__main__':unittest.main()

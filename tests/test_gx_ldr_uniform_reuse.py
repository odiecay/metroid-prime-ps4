"""Compare the live decoder with LDR UBO reuse enabled and disabled."""
import contextlib
import io
import os
from pathlib import Path
import re
import runpy
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/pc-asset-importer'


def pack_counts(output):
    return {
        fields['scenario']: int(fields['calls'])
        for line in output.splitlines()
        if line.startswith('DRAW_STAGE ') and 'stage=uniform_pack ' in line
        for fields in [dict(re.findall(r'(\w+)=([^ ]+)', line))]
    }


class LdrUniformReuseTests(unittest.TestCase):
    @unittest.skipUnless((BUILD / 'compile_commands.json').exists(),
                         'existing host renderer dependencies required')
    def test_actual_decoder_bytes_transitions_and_fallbacks(self):
        sanitized = os.environ.get('GX_DRAW_SANITIZE') == '1'
        out = ROOT / 'build/gx-ldr-uniform-reuse-test'
        if sanitized:
            out = out.with_name(out.name + '-sanitized')
        out.mkdir(exist_ok=True)
        baseline_file = out / 'baseline.bin'
        candidate_file = out / 'candidate.bin'
        env = dict(os.environ)
        env.update(
            GX_DRAW_BUILD_DIR=str(out),
            GX_DRAW_FIXTURE_SOURCE=str(ROOT / 'tests/gx_ldr_uniform_reuse.cpp'),
            GX_DRAW_PROCESSOR_SOURCE=str(ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gx/command_processor.cpp'),
            GX_DRAW_RECORDING_SOURCE=str(ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gfx/recording.cpp'),
            GX_DRAW_SHADER_INFO_SOURCE=str(ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gx/shader_info.cpp'),
            GX_DRAW_VERIFIED_ARRAYS='2700',
            MP_GX_LDR_UNIFORM_REUSE='0',
            MP_GX_PRIMITIVE_BATCH='1',
            MP_GX_TRIANGLE_BATCH='1',
            MP_GX_DRAW_PREP_PROFILE='0',
            GX_REUSE_CANONICAL=str(baseline_file),
        )
        env.pop('GX_REUSE_BENCH_ONLY', None)
        env.pop('GX_REUSE_SCENE_MASK', None)
        # Reuse the existing compiler/link/staging harness. It compiles the
        # live decoder, recorder and packer; only GPU objects are wrapped.
        driver = runpy.run_path(str(ROOT / 'tests/test_gx_draw_preparation.py'),
                                run_name='gx_uniform_reuse_driver')
        output = io.StringIO()
        with patch.dict(os.environ, env, clear=True), contextlib.redirect_stdout(output):
            driver['GxDrawPreparationTests'].test_actual_draw_helpers_and_fetched_array_bytes(self)
        baseline_output = output.getvalue()

        env.update(MP_GX_LDR_UNIFORM_REUSE='1', GX_REUSE_CANONICAL=str(candidate_file))
        if sanitized:
            env.update(
                LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
            )
        result = subprocess.run([out / 'run'], env=env, capture_output=True,
                                text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        marker = 'REUSE_TRANSITIONS cases=1536 cache_resets=96 frame_resets=32 exact_ubo_pipeline=1'
        self.assertIn(marker, baseline_output)
        self.assertIn(marker, result.stdout)
        baseline = baseline_file.read_bytes()
        candidate = candidate_file.read_bytes()
        self.assertTrue(baseline == candidate,
                        'Pipeline, immediate, draw, binding or fetched UBO bytes changed')
        self.assertGreater(len(baseline), 1_000_000)
        before, after = pack_counts(baseline_output), pack_counts(result.stdout)
        self.assertEqual(before['shader_only_triangles'], 900)
        self.assertEqual(before['shader_only_strips'], 900)
        self.assertEqual(after['shader_only_triangles'], 2)
        self.assertEqual(after['shader_only_strips'], 2)
        self.assertEqual(before['mixed_shader_matrix_strips'], 900)
        self.assertEqual(after['mixed_shader_matrix_strips'], 226)
        for scene in ('same_sized_texture_strips', 'changed_texture_width_strips'):
            self.assertEqual(before[scene], 900)
            self.assertEqual(after[scene], 900)
        self.assertEqual(before['cached_triangles'], 1)
        self.assertEqual(after['cached_triangles'], 1)
        print('LDR_UNIFORM_REUSE workloads=6 transitions=1536 frame_resets=32 '
              f'exact_canonical_bytes={len(baseline)} shader_packs=900_to_2 texture_fallback=1')


if __name__ == '__main__':
    unittest.main()

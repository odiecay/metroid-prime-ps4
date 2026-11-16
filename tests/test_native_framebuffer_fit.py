"""Compare native Auto sizing with the pinned logical-aspect fit implementation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'game/MetroidPrimePort'


def function(source, name):
    start = source.index(name)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


class NativeFramebufferFitTests(unittest.TestCase):
    @unittest.skipUnless((GAME / 'extern/aurora/lib/window.cpp').is_file(), 'Bootstrap upstream source first')
    def test_actual_sizing_reference_limits_and_platform_boundaries(self):
        sanitized = os.environ.get('PRIME_FRAMEBUFFER_SANITIZE') == '1'
        out = ROOT / ('build/native-framebuffer-fit-regression' + ('-sanitized' if sanitized else ''))
        out.mkdir(parents=True, exist_ok=True)
        pin = next(c['commit'] for c in json.loads((ROOT / 'upstream.lock.json').read_text())['components']
                   if c['path'] == 'game/MetroidPrimePort')
        baseline = subprocess.check_output(['git', '-C', str(GAME), 'show',
                                           pin + ':extern/aurora/lib/window.cpp'], text=True)
        candidate = (GAME / 'extern/aurora/lib/window.cpp').read_text()
        for label, source in [('baseline', baseline), ('candidate', candidate)]:
            parts = [function(source, 'Vec2<int> scale_frame_buffer_to_aspect('),
                     function(source, 'Vec2<int> fit_frame_buffer_to_aspect(')]
            if label == 'candidate':
                parts += ['#if defined(__ORBIS__)\n' + function(source, 'static bool native_auto_quantization_fit(') + '#endif\n']
            parts += [function(source, 'Vec2<int> frame_buffer_size_for_scale(')]
            (out / (label + '_functions.inc')).write_text('\n'.join(parts))
        (out / 'actual_env.inc').write_text(function(
            (GAME / 'extern/aurora/lib/internal.hpp').read_text(), 'inline bool env_flag('))
        (out / 'present_viewport.inc').write_text(function(
            (GAME / 'extern/aurora/lib/webgpu/gpu.cpp').read_text(), 'Viewport calculate_present_viewport('))
        compiler = shutil.which('g++') if sanitized else (shutil.which('clang++') or shutil.which('g++'))
        self.assertIsNotNone(compiler)
        for orbis in (False, True):
            exe = out / ('orbis-logic' if orbis else 'desktop-logic')
            flags = ['-std=c++20', '-O2', '-g', '-I' + str(out),
                     '-I' + str(GAME / 'extern/aurora/include')]
            if orbis:
                flags += ['-D__ORBIS__']
            if sanitized:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            built = subprocess.run([compiler, *flags, str(ROOT / 'tests/native_framebuffer_fit.cpp'),
                                    '-o', str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for value in (None, '0', 'false', 'off', '1'):
                env = dict(os.environ)
                env.pop('MP_PS4_NATIVE_EFB', None)
                if value is not None:
                    env['MP_PS4_NATIVE_EFB'] = value
                if sanitized:
                    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
                run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=30)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn('sizing_cases=207480', run.stdout)
                if orbis:
                    self.assertIn('integer_boundary_cases=200000', run.stdout)
                print(run.stdout, end='')


if __name__ == '__main__':
    unittest.main()

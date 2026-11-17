"""Actual native scanout readiness code, with only Sony services substituted."""
from pathlib import Path
import subprocess
import unittest

if __package__:
    from .test_present_prewait import block
else:
    from test_present_prewait import block

ROOT = Path(__file__).resolve().parents[1]


class WsiPresentPrewaitTests(unittest.TestCase):
    def test_actual_registry_wait_and_teardown_lifetime(self):
        source = (ROOT / 'external/mesa-ps4/src/vulkan/wsi/wsi_orbis.c').read_text()
        out = ROOT / 'build/wsi-present-prewait-tests'
        out.mkdir(exist_ok=True)
        start = source.index('static pthread_mutex_t prime_scanout_mutex')
        end = source.index('static void\nprime_scanout_register', start)
        actual = [block(source, 'struct wsi_orbis_scanout {') + ';', source[start:end]]
        for signature in ('static void\nprime_scanout_register',
                          'static void\nprime_scanout_unregister',
                          'uint32_t\nwsi_orbis_scanout_reserve',
                          'static bool\nwsi_orbis_wait_for_flip_slot',
                          'int\nwsi_orbis_prepare_present'):
            actual.append(block(source, signature))
        (out / 'actual.inc').write_text('\n'.join(actual))
        create = block(source, 'struct wsi_orbis_scanout *\nwsi_orbis_scanout_create')
        destroy = block(source, 'void\nwsi_orbis_scanout_destroy')
        self.assertLess(create.index('prime_scanout_register(so)'), create.index('return so;'))
        self.assertLess(destroy.index('prime_scanout_unregister(so)'), destroy.index('wsi_orbis_release(so)'))
        present = block(source, 'bool\nwsi_orbis_scanout_present')
        self.assertIn('if (!wsi_orbis_wait_for_flip_slot(so))', present)
        for sanitized in (False, True):
            exe = out / ('run-san' if sanitized else 'run')
            cmd = ['g++-14', '-std=c++20', '-O2', '-pthread', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(out), str(ROOT / 'tests/wsi_present_prewait.cpp'), '-o', str(exe)]
            if sanitized:
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(cmd, check=True)
            result = subprocess.run([exe], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('authoritative_recheck=1 lifetime_pin=1', result.stdout)
            print(result.stdout.strip())

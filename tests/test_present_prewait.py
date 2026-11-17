"""Exercise the actual Aurora/Dawn present blocks across the device-lock seam."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class PresentPrewaitTests(unittest.TestCase):
    def test_real_present_guards_and_ownership_are_preserved(self):
        aurora = (ROOT / 'game/MetroidPrimePort/extern/aurora/lib/aurora.cpp').read_text()
        dawn = (ROOT / 'external/dawn/src/dawn/native/Surface.cpp').read_text()
        present = block(aurora, '    if (canPresent && g_surface) {')
        self.assertEqual(present.count('prime_orbis_prepare_present();'), 1)
        self.assertLess(present.index('window::SurfaceLock'), present.index('prime_orbis_prepare_present();'))
        self.assertLess(present.index('window::is_presentable()'), present.index('prime_orbis_prepare_present();'))
        self.assertLess(present.index('prime_orbis_prepare_present();'), present.index('g_surface.Present()'))
        # The advisory return value never replaces the authoritative Present.
        self.assertIn('void prime_orbis_prepare_present();', aurora)
        actual_api = block(dawn, 'wgpu::Status Surface::APIPresent()')
        self.assertIn('auto deviceGuard = GetCurrentDevice()->GetGuard();', actual_api)
        self.assertIn('DAWN_TRY(mSwapChain->Present());', actual_api)
        out = ROOT / 'build/present-prewait-tests'
        out.mkdir(exist_ok=True)
        (out / 'actual_aurora_present.inc').write_text(present)
        (out / 'actual_dawn_present.inc').write_text(actual_api)
        for native in (False, True):
            for sanitized in (False, True):
                program = out / (('orbis' if native else 'desktop') + ('-san' if sanitized else ''))
                command = ['g++-14', '-std=c++20', '-O2', '-pthread', '-I' + str(out),
                           str(ROOT / 'tests/present_prewait.cpp'), '-o', str(program)]
                if native:
                    command += ['-D__ORBIS__']
                if sanitized:
                    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                subprocess.run(command, check=True)
                result = subprocess.run([program], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('unchanged_authoritative_present=1 ownership_checks=1', result.stdout)
                print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()

from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FifoMetricsTests(unittest.TestCase):
    def test_cpu_wall_accounting_and_unsupported_clocks(self):
        out = ROOT / 'build/fifo-metrics-tests'
        out.mkdir(exist_ok=True)
        binary = out / 'metrics'
        subprocess.run(['g++-14', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gx'),
                        str(ROOT / 'tests/fifo_metrics.cpp'), '-o', str(binary)], check=True)
        result = subprocess.run([binary], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_fifo_worker_sampling_and_frame_attribution(self):
        out = ROOT / 'build/fifo-metrics-tests'
        out.mkdir(exist_ok=True)
        host = ROOT / 'build/pc-asset-importer'
        binary = out / 'worker'
        upstream = ROOT / 'game/MetroidPrimePort/extern/aurora'
        subprocess.run(['g++-14', '-std=c++20', '-O1', '-g', '-pthread', '-DAURORA', '-DTARGET_PC',
            '-DPRIME_TRACE_TEST=1', '-DTRACY_ON_DEMAND', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(upstream / 'include'), '-I' + str(upstream / 'lib'),
            '-I' + str(host / '_deps/fmt-src/include'), '-I' + str(host / '_deps/tracy-src/public'),
            '-I' + str(ROOT / 'external/SDL-release-3.4.10/include'), '-I' + str(host / 'sdl/include-revision'),
            str(ROOT / 'tests/fifo_batch_instrumentation.cpp'), str(upstream / 'lib/gx/fifo.cpp'),
            str(upstream / 'lib/thread.cpp'), str(host / '_deps/fmt-build/libfmt.a'),
            str(host / '_deps/tracy-build/libTracyClient.a'), str(host / 'sdl/libSDL3.a'),
            '-ldl', '-lm', '-o', str(binary)], check=True)
        for _ in range(10):
            result = subprocess.run([binary], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_orbis_startup_reserve_and_fallback_ownership(self):
        # Same production FIFO/worker fixture, compiling only fifo.cpp with its
        # Orbis branch. Thread/SDL remain host services for concurrency testing.
        host = ROOT / 'build/pc-asset-importer'
        upstream = ROOT / 'game/MetroidPrimePort/extern/aurora'
        for sanitize in (False, True):
            out = ROOT / 'build/fifo-metrics-tests' / ('reserve-sanitized' if sanitize else 'reserve-normal')
            out.mkdir(parents=True, exist_ok=True)
            flags=['g++-14','-std=c++20','-O1','-g','-pthread','-DAURORA','-DTARGET_PC',
                '-DPRIME_TRACE_TEST=1','-DTRACY_ON_DEMAND','-I'+str(upstream/'include'),'-I'+str(upstream/'lib'),
                '-I'+str(host/'_deps/fmt-src/include'),'-I'+str(host/'_deps/tracy-src/public'),
                '-I'+str(ROOT/'external/SDL-release-3.4.10/include'),'-I'+str(host/'sdl/include-revision')]
            if sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            obj=out/'fifo.o';binary=out/'worker'
            subprocess.run(flags+['-D__ORBIS__=1','-c',str(upstream/'lib/gx/fifo.cpp'),'-o',str(obj)],check=True)
            subprocess.run(flags+['-DPRIME_FIFO_RESERVE_TEST=1',str(ROOT/'tests/fifo_batch_instrumentation.cpp'),str(obj),
                str(upstream/'lib/thread.cpp'),str(host/'_deps/fmt-build/libfmt.a'),str(host/'_deps/tracy-build/libTracyClient.a'),
                str(host/'sdl/libSDL3.a'),'-Wl,--wrap=malloc','-Wl,--wrap=realloc','-ldl','-lm','-o',str(binary)],check=True)
            for _ in range(3):
                result=subprocess.run([binary],capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('allocation-failure64KiB fallback passed',result.stdout)

if __name__ == '__main__':
    unittest.main()

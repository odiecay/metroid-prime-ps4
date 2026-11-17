"""Run the actual loader/token branches with the real bounded trace lifetimes."""
from pathlib import Path
import subprocess
import os
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'game/MetroidPrimePort'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class LoaderTraceTests(unittest.TestCase):
    def test_actual_loader_and_token_paths(self):
        loader = (GAME / 'src/MetroidPrime/CMFGame.cpp').read_text()
        resource = (GAME / 'src/Kyoto/CResFactory.cpp').read_text()
        code = (ROOT / 'tests/loader_trace.cpp').read_text()
        code = code.replace('/* ACTUAL_LOADER */', function(loader, 'CIOWin::EMessageReturn CMFGameLoader::OnMessage('))
        code = code.replace('/* ACTUAL_TOKEN */', function(resource, 'rstl::auto_ptr< IObj > CResFactory::Build('))
        out = ROOT / 'build/loader-trace-tests'
        out.mkdir(exist_ok=True)
        unit = out / 'actual.cpp'
        unit.write_text(code)
        # Native-only tracing must leave the exact call/return/exception paths
        # unchanged. The enabled arm uses Aurora's real ring/RAII implementation.
        for enabled, sanitizer in ((False, False), (True, False), (True, True)):
            binary = out / ('sanitized' if sanitizer else 'normal' if enabled else 'disabled')
            command = ['g++-14', '-std=c++20', '-O2', '-I' + str(GAME / 'extern/aurora/include'), str(unit), '-o', str(binary)]
            if enabled:
                command += ['-D__ORBIS__']
            if sanitizer:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('loader_paths=7 token_paths=4 order_and_unwind=ok', result.stdout)
            print(result.stdout.strip())

    def test_actual_deep_loader_branches_and_unwind(self):
        # PRIME_LOADER_SOURCE_DIR tests the reviewed private candidate before
        # integration; by default this compiles the actual production sources.
        sources = Path(os.environ.get('PRIME_LOADER_SOURCE_DIR', GAME / 'src/MetroidPrime'))
        out = ROOT / 'build/loader-deep-trace-tests'
        out.mkdir(exist_ok=True)
        fixture = (ROOT / 'tests/loader_deep_trace.cpp').read_text()
        def read(name):
            candidate = sources / ('candidate-' + name)
            return (candidate if candidate.exists() else sources / name).read_text()
        state = read('CStateManager.cpp')
        guard_start = state.index('#ifdef __ORBIS__\nnamespace {\nthread_local bool sPortLoaderInitTraceActive')
        guard_end = state.index('void CStateManager::InitializeState(', guard_start)
        code = fixture.replace('/* ACTUAL_GUARD */', state[guard_start:guard_end])
        labels = set()
        for marker, name, signature in (
            ('INITIALIZE', 'CStateManager.cpp', 'void CStateManager::InitializeState('),
            ('WORLD', 'CWorld.cpp', 'bool CWorld::CheckWorldComplete('),
            ('VALIDATE', 'CGameArea.cpp', 'void CGameArea::Validate('),
            ('STREAM', 'CGameArea.cpp', 'void CGameArea::StartStreamIn('),
        ):
            actual = function(read(name), signature)
            code = code.replace('/* ACTUAL_' + marker + ' */', actual)
            labels.update(re.findall(r'(?:loaderCallTrace\(|loaderCallTrace.emplace\()"([^"]+)"', actual))
        self.assertEqual(len(labels), 24)
        (out / 'actual_label_names.inc').write_text(''.join('"' + label + '",\n' for label in sorted(labels)))
        unit = out / 'actual.cpp'
        unit.write_text(code)
        results = []
        for enabled, sanitizer in ((False, False), (True, False), (True, True)):
            binary = out / ('sanitized' if sanitizer else 'normal' if enabled else 'disabled')
            command = ['g++-14', '-std=c++20', '-O1', '-g', '-pthread', '-Wno-multichar',
                '-Wno-format', '-I' + str(GAME / 'extern/aurora/include'), str(unit), '-o', str(binary)]
            if enabled:
                command += ['-D__ORBIS__', '-DPRIME_LOADER_DEEP_TRACE_EXPECTED']
            if sanitizer:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
            subprocess.run(command, check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('deep_loader_cases=47 injected_exceptions=28 drops=0', result.stderr)
            results.append(result.stdout)
        self.assertEqual(results[0], results[1])
        self.assertEqual(results[1], results[2])
        baseline_dir = os.environ.get('PRIME_LOADER_BASELINE_DIR')
        if baseline_dir:
            baseline_code = code
            for _, name, signature in (
                ('INITIALIZE', 'CStateManager.cpp', 'void CStateManager::InitializeState('),
                ('WORLD', 'CWorld.cpp', 'bool CWorld::CheckWorldComplete('),
                ('VALIDATE', 'CGameArea.cpp', 'void CGameArea::Validate('),
                ('STREAM', 'CGameArea.cpp', 'void CGameArea::StartStreamIn('),
            ):
                old_body = function((Path(baseline_dir) / ('baseline-' + name)).read_text(), signature)
                baseline_code = baseline_code.replace(function(baseline_code, signature), old_body)
            baseline_unit = out / 'baseline.cpp'
            baseline_unit.write_text(baseline_code)
            binary = out / 'baseline'
            subprocess.run(['g++-14', '-std=c++20', '-O1', '-g', '-pthread', '-Wno-multichar',
                '-Wno-format', '-D__ORBIS__', '-I' + str(GAME / 'extern/aurora/include'),
                str(baseline_unit), '-o', str(binary)], check=True)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout, results[0])
        print('deep loader trace on/off: 47 identical call/state paths, 28 unwind cases, 24 labels')


if __name__ == '__main__':
    unittest.main()

"""Run the actual loader/token branches with the real bounded trace lifetimes."""
from pathlib import Path
import subprocess
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


if __name__ == '__main__':
    unittest.main()

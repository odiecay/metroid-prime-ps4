"""Check pass-local lookup ownership against actual Dawn command streams."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/pc-asset-importer'
GFX = ROOT / 'game/MetroidPrimePort/extern/aurora/lib/gfx'


def function(source, signature):
    start = source.index(signature)
    begin = source.index('{', start)
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class GfxPassLookupTests(unittest.TestCase):
    @unittest.skipUnless((BUILD / 'compile_commands.json').exists(),
                         'Build existing Dawn Null host dependencies first')
    def test_actual_encoding_commands_usage_ownership_and_lifecycle(self):
        source = (GFX / 'encoding.cpp').read_text()
        render_pass = function(source, 'void render_pass(')
        self.assertIn('ScopedPassObjectLookups passObjectLookups;', render_pass)
        custom = render_pass.split('case CommandType::CustomDraw:', 1)[1].split('case CommandType::DebugMarker:', 1)[0]
        self.assertLess(custom.index('ScopedPassObjectLookupPause pause;'), custom.index('render_custom_draw('))
        self.assertLess(custom.index('render_custom_draw('), custom.index('g_passObjectLookups->clear();'))
        self.assertIn('g_currentPipeline = UINTPTR_MAX;', custom)
        self.assertIn('g_currentTextureGroup = 0;', custom)

        out = ROOT / 'build/gfx-pass-lookup-host'
        out.mkdir(exist_ok=True)
        (out / 'pass_lookup_memo.hpp').write_bytes((GFX / 'pass_lookup_memo.hpp').read_bytes())
        start = source.index('struct PassObjectLookups')
        end = source.index('\nvoid apply_viewport', start)
        (out / 'actual_pass_lookup_scopes.inc').write_text(source[start:end])
        binds = '[[gnu::noinline]] ' + function(source, 'bool bind_changed_pipeline(') + '\n[[gnu::noinline]] ' + function(source, 'void bind_changed_texture_group(') + '\n'
        binds += function(source, 'bool bind_pipeline(') + '\n' + function(source, 'void bind_texture_group(')
        binds = binds.replace('bool bind_pipeline(', '[[gnu::noinline]] bool after_bind_pipeline(').replace(
            'void bind_texture_group(', '[[gnu::noinline]] void after_bind_texture_group(')
        (out / 'after_lookup_bind.inc').write_text(binds)
        pipeline = (GFX / 'pipeline_cache.cpp').read_text()
        group = (GFX / 'resource_cache.cpp').read_text()
        lookups = function(pipeline, 'bool get_pipeline(').replace('bool get_pipeline(', 'bool actual_get_pipeline(')
        lookups += '\n' + function(group, 'wgpu::BindGroup find_bind_group(').replace(
            'wgpu::BindGroup find_bind_group(', 'wgpu::BindGroup actual_find_bind_group(')
        (out / 'actual_cache_lookup.inc').write_text(lookups)

        sanitizer = os.environ.get('PRIME_LOOKUP_SANITIZE') == '1'
        extra = []
        if sanitizer:
            headers = out / 'headers'
            headers.mkdir(exist_ok=True)
            if not (headers / 'sanitizer').exists():
                (headers / 'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer', target_is_directory=True)
            # Match the existing unsanitized Abseil container ABI in the host libraries.
            extra = ['-DNDEBUG_SANITIZER', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-isystem', str(headers)]
        recipes = json.loads((BUILD / 'compile_commands.json').read_text())
        recipe = next(row for row in recipes if row['file'].endswith('/PassResourceUsageTracker.cpp'))
        args = shlex.split(recipe['command'])
        obj = out / 'test.o'
        args[args.index('-o') + 1] = str(obj)
        args[args.index('-c') + 1] = str(ROOT / 'tests/gfx_pass_lookup.cpp')
        args += extra + ['-UNDEBUG', '-I' + str(out)]
        result = subprocess.run(args, cwd=recipe['directory'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        line = subprocess.check_output(['ninja', '-C', str(BUILD), '-t', 'commands', 'gfx_recording_tests'], text=True).splitlines()[-1]
        tokens = shlex.split(line)
        args = tokens[2:tokens.index('&&', 2)]
        args = [arg for arg in args if not ('gfx_recording_tests.dir/' in arg and arg.endswith('.o'))]
        dependency = next((i for i, arg in enumerate(args) if arg.startswith('--dependency-file=')), None)
        if dependency is not None:
            del args[dependency - 1:dependency + 1]
        exe = out / 'run'
        args[args.index('-o') + 1] = str(exe)
        args.insert(args.index('-o'), str(obj))
        env = dict(os.environ)
        if sanitizer:
            args += ['-fsanitize=address,undefined', '-fno-sanitize-link-runtime', '-lasan', '-lubsan']
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('actual_dawn_draws=192000 same_command_stream=1 devices=12', result.stdout)
        self.assertIn('owned_refs_return_to_baseline=1 bounded_bytes=2080', result.stdout)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()

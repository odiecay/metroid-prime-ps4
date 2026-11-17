"""Run the existing real-Dawn usage/encoding fixture with the actual cache policy."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/pc-asset-importer'


class DawnBindGroupUsageTests(unittest.TestCase):
    @unittest.skipUnless((BUILD / 'compile_commands.json').exists(),
                         'Build the existing Dawn Null host dependencies first')
    def test_actual_cache_identity_lifetimes_and_finished_encoding(self):
        out = ROOT / 'build/dawn-usage-policy-tests'
        out.mkdir(exist_ok=True)
        source = (ROOT / 'external/dawn/src/dawn/native/PassResourceUsageTracker.cpp').read_text()
        begin = source.index('bool BindGroupUsageReuseEnabled() {')
        end = source.index('\n}  // namespace', begin)
        # Change only the existing enable selector in this proof object. This
        # lets one real device compare full tracking and the production cache
        # without changing its resources, command stream or native class ABI.
        source = source[:begin] + 'bool BindGroupUsageReuseEnabled() { return prime_usage_test_reuse; }\n' + source[end:]
        source = 'extern "C" bool prime_usage_test_reuse;\n' + source.replace('#ifdef __ORBIS__', '#if 1')
        actual = out / 'actual_tracker.cpp'
        actual.write_text(source)
        recipes = json.loads((BUILD / 'compile_commands.json').read_text())
        recipe = next(c for c in recipes if c['file'].endswith('/PassResourceUsageTracker.cpp'))
        command = subprocess.check_output(['ninja', '-C', str(BUILD), '-t', 'commands',
                                           'gfx_recording_tests'], text=True).splitlines()[-1]
        for sanitized in (False, True):
            extra = []
            env = dict(os.environ)
            env.pop('MP_BIND_GROUP_USAGE_REUSE', None)
            if sanitized:
                headers = out / 'headers'
                headers.mkdir(exist_ok=True)
                if not (headers / 'sanitizer').exists():
                    (headers / 'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',
                                                     target_is_directory=True)
                extra = ['-DNDEBUG_SANITIZER', '-fsanitize=address,undefined',
                         '-fno-omit-frame-pointer', '-isystem', str(headers)]
                env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            objects = []
            for name, unit in [('tracker', actual), ('fixture', ROOT / 'tests/dawn_bind_group_usage.cpp')]:
                obj = out / (name + ('-san' if sanitized else '') + '.o')
                args = shlex.split(recipe['command'])
                args[args.index('-c') + 1] = str(unit)
                args[args.index('-o') + 1] = str(obj)
                args += ['-UNDEBUG', '-DPRIME_USAGE_POLICY_PROOF=1', *extra]
                result = subprocess.run(args, cwd=recipe['directory'], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(str(obj))
            args = shlex.split(command)[2:-2]
            args = [a for a in args if not ('gfx_recording_tests.dir/' in a and a.endswith('.o'))]
            dependency = next((i for i, a in enumerate(args) if a.startswith('--dependency-file=')), None)
            if dependency is not None:
                del args[dependency - 1:dependency + 1]
            program = out / ('sanitized' if sanitized else 'normal')
            args[args.index('-o') + 1] = str(program)
            args[args.index('-o'):args.index('-o')] = objects
            if sanitized:
                args += ['-fsanitize=address,undefined', '-fno-sanitize-link-runtime', '-lasan', '-lubsan']
            result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([program], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('actual_null_usage_equal=1', result.stdout)
            self.assertIn('monotonic_cases=250000 baseline_hits_preserved=1 actual_hit_counts_equal_model=1', result.stdout)
            print(result.stdout.strip())
            for pattern in ('reuse16', 'reuse48', 'reuse96', 'skew', 'cold', 'empty'):
                result = subprocess.run([program, '--verify'], env=dict(env, PRIME_USAGE_WORKLOAD=pattern),
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                self.assertEqual(len(rows), 2)
                self.assertTrue(all(row['same_commands_and_usage'] for row in rows))
                self.assertEqual(rows[0]['hits'], 0)
                self.assertEqual(rows[0]['hits'] + rows[0]['misses'], rows[1]['hits'] + rows[1]['misses'])
            print('actual_encoder workloads=6 frames_per_mode=64 unchanged_commands_offsets_usage=1 sanitized=' + str(sanitized))


if __name__ == '__main__':
    unittest.main()

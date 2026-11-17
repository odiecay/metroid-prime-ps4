"""Compare the complete GX LDR builder with its generic reference on real Dawn objects."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/pc-asset-importer'

class GxBindingTests(unittest.TestCase):
    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing Dawn host dependencies required')
    def test_actual_descriptors_hashes_cache_and_device_lifecycle(self):
        sanitized=os.environ.get('PRIME_BINDINGS_SANITIZE')=='1'
        out=Path(os.environ.get('PRIME_BINDINGS_BUILD_DIR',str(ROOT/'build/gx-bindings-host')))/('sanitized'if sanitized else'normal')
        out.mkdir(parents=True,exist_ok=True)
        original=ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/gx.cpp'
        source=original.read_text()
        reference=(ROOT/'tests/gx_bindings_reference.inc').read_text()
        insertion=source.index('void initialize() noexcept {')
        extra=reference+'\nsize_t test_ldr_binding_scratch_size() noexcept {return sizeof(LdrBindingScratch);}\n'
        candidate=out/'actual_gx.cpp';candidate.write_text(source[:insertion]+extra+source[insertion:])
        recipes=json.loads((BUILD/'compile_commands.json').read_text())
        dawn=next(c for c in recipes if c['file'].endswith('/PassResourceUsageTracker.cpp'))
        includes=[];dawn_args=shlex.split(dawn['command'])
        for i,arg in enumerate(dawn_args):
            if arg.startswith('-I'):includes.append(arg)
            elif arg in ('-isystem','-iquote'):includes.extend([arg,dawn_args[i+1]])
        flags=[]
        if sanitized:
            headers=out/'headers';headers.mkdir(exist_ok=True)
            if not (headers/'sanitizer').exists():(headers/'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
            # Retain the archive's release container/debug layout under sanitizers.
            flags=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        objects=[]
        for suffix,file,stem in [('/gx/gx.cpp',candidate,'gx'),('/gx_uniform_limit_test.cpp',ROOT/'tests/gx_bindings.cpp','test')]:
            recipe=next(c for c in recipes if c['file'].endswith(suffix));args=shlex.split(recipe['command']);obj=out/(stem+'.o')
            args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(file)
            args+=['-I'+str(original.parent),'-I'+str(ROOT/'external/dawn'),'-I'+str(BUILD/'dawn/gen')]+includes+flags
            run=subprocess.run(args,cwd=recipe['directory'],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr);objects.append(str(obj))
        line=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
        args=shlex.split(line)[2:-2];args=[a for a in args if not('gfx_recording_tests.dir/'in a and a.endswith('.o'))]
        dep=next((i for i,a in enumerate(args)if a.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        exe=out/'run';args[args.index('-o')+1]=str(exe);args[args.index('-o'):args.index('-o')]=objects
        args+=['-Wl,--wrap=_ZN6aurora3gfx14bind_group_refEmRK23WGPUBindGroupDescriptor','-Wl,--wrap=_ZN6aurora3gfx13current_frameEv']
        env=dict(os.environ)
        if sanitized:
            args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run=subprocess.run(args,cwd=BUILD,capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        run=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=60);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('paired_builders=6144 actual_api_pairs=5120 lightmap17_descriptor_only_pairs=1024 devices=8',run.stdout)
        self.assertIn('descriptors_hashes_and_fields_equal=1 scratch_bytes=1744',run.stdout)
        print(run.stdout,end='')

if __name__=='__main__':unittest.main()

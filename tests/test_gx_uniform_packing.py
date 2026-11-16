"""Compare the actual uniform packer with its pinned upstream wire serializer."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/pc-asset-importer'


def function_span(source,signature):
    start=source.index(signature)
    pos=source.index('{',start)+1
    depth=1
    end=pos
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return start,end


class UniformPackingTests(unittest.TestCase):
    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing host renderer dependencies required')
    def test_complete_uniform_bytes_across_state_and_frame_changes(self):
        out=ROOT/'build/gx-uniform-packing-test'
        if os.environ.get('GX_UNIFORM_SANITIZE')=='1':out=out.with_name(out.name+'-sanitized')
        out.mkdir(exist_ok=True)
        recipes=json.loads((BUILD/'compile_commands.json').read_text())
        source=ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/shader_info.cpp'
        current=source.read_text()
        lock=json.loads((ROOT/'upstream.lock.json').read_text())
        pin=next(c['commit'] for c in lock['components'] if c['path']=='game/MetroidPrimePort')
        upstream=subprocess.check_output(['git','-C',str(ROOT/'game/MetroidPrimePort'),'show',
            pin+':extern/aurora/lib/gx/shader_info.cpp'],text=True)
        # Reuse the unchanged analyzer/texture helpers, but obtain the reference
        # serializer directly from the exact upstream pin, before bulk packing.
        a,b=function_span(current,'static void fill_uniform(')
        c,d=function_span(upstream,'static void fill_uniform(')
        reference=out/'reference_shader_info.cpp'
        reference.write_text(current[:a]+upstream[c:d]+current[b:])
        sanitized=os.environ.get('GX_UNIFORM_SANITIZE')=='1'
        flags=[]
        if sanitized:
            headers=out/'headers'
            headers.mkdir(exist_ok=True)
            shim=headers/'sanitizer'
            if not shim.exists():shim.symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
            flags=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),
                   '-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        objects=[]
        renames=['-Dbuild_uniform=baseline_build_uniform','-Dbuild_shader_info=baseline_build_shader_info',
                 '-Dpipeline_unused_inputs=baseline_pipeline_unused_inputs']
        inputs=[('/gx_uniform_limit_test.cpp',ROOT/'tests/gx_uniform_packing.cpp','fixture',[]),
                ('/gx/shader_info.cpp',reference,'reference',renames),
                ('/gx/shader_info.cpp',source,'candidate',[])]
        for suffix,path,label,definitions in inputs:
            recipe=next(c for c in recipes if c['file'].endswith(suffix))
            args=shlex.split(recipe['command'])
            obj=out/(label+'.o')
            args[args.index('-o')+1]=str(obj)
            args[args.index('-c')+1]=str(path)
            args+=['-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx'),
                   '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/tests')]+definitions+flags
            result=subprocess.run(args,cwd=recipe['directory'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            objects.append(str(obj))
        line=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
        args=shlex.split(line)[2:-2]
        args=[a for a in args if not ('gfx_recording_tests.dir/' in a and a.endswith('.o'))]
        dep=next((i for i,a in enumerate(args)if a.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        executable=out/'run'
        args[args.index('-o')+1]=str(executable)
        args[args.index('-o'):args.index('-o')]=objects
        if sanitized:args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        result=subprocess.run(args,cwd=BUILD,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
            ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run([executable],env=env,capture_output=True,text=True,timeout=90)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('UNIFORM_BYTES cases=8192 frame_resets=127',result.stdout)
        self.assertIn('exact_bits=1',result.stdout)
        print(result.stdout,end='')


if __name__=='__main__':unittest.main()

"""Differential pruning check against the full production shader analyser."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/pc-asset-importer'

class GxInputPruningTests(unittest.TestCase):
    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing renderer dependencies required')
    def test_all_pruned_bytes_match_full_analyser(self):
        out=ROOT/'build/gx-input-pruning';out.mkdir(exist_ok=True)
        recipes=json.loads((BUILD/'compile_commands.json').read_text())
        sanitized=os.environ.get('GX_PRUNE_SANITIZE')=='1'
        extra=[]
        if sanitized:
            headers=out/'headers';headers.mkdir(exist_ok=True)
            shim=headers/'sanitizer'
            if not shim.exists():shim.symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
            extra=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        objs=[]
        for suffix,src,name in [('/gx_uniform_limit_test.cpp',ROOT/'tests/gx_input_pruning.cpp','test'),('/gx/shader_info.cpp',ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/shader_info.cpp','candidate')]:
            recipe=next(c for c in recipes if c['file'].endswith(suffix))
            args=shlex.split(recipe['command']);obj=out/(name+'.o')
            args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(src)
            args+=extra
            subprocess.run(args,cwd=recipe['directory'],check=True,capture_output=True,text=True)
            objs.append(str(obj))
        line=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
        args=shlex.split(line)[2:-2]
        args=[a for a in args if not ('gfx_recording_tests.dir/' in a and a.endswith('.o'))]
        dep=next((i for i,a in enumerate(args)if a.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        exe=out/'run';args[args.index('-o')+1]=str(exe);args[args.index('-o'):args.index('-o')]=objs
        if sanitized:args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(args,cwd=BUILD,check=True,capture_output=True,text=True)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('configs=131584',result.stdout)
        print(result.stdout,end='')

if __name__=='__main__':unittest.main()

import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class BenchmarkInputTests(unittest.TestCase):
    def test_actual_padread_exclusive_ownership(self):
        build=Path(os.environ.get('PRIME_HOST_BUILD',str(ROOT/'build/pc-asset-importer')))
        out=Path(os.environ.get('PRIME_BENCHMARK_INPUT_BUILD',str(ROOT/'build/benchmark-input-ownership')))
        out.mkdir(parents=True,exist_ok=True)
        actual=ROOT/'game/MetroidPrimePort/extern/aurora/lib/dolphin/pad/pad.cpp'
        entries=json.loads((build/'compile_commands.json').read_text())
        entry=next(e for e in entries if e['file']==str(actual))
        command=entry.get('arguments') or shlex.split(entry['command'])
        command=[a for a in command if a not in ('-DNDEBUG','-fcolor-diagnostics')]
        command[command.index('-c')+1]=str(ROOT/'tests/benchmark_input.cpp')
        command[command.index('-o')+1]=str(out/'input.o')
        command+=['-DPRIME_ACTUAL_PAD_CPP="'+str(actual)+'"']
        sanitized=os.environ.get('PRIME_BENCHMARK_SANITIZE')=='1'
        # Existing host libraries use Abseil's normal container layout. Its
        # supported switch keeps that ABI while actual PAD code remains fully
        # instrumented; fixture assertions remain enabled (no NDEBUG).
        san=['-O1','-g','-DNDEBUG_SANITIZER','-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer',
             '-idirafter','/usr/lib/gcc/x86_64-linux-gnu/14/include',
             '-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        if sanitized:command+=san
        compiled=subprocess.run(command,cwd=entry['directory'],capture_output=True,text=True)
        self.assertEqual(compiled.returncode,0,compiled.stderr)
        line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
        args=shlex.split(line);args=args[args.index('/usr/lib/llvm-19/bin/clang++'):]
        if '&&' in args:args=args[:args.index('&&')]
        args=[a for a in args if not a.endswith('.o') and not a.startswith('--dependency-file=')]
        args=[a for a in args if a!='-Xlinker']
        args[args.index('-o')+1]=str(out/'run')
        args.insert(1,str(out/'input.o'))
        # This standalone input fixture first references Aurora core; its
        # existing static core/GX/VI libraries have reciprocal dependencies.
        first=next(i for i,a in enumerate(args) if a.endswith('.a'))
        args.insert(first,'-Wl,--start-group');args.append('-Wl,--end-group')
        args.append('-Wl,--wrap=SDL_GetKeyboardState')
        if sanitized:args+=san
        linked=subprocess.run(args,cwd=build,capture_output=True,text=True)
        self.assertEqual(linked.returncode,0,linked.stderr)
        env=dict(os.environ,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy')
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        result=subprocess.run([str(out/'run')],cwd=build,env=env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('actual PADRead:',result.stdout)
if __name__=='__main__':unittest.main()

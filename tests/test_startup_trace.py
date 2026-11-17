"""Preserve real early-constructor tracing while removing repeated opens."""
import json
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class StartupTraceTests(unittest.TestCase):
    def test_actual_constructor_runner_normal_and_sanitized(self):
        out=ROOT/'build/startup-trace-tests';out.mkdir(parents=True,exist_ok=True)
        headers=out/'orbis';headers.mkdir(exist_ok=True)
        (headers/'libkernel.h').write_text('#pragma once\n')
        (headers/'SystemService.h').write_text('#pragma once\n')
        for sanitize in (False,True):
            exe=out/('sanitized'if sanitize else'normal')
            cmd=['gcc-14','-std=c11','-Wall','-Wextra','-Werror','-O1','-g',
                 '-I'+str(out),str(ROOT/'tests/startup_trace.c'),'-o',str(exe)]
            if sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            compile_result=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(compile_result.returncode,0,compile_result.stdout+compile_result.stderr)
            result=subprocess.run([exe],check=True,capture_output=True,text=True,timeout=10)
            self.assertIn('one owned fd',result.stdout)

    def test_native_startup_object(self):
        build=ROOT/'build/game-native'
        if not(build/'compile_commands.json').is_file():self.skipTest('Existing native build required')
        recipe=next(r for r in json.loads((build/'compile_commands.json').read_text())
                    if r['file'].endswith('/platform/ps4/startup.c'))
        out=ROOT/'build/startup-trace-tests';out.mkdir(exist_ok=True)
        args=shlex.split(recipe['command']);args[args.index('-o')+1]=str(out/'native-startup.o')
        subprocess.run(args,cwd=recipe['directory'],check=True,capture_output=True,text=True)

if __name__=='__main__':unittest.main()

"""Exact FIFO copy/policy checks and the existing real decoder's SDK differential.

The optional decoder test needs the existing host renderer build and PS4 SDK.
It checks bytes and restart controls; it does not establish PS4 performance.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(os.environ.get('PRIME_PROJECT_ROOT', Path(__file__).resolve().parents[1]))
AURORA = ROOT / 'game/MetroidPrimePort/extern/aurora'
STAGED = Path(os.environ.get('GX_SMALL_COPY_SOURCE_ROOT', ROOT))
GX = STAGED / 'game/MetroidPrimePort/extern/aurora/lib/gx'
TESTS = Path(__file__).resolve().parent
OUT = Path(os.environ.get('GX_SMALL_COPY_TEST_BUILD', ROOT / 'build/gx-small-copy-tests'))
SDK = Path(os.environ.get('PRIME_SDK_BUNDLE', '/opt/orbis-sdk'))
LIBC = SDK / 'sdk/lib/libc.a'
HOST = ROOT / 'build/pc-asset-importer'
CLANG = '/usr/bin/clang++-19'
ISA = ['-msse4.2', '-mpopcnt', '-mcx16', '-mtune=btver2', '-mno-avx']


def function(text, signature):
    start = text.index(signature); end = text.index('{', start) + 1; depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


def sdk_memcpy(out):
    out.mkdir(parents=True, exist_ok=True)
    raw = out / 'sdk-memcpy.o'; renamed = out / 'prime-sdk-memcpy.o'
    raw.write_bytes(subprocess.check_output(['/usr/bin/llvm-ar-18', 'p', str(LIBC), 'memcpy.lo']))
    subprocess.run(['/usr/bin/llvm-objcopy-18', '--redefine-sym', 'memcpy=prime_sdk_memcpy', str(raw), str(renamed)], check=True)
    return renamed


def sanitizer_env():
    return dict(os.environ, LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')


class GxSmallCopyTests(unittest.TestCase):
    def run_bounds(self, sanitized):
        out = OUT / ('bounds-san' if sanitized else 'bounds'); out.mkdir(parents=True, exist_ok=True)
        obj = sdk_memcpy(out)
        flags = ['-std=c++20', '-O2', *ISA, '-I'+str(GX)]
        if sanitized:
            flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                      '-fno-sanitize-link-runtime', '-fno-omit-frame-pointer']
        args = [CLANG, *flags, str(TESTS / 'fifo_small_copy.cpp'), str(obj), '-o', str(out/'run')]
        if sanitized: args += ['-Wl,--no-as-needed', '-lasan', '-lubsan']
        subprocess.run(args, check=True)
        result = subprocess.check_output([str(out/'run')], text=True, env=sanitizer_env() if sanitized else None)
        (out/'result.json').write_text(result)
        self.assertEqual(json.loads(result)['alignment_cases'], 263168)
        self.assertEqual(json.loads(result)['guard_page_cases'], 1028)

    @unittest.skipUnless(LIBC.exists(), 'installed PS4 SDK libc required')
    def test_exact_copy_bounds_policy_and_counters(self): self.run_bounds(False)

    @unittest.skipUnless(LIBC.exists(), 'installed PS4 SDK libc required')
    def test_exact_copy_bounds_policy_and_counters_sanitized(self): self.run_bounds(True)

    def test_restart_option_defaults_and_latch(self):
        out=OUT/'option';out.mkdir(parents=True,exist_ok=True)
        source=(GX/'command_processor.cpp').read_text()
        getter=function(source,'static bool small_copy_enabled()')
        envflag=function((AURORA/'lib/internal.hpp').read_text(),'inline bool env_flag(')
        program='#include <cstdlib>\n#include <cstdio>\n#include <initializer_list>\n'+envflag+'\n'+getter+r'''
int main(){bool a=small_copy_enabled();setenv("MP_GX_SMALL_COPY",a?"0":"1",1);bool b=small_copy_enabled();
 if(a!=b)return 2;std::printf("%d\n",int(a));}
'''
        cpp=out/'option.cpp';cpp.write_text(program)
        for native in (False,True):
            exe=out/('native-default'if native else'host-default')
            subprocess.run([CLANG,'-std=c++20','-O2',*(['-D__ORBIS__']if native else[]),str(cpp),'-o',str(exe)],check=True)
            for setting,expect in [(None,native),('0',False),('off',False),('FALSE',False),('',False),('1',True),('yes',True)]:
                env=dict(os.environ)
                if setting is None:env.pop('MP_GX_SMALL_COPY',None)
                else:env['MP_GX_SMALL_COPY']=setting
                self.assertEqual(subprocess.check_output([str(exe)],env=env,text=True).strip(),str(int(expect)))

    @unittest.skipUnless(LIBC.exists() and (HOST/'compile_commands.json').exists(),
                         'existing host renderer build and installed SDK required')
    def test_actual_sdk_decoder_canonical_and_disabled_default(self):
        sanitized=os.environ.get('GX_SMALL_COPY_SANITIZE')=='1'
        out=OUT/('decoder-san'if sanitized else'decoder');out.mkdir(parents=True,exist_ok=True)
        obj=sdk_memcpy(out)
        source=(ROOT/'tests/gx_primitive_batch.cpp').read_text()
        source=source.replace('#include "gx_draw_preparation.cpp"','#include '+json.dumps(str(ROOT/'tests/gx_draw_preparation.cpp')))
        source=source.replace('iteration<14','iteration<2').replace('iteration==13','iteration==1')
        source=source.replace('if(iteration>=4&&iteration<13)samples[iteration-4]=elapsed;','samples[iteration]=elapsed;')
        cpp=out/'fixture.cpp';cpp.write_text(source)
        spec=importlib.util.spec_from_file_location('small_copy_actual_fixture',ROOT/'tests/test_gx_draw_preparation.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        realrun=subprocess.run
        def selected_run(command,*args,**kwargs):
            command=list(command)
            if '-c' in command:
                if not sanitized:command += ['-O2',*ISA]
                result=realrun(command,*args,**kwargs)
                target=command[command.index('-o')+1]
                if Path(target).name in ('candidate_processor.o','actual_recording.o'):
                    realrun(['/usr/bin/llvm-objcopy-18','--redefine-sym','memcpy=prime_sdk_memcpy',target],check=True)
                return result
            if '-o'in command and any(Path(str(x)).name=='candidate_processor.o'for x in command):
                command += [str(obj)]
            return realrun(command,*args,**kwargs)
        initial=out/'mode0.bin';initial.unlink(missing_ok=True)
        env={'GX_DRAW_BUILD_DIR':str(out),'GX_DRAW_FIXTURE_SOURCE':str(cpp),
             'GX_DRAW_PROCESSOR_SOURCE':str(GX/'command_processor.cpp'),'GX_DRAW_VERIFIED_ARRAYS':'2700',
             'MP_GX_DRAW_PREP_PROFILE':'1','MP_GX_PRIMITIVE_BATCH':'1','MP_GX_TRIANGLE_BATCH':'1',
             'MP_GX_SMALL_COPY':'0','GX_CANONICAL':str(initial),'GX_INDEXED_KIND':'4',
             'GX_DRAW_SANITIZE':'1'if sanitized else'0'}
        with patch.dict(os.environ,env),patch.object(fixture.subprocess,'run',selected_run):
            fixture.GxDrawPreparationTests('test_actual_draw_helpers_and_fetched_array_bytes').test_actual_draw_helpers_and_fetched_array_bytes()
        metadata=[]
        for setting in ('1',None):
            path=out/('mode1.bin'if setting else'default.bin');path.unlink(missing_ok=True)
            current=sanitizer_env()if sanitized else dict(os.environ)
            current.update({k:v for k,v in env.items()if k not in ('GX_CANONICAL','MP_GX_SMALL_COPY')})
            current['GX_CANONICAL']=str(path)
            if setting is None:current.pop('MP_GX_SMALL_COPY',None)
            else:current['MP_GX_SMALL_COPY']=setting
            run=subprocess.run([str(out/'run')],env=current,capture_output=True,text=True,timeout=60)
            (out/('mode1.log'if setting else'default.log')).write_text(run.stdout+run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertEqual(run.stdout.count('ADVERSARIAL case='),29)
            digest=hashlib.sha256();total=0
            with initial.open('rb')as a,path.open('rb')as b:
                while True:
                    left=a.read(1024*1024);right=b.read(1024*1024)
                    self.assertEqual(left,right,'canonical commands/staging bytes differ')
                    if not left:break
                    total+=len(left);digest.update(left)
            metadata.append({'setting':setting,'bytes_equal':total,'sha256':digest.hexdigest()})
            path.unlink()
        initial.unlink()
        (out/'canonical-results.json').write_text(json.dumps(metadata,indent=2)+'\n')


if __name__=='__main__': unittest.main()

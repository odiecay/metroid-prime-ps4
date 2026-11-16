"""Run actual CPU GX draw preparation; host wrappers replace only GPU objects."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/pc-asset-importer'
WRAPS=['_ZN6aurora2gx24populate_pipeline_configERNS0_14PipelineConfigE11GXPrimitive8GXVtxFmt',
'_ZN6aurora2gx17build_shader_infoERKNS0_12ShaderConfigE','_ZN6aurora2gx13build_uniformERKNS0_10ShaderInfoE',
'_ZN6aurora3gfx12push_storageEPKhm','_ZN6aurora3gfx10push_vertsEPKhmm','_ZN6aurora3gfx12push_indicesEPKhmm',
'_ZN6aurora2gx24resolve_sampled_texturesERKNS0_10ShaderInfoE',
'_ZN6aurora3gfx12pipeline_refERKNS_2gx14PipelineConfigEb','_ZN6aurora3gfx12pipeline_refINS_2gx14PipelineConfigEEEmRKT_',
'_ZN6aurora3gfx14touch_pipelineEm','_ZN6aurora2gx17build_bind_groupsERKNS0_10ShaderInfoE',
'_ZN6aurora3gfx21scene_hdr_pass_activeEv']

class GxDrawPreparationTests(unittest.TestCase):
    @unittest.skipUnless((BUILD/'compile_commands.json').exists(),'existing host renderer dependencies required')
    def test_actual_draw_helpers_and_fetched_array_bytes(self):
        out=Path(os.environ.get('GX_DRAW_BUILD_DIR', str(ROOT/'build/gx-draw-preparation')));out.mkdir(exist_ok=True)
        commands=json.loads((BUILD/'compile_commands.json').read_text())
        recipe=next(c for c in commands if c['file'].endswith('/gx_uniform_limit_test.cpp'))
        args=shlex.split(recipe['command']);obj=out/'test.o'
        args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=os.environ.get('GX_DRAW_FIXTURE_SOURCE',str(ROOT/'tests/gx_draw_preparation.cpp'))
        args+=['-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/tests'),'-I'+str(out)]
        candidate = os.environ.get('GX_DRAW_PROCESSOR_SOURCE', str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/command_processor.cpp'))
        processor = Path(candidate) if candidate else ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx/command_processor.cpp'
        body = processor.read_text()
        helper = 'static void populate_cached_retail_raster_config('
        if helper in body:
            start=body.index(helper);pos=body.index('{',start);depth=1;end=pos+1
            while depth:
                depth+=(body[end]=='{')-(body[end]=='}');end+=1
            (out/'actual_raster_config.inc').write_text(body[start:end])
            args+=['-DGX_RASTER_CONFIG_EQUIVALENCE']
        sanitized=os.environ.get('GX_DRAW_SANITIZE')=='1'
        if sanitized:
            headers=out/'headers';headers.mkdir(exist_ok=True)
            shim=headers/'sanitizer'
            if not shim.exists():
                shim.symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
            # Match the unmodified Abseil archive's container ABI; retain full
            # ASan/UBSan instrumentation in this fixture and candidate decoder.
            args+=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        subprocess.run(args,cwd=recipe['directory'],check=True,capture_output=True,text=True)
        extra=[]
        if candidate:
            cp=next(c for c in commands if c['file'].endswith('/gx/command_processor.cpp'))
            ca=shlex.split(cp['command']);co=out/'candidate_processor.o'
            ca[ca.index('-o')+1]=str(co);ca[ca.index('-c')+1]=str(processor)
            ca+=['-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx')]
            if sanitized:
                ca+=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
            subprocess.run(ca,cwd=cp['directory'],check=True,capture_output=True,text=True)
            extra=[str(co)]
        shader_source=os.environ.get('GX_DRAW_SHADER_INFO_SOURCE')
        if shader_source:
            recipe=next(c for c in commands if c['file'].endswith('/gx/shader_info.cpp'))
            args=shlex.split(recipe['command']);shader_obj=out/'candidate_shader_info.o'
            args[args.index('-o')+1]=str(shader_obj);args[args.index('-c')+1]=shader_source
            args+=['-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gx')]
            if sanitized:
                args+=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
            subprocess.run(args,cwd=recipe['directory'],check=True,capture_output=True,text=True)
            extra.append(str(shader_obj))
        # Match the freshly compiled decoder's recorder interface instead of
        # silently linking a stale archive after a recording API change.
        recipe=next(c for c in commands if c['file'].endswith('/gfx/recording.cpp'))
        args=shlex.split(recipe['command']);recording_obj=out/'actual_recording.o'
        args[args.index('-o')+1]=str(recording_obj)
        args[args.index('-c')+1]=os.environ.get('GX_DRAW_RECORDING_SOURCE',str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gfx/recording.cpp'))
        args+=['-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/lib/gfx')]
        if sanitized:
            args+=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
        subprocess.run(args,cwd=recipe['directory'],check=True,capture_output=True,text=True)
        extra.append(str(recording_obj))
        line=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
        args=shlex.split(line)[2:-2]
        args=[a for a in args if not ('gfx_recording_tests.dir/' in a and a.endswith('.o'))]
        dep=next((i for i,a in enumerate(args)if a.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        exe=out/'run';args[args.index('-o')+1]=str(exe);args[args.index('-o'):args.index('-o')] = [str(obj)]+extra
        args += ['-Wl,--wrap='+s for s in WRAPS]
        if sanitized:
            args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(args,cwd=BUILD,check=True,capture_output=True,text=True)
        env=dict(os.environ)
        if sanitized:
            env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        expected_arrays=os.environ.get('GX_DRAW_VERIFIED_ARRAYS','3600')
        self.assertEqual(result.stdout.count('verified_arrays='+expected_arrays),6)
        print(result.stdout,end='')

if __name__=='__main__':unittest.main()

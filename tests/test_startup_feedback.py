"""Nonrender PS4 startup timing and unconditional native shader-UI removal."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
GAME=ROOT/'game/MetroidPrimePort'
OUT=Path(os.environ.get('PRIME_STARTUP_FEEDBACK_BUILD',ROOT/'build/startup-feedback-tests'))


def function(source,signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]


class StartupFeedbackTests(unittest.TestCase):
    def run_actual(self,sanitized):
        out=OUT/('logging-san'if sanitized else'logging-normal');out.mkdir(parents=True,exist_ok=True)
        command=['clang++-19','-std=c++20','-O2','-UNDEBUG','-D__ORBIS__',
                 '-I'+str(GAME/'platform/include'),'-I'+str(ROOT/'external/SDL-release-3.4.10/include'),
                 str(GAME/'platform/port_startup.cpp'),str(ROOT/'tests/startup_feedback.cpp'),'-o',str(out/'run')]
        if sanitized:command+=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer',
                              '-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        result=subprocess.run(command,capture_output=True,text=True)
        (out/'build.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run([str(out/'run')],capture_output=True,text=True,env=env,timeout=10)
        (out/'run.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('no_renderer_linked=1',result.stdout)
        self.assertIn('monotonic_elapsed=1',result.stdout)

    def test_actual_nonrender_logger(self):self.run_actual(False)
    def test_actual_nonrender_logger_asan_ubsan(self):self.run_actual(True)

    def test_native_shader_ui_disabled_and_desktop_unchanged(self):
        out=OUT/'no-ui';out.mkdir(parents=True,exist_ok=True)
        source=(GAME/'platform/debug_ui/overlays.cpp').read_text()
        toast=function(source,'void DrawShaderCompilationToast()')
        body=toast.split('#if !defined(__ORBIS__)',1)[1].rsplit('#endif',1)[0]
        self.assertEqual(hashlib.sha256(body.encode()).hexdigest(),
                         'eca44463f700dc227055778c28db24db56ae47740a3abaae663680f3fd0934c2')
        # No ImGui/readiness/settings declarations: the exact production native
        # function must compile and run even with the old setting enabled.
        cpp=out/'toast.cpp';cpp.write_text('bool sShowShaderCompilation=true;\n'+toast+'\nint main(){DrawShaderCompilationToast();}\n')
        subprocess.run(['clang++-19','-std=c++20','-D__ORBIS__','-Wall','-Werror',str(cpp),'-o',str(out/'toast')],check=True)
        subprocess.run([str(out/'toast')],check=True,timeout=5)
        startup=(GAME/'platform/port_startup.cpp').read_text()
        for removed in ['ImGui','aurora_begin_frame','aurora_end_frame','aurora_get_pipeline','SDL_PumpEvents','SDL_PeepEvents']:
            self.assertNotIn(removed,startup)
        main=(GAME/'platform/main.cpp').read_text()
        for removed in ['PortStartup::Present','PortStartup::Draw','PortStartup::GraphicsReady','PortStartup::ExitRequested']:
            self.assertNotIn(removed,main)
        self.assertIn('if (startupExit || prime_ps4_quit_requested())',main)
        engine=(GAME/'src/MetroidPrime/main.cpp').read_text()
        self.assertIn('(firstFrameNs - loopBeganNs) / 1000000ull',engine)

    def test_native_objects_and_non_native_exclusion(self):
        native=ROOT/'build/game-native/compile_commands.json'
        if not native.is_file():self.skipTest('Existing native Orbis build recipe required')
        out=OUT/'native-logging';out.mkdir(parents=True,exist_ok=True)
        recipes=json.loads(native.read_text());commands=[]
        for name in ['platform/port_startup.cpp','platform/main.cpp','platform/debug_ui/overlays.cpp',
                     'platform/debug_ui/video.cpp','src/MetroidPrime/main.cpp']:
            find='/platform/port_imgui_fonts.cpp'if name.endswith('port_startup.cpp')else'/'+name
            record=next(r for r in recipes if r['file'].endswith(find))
            args=shlex.split(record['command']);obj=out/(name.replace('/','_')+'.o')
            args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(GAME/name)
            result=subprocess.run(args,cwd=record['directory'],capture_output=True,text=True)
            (out/(obj.name+'.log')).write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);commands.append(args)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        undefined=subprocess.check_output(['llvm-nm-19','--undefined-only',str(out/'platform_port_startup.cpp.o')],text=True)
        for removed in ['ImGui','aurora_','SDL_PumpEvents','SDL_PeepEvents']:
            self.assertNotIn(removed,undefined)
        obj=out/'non-native.o'
        subprocess.run(['clang++-19','-std=c++20','-I'+str(GAME/'platform/include'),'-c',str(GAME/'platform/port_startup.cpp'),'-o',str(obj)],check=True)
        symbols=subprocess.check_output(['llvm-nm-19','--defined-only',str(obj)],text=True)
        self.assertNotIn('PortStartup',symbols)


if __name__=='__main__':unittest.main()

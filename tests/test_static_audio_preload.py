"""Compile the production static player with real rstl and the actual DVD worker."""
from pathlib import Path
import os
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
GAME=ROOT/'game/MetroidPrimePort'
HOST=ROOT/'build/pc-asset-importer'

class StaticAudioPreloadTests(unittest.TestCase):
    def test_actual_player_and_worker(self):
        source=Path(os.environ.get('STATIC_AUDIO_CANDIDATE',str(GAME)))
        out=ROOT/'build/static-audio-preload/proof';out.mkdir(parents=True,exist_ok=True)
        includes=[source/'include',GAME/'platform/include',GAME/'include',GAME/'extern/aurora/include',
                  GAME/'extern/musyx/include',GAME/'extern/aurora/lib',GAME/'extern/aurora/lib/dolphin/dvd',HOST/'_deps/fmt-src/include',
                  ROOT/'external/SDL-release-3.4.10/include',ROOT/'external/nod-host-package/include',
                  HOST/'_deps/tracy-src/public']
        files=[ROOT/'tests/static_audio_preload.cpp', source/'src/Kyoto/DolphinCDvdFile.cpp',
               GAME/'src/Kyoto/CDvdRequest.cpp', GAME/'src/Kyoto/Audio/g721.cpp',
               GAME/'src/Kyoto/rstl/rstl_strings.cpp',GAME/'src/Kyoto/Alloc/CCallStackDolphin.cpp',
               source/'extern/aurora/lib/dolphin/dvd/dvd.cpp',GAME/'extern/aurora/lib/logging.cpp',
               GAME/'extern/aurora/tests/dvd_test_stubs.cpp',
               GAME/'src/Kyoto/Streams/CInputStream.cpp',GAME/'src/Kyoto/Streams/COutputStream.cpp']
        libraries=[HOST/'importer/extern/aurora/libaurora_dvd.a',ROOT/'external/nod-host-package/lib/libnod.a', HOST/'_deps/fmt-build/libfmt.a',
                   HOST/'sdl/libSDL3.a']
        common=['clang++-19','-std=c++20','-O1','-g','-pthread','-DTARGET_PC','-DAURORA',
                '-DMUSY_TARGET=MUSY_TARGET_PC','-DTRACY_ON_DEMAND','-DPRIME_TRACE_TEST=1','-fsigned-char','-fwrapv',
                '-ffunction-sections','-fdata-sections','-Wno-multichar',
                '-include',str(GAME/'platform/compat.h'),
                '-DSTATIC_AUDIO_SOURCE="'+str(source/'src/Kyoto/Audio/CStaticAudioPlayer.cpp')+'"']
        common += ['-I'+str(p) for p in includes]
        for sanitized in (False,True):
            flags=(['g++-14']+common[1:] if sanitized else common)+(['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitized else [])
            objects=[]
            for i,file in enumerate(files):
                obj=out/f'{"san" if sanitized else "normal"}-{i}.o'
                subprocess.run(flags+['-c',str(file),'-o',str(obj)],check=True)
                objects.append(obj)
            binary=out/('sanitized' if sanitized else 'normal')
            subprocess.run(flags+list(map(str,objects+libraries))+['-Wl,--gc-sections','-ldl','-lm','-o',str(binary)],check=True)
            env=os.environ|{'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
            result=subprocess.run([binary,ROOT/'private/prepared/MetroidPrime.iso'],env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PCM_loop_restart=ok',result.stdout)
            print(result.stdout.strip())

if __name__=='__main__':unittest.main()

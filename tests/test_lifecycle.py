from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class LifecycleTests(unittest.TestCase):
    def test_native_request_signal_and_refusal_without_libc_exit(self):
        build=ROOT/'build/lifecycle-host'
        headers=build/'orbis';headers.mkdir(parents=True,exist_ok=True)
        (headers/'libkernel.h').write_text('extern "C" unsigned long sceKernelGetProcessTime();\nextern "C" int sceKernelDebugOutText(int,const char*);\nextern "C" int sceKernelUsleep(unsigned);\n')
        (headers/'SystemService.h').write_text('extern "C" int sceSystemServiceLoadExec(const char*,const char*[]);\n')
        (headers/'Sysmodule.h').write_text('#define ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE 1\nextern "C" int sceSysmoduleLoadModuleInternal(unsigned);\n')
        log=build/'shutdown.log'
        subprocess.run(['clang++-19','-std=c++20','-O2','-pthread','-DPRIME_LIFECYCLE_TEST',
                        '-DPRIME_SHUTDOWN_LOG="'+str(log)+'"','-I'+str(build),
                        '-I'+str(ROOT/'platform/ps4'),str(ROOT/'platform/ps4/lifecycle.cpp'),
                        str(ROOT/'tests/lifecycle.cpp'),'-o',str(build/'run')],check=True)
        for mode in ['accepted','refused']:
            subprocess.run([build/'run',mode,log],check=True,timeout=5)
if __name__=='__main__':unittest.main()

from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]

class CloseDiagnosticsTests(unittest.TestCase):
    def test_native_context_anchors_and_fault_address(self):
        out=ROOT/'build/close-diagnostics';out.mkdir(exist_ok=True)
        binary=out/'context'
        subprocess.run(['g++-14','-std=c++20','-O2','-fsanitize=address,undefined',
                        '-I'+str(ROOT/'platform/ps4'),str(ROOT/'tests/native_signal_context.cpp'),
                        '-o',str(binary)],check=True)
        subprocess.run([binary],check=True,timeout=10)

    def test_actual_close_handler_blocked_monitor_and_errno(self):
        out=ROOT/'build/close-diagnostics';headers=out/'orbis';headers.mkdir(parents=True,exist_ok=True)
        (headers/'libkernel.h').write_text('extern "C" unsigned long sceKernelGetProcessTime();\nextern "C" int sceKernelDebugOutText(int,const char*);\nextern "C" int sceKernelUsleep(unsigned);\n')
        (headers/'SystemService.h').write_text('extern "C" int sceSystemServiceLoadExec(const char*,const char*[]);\n')
        (headers/'Sysmodule.h').write_text('#define ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE 1\nextern "C" int sceSysmoduleLoadModuleInternal(unsigned);\n')
        binary=out/'signals';log=out/'shutdown.log'
        subprocess.run(['g++-14','-std=c++20','-O1','-pthread','-fsanitize=address,undefined',
                        '-DPRIME_SHUTDOWN_LOG="'+str(log)+'"','-I'+str(out),
                        '-I'+str(ROOT/'platform/ps4'),str(ROOT/'tests/lifecycle_signals.cpp'),
                        '-o',str(binary)],check=True)
        subprocess.run([binary],check=True,timeout=10)

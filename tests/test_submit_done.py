from pathlib import Path
import re
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class SubmitDoneTests(unittest.TestCase):
    def test_native_frame_boundary_gate_and_entry_order(self):
        wsi=(ROOT/'external/mesa-ps4/src/vulkan/wsi/wsi_orbis.c').read_text()
        entry=(ROOT/'platform/ps4/entry.cpp').read_text()
        drm=(ROOT/'external/mesa-ps4/src/amd/common/ac_orbis_drm.c').read_text()
        present=wsi[wsi.index('wsi_orbis_scanout_present('):wsi.index('wsi_orbis_scanout_destroy(')]
        # This is the existing native frame boundary, not every Vulkan submit.
        self.assertLess(present.index('ac_orbis_wait_gpu_seq('),present.index('sceGnmSubmitDone()'))
        self.assertLess(present.index('sceGnmSubmitDone()'),present.index('sceVideoOutSubmitFlip('))
        self.assertEqual(present.count('sceGnmSubmitDone()'),1)
        actual_submit=drm[drm.index('const int32_t err = sceGnmSubmitCommandBuffers(n_dcb'):]
        self.assertNotRegex(actual_submit,r'(?m)^\s*(?:const\s+\w+\s+\w+\s*=\s*)?sceGnmSubmitDone\s*\(')
        self.assertIn('setenv("ORBIS_SUBMIT_DONE","1",1);',entry)
        self.assertLess(entry.index('setenv("ORBIS_SUBMIT_DONE"'),entry.index('aurora_main(argc,argv)'))

    def test_actual_existing_wsi_gate_notifies_once_per_present(self):
        wsi=(ROOT/'external/mesa-ps4/src/vulkan/wsi/wsi_orbis.c').read_text()
        start=wsi.index('   {\n      static int enabled = -1;',wsi.index('wsi_orbis_scanout_present('))
        end=wsi.index('\n\n   /*',start)
        block=wsi[start:end]
        self.assertIn('getenv("ORBIS_SUBMIT_DONE")',block)
        out=ROOT/'build/close-diagnostics';out.mkdir(exist_ok=True)
        source=out/'submit_done.cpp'
        source.write_text('''#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
static unsigned doneCalls,allowedCalls;
static uint64_t clockNs;
static int sceGnmAreSubmitsAllowed(){++allowedCalls;return 1;}
static int sceGnmSubmitDone(){++doneCalls;return 0;}
static uint64_t os_time_get_nano(){return ++clockNs;}
#define mesa_logi(...) ((void)0)
static void ExistingFrameBoundary() {
'''+block+'''
}
int main(int argc,char**argv){
  assert(argc==2);
  const bool enabled=std::strcmp(argv[1],"enabled")==0;
  if(enabled)setenv("ORBIS_SUBMIT_DONE","1",1);else unsetenv("ORBIS_SUBMIT_DONE");
  for(unsigned frame=0;frame<512;++frame)ExistingFrameBoundary();
  assert(doneCalls==(enabled?512u:0u));
  assert(allowedCalls==doneCalls);
}
''')
        binary=out/'submit_done'
        subprocess.run(['g++-14','-std=c++20','-O2','-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
        for mode in ['disabled','enabled']:
            subprocess.run([binary,mode],check=True,timeout=10)

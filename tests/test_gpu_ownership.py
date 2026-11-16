"""Fault-inject native probe ownership; host tests never count as GPU execution."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class GpuOwnershipTests(unittest.TestCase):
    def test_completion_errors_and_timeout_never_free_live_resources(self):
        bundle=Path(os.environ.get('PRIME_SDK_BUNDLE','/opt/orbis-sdk'))
        if not (bundle/'mesa/include/vulkan/vulkan.h').exists():
            self.skipTest('native SDK Vulkan headers are not available')
        program=ROOT/'build/gpu-ownership-faults-host';program.parent.mkdir(exist_ok=True)
        subprocess.run(['clang-19','-std=c11','-O1','-g',
            '-I'+str(bundle/'mesa/include'),'-I'+str(bundle/'orbis-compat/vkloader'),
            str(ROOT/'tests/gpu_ownership_faults.c'),'-o',str(program)],check=True)
        result=subprocess.run([program],check=True,text=True,stdout=subprocess.PIPE)
        self.assertIn('5 fault/success scenarios passed; no GPU executed',result.stdout)

if __name__=='__main__':unittest.main()

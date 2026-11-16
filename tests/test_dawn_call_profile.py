"""Exercise actual Vulkan call sampling and existing SubmitImpl lifetime."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
LEGACY_PROFILE_BLOB='fa19fa115151ea54587288048d9c6f116fabc52c'
LEGACY_PROFILE_SHA256='c8a435da281b949121dbd7eba945241892d134a79fa28f5b53e876d96b7ae1d0'


def function(source,signature):
    start=source.index(signature);brace=source.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]


class DawnCallProfileTests(unittest.TestCase):
    def build(self,name,sources,definitions=()):
        out=ROOT/'build/dawn-call-profile-tests';out.mkdir(parents=True,exist_ok=True)
        program=out/name
        args=['g++-14','-std=c++20','-O1','-g','-pthread','-fsanitize=address,undefined',
              '-fno-omit-frame-pointer','-I'+str(ROOT/'platform/ps4/compat'),
              '-I'+str(ROOT/'platform/ps4'),'-I'+str(out),*definitions,*map(str,sources),'-o',str(program)]
        result=subprocess.run(args,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return program

    def test_bounded_sampling_arguments_rotation_tls_and_optional_observer(self):
        for absent in (False,True):
            program=self.build('without-observer' if absent else 'with-observer',
                [ROOT/'tests/dawn_call_profile.cpp',ROOT/'tests/dawn_call_profile_other.cpp'],
                ['-DNO_OBSERVER'] if absent else [])
            result=subprocess.run([program],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('zero_clock_reads=1' if absent else 'capped_samples=256',result.stdout)
            print(result.stdout.strip())

    def test_actual_submit_scope_covers_commands_and_early_errors(self):
        source=(ROOT/'external/dawn/src/dawn/native/vulkan/QueueVk.cpp').read_text()
        body=function(source,'MaybeError Queue::SubmitImpl(')
        self.assertLess(body.index('profileCalls'),body.index('for (CommandBufferBase*'))
        self.assertLess(body.index('for (CommandBufferBase*'),body.index('SubmitPendingCommandsImpl'))
        out=ROOT/'build/dawn-call-profile-tests';out.mkdir(parents=True,exist_ok=True)
        (out/'actual_dawn_submit.inc').write_text(body)
        program=self.build('submit',[ROOT/'tests/dawn_call_submit.cpp'])
        result=subprocess.run([program],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        source=(ROOT/'external/dawn/src/dawn/native/vulkan/CommandBufferVk.cpp').read_text()
        for name in ['CmdDraw','CmdDrawIndexed','CmdDrawIndirect','CmdDrawIndexedIndirect',
                     'CmdDrawIndirectCountKHR','CmdDrawIndexedIndirectCountKHR',
                     'CmdBindDescriptorSets','CmdBindPipeline','CmdPipelineBarrier']:
            self.assertIn(', vk.'+name+',' if name!='CmdPipelineBarrier' else ', device->fn.'+name+',',source)
        self.assertIn('PRIME_DAWN_VK_CALL(Barrier, device->fn.CmdPipelineBarrier,',
            (ROOT/'external/dawn/src/dawn/native/vulkan/CommandRecordingContextVk.cpp').read_text())

    def test_versioned_configuration_preserves_old_release_rollback(self):
        fixture=ROOT/'tests/dawn_call_profile_rollback.cpp'
        program=self.build('new-configuration',[fixture,ROOT/'platform/ps4/candidate_profile.cpp'])
        with tempfile.TemporaryDirectory()as folder:
            result=subprocess.run([program,folder],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        # Native platform code is project-authored, so the exact43 source blob
        # comes from retained project Git history rather than upstream Dawn.
        old=subprocess.run(['git','-C',str(ROOT),'cat-file','blob',LEGACY_PROFILE_BLOB],capture_output=True)
        if old.returncode:
            self.skipTest('Exact43 platform blob unavailable in this source snapshot; new-config/save checks passed')
        self.assertEqual(hashlib.sha256(old.stdout).hexdigest(),LEGACY_PROFILE_SHA256)
        out=ROOT/'build/dawn-call-profile-tests';out.mkdir(parents=True,exist_ok=True)
        legacy=out/'legacy_candidate_profile.cpp';legacy.write_bytes(old.stdout)
        self.assertNotIn(b'benchmark-v2.cfg',old.stdout)
        program=self.build('old43-configuration',[fixture,legacy],['-DBASELINE_43'])
        with tempfile.TemporaryDirectory()as folder:
            result=subprocess.run([program,folder],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()

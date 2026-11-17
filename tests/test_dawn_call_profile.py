"""Exercise actual Vulkan call sampling and existing SubmitImpl lifetime."""
from pathlib import Path
import hashlib
import json
import os
import shlex
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
            self.assertIn('zero_clock_reads=1' if absent else 'capped_samples=384',result.stdout)
            print(result.stdout.strip())

    def test_frontend_sampling_value_returns_and_independent_tls(self):
        program=self.build('frontend-profile',[ROOT/'tests/dawn_frontend_profile.cpp'])
        result=subprocess.run([program],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('driver_ids_preserved=1 independent_nested_tls=1',result.stdout)
        print(result.stdout.strip())

    @unittest.skipUnless((ROOT/'build/pc-asset-importer/compile_commands.json').exists(),
                         'Existing real Dawn Null dependencies required')
    def test_frontend_actual_dawn_commands_and_owned_references(self):
        host=ROOT/'build/pc-asset-importer';out=ROOT/'build/dawn-call-profile-tests';out.mkdir(exist_ok=True)
        recipes=json.loads((host/'compile_commands.json').read_text())
        recipe=next(r for r in recipes if r['file'].endswith('/PassResourceUsageTracker.cpp'))
        for sanitized in (False,True):
            obj=out/('frontend-null-san.o' if sanitized else 'frontend-null.o')
            args=shlex.split(recipe['command']);args[args.index('-o')+1]=str(obj)
            args[args.index('-c')+1]=str(ROOT/'tests/dawn_frontend_commands.cpp')
            args+=['-UNDEBUG','-DPRIME_DAWN_FRONTEND_TEST=1','-I'+str(ROOT/'platform/ps4/compat')]
            if sanitized:
                headers=out/'headers';headers.mkdir(exist_ok=True)
                if not(headers/'sanitizer').exists():
                    (headers/'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer',target_is_directory=True)
                args+=['-O1','-g','-DNDEBUG_SANITIZER','-isystem',str(headers),'-fsanitize=address,undefined','-fno-sanitize-link-runtime','-fno-omit-frame-pointer']
            result=subprocess.run(args,cwd=recipe['directory'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            command=subprocess.check_output(['ninja','-C',str(host),'-t','commands','gfx_recording_tests'],text=True).splitlines()[-1]
            tokens=shlex.split(command);link=tokens[2:tokens.index('&&',2)]
            link=[a for a in link if not('gfx_recording_tests.dir/'in a and a.endswith('.o'))]
            dependency=next((i for i,a in enumerate(link)if a.startswith('--dependency-file=')),None)
            if dependency is not None:del link[dependency-1:dependency+1]
            exe=out/('frontend-null-san'if sanitized else'frontend-null')
            link[link.index('-o')+1]=str(exe);link.insert(link.index('-o'),str(obj));env=dict(os.environ)
            if sanitized:
                link+=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
                env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            result=subprocess.run(link,cwd=host,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([exe],env=env,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('complete_command_stream_equal=1',result.stdout)
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
                     'CmdBindDescriptorSets','CmdBindPipeline','CmdPipelineBarrier','CmdPushConstants']:
            self.assertIn(', vk.'+name+',' if name!='CmdPipelineBarrier' else ', device->fn.'+name+',',source)
        self.assertIn('PRIME_DAWN_VK_CALL(Barrier, device->fn.CmdPipelineBarrier,',
            (ROOT/'external/dawn/src/dawn/native/vulkan/CommandRecordingContextVk.cpp').read_text())
        for name in ('CmdSetViewport','CmdSetScissor','CmdSetBlendConstants','CmdSetStencilReference',
                     'CmdSetDepthBounds','CmdSetLineWidth'):
            self.assertIn('PRIME_DAWN_VK_CALL(DynamicState, device->fn.'+name+',',source)
        pipeline=(ROOT/'external/dawn/src/dawn/native/vulkan/RenderPipelineVk.cpp').read_text()
        self.assertIn('PRIME_DAWN_VK_CALL(DynamicState, device->fn.function,',pipeline)
        self.assertEqual(pipeline.count('PRIME_DAWN_VK_CALL(DynamicState, device->fn.CmdSetStencilOpEXT,'),2)

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

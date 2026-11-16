"""Test the patched LLVM guard against the native SDK's gettid=getpid alias."""
from pathlib import Path
import resource
import signal
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
class StaticGuardTests(unittest.TestCase):
    def test_contention_is_not_recursion(self):
        root=ROOT/'build/static-guard-regression';root.mkdir(exist_ok=True)
        config=(ROOT/'build/modern-runtime/include/c++/v1/__config_site').read_text()
        (root/'__config_site').write_text(config.replace('#define _LIBCPP_HAS_MUSL_LIBC',
                                                        '/* host test uses glibc */'))
        original=root/'original';original.mkdir(exist_ok=True)
        (original/'cxa_guard_impl.h').write_bytes(subprocess.check_output([
            'git','-C',str(ROOT/'external/llvm-project'),'show',
            'HEAD:libcxxabi/src/cxa_guard_impl.h']))
        def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        for fixed in (False,True):
            with self.subTest(fixed=fixed):
                program=root/('fixed' if fixed else 'original-guard')
                command=['clang++-19','-std=c++20','-O2','-g','-pthread',
                         '-D_LIBCPP_HAS_THREAD_API_PTHREAD','-nostdinc++',
                         '-isystem',str(root),'-isystem',str(ROOT/'build/modern-runtime/include/c++/v1')]
                if not fixed:command+=['-I'+str(original)]
                command+=['-I'+str(ROOT/'external/llvm-project/libcxxabi/src'),
                          '-I'+str(ROOT/'external/llvm-project/libcxx/src'),
                          str(ROOT/'tests/static_guard.cpp'),'-o',str(program)]
                subprocess.run(command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                result=subprocess.run([program],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                      timeout=20,preexec_fn=no_core)
                if fixed:
                    self.assertEqual(result.returncode,0,result.stdout.decode(errors='replace'))
                    self.assertIn(b'40 eight-thread races',result.stdout)
                    self.assertIn(b'true recursion detection passed',result.stdout)
                else:
                    self.assertEqual(result.returncode,-signal.SIGABRT,
                                     'Original guard must reproduce the false recursion abort')
if __name__=='__main__':unittest.main()

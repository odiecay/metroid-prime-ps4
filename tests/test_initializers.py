"""Cross-link actual priority constructors and inspect the emitted CRT array."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_elf import parse,verify_initializers

class InitializerLayoutTests(unittest.TestCase):
    def test_sdk_omission_is_rejected_and_corrected_array_runs_priority_first(self):
        sdk=Path(os.environ.get('PRIME_SDK_BUNDLE',
            '/opt/orbis-sdk'))
        old=sdk/'orbis-compat/cmake/orbis-tls.ld'
        if not old.exists():self.skipTest('Native SDK not present')
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory);source=temp/'fixture.c'
            source.write_text('''
typedef void (*init)(void);
extern init __init_array_start[],__init_array_end[];
volatile int value;
__attribute__((constructor(100))) void early(void) { value=1; }
__attribute__((constructor)) void ordinary(void) { value=2; }
void _start(void) { for(init *p=__init_array_start;p<__init_array_end;++p) (*p)(); }
''')
            for script,good in ((old,False),(ROOT/'platform/ps4/orbis-native.ld',True)):
                output=temp/('fixed.elf' if good else 'sdk.elf')
                result=subprocess.run(['/usr/lib/llvm-19/bin/clang','--target=x86_64-pc-freebsd12-elf',
                    '-nostdlib','-fPIC','-fuse-ld=lld','-pie','-Wl,--no-rosegment','-Wl,-e,_start',
                    '-Wl,--script='+str(script),source,'-o',output],
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                parsed=parse(output)
                if not good:
                    with self.assertRaisesRegex(ValueError,'Priority constructor'):
                        verify_initializers(parsed)
                    continue
                verify_initializers(parsed)
                sections=dict(parsed['sections']);array=sections['.init_array']
                self.assertEqual(array[5],16)
                symbols={}
                for line in subprocess.check_output(['/usr/lib/llvm-19/bin/llvm-nm','-P',output],text=True).splitlines():
                    items=line.split()
                    if items[0] in ('early','ordinary'):symbols[items[0]]=int(items[2],16)
                rel=sections['.rela.dyn'];data=output.read_bytes()
                pointers={offset:addend for offset,info,addend in struct.iter_unpack('<QQq',
                    data[rel[4]:rel[4]+rel[5]]) if info&0xffffffff==8}
                self.assertEqual([pointers[array[3]+i*8] for i in range(2)],
                                 [symbols['early'],symbols['ordinary']])

if __name__=='__main__':unittest.main()

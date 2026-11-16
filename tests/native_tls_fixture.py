"""Small real Orbis link/conversion fixture; never installs or runs on PS4."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

def build_fixture(name='fixed-17',tail_bytes=17,script=None):
    foundation=ROOT/'build/native-foundation'
    database=foundation/'compile_commands.json'
    if not database.is_file():raise unittest.SkipTest('Native foundation toolchain is not configured')
    row=next(r for r in json.loads(database.read_text()) if r['file'].endswith('/runtime_modern_probe.cpp'))
    out=ROOT/'build/native-tls-regression'/name
    out.mkdir(parents=True,exist_ok=True)
    source=out/'fixture.cpp';obj=out/'fixture.obj';elf=out/'fixture.elf'
    oelf=out/'fixture.oelf';eboot=out/'eboot.bin'
    source.write_text('''extern "C" {
thread_local volatile unsigned prime_tls_canary=0x5052544c;
thread_local unsigned char prime_tls_tail[TAIL_BYTES];
__attribute__((noinline)) unsigned prime_tls_read() { return prime_tls_canary; }
__attribute__((noinline)) void* prime_tls_tail_address() { return prime_tls_tail; }
long prime_tls_local_exec_offset();
}
asm(".text\\n.globl prime_tls_local_exec_offset\\n"
    ".type prime_tls_local_exec_offset,@function\\n"
    "prime_tls_local_exec_offset:\\nmovq $prime_tls_canary@TPOFF,%rax\\nret\\n"
    ".size prime_tls_local_exec_offset,.-prime_tls_local_exec_offset\\n");
int main() { return prime_tls_read()+static_cast<unsigned char*>(prime_tls_tail_address())[0]; }
'''.replace('TAIL_BYTES',str(tail_bytes)))
    compile=shlex.split(row['command'])
    compile[compile.index('-c')+1]=str(source)
    compile[compile.index('-o')+1]=str(obj)
    listing=subprocess.check_output(['ninja','-C',str(foundation),'-t','commands','prime_runtime_modern_probe'],text=True)
    tokens=shlex.split(listing.splitlines()[-1]);link=tokens[2:tokens.index('&&',2)]
    link[link.index('-o')+1]=str(elf)
    for i,arg in enumerate(link):
        if arg.endswith('runtime_modern_probe.cpp.obj'):link[i]=str(obj)
        if arg.startswith('-Wl,--script='):
            link[i]='-Wl,--script='+str(script or ROOT/'platform/ps4/orbis-native.ld')
    for i in range(len(link)-2,-1,-1):
        if link[i]=='-Xlinker' and link[i+1].startswith('--dependency-file='):del link[i:i+2]
    sdk=next(Path(link[i+1]) for i,arg in enumerate(link[:-1]) if arg=='-isysroot')
    converter=sdk/'bin/linux/create-fself'
    convert=[str(converter),'-in='+str(elf),'-out='+str(oelf),'--eboot',str(eboot),'--paid','0x3800000000000011']
    commands=[compile,link,convert]
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    with (out/'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command,cwd=foundation,env=os.environ|{'OO_PS4_TOOLCHAIN':str(sdk)},
                           stdout=log,stderr=subprocess.STDOUT,check=True)
    return elf,oelf,eboot

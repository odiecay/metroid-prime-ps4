"""Actual FIFO races and Orbis libc++ typed-wait ABI checks; no console access.

PRIME_FIFO_WAKE_SOURCE selects an unapplied review candidate. By default the
tests compile the real Aurora FIFO. PRIME_FIFO_WAKE_SANITIZE=1 enables ASan/UBSan.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
UPSTREAM=ROOT/'game/MetroidPrimePort/extern/aurora'


def run_compile(arguments,directory,log):
    with log.open('w')as output:
        result=subprocess.run(arguments,cwd=directory,stdout=output,stderr=subprocess.STDOUT)
    if result.returncode:
        raise AssertionError(f'Compile/link failed; see {log}')


def source_copy(source,out,hooks=False):
    text=source.read_text()
    for name,path in [('fifo.hpp',UPSTREAM/'lib/gx/fifo.hpp'),('../thread.hpp',UPSTREAM/'lib/thread.hpp'),
                      ('command_processor.hpp',UPSTREAM/'lib/gx/command_processor.hpp'),
                      ('fifo_metrics.hpp',UPSTREAM/'lib/gx/fifo_metrics.hpp')]:
        old='#include "'+name+'"'
        if text.count(old)!=1:raise AssertionError(f'Actual FIFO include changed: {name}')
        text=text.replace(old,'#include "'+str(path)+'"')
    if hooks:
        text=text.replace('namespace aurora::gx::fifo {',
            'extern "C" void prime_fifo_wake64_hook(unsigned) noexcept;\nnamespace aurora::gx::fifo {',1)
        for before,after in [
            ('    const int64_t event = sWorkerEvent.load(std::memory_order_acquire);',
             '    prime_fifo_wake64_hook(1);'),
            ('    if (token.stop_requested()) break;','    prime_fifo_wake64_hook(2);'),
            ('    sWorkerEvent.wait(event, std::memory_order_acquire);','    prime_fifo_wake64_hook(3);')]:
            if text.count(before)!=1:raise AssertionError(f'Actual worker boundary changed: {before}')
            text=text.replace(before,before+'\n'+after if after.endswith('(1);')else after+'\n'+before)
        marker='} // namespace aurora::gx::fifo'
        if text.count(marker)!=1:raise AssertionError('Actual FIFO namespace changed')
        # Only an empty, joined test FIFO can change its absolute origin.
        text=text.replace(marker,'''extern "C" void prime_fifo_wake64_set_stream(uint64_t base) {
  stop_worker();
  {
    std::lock_guard lock{sBufferMutex};
    AURORA_ASSERT(detail::sBufferSize == 0,"test stream reset requires an empty FIFO");
    sStreamBase=base;sPublished.store(base);sProcessed.store(base);
    sWorkerEvent.store(std::bit_cast<int64_t>(base));
  }
  start_worker();
}
'''+marker)
    out.write_text(text)


class FifoNativeWakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base=Path(os.environ.get('PRIME_FIFO_WAKE_BUILD_DIR',ROOT/'build/fifo-native-wake-tests'))
        cls.base.mkdir(parents=True,exist_ok=True)
        cls.source=Path(os.environ.get('PRIME_FIFO_WAKE_SOURCE',UPSTREAM/'lib/gx/fifo.cpp'))

    def test_actual_fifo_registration_idle_stop_and_absolute_watermarks(self):
        host=ROOT/'build/pc-asset-importer'
        if not(host/'compile_commands.json').is_file():self.skipTest('Build the existing host GX/FIFO tests first')
        sanitized=os.environ.get('PRIME_FIFO_WAKE_SANITIZE')=='1'
        out=self.base/('sanitized'if sanitized else'normal');out.mkdir(exist_ok=True)
        copy=out/'fifo.cpp';source_copy(self.source,copy,hooks=True)
        recipes=json.loads((host/'compile_commands.json').read_text());objects={};commands=[]
        for suffix,new in [('/gx/fifo.cpp',copy),('/gx_fifo_test.cpp',ROOT/'tests/fifo_native_wake.cpp')]:
            record=next(r for r in recipes if r['file'].endswith(suffix)and'gx_fifo_tests.dir'in r['command'])
            args=shlex.split(record['command']);old=args[args.index('-o')+1];obj=out/(new.stem+'.o')
            args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(new)
            if sanitized:
                headers=out/'san-headers';headers.mkdir(exist_ok=True)
                if not(headers/'sanitizer').exists():(headers/'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer')
                args+=['-isystem',str(headers),'-O1','-g','-fsanitize=address,undefined',
                       '-fno-sanitize-link-runtime','-fno-omit-frame-pointer','-DNDEBUG_SANITIZER']
            run_compile(args,record['directory'],out/(new.stem+'.log'));objects[old]=str(obj);commands.append(args)
        line=subprocess.check_output(['ninja','-C',str(host),'-t','commands','gx_fifo_tests'],text=True).splitlines()[-1]
        args=shlex.split(line);args=args[2:args.index('&&',2)];args=[objects.get(x,x)for x in args]
        dep=next((i for i,x in enumerate(args)if x.startswith('--dependency-file=')),None)
        if dep is not None:del args[dep-1:dep+1]
        args[args.index('-o')+1]=str(out/'tests')
        if sanitized:args[1:1]=['-Wl,--no-as-needed','-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        run_compile(args,host,out/'link.log');commands.append(args)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        env=dict(os.environ)
        if sanitized:env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
            ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=subprocess.run([str(out/'tests'),'--gtest_filter=*Native64*','--gtest_brief=1'],
            env=env,capture_output=True,text=True,timeout=45)
        (out/'run.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('3 tests',result.stdout)

    def test_native_signed64_typed_channel_and_publish_has_no_locked_write(self):
        native=ROOT/'build/game-native'
        if not(native/'compile_commands.json').is_file():self.skipTest('Configure the native Orbis build first')
        out=self.base/'native';out.mkdir(exist_ok=True)
        record=next(r for r in json.loads((native/'compile_commands.json').read_text())if r['file'].endswith('/gx/fifo.cpp'))
        original=shlex.split(record['command']);copy=out/'fifo.cpp';source_copy(self.source,copy)
        probe=out/'typed.cpp';probe.write_text('''#include <atomic>
#include <cstdint>
#include <type_traits>
static_assert(std::is_same_v<std::__cxx_contention_t,int64_t>);
std::atomic<uint32_t> a32;std::atomic<uint64_t> au64;std::atomic<int64_t> ai64;
extern "C" void probe32(){a32.notify_all();}
extern "C" void probeU64(){au64.notify_all();}
extern "C" void probeI64(){ai64.notify_all();}
''')
        commands=[]
        for source in (copy,probe):
            args=list(original);args[args.index('-o')+1]=str(out/(source.stem+'.o'));args[args.index('-c')+1]=str(source)
            run_compile(args,record['directory'],out/(source.stem+'.log'));commands.append(args)
        objdump='/usr/lib/llvm-19/bin/llvm-objdump'
        text=subprocess.check_output([objdump,'-dr','--no-show-raw-insn',str(out/'typed.o')],text=True)
        void='_ZNSt3__123__cxx_atomic_notify_allEPVKv'
        typed='_ZNSt3__123__cxx_atomic_notify_allEPVKNS_17__cxx_atomic_implIl'
        self.assertEqual(text.count(void),2);self.assertEqual(text.count(typed),1)
        publish=subprocess.check_output([objdump,'-dr','--no-show-raw-insn',
            '--disassemble-symbols=_ZN6aurora2gx4fifo7publishEv',str(out/'fifo.o')],text=True)
        self.assertIn(typed,publish);self.assertNotIn('xchg',publish);self.assertNotIn('lock\t',publish)
        (out/'typed.s').write_text(text);(out/'publish.s').write_text(publish)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')


if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Cross-build libc++19/libc++abi/libunwind for Orbis, in this project only."""
import os
from pathlib import Path
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
bundle=os.environ.get('PRIME_SDK_BUNDLE','/opt/orbis-sdk')
build=ROOT/'build/libcxx19-orbis'
cmake=ROOT/'.tools/bin/cmake'
env=os.environ.copy(); env['PATH']='/usr/lib/llvm-19/bin:'+env['PATH']
configure=[str(cmake),'-S',str(ROOT/'external/llvm-project/runtimes'),'-B',str(build),'-G','Ninja',
 '-UCMAKE_C_FLAGS','-UCMAKE_CXX_FLAGS',
 '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'platform/ps4/toolchain-runtime.cmake'),
 '-DPRIME_SDK_BUNDLE='+bundle,'-DCMAKE_BUILD_TYPE=Release',
 '-DCMAKE_INSTALL_PREFIX='+str(ROOT/'build/modern-runtime'),
 '-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx',
 '-DLIBCXX_ENABLE_SHARED=OFF','-DLIBCXXABI_ENABLE_SHARED=OFF','-DLIBUNWIND_ENABLE_SHARED=OFF',
 '-DLIBCXX_ENABLE_STATIC=ON','-DLIBCXXABI_ENABLE_STATIC=ON','-DLIBUNWIND_ENABLE_STATIC=ON',
 '-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON','-DLIBCXXABI_USE_LLVM_UNWINDER=ON',
 '-DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON','-DLIBUNWIND_IS_BAREMETAL=ON',
 '-DLIBCXX_HAS_MUSL_LIBC=ON','-DLIBCXX_ENABLE_INCOMPLETE_FEATURES=ON',
 '-DLIBCXX_ENABLE_EXPERIMENTAL_LIBRARY=ON','-DLIBCXX_INCLUDE_BENCHMARKS=OFF',
 '-DLLVM_INCLUDE_TESTS=OFF',
 '-DLIBCXX_INCLUDE_TESTS=OFF','-DLIBCXXABI_INCLUDE_TESTS=OFF','-DLIBUNWIND_INCLUDE_TESTS=OFF']
for label,cmd in [('configure',configure),('build',[str(cmake),'--build',str(build),'-j',env.get('PRIME_BUILD_JOBS','6')]),
                  ('install',[str(cmake),'--install',str(build)])]:
    log=ROOT/'evidence'/('libcxx19-'+label+'.log')
    if log.exists(): log.rename(log.with_name(log.stem+'-'+str(time.time_ns())+'.log'))
    with log.open('w') as output:
        r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
    print(label,r.returncode,log,flush=True)
    if r.returncode:
        print(log.read_text()[-10000:]);raise SystemExit(r.returncode)

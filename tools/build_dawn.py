#!/usr/bin/env python3
"""Build the pinned Dawn/Tint fork against Project Prime's native C++ runtime."""
import os
from pathlib import Path
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
build=ROOT/'build/dawn-modern'
cmake=ROOT/'.tools/bin/cmake'
env=os.environ.copy();env['PATH']='/usr/lib/llvm-19/bin:'+env['PATH']
bundle=env.get('PRIME_SDK_BUNDLE','/opt/orbis-sdk')
configure=[str(cmake),'-S',str(ROOT/'external/dawn'),'-B',str(build),'-G','Ninja',
 '-UCMAKE_C_FLAGS','-UCMAKE_CXX_FLAGS',
 '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'platform/ps4/toolchain-modern.cmake'),
 '-DPRIME_SDK_BUNDLE='+bundle,'-DCMAKE_BUILD_TYPE=Release',
 '-DCMAKE_INSTALL_PREFIX='+str(ROOT/'build/dawn-native'),
 '-DDAWN_FETCH_DEPENDENCIES=ON','-DDAWN_BUILD_SAMPLES=OFF','-DDAWN_BUILD_TESTS=OFF',
 '-DDAWN_BUILD_PROTOBUF=OFF','-DTINT_BUILD_TESTS=OFF','-DTINT_BUILD_CMD_TOOLS=OFF',
 '-DDAWN_ENABLE_VULKAN=ON','-DDAWN_ENABLE_DESKTOP_GL=OFF','-DDAWN_ENABLE_OPENGLES=OFF',
 '-DDAWN_ENABLE_NULL=OFF','-DDAWN_ENABLE_SPIRV_VALIDATION=ON','-DDAWN_USE_GLFW=OFF',
 '-DTINT_BUILD_GLSL_WRITER=OFF','-DTINT_BUILD_HLSL_WRITER=OFF','-DTINT_BUILD_MSL_WRITER=OFF',
 '-DTINT_BUILD_SPV_READER=OFF','-DTINT_BUILD_SPV_WRITER=ON','-DDAWN_ENABLE_INSTALL=ON']
for label,cmd in [('configure',configure),('build',[str(cmake),'--build',str(build),'-j','6']),
                  ('install',[str(cmake),'--install',str(build)])]:
    log=ROOT/'evidence'/('dawn-native-'+label+'.log')
    if log.exists():log.rename(log.with_name(log.stem+'-'+str(time.time_ns())+'.log'))
    with log.open('w') as output:
        result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
    print(label,result.returncode,log,flush=True)
    if result.returncode:
        print(log.read_text()[-10000:]);raise SystemExit(result.returncode)

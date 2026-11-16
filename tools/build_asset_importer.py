#!/usr/bin/env python3
"""Build the upstream PC asset import CLI; never a PS4 runtime dependency."""
import os
from pathlib import Path
import subprocess
import time
ROOT=Path(__file__).resolve().parents[1]
env=os.environ.copy()
cmake=ROOT/'.tools/bin/cmake';build=ROOT/'build/pc-asset-importer'
commands=[('configure',[cmake,'-S',ROOT/'tools/asset-importer','-B',build,'-G','Ninja',
    '-DCMAKE_C_COMPILER=/usr/lib/llvm-19/bin/clang',
    '-DCMAKE_CXX_COMPILER=/usr/lib/llvm-19/bin/clang++','-DCMAKE_BUILD_TYPE=Release']),
    ('build',[cmake,'--build',build,'--target','metroid_prime_port','-j',
              env.get('PRIME_IMPORT_BUILD_JOBS','2')])]
for label,command in commands:
    log=ROOT/'evidence'/('pc-asset-importer-'+label+'.log')
    if log.exists():log.rename(log.with_name(log.stem+'-'+str(time.time_ns())+'.log'))
    with log.open('w') as output:
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
    print(label,result.returncode,log,flush=True)
    if result.returncode:
        print(log.read_text()[-5000:]);raise SystemExit(result.returncode)

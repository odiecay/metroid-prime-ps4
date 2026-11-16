#!/usr/bin/env python3
"""Configure and cross-build the real game with the native Orbis dependency stack."""
import os
from pathlib import Path
import subprocess
import time
from source_catalog import verify_populated
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env['PATH'] = '/usr/lib/llvm-19/bin:' + env['PATH']
bundle = env.get('PRIME_SDK_BUNDLE', '/opt/orbis-sdk')
cmake = ROOT / '.tools/bin/cmake'
build = ROOT / 'build/game-native'
if not (ROOT / 'build/dawn-modern/src/dawn/native/libwebgpu_dawn.a').is_file():
    raise SystemExit('Complete tools/build_dawn.py first; the real Dawn archive is required')
with (ROOT/'evidence/mesa-memory-overlay-build.log').open('w') as output:
    overlay=subprocess.run([sys.executable,ROOT/'tools/build_mesa_overlay.py'],
        cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
if overlay.returncode:
    raise SystemExit('Native Mesa diagnostic object failed; see evidence/mesa-memory-overlay-build.log')
with (ROOT/'evidence/native-gpu-wait-bridge-build.log').open('w') as output:
    bridge=subprocess.run([sys.executable,ROOT/'tools/build_dawn_bridge.py'],
        cwd=ROOT,env=env,stdout=output,stderr=subprocess.STDOUT)
if bridge.returncode:
    raise SystemExit('Native Dawn wait bridge failed; see evidence/native-gpu-wait-bridge-build.log')
commands = [
    ('configure', [cmake, '-S', ROOT / 'game', '-B', build, '-G', 'Ninja',
                   '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'platform/ps4/toolchain-modern.cmake'),
                   '-DPRIME_SDK_BUNDLE=' + bundle, '-DCMAKE_BUILD_TYPE=RelWithDebInfo']),
    ('build', [cmake, '--build', build, '-j', env.get('PRIME_BUILD_JOBS', '6')]),
]
for label, command in commands:
    log = ROOT / 'evidence' / ('game-native-' + label + '.log')
    if log.exists():
        log.rename(log.with_name(log.stem + '-' + str(time.time_ns()) + '.log'))
    with log.open('w') as output:
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
    print(label, result.returncode, log, flush=True)
    if result.returncode:
        print(log.read_text()[-10000:])
        raise SystemExit(result.returncode)
    if label=='configure':
        count=verify_populated(ROOT,json.loads((ROOT/'upstream.lock.json').read_text()).get('aurora_archive_dependencies',[]))
        print('Verified populated dependency source trees:',count,flush=True)

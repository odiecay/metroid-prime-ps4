#!/usr/bin/env python3
"""Record immutable upstream identities and the exact native SDK inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
from source_catalog import populated_catalog
ROOT=Path(__file__).resolve().parents[1]
SDK=Path('/opt/orbis-sdk')
def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(path,*args):
    return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
def identity(relative):
    p=ROOT/relative
    return {'path':relative,'commit':git(p,'rev-parse','HEAD'),
            'remote':git(p,'remote','get-url','origin'),
            'patch':relative.split('/')[-1]+'.patch'}
components=[identity(x) for x in ('game/MetroidPrimePort','external/dawn',
    'external/mesa-ps4','external/orbis-compat','external/ps4-mesa-docs','external/llvm-project')]
components[0]['canonical_remote']='https://github.com/Odrannnn/MetroidPrimePort.git'
components[0]['canonical_fetch_status']='404 on 2026-11-15; exact v0.46.0 commit recovered through the public fork network'
components[1]['canonical_remote']='https://github.com/Odrannnn/dawn.git'
components[1]['canonical_fetch_status']='404 on 2026-11-15; exact game dependency commit recovered through google/dawn'
deps=[]
for dotgit in sorted((ROOT/'external/dawn/third_party').rglob('.git')):
    p=dotgit.parent
    remotes=git(p,'remote').splitlines()
    relative=str(p.relative_to(ROOT/'external/dawn'))
    change=subprocess.check_output(['git','-C',str(p),'diff','--binary'])
    patch_name='dawn-'+relative.replace('/','_')+'.patch' if change else None
    if change:
        (ROOT/'patches').mkdir(exist_ok=True)
        (ROOT/'patches'/patch_name).write_bytes(change)
    deps.append({'path':relative,'patch':patch_name,
                 'commit':git(p,'rev-parse','HEAD'),
                 'remote':git(p,'remote','get-url',remotes[0]) if remotes else None,
                 'fetch_source':(dotgit/'FETCH_HEAD').read_text().strip() if (dotgit/'FETCH_HEAD').exists() else None})
lock={'schema':1,'components':components,'dawn_transitive_git_dependencies':deps,
      'sdl':{'path':'external/SDL-release-3.4.10','url':'https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.10.tar.gz',
             'sha256':sha(ROOT/'.tools/SDL-release-3.4.10.tar.gz'),'patch':'SDL-release-3.4.10.patch'},
      'nod_headers_and_host_reference':{'url':'https://github.com/encounter/nod/releases/download/v2.0.0-alpha.12/libnod-linux-x86_64.tar.gz',
                                       'sha256':sha(ROOT/'.tools/libnod-linux-x86_64.tar.gz')},
      'nodtool':{'url':'https://github.com/encounter/nod/releases/download/v2.0.0-alpha.12/nodtool-linux-x86_64',
                 'sha256':sha(ROOT/'.tools/nodtool')},
      'cmake_python_package':'cmake==3.31.10'}
game_dependencies=populated_catalog(ROOT)
if not game_dependencies and (ROOT/'upstream.lock.json').exists():
    game_dependencies=json.loads((ROOT/'upstream.lock.json').read_text()).get('aurora_archive_dependencies',[])
lock['aurora_archive_dependencies']=game_dependencies
if (ROOT/'upstream.lock.json').exists():
    lock['native_startup_modules']=json.loads((ROOT/'upstream.lock.json').read_text()).get('native_startup_modules',[])
(ROOT/'upstream.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
patches=ROOT/'patches';patches.mkdir(exist_ok=True)
for part in components:
    diff=subprocess.check_output(['git','-C',str(ROOT/part['path']),'diff','--binary'])
    if diff:(patches/part['patch']).write_bytes(diff)
diff=subprocess.check_output(['git','-C',str(ROOT/lock['sdl']['path']),'diff','--binary'])
(patches/lock['sdl']['patch']).write_bytes(diff)
selected=['BUNDLE.txt','SHA256SUMS','sdk/link.x','sdk/lib/crt1.o','sdk/lib/libc.a',
          'sdk/lib/libc++.a','sdk/lib/libc++abi.a','sdk/lib/libunwind.a',
          'orbis-compat/build/liborbis-compat.a','mesa/build-orbis/src/amd/vulkan/libvulkan_radeon.a']
selected += ['orbis-compat/vkloader/'+name for name in
             ('CMakeLists.txt','vkloader.c','vkloader.h','vkthunks.c')]
selected += [str(p.relative_to(SDK)) for p in sorted((SDK/'mesa/build-orbis/subprojects').glob('zlib-*/libz.a'))]
selected += [str(p.relative_to(SDK)) for p in sorted((SDK/'sdk/lib').glob('*.so'))]
sdk={'path':str(SDK),'files':{p:sha(SDK/p) for p in selected},
     'compiler':{'path':'/usr/lib/llvm-19/bin/clang','sha256':sha('/usr/lib/llvm-19/bin/clang'),
                 'version':subprocess.check_output(['/usr/lib/llvm-19/bin/clang','--version'],text=True)},
     'linker':{'path':'/usr/lib/llvm-19/bin/ld.lld','sha256':sha('/usr/lib/llvm-19/bin/ld.lld')},
     'read_only_reference':True}
(ROOT/'evidence/sdk-inputs.json').write_text(json.dumps(sdk,indent=2)+'\n')
print('Pinned',len(components),'source checkouts,',len(deps),'Dawn dependency checkouts and native SDK hashes')

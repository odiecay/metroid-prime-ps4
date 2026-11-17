#!/usr/bin/env python3
"""Build project-owned DRM/WSI overlays; preserve the remaining SDK members."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from ar_members import member_hashes

ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('PRIME_SDK_BUNDLE','/opt/orbis-sdk'))
OUT=ROOT/'build/mesa-native-memory';OUT.mkdir(parents=True,exist_ok=True)
recipe=json.loads((ROOT/'evidence/mesa-overlay-compile-command.json').read_text())
oldbuild=Path(recipe['directory'])
oldsource=Path('/opt/mesa-source')
source=ROOT/'external/mesa-ps4'
generated=OUT/'generated';generated.mkdir(exist_ok=True)
format_dir=generated/'src/util/format';format_dir.mkdir(parents=True,exist_ok=True)
with (format_dir/'u_format_gen.h').open('w') as output:
    subprocess.run([sys.executable,source/'src/util/format/u_format_table.py',
                    source/'src/util/format/u_format.yaml','--enums'],stdout=output,check=True)
args=recipe['arguments'];command=[args[0],'-I'+str(ROOT/'renderer/orbis'),'-I'+str(generated)]
i=1
while i<len(args):
    arg=args[i]
    if arg in ('-o','-MF','-MQ','-c'):
        i+=2;continue
    if arg=='-MD' or arg=='-fdiagnostics-color=always':i+=1;continue
    if arg.startswith('-I'):
        inc=(oldbuild/arg[2:]).resolve()
        if inc.is_relative_to(oldsource):inc=source/inc.relative_to(oldsource)
        elif inc.is_relative_to(oldbuild):inc=generated/inc.relative_to(oldbuild)
        if 'native-mesa-packet-copy-1/source' in str(inc):inc=source/'src/amd/common'
        command.append('-I'+str(inc))
    else:
        command.append(arg.replace('/opt/orbis-sdk',str(SDK)))
    i+=1
base_command=command.copy()
obj=OUT/'ac_orbis_drm.c.o';dep=OUT/'ac_orbis_drm.c.o.d'
command+=['-MD','-MF',str(dep),'-c',str(source/'src/amd/common/ac_orbis_drm.c'),'-o',str(obj)]
(OUT/'command.json').write_text(json.dumps(command,indent=2)+'\n')
subprocess.run(command,cwd=ROOT,check=True)
wsi_obj=OUT/'wsi_orbis.c.o';wsi_dep=OUT/'wsi_orbis.c.o.d'
wsi_command=base_command+['-MD','-MF',str(wsi_dep),'-c',str(source/'src/vulkan/wsi/wsi_orbis.c'),'-o',str(wsi_obj)]
(OUT/'wsi-command.json').write_text(json.dumps(wsi_command,indent=2)+'\n')
subprocess.run(wsi_command,cwd=ROOT,check=True)
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
base=SDK/'mesa/build-orbis/src/amd/vulkan/libvulkan_radeon.a'
assert sha(base)=='d1ba74620b23c78cfb0e26739941036a820a6ff650edbece2efb88d3a9a141dd'
archive=OUT/'libvulkan_radeon.a';shutil.copyfile(base,archive)
subprocess.run(['/usr/lib/llvm-19/bin/llvm-ar','rcs',archive,obj,wsi_obj],check=True)
before,after=member_hashes(base),member_hashes(archive)
assert before.keys()==after.keys() and len(before)==679
changed=[name for name in before if before[name]!=after[name]]
assert changed==['ac_orbis_drm.c.o','wsi_orbis.c.o'],changed
inputs={}
for depfile in (dep,wsi_dep):
    for name in depfile.read_text().replace('\\\n',' ').split(':',1)[1].split():
        path=(ROOT/name).resolve()
        inputs[str(path)]=sha(path)
receipt={'schema':1,'base_archive_sha256':sha(base),'overlay_archive_sha256':sha(archive),
         'object_sha256':sha(obj),'command':command,'working_directory':str(ROOT),
         'wsi_object_sha256':sha(wsi_obj),'wsi_command':wsi_command,
         'dependency_hashes':inputs,'purpose':'Native VA accounting, fence-checked recovery and lifetime-pinned advisory presentation readiness outside the Dawn device lock'}
receipt.update({'unchanged_archive_members':677,'changed_archive_members':changed,
                'format_abi_sha256':sha(format_dir/'u_format_gen.h')})
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'archive':str(archive),'sha256':receipt['overlay_archive_sha256'],'dependencies':len(inputs)}))

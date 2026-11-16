#!/usr/bin/env python3
"""Compile the native pressure-wait bridge against the exact pinned Dawn ABI."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/dawn-bridge';OUT.mkdir(parents=True,exist_ok=True)
commands=json.loads((ROOT/'build/dawn-modern/compile_commands.json').read_text())
recipe=next(x for x in commands if x['file'].endswith('/src/dawn/native/DawnNative.cpp'))
arguments=recipe.get('arguments') or shlex.split(recipe['command'])
result=[];i=0
while i<len(arguments):
    arg=arguments[i]
    if arg in ('-o','-MF','-MT','-MQ','-c'):i+=2;continue
    if arg in ('-MD','-MMD'):i+=1;continue
    result.append(arg);i+=1
source=ROOT/'platform/ps4/gpu_wait.cpp';obj=OUT/'gpu_wait.cpp.o';deps=OUT/'gpu_wait.d'
result+=['-MD','-MF',str(deps),'-c',str(source),'-o',str(obj)]
subprocess.run(result,cwd=recipe['directory'],check=True)
archive=OUT/'libprime_gpu_wait.a'
subprocess.run(['/usr/lib/llvm-19/bin/llvm-ar','rcs',archive,obj],check=True)
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
inputs={}
for name in deps.read_text().replace('\\\n',' ').split(':',1)[1].split():
    path=(Path(recipe['directory'])/name).resolve();inputs[str(path)]=sha(path)
receipt={'schema':1,'target':'orbis','dawn_native_archive_sha256':sha(ROOT/'build/dawn-modern/src/dawn/native/libwebgpu_dawn.a'),
 'archive_sha256':sha(archive),'source_sha256':sha(source),'command':result,'dependency_hashes':inputs}
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Native GPU wait bridge built:',receipt['archive_sha256'])

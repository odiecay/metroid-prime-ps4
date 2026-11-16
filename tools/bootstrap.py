#!/usr/bin/env python3
"""Restore exact pinned sources, or verify an existing Project Prime workspace."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import urllib.request
from source_catalog import verify_populated

ROOT=Path(__file__).resolve().parents[1]

def run(*args, **kwargs):return subprocess.run(list(map(str,args)),check=True,**kwargs)
def output(*args):return subprocess.check_output(list(map(str,args))).strip()
def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def download(url,digest,destination):
    if destination.exists():
        if sha(destination)!=digest:raise RuntimeError('Existing download differs from pin: '+str(destination))
        return
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent,delete=False) as out:
        temporary=Path(out.name)
        with urllib.request.urlopen(url,timeout=60) as incoming:
            while part:=incoming.read(1024*1024):out.write(part)
    if sha(temporary)!=digest:
        temporary.unlink();raise RuntimeError('Download hash mismatch: '+url)
    temporary.rename(destination)

def checkout(part,verify):
    path=ROOT/part['path']
    if not (path/'.git').exists():
        if verify:raise RuntimeError('Missing source: '+str(path))
        path.mkdir(parents=True,exist_ok=True)
        run('git','init',path,stdout=subprocess.DEVNULL)
        run('git','-C',path,'remote','add','origin',part['remote'])
        run('git','-C',path,'fetch','--filter=blob:none','--depth','1','origin',part['commit'])
        if part['path'].endswith('llvm-project'):
            run('git','-C',path,'sparse-checkout','init','--cone')
            run('git','-C',path,'sparse-checkout','set','libcxx','libcxxabi','libunwind','runtimes','cmake','llvm/cmake','llvm/include/llvm/Config')
        run('git','-C',path,'checkout','--detach',part['commit'])
    actual=output('git','-C',path,'rev-parse','HEAD').decode()
    if actual!=part['commit']:raise RuntimeError('Wrong source commit: '+str(path))
    patch=ROOT/'patches'/part['patch']
    expected=patch.read_bytes() if patch.exists() else b''
    diff=output('git','-C',path,'diff','--binary')
    if diff!=expected.strip():
        if not diff and expected and not verify:run('git','-C',path,'apply',patch)
        else:raise RuntimeError('Source changes differ from recorded patch: '+str(path))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--verify',action='store_true');a=p.parse_args()
    lock=json.loads((ROOT/'upstream.lock.json').read_text())
    for part in lock.get('native_startup_modules',[]):
        path=ROOT/part['path']
        if not a.verify:download(part['url'],part['sha256'],path)
        if not path.exists() or sha(path)!=part['sha256']:
            raise RuntimeError('Native startup module pin mismatch: '+str(path))
    for part in lock['components']:checkout(part,a.verify)
    if not a.verify:
        run('python3',ROOT/'external/dawn/tools/fetch_dawn_dependencies.py',cwd=ROOT/'external/dawn')
    for part in lock['dawn_transitive_git_dependencies']:
        path=ROOT/'external/dawn'/part['path']
        if output('git','-C',path,'rev-parse','HEAD').decode()!=part['commit']:
            raise RuntimeError('Dawn dependency pin mismatch: '+str(path))
        expected=(ROOT/'patches'/part['patch']).read_bytes().strip() if part.get('patch') else b''
        diff=output('git','-C',path,'diff','--binary')
        if diff!=expected:
            if expected and not diff and not a.verify:run('git','-C',path,'apply',ROOT/'patches'/part['patch'])
            else:raise RuntimeError('Dawn dependency patch mismatch: '+str(path))
    sdl=lock['sdl'];archive=ROOT/'.tools/SDL-release-3.4.10.tar.gz'
    nod=lock['nod_headers_and_host_reference'];nod_archive=ROOT/'.tools/libnod-linux-x86_64.tar.gz'
    if not a.verify:
        for part,dest in ((sdl,archive),(nod,nod_archive),(lock['nodtool'],ROOT/'.tools/nodtool')):
            download(part['url'],part['sha256'],dest)
        if not (ROOT/sdl['path']).exists():
            with tarfile.open(archive) as t:t.extractall(ROOT/'external',filter='data')
            path=ROOT/sdl['path'];run('git','init',path,stdout=subprocess.DEVNULL);run('git','-C',path,'add','.')
            run('git','-C',path,'-c','user.name=Project Prime','-c','user.email=prime@localhost','commit','-qm','Pristine SDL release archive')
            run('git','-C',path,'apply',ROOT/'patches'/sdl['patch'])
        if not (ROOT/'external/nod-host-package/include/nod.h').exists():
            (ROOT/'external/nod-host-package').mkdir(parents=True,exist_ok=True)
            with tarfile.open(nod_archive) as t:t.extractall(ROOT/'external/nod-host-package',filter='data')
        (ROOT/'.tools/nodtool').chmod(0o755)
    if not archive.exists() or sha(archive)!=sdl['sha256']:raise RuntimeError('SDL archive pin mismatch')
    diff=output('git','-C',ROOT/sdl['path'],'diff','--binary')
    if diff!=(ROOT/'patches'/sdl['patch']).read_bytes().strip():raise RuntimeError('SDL platform patch mismatch')
    if not nod_archive.exists() or sha(nod_archive)!=nod['sha256']:raise RuntimeError('nod package pin mismatch')
    verified=verify_populated(ROOT,lock.get('aurora_archive_dependencies',[]))
    print('Verified',verified,'populated Aurora archive dependency trees; CMake restores absent ones by archive hash.')
    print('Pinned sources and recorded platform patches verified. Native SDK is supplied separately.')

if __name__=='__main__':main()

#!/usr/bin/env python3
"""Build a pinned package verifier without changing the shared PS4 SDK."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
    lock=json.loads((ROOT/'packaging/host-tools.lock.json').read_text())
    source=ROOT/lock['source'];patch=ROOT/lock['patch']
    if sha(patch)!=lock['patch_sha256']:raise RuntimeError('Package reader patch differs from pin')
    if not source.exists():
        subprocess.run(['git','clone',lock['canonical_remote'],source],check=True)
        subprocess.run(['git','-C',source,'checkout','--detach',lock['commit']],check=True)
        subprocess.run(['git','-C',source,'apply',patch],check=True)
    head=subprocess.check_output(['git','-C',source,'rev-parse','HEAD'],text=True).strip()
    diff=subprocess.check_output(['git','-C',source,'diff','--binary'])
    if head!=lock['commit'] or hashlib.sha256(diff).hexdigest()!=lock['patch_sha256']:
        raise RuntimeError('Package reader source differs from pinned commit and patch')
    runtime=os.environ.get('PRIME_DOTNET_ROOT')
    dotnet=Path(runtime)/'dotnet' if runtime else Path(shutil.which('dotnet') or '/opt/dotnet/dotnet')
    if not dotnet.is_file():raise RuntimeError('Set PRIME_DOTNET_ROOT to an existing .NET 8 SDK')
    env=os.environ.copy();env.update(DOTNET_ROOT=str(dotnet.parent),DOTNET_CLI_HOME=str(ROOT/'.tools/dotnet-home'),DOTNET_SYSTEM_GLOBALIZATION_INVARIANT='1')
    output=ROOT/'build/pkgtool-reader'
    subprocess.run([dotnet,'build',source/'PkgTool.Core/PkgTool.Core.csproj','-c','Release','-o',output,'--disable-build-servers'],env=env,check=True)
    files={p.name:sha(p) for p in sorted(output.iterdir()) if p.is_file() and p.suffix!='.pdb' and p.name!='receipt.json'}
    receipt=dict(source=lock,files=files,dotnet_root=str(dotnet.parent),dotnet_sha256=sha(dotnet),purpose='host extraction verification only; native SDK and package bytes unchanged')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(output/'PkgTool.Core')
if __name__=='__main__':main()

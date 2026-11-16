#!/usr/bin/env python3
"""Build the read-only PC preflight against the pinned real NSP/NSO reader."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
compiler=Path(shutil.which('clang++-19'))
output=ROOT/'build/remastered-input-probe';deps=ROOT/'build/remastered-input-probe.d'
sources=[ROOT/'tools/remastered_probe.cpp',ROOT/'game/MetroidPrimePort/platform/port_remastered_nsp.cpp',
         ROOT/'game/MetroidPrimePort/platform/port_remastered_nso.cpp']
sources.append(ROOT/'game/MetroidPrimePort/platform/port_remastered_pak.cpp')
command=[str(compiler),'-std=c++20','-O2','-pthread','-DMP_HAVE_OPENSSL','-ffunction-sections',
         '-fdata-sections','-I'+str(ROOT/'game/MetroidPrimePort/platform/include'),
         *map(str,sources),'-Wl,--gc-sections','-lcrypto','-o',str(output)]
subprocess.run(command,cwd=ROOT,check=True)
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
headers=[ROOT/'game/MetroidPrimePort/platform/include'/name for name in
         ('port_remastered_nsp.h','port_remastered_nso.h','port_remastered_pak.h',
          'port_remastered_lzss.h','port_hash.h','port_bytes.h','port_strings.h')]
crypto=Path('/usr/lib/x86_64-linux-gnu/libcrypto.so').resolve()
receipt={'schema':1,'target':'pc_asset_preflight','compiler_sha256':sha(compiler),'command':command,
 'reader_sources':{str(p.relative_to(ROOT)):sha(p) for p in sources+headers},
 'crypto_library_sha256':sha(crypto),'executable_sha256':sha(output),
 'purpose':'Read-only NSP/RomFS and supported-version check; no game execution or console operation'}
(ROOT/'evidence/remastered-reader-build.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Pinned reader preflight built and its actual input hashes recorded')

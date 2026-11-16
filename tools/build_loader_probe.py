#!/usr/bin/env python3
"""Make a separately identified early-entry diagnostic from the real game ELF.

Only the ELF entry field differs; game code, imports, data and relocations stay
identical. This probe does not run the game and never installs or launches.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
from verify_elf import verify

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/game-native/upstream/metroid_prime_port'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=ROOT/'build/loader-probe')
OUT=parser.parse_args().out.resolve()
if OUT.exists():raise SystemExit('Select a fresh loader-probe build directory')
OUT.mkdir(parents=True)
symbols=subprocess.check_output(['/usr/lib/llvm-19/bin/llvm-nm','-P',SOURCE],text=True)
row=next(line.split() for line in symbols.splitlines() if line.startswith('prime_ps4_loader_entry '))
entry=int(row[2],16)
data=bytearray(SOURCE.read_bytes());original_entry=struct.unpack_from('<Q',data,24)[0]
struct.pack_into('<Q',data,24,entry)
target=OUT/'prime_loader_probe'
target.write_bytes(data)
assert SOURCE.read_bytes()[:24]+SOURCE.read_bytes()[32:]==data[:24]+data[32:]
bundle=Path(os.environ.get('PRIME_SDK_BUNDLE',
    '/opt/orbis-sdk'))
env=os.environ.copy();env['OO_PS4_TOOLCHAIN']=str(bundle/'sdk')
result=subprocess.run([bundle/'sdk/bin/linux/create-fself','-in='+str(target),
    '-out='+str(target.with_suffix('.oelf')),'--eboot',OUT/'eboot.bin',
    '--paid','0x3800000000000011'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(OUT/'conversion.log').write_bytes(result.stdout)
result.check_returncode()
mapping=verify(target,target.with_suffix('.oelf'))
receipt={'schema':1,'purpose':'Isolate executable loading from C/C++ startup',
    'full_game_elf_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    'game_entry':original_entry,'diagnostic_entry':entry,'only_elf_entry_field_changed':True,
    'runs_game':False,'hardware_tested':False,'mapping':mapping}
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in receipt.items() if k!='mapping'},indent=2))

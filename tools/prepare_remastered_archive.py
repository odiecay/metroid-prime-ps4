#!/usr/bin/env python3
"""Verify user-supplied split RAR and key ZIP, extract private import inputs."""
import argparse
import binascii
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def pfs0(path):
    size=path.stat().st_size
    with path.open('rb') as f:
        header=f.read(16)
        if len(header)!=16 or header[:4]!=b'PFS0':raise ValueError('Image is not an NSP PFS0 container')
        count,strings,_=struct.unpack_from('<III',header,4)
        if not 0<count<=4096 or strings>8*1024*1024:raise ValueError('Invalid PFS0 tables')
        table=f.read(count*24);names=f.read(strings);base=16+count*24+strings
        if len(table)!=count*24 or len(names)!=strings or base>size:raise ValueError('Truncated PFS0 tables')
        entries=[]
        for n in range(count):
            offset,length,index,_=struct.unpack_from('<QQII',table,n*24)
            if index>=strings or offset>size-base or length>size-base-offset:raise ValueError('PFS0 entry outside image')
            end=names.find(b'\0',index)
            if end<0:raise ValueError('Unterminated PFS0 entry')
            name=names[index:end].decode('utf-8')
            if Path(name).name!=name or name in ('.','..'):raise ValueError('Unsafe PFS0 entry name')
            entries.append({'name':name,'offset':offset,'bytes':length})
        return entries

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',required=True,type=Path)
    parser.add_argument('--keys-zip',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--unrar',type=Path,help='Verified private UnRAR for distributions that omit the 7-Zip RAR codec')
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to((ROOT/'private').resolve()):parser.error('Output must be under private/')
    out.mkdir(mode=0o700,parents=True,exist_ok=False)
    receipt={'schema':1,'started_utc':datetime.now(timezone.utc).isoformat(),'stage':'preparing',
             'archive':str(args.archive.resolve()),'key_zip_sha256':sha(args.keys_zip),'parts':[]}
    def save():(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    save()
    with zipfile.ZipFile(args.keys_zip) as z:
        candidates=[i for i in z.infolist() if Path(i.filename).name.lower()=='prod.keys' and not i.is_dir()]
        if len(candidates)!=1 or candidates[0].file_size>1024*1024:raise ValueError('Expected one bounded prod.keys entry')
        key=z.read(candidates[0])
    parsed={}
    for line in key.decode('ascii').splitlines():
        match=re.fullmatch(r'\s*([a-zA-Z0-9_]+)\s*=\s*([a-fA-F0-9]+)\s*',line)
        if match:parsed[match[1].lower()]=match[2]
    header=parsed.get('header_key','')
    if len(header)!=64 or not any(c!='0' for c in header):raise ValueError('Key input lacks a valid header_key')
    key_path=out/'prod.keys'
    fd=os.open(key_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'wb') as f:f.write(key)
    receipt['keys']={'file':'prod.keys','sha256':sha(key_path),'header_key_present':True,
        'title_kek_count':sum(n.startswith('titlekek_') for n in parsed),
        'application_key_area_count':sum(n.startswith('key_area_key_application_') for n in parsed)}
    del parsed,key,header
    seven=shutil.which('7z')
    if not seven:raise ValueError('7z is required')
    receipt['unpacker']={'path':seven,'sha256':sha(seven)}
    binary=Path('/usr/lib/p7zip/7z')
    if binary.is_file():receipt['unpacker']['binary_sha256']=sha(binary)
    listing=subprocess.run([seven,'l','-slt',str(args.archive)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
    (out/'archive-list.log').write_bytes(listing.stdout)
    text=listing.stdout.decode(errors='strict')
    names=re.findall(r'^Path = (.*)$',text,re.M)[1:]
    lengths=re.findall(r'^Size = (\d+)$',text,re.M)
    if len(names)!=1 or len(lengths)!=1 or Path(names[0]).name!=names[0] or not names[0].lower().endswith('.nsp'):
        raise ValueError('Expected exactly one regular NSP image in the archive')
    image_name=names[0];expected_bytes=int(lengths[0])
    sfv=args.archive.with_suffix('.sfv');receipt['sfv_sha256']=sha(sfv)
    parts={}
    for line in sfv.read_text(encoding='ascii').splitlines():
        if not line.strip() or line.lstrip().startswith(';'):continue
        match=re.fullmatch(r'(.+?)\s+([A-Fa-f0-9]{8})\s*',line)
        if not match:raise ValueError('Invalid SFV record')
        name,crc=match.groups()
        if Path(name).name!=name or not re.search(r'\.(?:rar|r\d+)$',name,re.I) or name in parts:
            raise ValueError('Invalid or duplicate SFV part name')
        parts[name]=crc.lower()
    if args.archive.name not in parts:raise ValueError('SFV does not include the selected archive')
    for number,(name,expected) in enumerate(sorted(parts.items()),1):
        path=args.archive.parent/name;digest=hashlib.sha256();crc=0
        with path.open('rb') as f:
            while block:=f.read(8*1024*1024):digest.update(block);crc=binascii.crc32(block,crc)
        actual=f'{crc&0xffffffff:08x}'
        if actual!=expected:raise ValueError('SFV mismatch: '+name)
        receipt['parts'].append({'name':name,'bytes':path.stat().st_size,'sha256':digest.hexdigest(),'crc32':actual})
        if number%10==0 or number==len(parts):print(f'Verified archive parts {number}/{len(parts)}',flush=True)
        save()
    with (out/'extraction.log').open('w') as log:
        if args.unrar:
            binary=args.unrar.resolve()
            receipt['rar_extractor']={'path':str(binary),'sha256':sha(binary)}
            command=[str(binary),'e','-idq','-o-','-y',str(args.archive),image_name,str(out)+'/']
        else:
            command=[seven,'x','-y','-bsp0','-spd','-o'+str(out),str(args.archive),image_name]
        save()
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    image=out/image_name
    if not image.is_file() or image.stat().st_size!=expected_bytes:raise ValueError('Extracted image size differs')
    receipt['image']={'file':image.name,'bytes':expected_bytes,'sha256':sha(image),'pfs0_entries':pfs0(image)}
    receipt.update({'stage':'private_inputs_verified_and_extracted','completed_utc':datetime.now(timezone.utc).isoformat(),
                    'decryption_tested':False,'assets_converted':False,'console_modified':False})
    save();print('Verified private NSP and key input; asset conversion is the next stage',flush=True)
if __name__=='__main__':main()

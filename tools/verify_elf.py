#!/usr/bin/env python3
"""Check ELF to OELF mapping preservation, including the entire BSS extent."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def program_headers(data,start=0):
    if data[start:start+6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,start+18)[0]!=62:
        raise ValueError('Expected little-endian ELF64 x86-64')
    off=struct.unpack_from('<Q',data,start+32)[0]
    size,count=struct.unpack_from('<HH',data,start+54)
    return [struct.unpack_from('<IIQQQQQQ',data,start+off+i*size) for i in range(count)]

def tls_images(data,programs,self_entries=None):
    images=[]
    for tls in (p for p in programs if p[0]==7):
        if tls[5]>tls[6] or not tls[7] or tls[7]&(tls[7]-1):
            raise ValueError('Invalid PT_TLS size or alignment')
        offset=tls[2]
        if self_entries is not None:
            index,load=next(((i,p) for i,p in enumerate(programs)
                            if p[0] in (1,0x61000010) and p[3]<=tls[3]
                            and tls[3]+tls[5]<=p[3]+p[5]),(None,None))
            if load is None:raise ValueError('SELF TLS template is outside file-backed segments')
            entries=[e for e in self_entries if (e[0]&(1<<11)) and ((e[0]>>20)&0xffff)==index]
            if len(entries)!=1 or entries[0][2]!=load[5]:
                raise ValueError('SELF TLS requires one uncompressed segment data entry')
            offset=entries[0][1]+tls[3]-load[3]
        if offset+tls[5]>len(data):raise ValueError('Truncated PT_TLS template')
        template=data[offset:offset+tls[5]]
        images.append({'address':tls[3],'filesz':tls[5],'memsz':tls[6],'alignment':tls[7],
                       'local_exec_span':(tls[6]+tls[7]-1)&~(tls[7]-1),
                       'template_sha256':hashlib.sha256(template).hexdigest()})
    if len(images)>1:raise ValueError('Multiple PT_TLS segments')
    return images

def self_tls(path):
    data=Path(path).read_bytes()
    if len(data)<32 or struct.unpack_from('<I',data)[0]!=0x1d3d154f:
        raise ValueError('Expected a native SELF image')
    count=struct.unpack_from('<H',data,24)[0]
    entries=[struct.unpack_from('<QQQQ',data,32+i*32) for i in range(count)]
    return tls_images(data,program_headers(data,32+count*32),entries)

def compare_tls(original,converted,label):
    if len(original)!=len(converted):raise ValueError(label+' loses or adds PT_TLS')
    for a,b in zip(original,converted):
        if a['alignment']!=b['alignment']:
            raise ValueError(label+' changes PT_TLS alignment after local-exec relocation')
        if a!=b:raise ValueError(label+' changes PT_TLS layout or initialized template bytes')

def parse(path):
    data=Path(path).read_bytes()
    programs=program_headers(data)
    soff=struct.unpack_from('<Q',data,40)[0];ssize,n,strings=struct.unpack_from('<HHH',data,58)
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,soff+i*ssize) for i in range(n)] if soff else []
    names=b''
    if sections:
        s=sections[strings];names=data[s[4]:s[4]+s[5]]
    entries=[]
    for s in sections:
        name=names[s[0]:].split(b'\0',1)[0].decode()
        entries.append((name,s))
    return {'entry':struct.unpack_from('<Q',data,24)[0],'programs':programs,'sections':entries,
            'interpreters':[data[p[2]:p[2]+p[5]] for p in programs if p[0]==3],
            'tls':tls_images(data,programs),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}

def verify_initializers(original):
    for name,s in original['sections']:
        if s[2]&2 and name.startswith('.init_array.') and s[5]:
            raise ValueError('Priority constructor is outside the CRT initializer array: '+name)

def verify(elf,oelf,eboot=None):
    original=parse(elf);converted=parse(oelf)
    verify_initializers(original)
    compare_tls(original['tls'],converted['tls'],'Converted executable')
    if eboot is not None:compare_tls(original['tls'],self_tls(eboot),'SELF executable')
    if len(converted['interpreters'])!=1:
        raise ValueError('Converted executable must contain exactly one interpreter header')
    if converted['interpreters'][0]!=b'/libexec/ld-elf.so.1\0':
        raise ValueError('Converted interpreter pathname is invalid')
    if converted['entry']!=original['entry']:
        raise ValueError('Converted entry point differs from native input')
    loads=[p for p in converted['programs'] if p[0] in (1,0x61000010)]
    def covers(start,end,write=False):
        cursor=start
        for p in sorted(loads,key=lambda p:p[3]):
            if write and not (p[1]&2):continue
            lo=p[3];hi=(p[3]+p[6]+0x3fff)&~0x3fff
            if lo>cursor:break
            if hi>cursor:cursor=hi
        return cursor>=end
    for p in loads:
        if p[3]%0x4000:raise ValueError('Converted segment is not 16 KiB aligned')
    for p in original['programs']:
        if p[0]==1 and not covers(p[3],p[3]+p[6]):
            raise ValueError('Converted image loses original LOAD bytes')
    mutable=[]
    for name,s in original['sections']:
        # .got and the process parameter may become SCE RELRO after relocation.
        # All real mutable data and zero-initialized storage must stay writable.
        required=(name=='.data' or name.startswith('.data.') or name.startswith('.bss'))
        if name=='.data.sce_process_param' or name=='.data.sce_module_param':required=False
        if name=='.data.rel.ro' or name.startswith('.data.rel.ro.'):required=False
        if required and s[5] and s[2]&2:
            if not covers(s[3],s[3]+s[5],True):raise ValueError('Mutable section is not mapped RW: '+name)
            mutable.append({'section':name,'address':s[3],'bytes':s[5]})
    entry=original['entry']
    if not any(p[1]&1 and p[3]<=entry<p[3]+p[6] for p in loads):raise ValueError('Entry point is not executable')
    if not any(name=='.data.sce_process_param' for name,s in original['sections']):
        raise ValueError('Missing Orbis process parameter')
    return {'stage':'host_structure_verified','elf':str(elf),'oelf':str(oelf),
            'elf_sha256':original['sha256'],'oelf_sha256':converted['sha256'],
            'entry':entry,'mutable_sections':mutable,'all_load_bytes_mapped':True,
            'tls':original['tls'],'tls_template_preserved':True,'self_tls_verified':eboot is not None,
            'hardware_tested':False}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('oelf',type=Path)
    p.add_argument('--eboot',type=Path)
    p.add_argument('--output',type=Path);a=p.parse_args();r=verify(a.elf,a.oelf,a.eboot)
    text=json.dumps(r,indent=2)+'\n'
    if a.output:a.output.write_text(text)
    else:print(text,end='')

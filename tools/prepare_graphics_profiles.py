#!/usr/bin/env python3
"""Prepare PRIVATE, selective Remastered mods from the pinned upstream importer.

No keys are read. No console connection occurs. Existing destinations are refused.
The original ISO and complete conversion are inputs only, never modified.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PIN='c872da704adde9a7838492aaa56dac73a32053e4'
# Retail body ids documented in port_dread_samus.h. These identify Prime models;
# only existing output from Prime Remastered's importer is used here.
SAMUS_BODIES={0xF39476F2,0x776C1A96,0x57F5C930,0x2782648E,0xB333E1D7,
              0x4A1A8FD3,0xE449874A,0x57489AD3,0x891E00FD,
              0xCC25E002,0xA602C954,0x11A5BC8A,0x5644E7A1,
              0x00A6EC53,0xCBFA3FF6,0xD6FF0F4E}
HIDE_LITE='geometry,effects,hud,font,text,map,movies'
BEAM_ANCS={0x41B63D84,0xC1135EA3,0x02B4357C,0x6AB61E5E,0x5E7FF762}


def sha(path):
    with Path(path).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def u32(data,at):
    if at<0 or at+4>len(data): raise ValueError('truncated resource table')
    return struct.unpack_from('>I',data,at)[0]


def cmdl_textures(data):
    if len(data)<44 or u32(data,0)!=0xDEADBABE or u32(data,4)!=2:
        raise ValueError('not a converted Prime v2 CMDL')
    count,mats=u32(data,0x24),u32(data,0x28)
    if not 0<mats<=count<=65536: raise ValueError('invalid CMDL section counts')
    at=(44+count*4+31)&~31
    ids=set()
    for i in range(count):
        size=u32(data,44+i*4)
        if size>len(data)-at: raise ValueError('CMDL section exceeds file')
        if i<mats:
            n=u32(data,at)
            if n>65536 or 4+n*4>size: raise ValueError('invalid CMDL texture table')
            ids.update(u32(data,at+4+j*4) for j in range(n))
        at+=size
    return ids


def cmdl_material_cubes(data):
    cubes=set();at=0
    while True:
        at=data.find(b'PBR7',at)
        if at<0: return cubes
        if at<4: raise ValueError('truncated PBR7 material trailer')
        cubes.add(u32(data,at-4));at+=4


def ancs_model_skin_pairs(data,models,skins):
    # Upstream's own Rebind logic searches unaligned big-endian (model,skin)
    # pairs. Requiring BOTH typed resource ids avoids guessing dependencies from
    # arbitrary 32-bit values; no game logic, animation, or collision is rewritten.
    pairs=set()
    for at in range(max(0,len(data)-11)):
        model,skin=struct.unpack_from('>II',data,at)
        if model in models and skin in skins: pairs.add((model,skin))
    return pairs


def pak_model_ids(data):
    if u32(data,0)!=0x00030005: raise ValueError('not a supported original PAK')
    n=u32(data,8);at=12
    if n>100000: raise ValueError('too many PAK names')
    for _ in range(n):
        length=u32(data,at+8);at+=12+length
        if at>len(data): raise ValueError('truncated PAK names')
    count=u32(data,at);at+=4
    if count>100000 or count*20>len(data)-at: raise ValueError('truncated PAK resource table')
    return {u32(data,at+i*20+8) for i in range(count)
            if data[at+i*20+4:at+i*20+8]==b'CMDL'}


def samus_gun_models(iso):
    with Path(iso).open('rb') as f:
        f.seek(0x424);offset,size=struct.unpack('>II',f.read(8))
        if size>32*1024*1024: raise ValueError('unexpected ISO filesystem size')
        f.seek(offset);fst=f.read(size)
        count=u32(fst,8);names=count*12
        if count>1000000 or names>len(fst): raise ValueError('invalid ISO filesystem')
        for i in range(1,count):
            entry,offset,size=struct.unpack_from('>III',fst,i*12)
            if entry>>24: continue
            start=names+(entry&0xffffff)
            end=fst.find(b'\0',start)
            if end<0: raise ValueError('unterminated ISO filename')
            if fst[start:end].lower()==b'samusgun.pak':
                if size>64*1024*1024: raise ValueError('unexpected SamusGun PAK size')
                f.seek(offset);return pak_model_ids(f.read(size))
    raise ValueError('supported original ISO has no SamusGun.pak')


def resource_index(source):
    result={}
    for path in Path(source).iterdir():
        if path.is_symlink(): raise ValueError('symlink in input mod')
        match=re.fullmatch(r'([0-9a-fA-F]{8})\.(CMDL|CSKR|ANCS|TXTR|dds)',path.name)
        if match and path.is_file(): result[(int(match[1],16),match[2])]=path
    return result


def table_skins():
    text=(ROOT/'game/MetroidPrimePort/platform/port_remastered_table.cpp').read_text()
    all_skins=[int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]+)',text.split('kSkins[] = {',1)[1].split('};',1)[0])]
    result={}
    for line in text.splitlines():
        match=re.match(r'\s*\{0x([0-9A-Fa-f]{8}),.*\}, (\d+), (\d+), -?\d+, (?:true|false), 0x[0-9A-Fa-f]+, -?\d+',line)
        if match:
            model,first,count=int(match[1],16),int(match[2]),int(match[3])
            result.setdefault(model,set()).update(all_skins[first:first+count])
    return result


def select_resources(source,seeds,extra_models=False,skin_table=None,seed_ancs=None):
    index=resource_index(source)
    models={key[0] for key in index if key[1]=='CMDL'}
    skins={key[0] for key in index if key[1]=='CSKR'}
    selected_models=set(seeds)&models
    if extra_models:
        # Enhanced starts with all converted character/object models, still with
        # original room geometry and original effects. Room geometry is a later
        # explicitly separate experiment, not silently enabled by this profile.
        selected_models=models.copy()
    ancs={key[0]:ancs_model_skin_pairs(path.read_bytes(),models,skins)
          for key,path in index.items() if key[1]=='ANCS'}
    selected_ancs=set(BEAM_ANCS if seed_ancs is None else seed_ancs)&set(ancs);selected_skins=set()
    bindings=table_skins() if skin_table is None else skin_table
    while True:
        before=(len(selected_models),len(selected_ancs),len(selected_skins))
        for ident,pairs in ancs.items():
            if ident in selected_ancs or any(model in selected_models for model,_ in pairs):
                selected_ancs.add(ident)
                selected_models.update(model for model,_ in pairs)
                selected_skins.update(skin for _,skin in pairs)
        if before==(len(selected_models),len(selected_ancs),len(selected_skins)): break
    for model in selected_models: selected_skins.update(bindings.get(model,set())&skins)
    selected={(ident,'CMDL') for ident in selected_models}
    selected.update((ident,'CSKR') for ident in selected_skins)
    selected.update((ident,'ANCS') for ident in selected_ancs)
    textures=set()
    for ident in selected_models: textures.update(cmdl_textures(index[ident,'CMDL'].read_bytes()))
    missing=[]
    for ident in textures:
        if (ident,'TXTR') in index: selected.add((ident,'TXTR'))
        else: missing.append(f'{ident:08X}.TXTR (retail fallback required)')
        if (ident,'dds') in index: selected.add((ident,'dds'))
    support=[]
    for ident in selected_models:
        for cube in cmdl_material_cubes(index[ident,'CMDL'].read_bytes()):
            path=Path(source)/f'{cube:08X}.envcube'
            if path.is_file() and not path.is_symlink(): support.append(path)
            else: missing.append(f'{cube:08X}.envcube (default material reflection fallback required)')
    brdf=Path(source)/'roomenv/brdf.lut'
    if selected_models and brdf.is_file() and not brdf.is_symlink(): support.append(brdf)
    placement=Path(source)/'retailplacements.bin'
    if extra_models and placement.is_file() and not placement.is_symlink(): support.append(placement)
    return [index[key] for key in sorted(selected)]+sorted(set(support)),dict(models=len(selected_models),skins=len(selected_skins),
        character_sets=len(selected_ancs),textures=len(textures),retail_texture_fallbacks=sorted(missing),
        requested_models=len(seeds),available_requested_models=len(set(seeds)&models),
        missing_requested_models=[f'{i:08X}' for i in sorted(set(seeds)-models)])


def importer_version(header):
    text=Path(header).read_text()
    values=[int(re.search(r'inline constexpr int '+n+r' = (\d+);',text)[1])
        for n in ('kConverter','kTextures','kModels','kEffects','kRooms','kRoomModels','kText','kHud','kMovies','kGallery')]
    base=int(re.search(r'kImportVersion = (\d+) \+',text)[1])
    return base+sum(values)


def prepare(source,iso,out,converter,private_root=None,max_lite_bytes=512*1024*1024):
    source,out=Path(source).resolve(),Path(out).absolute()
    root=(Path(private_root) if private_root else ROOT/'private').resolve()
    if not out.resolve().is_relative_to(root): raise ValueError('proprietary outputs must remain under private/')
    if out.exists(): raise ValueError('refusing to overwrite an existing profile set')
    if not source.is_relative_to(root) or source.is_symlink(): raise ValueError('conversion must be a private regular directory')
    expected=importer_version(ROOT/'game/MetroidPrimePort/platform/include/port_remastered_import.h')
    stamp=source/'.import-version'
    if not stamp.is_file() or int(stamp.read_text().splitlines()[0])!=expected:
        raise ValueError('converted asset version does not match pinned importer')
    seeds=SAMUS_BODIES|samus_gun_models(iso)
    out.parent.mkdir(parents=True,exist_ok=True)
    staging=Path(tempfile.mkdtemp(prefix='.'+out.name+'-',dir=out.parent))
    try:
        profiles=[]
        for name,enhanced in [('original-hd',False),('remastered-lite',False),('remastered-enhanced',True)]:
            profile=staging/name;mod=profile/'mods/remastered-models'
            (profile/'mods').mkdir(parents=True)
            paths=[];coverage={}
            if name!='original-hd':
                paths,coverage=select_resources(source,seeds,extra_models=enhanced)
                mod.mkdir()
                shutil.copyfile(stamp,mod/stamp.name)
            total=sum(p.stat().st_size for p in paths)
            if name=='remastered-lite' and total>max_lite_bytes: raise ValueError('Lite asset footprint exceeds configured budget')
            catalog=[]
            for path in paths:
                relative=path.relative_to(source)
                target=mod/relative;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(path,target)
                catalog.append(dict(path='mods/remastered-models/'+relative.as_posix(),size=path.stat().st_size,sha256=sha(path)))
            env={'MP_REMASTERED_HIDE':HIDE_LITE,'MP_REMASTERED_GEOMETRY':'none','MP_REMASTERED_EFFECTS':'0',
                'MP_REMASTERED_EFFECT_ACTORS':'0','MP_REMASTERED_MOVIES':'0','MP_REMASTERED_GALLERY':'0'}
            if name!='original-hd':
                env.update(MP_TEXTURE_MAX_DIM='2048' if enhanced else '1024',MP_TEXTURE_CACHE_MB='512' if enhanced else '256')
            else: env['MP_REMASTERED_HIDE']='geometry,effects,models,hud,font,text,map,movies'
            rows=[(row['path'],row['size']) for row in catalog]
            if paths: rows.append(('mods/remastered-models/.import-version',stamp.stat().st_size))
            index=''.join(f'{size} {path}\n' for path,size in sorted(rows))
            if len(rows)>8192 or any(len(line)>511 for line in index.splitlines()): raise ValueError('runtime asset index exceeds bounds')
            (profile/'prime-assets-index.txt').write_text(index)
            marker=f'PROJECT_PRIME_ASSETS_V1\n{name}\n{PIN}\n{expected}\n'
            (profile/'prime-assets.txt').write_text(marker)
            manifest=dict(schema=1,profile=name,runtime_index_sha256=sha(profile/'prime-assets-index.txt'),runtime_marker_sha256=sha(profile/'prime-assets.txt'),upstream=PIN,import_version=expected,converter_sha256=sha(converter),
                source_stamp_sha256=sha(stamp),selection_version=1,proprietary_assets=bool(paths),not_for_distribution=True,
                hardware_tested=False,original_room_geometry=True,original_effects=True,movies=False,
                models_only=True,asset_bytes=total,asset_files=len(catalog),largest_file_bytes=max((p.stat().st_size for p in paths),default=0),
                gpu_residency='not measured; bounded native texture cache is a cache limit, not total GPU memory',
                runtime_environment=env,coverage=coverage,files=catalog)
            (profile/'profile.json').write_text(json.dumps(manifest,indent=2)+'\n')
            profiles.append({k:manifest[k] for k in ('profile','asset_bytes','asset_files','coverage')})
        (staging/'receipt.json').write_text(json.dumps(dict(schema=1,profiles=profiles,console_modified=False,
            importer_executable=str(Path(converter).resolve()),importer_sha256=sha(converter),
            keys_read=False,keys_copied=False),indent=2)+'\n')
        staging.rename(out)
    except Exception:
        shutil.rmtree(staging);raise
    return profiles


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'private/remastered-conversion-v1/mods/remastered-models')
    parser.add_argument('--iso',type=Path,default=ROOT/'private/prepared/MetroidPrime.iso')
    parser.add_argument('--out',type=Path,default=ROOT/'private/graphics-profiles-v1')
    parser.add_argument('--converter',type=Path,default=ROOT/'build/pc-asset-importer/importer/metroid_prime_port')
    args=parser.parse_args()
    for row in prepare(args.source,args.iso,args.out,args.converter): print(json.dumps(row,sort_keys=True))
if __name__=='__main__': main()

#!/usr/bin/env python3
"""Verify a private selective profile before copying it to application storage."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path,PurePosixPath
import re
import struct


def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def dds_info(data):
    if len(data)<148 or data[:4]!=b'DDS ' or struct.unpack_from('<I',data,4)[0]!=124 or data[84:88]!=b'DX10':
        raise ValueError('not a native DX10 DDS')
    height,width=struct.unpack_from('<II',data,12)
    mips=struct.unpack_from('<I',data,28)[0]
    fmt,dimension,misc,array_size,_=struct.unpack_from('<IIIII',data,128)
    if not (0<width<=16384 and 0<height<=16384) or not 0<mips<=15 or dimension!=3 or array_size!=1:
        raise ValueError('unsupported dimensions or texture array')
    if fmt not in (71,72,77,78,80,81,83,84,98,99): raise ValueError('unsupported BC texture format')
    block=8 if fmt in (71,72,80,81) else 16
    expected=148
    for level in range(mips): expected+=max(1,(max(1,width>>level)+3)//4)*max(1,(max(1,height>>level)+3)//4)*block
    # The importer writes 2D textures with complete mip chains. Reject truncated
    # payloads rather than letting the console discover them during a room load.
    if mips!=max(width,height).bit_length(): raise ValueError('DDS is missing a complete mip chain')
    if expected!=len(data): raise ValueError('DDS payload length does not match mip chain')
    return dict(width=width,height=height,mips=mips,format=fmt,bytes=len(data))


def verify(profile):
    profile=Path(profile).resolve()
    manifest=json.loads((profile/'profile.json').read_text())
    if manifest.get('schema')!=1 or manifest.get('selection_version')!=1: raise ValueError('unsupported profile metadata')
    marker=profile/'prime-assets.txt'
    marker_text=f"PROJECT_PRIME_ASSETS_V1\n{manifest['profile']}\n{manifest['upstream']}\n{manifest['import_version']}\n"
    if not marker.is_file() or marker.read_text()!=marker_text or sha(marker)!=manifest['runtime_marker_sha256']:
        raise ValueError('runtime compatibility marker mismatch')
    files=manifest['files'];expected=set();counts=Counter();formats=Counter();max_dim=0;total=0
    room_ids=set(manifest.get('selected_rooms',[]))
    if room_ids:
        if any(not re.fullmatch(r'[0-9A-F]{8}',ident) for ident in room_ids): raise ValueError('invalid selected room id')
        selection=profile/'room-selection.json'
        if not selection.is_file() or sha(selection)!=manifest.get('room_selection_manifest_sha256'):
            raise ValueError('selected room manifest mismatch')
        report=json.loads(selection.read_text())
        if report.get('selection')!='selected-remastered-rooms-v1' or {room['mrea'] for room in report.get('selected_rooms',[])}!=room_ids:
            raise ValueError('selected room report mismatch')
    for row in files:
        rel=PurePosixPath(row['path'])
        if rel.is_absolute() or '..' in rel.parts or rel.parts[:2]!=('mods','remastered-models'):
            raise ValueError('unsafe asset path')
        path=profile/rel
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(profile): raise ValueError('missing or linked asset')
        if str(rel) in expected: raise ValueError('duplicate asset')
        expected.add(str(rel))
        if path.stat().st_size!=row['size'] or sha(path)!=row['sha256']: raise ValueError('asset checksum mismatch: '+str(rel))
        total+=row['size'];counts[path.suffix]+=1
        if path.suffix=='.dds':
            info=dds_info(path.read_bytes());formats[info['format']]+=1;max_dim=max(max_dim,info['width'],info['height'])
        effects=('.PART','.SWHC','.ELSC','.DPSC','.WPSC','.CRSC','.VECF') if manifest['profile']=='remastered-consolidated' else ()
        scoped_room=(room_ids and ((path.suffix=='.roomgeo' and rel.parent.as_posix()=='mods/remastered-models/roomgeo' and path.stem in room_ids)
            or (path.suffix=='.roomenv' and rel.parent.as_posix()=='mods/remastered-models/roomenv' and path.stem in room_ids)
            or str(rel)=='mods/remastered-models/roomgeo/lods.bin'))
        if path.suffix not in effects+('.CMDL','.CSKR','.ANCS','.TXTR','.dds','.envcube') and not scoped_room and str(rel) not in ('mods/remastered-models/roomenv/brdf.lut','mods/remastered-models/retailplacements.bin','mods/remastered-models/effectids.map'): raise ValueError('unexpected runtime resource type')
    stamp=profile/'mods/remastered-models/.import-version'
    if files:
        if not stamp.is_file() or sha(stamp)!=manifest['source_stamp_sha256']: raise ValueError('importer stamp mismatch')
        if int(stamp.read_text().splitlines()[0])!=manifest['import_version']: raise ValueError('importer version mismatch')
        expected.add('mods/remastered-models/.import-version')
    actual={str(p.relative_to(profile)) for p in (profile/'mods').rglob('*') if p.is_file()}
    if actual!=expected: raise ValueError('unexpected or missing assets in profile directory')
    index=profile/'prime-assets-index.txt'
    rows=[(row['path'],row['size']) for row in files]
    if files: rows.append(('mods/remastered-models/.import-version',stamp.stat().st_size))
    expected_index=''.join(f'{size} {path}\n' for path,size in sorted(rows))
    if len(rows)>(16384 if manifest['profile']=='remastered-consolidated' else 8192) or any(len(line)>511 for line in expected_index.splitlines()): raise ValueError('runtime asset index exceeds bounds')
    if not index.is_file() or index.read_text()!=expected_index or sha(index)!=manifest['runtime_index_sha256']:
        raise ValueError('runtime asset index mismatch')
    if total!=manifest['asset_bytes'] or len(files)!=manifest['asset_files']: raise ValueError('profile footprint mismatch')
    return dict(profile=manifest['profile'],files=len(files),bytes=total,types=dict(counts),
        dds_formats=dict(formats),maximum_texture_dimension=max_dim,manifest_sha256=sha(profile/'profile.json'),
        hardware_tested=False,source='verified private converted files; not GPU rendering acceptance')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('profile',type=Path)
    args=parser.parse_args();print(json.dumps(verify(args.profile),indent=2))
if __name__=='__main__': main()

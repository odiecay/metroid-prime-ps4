#!/usr/bin/env python3
"""Prepare the original nine front-end movies from an already verified own ISO.

Uses the existing GamePrep disc/FST validation and JPEG/THP importer. The game
image is read in place, never copied. All converted game data stays with the
user; this tool never uploads or installs anything.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import time

from prepare_data import inspect_iso, sha
from prepare_menu_movie import prepare as prepare_movie

CORE_MOVIES=(
    '00_first_start.thp','01_startloop.thp',
    '02_start_fileselect_A.thp','02_start_fileselect_B.thp','02_start_fileselect_C.thp',
    '03_fileselectloop.thp',
    '04_fileselect_playgame_A.thp','04_fileselect_playgame_B.thp','04_fileselect_playgame_C.thp',
)


def disc_files(iso):
    """Resolve paths in the FST already checked by GamePrep.inspect_iso."""
    with iso.open('rb')as f:
        f.seek(0x424);offset,size=struct.unpack('>II',f.read(8))
        f.seek(offset);table=f.read(size)
    count=struct.unpack_from('>I',table,8)[0];names=table[count*12:]
    stack=[('',count,0)];files={}
    for i in range(1,count):
        while i>=stack[-1][1]:stack.pop()
        kind,start,length=struct.unpack_from('>III',table,i*12)
        name_offset=kind&0xffffff;end=names.index(0,name_offset)
        name=names[name_offset:end].decode('ascii')
        if not name or name in ('.','..') or '/'in name or '\\'in name:
            raise ValueError('Unsafe disc filename')
        path=stack[-1][0]+name
        if kind>>24:
            if start!=stack[-1][2]:raise ValueError('FST directory parent mismatch')
            stack.append((path+'/',length,i))
        else:
            key=path.lower()
            if key in files:raise ValueError('Duplicate disc filename')
            files[key]={'path':path,'offset':start,'bytes':length}
    return files


def prepare(iso,output,tool=None,ffmpeg='ffmpeg',progress=None):
    root=Path(__file__).resolve().parents[1]
    iso=Path(iso).resolve();output=Path(output).resolve()
    tool=Path(tool or root/'build/pc-asset-importer/prime_menu_movie').resolve()
    if output.exists():raise ValueError('Output exists; choose a fresh directory')
    identity=inspect_iso(iso);files=disc_files(iso)
    selected=[files['video/'+name.lower()]for name in CORE_MOVIES]
    output.parent.mkdir(parents=True,exist_ok=True)
    started=time.perf_counter();rows=[]
    with tempfile.TemporaryDirectory(prefix='.menu-upscale-',dir=output.parent)as temp:
        temp=Path(temp);sources=temp/'source';sources.mkdir()
        ready=temp/'ready';video=ready/'hd-menu/Video';video.mkdir(parents=True)
        logs=ready/'logs';logs.mkdir();receipts=ready/'receipts';receipts.mkdir()
        # Extract only these videos, logging their exact original extents/hashes.
        with iso.open('rb')as f:
            for item in selected:
                source=sources/Path(item['path']).name;f.seek(item['offset'])
                digest=hashlib.sha256();remaining=item['bytes']
                with source.open('xb')as dest:
                    while remaining:
                        data=f.read(min(1024*1024,remaining))
                        if not data:raise ValueError('Disc movie was truncated')
                        dest.write(data);digest.update(data);remaining-=len(data)
                item['sha256']=digest.hexdigest()
        for index,(name,item)in enumerate(zip(CORE_MOVIES,selected),1):
            if progress:progress(index,len(selected),name)
            source=sources/Path(item['path']).name;target=video/name
            try:
                row=prepare_movie(source,target,1.5,tool,ffmpeg,False)
            except Exception as error:
                failure=Path(tempfile.mkdtemp(prefix=output.name+'.failed-',dir=output.parent))
                shutil.copytree(logs,failure/'logs')
                sidecar=target.with_suffix('.thp.ffmpeg.log')
                if sidecar.is_file():shutil.copy2(sidecar,failure/'logs'/sidecar.name)
                (failure/'error.txt').write_text(f'{name}: {error}\n')
                raise RuntimeError(f'Movie preparation failed for{name}: {error}; logs retained at{failure}')from error
            (logs/(name+'.log')).write_text(json.dumps(row,indent=2)+'\n')
            meta=target.with_suffix('.thp.json')
            if row['width']!=960 or row['height']!=720 or not row['all_frames_decoded']or not row['plane_guards_intact']:
                raise ValueError('Prepared movie did not pass original-menu960x720 decoder validation')
            if row['audio_components']or row['audio_bytes']or row['audio_crc32']!='00000000':
                raise ValueError('Original front-end audio identity differs')
            row.update(source=item,output='hd-menu/Video/'+name,source_letterbox_preserved=True,
                       picture_band={'source':[0,60,640,360],'prepared':[0,90,960,540]},
                       output_sha256=sha(target),native_bundled_movie_directory='/app0/menus/hd-menu/Video')
            row['source_sha256']=item['sha256'];row.pop('ffmpeg_argv',None)
            (receipts/(name+'.json')).write_text(json.dumps(row,indent=2)+'\n')
            meta.unlink();target.with_suffix('.thp.ffmpeg.log').rename(logs/(name+'.ffmpeg.log'))
            rows.append(row)
        receipt={'schema':1,'prepared_by':'Project Prime PC menu preparation',
                 'disc':identity,'iso_sha256':sha(iso),'tool_sha256':sha(tool),
                 'scale':1.5,'width':960,'height':720,'cleanup':False,
                 'aspect_stretched':False,'source_letterbox_preserved':True,
                 'display_framing_unchanged':True,'source_pixels':640*480,
                 'prepared_pixels':960*720,'pixel_ratio':2.25,
                 'clips':rows,'total_movie_bytes':sum((video/name).stat().st_size for name in CORE_MOVIES),
                 'total_frames':sum(x['frames']for x in rows),'all_movies_decoded':True,
                 'host_elapsed_seconds':time.perf_counter()-started,
                 'native_bundled_movie_directory':'/app0/menus/hd-menu/Video',
                 'ps4_playback_acceptance':'pending hardware test; host decode is not a PS4 FPS result',
                 'upscale_does_not_recover_missing_source_detail':True,
                 'excluded_movies':'GBA, gallery, attract, credits and ending movies retain original assets',
                 'contains_converted_user_game_assets':True}
        (ready/'menu-movies.json').write_text(json.dumps(receipt,indent=2)+'\n')
        # Publish only a complete validated tree; original ISO remains untouched.
        if output.exists():raise ValueError('Output appeared during conversion; retained existing directory')
        ready.rename(output)
    return receipt


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('iso',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--tool',type=Path);p.add_argument('--ffmpeg',default='ffmpeg');a=p.parse_args()
    def progress(index,total,name):print(f'[{index}/{total}] {name}',flush=True)
    receipt=prepare(a.iso,a.output,a.tool,a.ffmpeg,progress)
    print(json.dumps({'output':str(a.output),'clips':len(receipt['clips']),
                      'bytes':receipt['total_movie_bytes'],'frames':receipt['total_frames'],
                      'all_movies_decoded':receipt['all_movies_decoded']},indent=2))


if __name__=='__main__':main()

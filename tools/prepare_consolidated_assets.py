#!/usr/bin/env python3
"""Prepare the single private Remastered set; never writes to the console."""
from pathlib import Path
import hashlib,json,shutil
from verify_graphics_profile import verify
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/remastered-conversion-v1/mods/remastered-models'
OUT=ROOT/'private/consolidated-remastered-v1'
PIN='c872da704adde9a7838492aaa56dac73a32053e4'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    if OUT.exists():raise SystemExit('Output exists; preserve prepared assets')
    mod=OUT/'mods/remastered-models';mod.mkdir(parents=True)
    allowed={'.CMDL','.CSKR','.ANCS','.TXTR','.dds','.envcube','.PART','.SWHC','.ELSC','.DPSC','.WPSC','.CRSC','.VECF'}
    selected=[p for p in SOURCE.iterdir() if p.is_file() and (p.suffix in allowed or p.name in ('retailplacements.bin','effectids.map'))]
    selected.append(SOURCE/'roomenv/brdf.lut')
    files=[]
    for p in sorted(selected):
        if p.is_symlink():raise ValueError('Symlink in source')
        rel=p.relative_to(SOURCE);dest=mod/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,dest)
        files.append(dict(path=str(dest.relative_to(OUT)),size=dest.stat().st_size,sha256=sha(dest)))
    stamp=mod/'.import-version';shutil.copyfile(SOURCE/'.import-version',stamp)
    marker=OUT/'prime-assets.txt';marker.write_text(f'PROJECT_PRIME_ASSETS_V1\nremastered-consolidated\n{PIN}\n520\n')
    index=OUT/'prime-assets-index.txt'
    rows=[(f['path'],f['size']) for f in files]+[('mods/remastered-models/.import-version',stamp.stat().st_size)]
    index.write_text(''.join(f'{size} {path}\n' for path,size in sorted(rows)))
    metadata=dict(schema=1,selection_version=1,profile='remastered-consolidated',upstream=PIN,import_version=520,
        source_stamp_sha256=sha(stamp),runtime_marker_sha256=sha(marker),runtime_index_sha256=sha(index),
        asset_bytes=sum(f['size'] for f in files),asset_files=len(files),files=files,
        source_conversion='private/remastered-conversion-v1',converter_sha256='681961dad25df3ebe01ef0a0f3cc011783215f21b9d5bb78789b3f271a3aaf05',
        proprietary_assets=True,not_for_distribution=True,hardware_tested=False,
        components=dict(models=True,textures=True,materials=True,converted_particle_effects=True,room_geometry=False,room_lighting=False),
        exclusions='Room geometry requires a separate 10.61 GB dependency set; matching room environment set is 3.66 GB. Neither is included partially. Existing controller controls can disable models/effects independently.',
        texture_dimension_cap=0,texture_cache_mib=512)
    (OUT/'profile.json').write_text(json.dumps(metadata,indent=2)+'\n')
    result=verify(OUT)
    (ROOT/'evidence/consolidation/remastered-assets-verified.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='types'},indent=2))
if __name__=='__main__':main()

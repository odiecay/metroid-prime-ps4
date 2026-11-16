#!/usr/bin/env python3
"""Prepare a PRIVATE, complete selected-room overlay from the existing conversion.

The pinned game's real geometry/environment parsers select models and LODs. Their
converted material tables select texture dependencies. No keys, conversion,
console access, gameplay changes, or modification of existing assets occurs.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from prepare_graphics_profiles import cmdl_material_cubes, cmdl_textures, importer_version
from verify_graphics_profile import dds_info, verify

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'private/remastered-conversion-v1/mods/remastered-models'
PIN = 'c872da704adde9a7838492aaa56dac73a32053e4'


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def scanner():
    sources = [ROOT / 'tools/room_asset_manifest.cpp']
    sources += [ROOT / 'game/MetroidPrimePort/platform' / name for name in
                ('port_room_geo_file.cpp', 'port_room_env_file.cpp')]
    headers = sorted((ROOT / 'game/MetroidPrimePort/platform/include').glob('port_*.h'))
    identity = hashlib.sha256(''.join(sha(p) for p in sources + headers).encode()).hexdigest()
    build = ROOT / 'build/room-assets'
    build.mkdir(parents=True, exist_ok=True)
    binary = build / 'room_asset_manifest'
    receipt = build / 'scanner-receipt.json'
    if not (binary.is_file() and receipt.is_file() and
            json.loads(receipt.read_text()).get('source_hash') == identity and
            json.loads(receipt.read_text()).get('binary_sha256') == sha(binary)):
        subprocess.run(['g++-14', '-std=c++20', '-O2',
                        '-I' + str(ROOT / 'game/MetroidPrimePort/platform/include'),
                        *map(str, sources), '-o', str(binary)], check=True)
        receipt.write_text(json.dumps(dict(source_hash=identity, binary_sha256=sha(binary),
            sources={str(p.relative_to(ROOT)): sha(p) for p in sources},
            compiler=subprocess.check_output(['g++-14', '--version'], text=True).splitlines()[0]), indent=2) + '\n')
    return binary, json.loads(receipt.read_text())


def regular(path, root):
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root):
        raise ValueError('missing or unsafe asset: ' + str(path))
    return path


def find_resource(source, ident, extension):
    # These are precisely the two folders the upstream room importer writes.
    candidates = [source / 'roomgeo' / f'{ident:08X}.{extension}', source / f'{ident:08X}.{extension}']
    found = [regular(p, source) for p in candidates if p.exists() or p.is_symlink()]
    if not found:
        return None
    if len(found) == 2 and sha(found[0]) != sha(found[1]):
        raise ValueError(f'conflicting resource id {ident:08X}.{extension}')
    return found[0]


def prepare(source, out, rooms, *, base=None, max_added_bytes=1024**3, private_root=None):
    private = Path(private_root or ROOT / 'private').resolve()
    source, out = Path(source).resolve(), Path(out).absolute()
    if not source.is_relative_to(private) or not source.is_dir():
        raise ValueError('input conversion must be under private/')
    if not out.resolve().is_relative_to(private) or out.exists():
        raise ValueError('refusing existing output or output outside private/')
    if max_added_bytes <= 0:
        raise ValueError('asset budget must be positive')
    rooms = sorted(set(rooms))
    if not rooms or any(not 0 <= ident <= 0xffffffff for ident in rooms):
        raise ValueError('select at least one valid MREA id')
    stamp = regular(source / '.import-version', source)
    expected = importer_version(ROOT / 'game/MetroidPrimePort/platform/include/port_remastered_import.h')
    if int(stamp.read_text().splitlines()[0]) != expected:
        raise ValueError('conversion version differs from the pinned importer')
    base_manifest = None
    if base:
        base = Path(base).resolve()
        if not base.is_relative_to(private):
            raise ValueError('base profile must remain under private/')
        verify(base)
        base_manifest = json.loads((base / 'profile.json').read_text())
    binary, parser_receipt = scanner()
    out.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.' + out.name + '-', dir=out.parent))
    try:
        args = [str(binary), str(regular(source / 'roomgeo/lods.bin', source)),
                str(staging / 'selected-lods.bin'), str(staging / 'scan.json')]
        selected = set()
        for ident in rooms:
            geo = regular(source / f'roomgeo/{ident:08X}.roomgeo', source)
            env = regular(source / f'roomenv/{ident:08X}.roomenv', source)
            args.extend(map(str, (geo, env)))
            selected.update((geo, env))
        subprocess.run(args, check=True, capture_output=True, text=True)
        parsed = json.loads((staging / 'scan.json').read_text())
        models = set()
        for ident, room in zip(rooms, parsed['rooms']):
            room['mrea'] = f'{ident:08X}'
            models.update(room['models'])
            room['models'] = [f'{model:08X}' for model in room['models']]
        textures, cubes = set(), set()
        for ident in sorted(models):
            path = find_resource(source, ident, 'CMDL')
            if path is None:
                raise ValueError(f'incomplete room: missing model {ident:08X}.CMDL')
            selected.add(path)
            data = path.read_bytes()
            textures.update(cmdl_textures(data))
            cubes.update(cmdl_material_cubes(data))
        native_maps, native_map_bytes = 0, 0
        txtr_fallbacks = []
        for ident in sorted(textures):
            path = find_resource(source, ident, 'TXTR')
            if path is None:
                raise ValueError(f'incomplete room: missing texture {ident:08X}.TXTR')
            selected.add(path)
            dds = find_resource(source, ident, 'dds')
            if dds:
                info = dds_info(dds.read_bytes())
                native_map_bytes += info['bytes'] - 148
                native_maps += 1
                selected.add(dds)
            else:
                txtr_fallbacks.append(f'{ident:08X}')
        for ident in sorted(cubes):
            if ident in (0, 0xffffffff):
                continue
            path = find_resource(source, ident, 'envcube')
            if path is None:
                raise ValueError(f'incomplete room: missing material cube {ident:08X}.envcube')
            selected.add(path)
        selected.add(regular(source / 'roomenv/brdf.lut', source))
        rows = []
        for path in sorted(selected):
            rows.append(dict(path='mods/remastered-models/' + path.relative_to(source).as_posix(),
                             size=path.stat().st_size, sha256=sha(path)))
        lod = staging / 'selected-lods.bin'
        rows.append(dict(path='mods/remastered-models/roomgeo/lods.bin', size=lod.stat().st_size, sha256=sha(lod)))
        total = sum(row['size'] for row in rows)
        if total > max_added_bytes:
            raise ValueError(f'selected rooms exceed budget: {total} > {max_added_bytes}')
        mod = staging / 'mods/remastered-models'
        if base:
            shutil.copytree(base / 'mods', staging / 'mods')
        else:
            mod.mkdir(parents=True)
        for path, row in zip(sorted(selected), rows):
            target = staging / row['path']
            if target.exists() and sha(target) != row['sha256']:
                raise ValueError('selected room conflicts with base profile: ' + row['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        (mod / 'roomgeo').mkdir(exist_ok=True)
        shutil.copyfile(lod, mod / 'roomgeo/lods.bin')
        shutil.copyfile(stamp, mod / '.import-version')
        manifest = dict(schema=1, selection='selected-remastered-rooms-v1', upstream=PIN,
            import_version=expected, proprietary_assets=True, not_for_distribution=True,
            hardware_tested=False, parser=parser_receipt, selected_rooms=parsed['rooms'],
            models_including_lods=len(models), textures=len(textures), native_dds_maps=native_maps,
            native_dds_block_bytes=native_map_bytes, txtr_only_fallbacks=txtr_fallbacks,
            selected_bytes=total, selected_files=len(rows), files=rows,
            original_collision_and_gameplay=True, room_effect_actors=False, remastered_liquid_surfaces=False,
            memory_limitations='DDS blocks, baked maps and probes are inventories, not measured peak GPU residency. Baked-lighting grid expansion and simultaneously loaded neighbouring rooms add memory. CPU parser acceptance is not visual or hardware acceptance.',
            activation='Restart with geometry unhidden in remastered_hide and MP_ROOM_GEO=1. MP_REMASTERED_GEOMETRY is import-time only. Only selected MREA files replace rooms; missing rooms retain original geometry. MP_ROOM_GEO=0 disables replacement; restart hiding geometry restores original room assets.')
        (staging / 'room-selection.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if base_manifest:
            combined = {row['path']: row for row in base_manifest['files']}
            combined.update({row['path']: row for row in rows})
            base_manifest['files'] = [combined[key] for key in sorted(combined)]
            base_manifest['asset_files'] = len(combined)
            base_manifest['asset_bytes'] = sum(row['size'] for row in combined.values())
            base_manifest['components'].update(room_geometry=True, room_lighting=True)
            base_manifest['selected_rooms'] = [f'{ident:08X}' for ident in rooms]
            base_manifest['room_selection_manifest_sha256'] = sha(staging / 'room-selection.json')
            base_manifest['exclusions'] = 'Unselected rooms, Remastered room particle actors and liquid surfaces; original gameplay and collision retained. Selected geometry and room lighting remain hardware-unverified.'
            marker = staging / 'prime-assets.txt'
            shutil.copyfile(base / 'prime-assets.txt', marker)
            index_rows = [(row['path'], row['size']) for row in base_manifest['files']]
            index_rows.append(('mods/remastered-models/.import-version', stamp.stat().st_size))
            index = staging / 'prime-assets-index.txt'
            index.write_text(''.join(f'{size} {path}\n' for path, size in sorted(index_rows)))
            base_manifest['runtime_index_sha256'] = sha(index)
            base_manifest['hardware_tested'] = False
            (staging / 'profile.json').write_text(json.dumps(base_manifest, indent=2) + '\n')
            verify(staging)
        else:
            (staging / 'prime-room-assets-index.txt').write_text(''.join(
                f'{row["size"]} {row["path"]}\n' for row in sorted(rows, key=lambda row: row['path'])))
        (staging / 'scan.json').unlink()
        lod.unlink()
        os.replace(staging, out)
        return manifest
    except BaseException:
        shutil.rmtree(staging)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--room', action='append', required=True, help='MREA id in hexadecimal; repeat for each room')
    parser.add_argument('--base', type=Path, help='Optional verified consolidated profile to copy and extend')
    parser.add_argument('--max-added-mib', type=int, default=1024)
    args = parser.parse_args()
    if any(not re.fullmatch(r'(?:0x)?[a-fA-F0-9]{1,8}', ident) for ident in args.room):
        parser.error('room ids must be hexadecimal MREA ids')
    result = prepare(args.source, args.out, [int(ident, 16) for ident in args.room],
                     base=args.base, max_added_bytes=args.max_added_mib * 1024**2)
    print(json.dumps({key: value for key, value in result.items() if key not in ('files', 'parser')}, indent=2))


if __name__ == '__main__':
    main()

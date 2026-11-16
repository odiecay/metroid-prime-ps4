#!/usr/bin/env python3
"""Assemble and independently read back a versioned native development PKG.

Never installs or launches anything. Private artwork is an explicit local input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
from datetime import datetime, timezone

from verify_elf import verify
from package_assets import add_asset_tree, archive_sources
from verify_graphics_profile import verify as verify_private_assets

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def startup_modules(lock, root=ROOT):
    required={'libc.prx','libSceFios2.prx'}
    result={}
    for item in lock.get('native_startup_modules',[]):
        path=(root/item['path']).resolve()
        name=path.name
        if name not in required or name in result or not path.is_relative_to((root/'external').resolve()):
            raise ValueError('Invalid native startup module catalog')
        if not path.is_file():
            raise ValueError('Missing required native startup module: '+name)
        if sha(path)!=item['sha256']:
            raise ValueError('Native startup module differs from pinned input: '+name)
        data=path.read_bytes()
        if data[:4]!=bytes.fromhex('4f153d1d') or len(data)<32:
            raise ValueError('Native startup module is not a FSELF: '+name)
        count=struct.unpack_from('<H',data,24)[0];offset=32+count*32
        if data[offset:offset+6]!=b'\x7fELF\x02\x01' or \
           struct.unpack_from('<HH',data,offset+16)!=(0xfe18,62):
            raise ValueError('Native startup module has an invalid Orbis module header: '+name)
        result[name]=path
    if result.keys()!=required:
        raise ValueError('Both libc.prx and libSceFios2.prx startup modules are required')
    return result

def sfo_values(data):
    if data[:4] != b'\0PSF' or len(data) < 20:
        raise ValueError('Invalid SFO header')
    _, _, keys, values, count = struct.unpack_from('<5I', data)
    if count > 256 or 20 + count * 16 > keys or not keys <= values <= len(data):
        raise ValueError('Invalid SFO tables')
    result = {}
    for i in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from('<HHIII', data, 20 + i * 16)
        start = keys + key
        end = data.find(b'\0', start, values)
        if start < keys or end < start or length > capacity or values + offset + capacity > len(data):
            raise ValueError('Invalid SFO entry bounds')
        name = data[start:end].decode('utf-8')
        if name in result:
            raise ValueError('Duplicate SFO key')
        raw = data[values + offset:values + offset + length]
        if fmt == 0x0204:
            result[name] = raw.rstrip(b'\0').decode('utf-8')
        elif fmt == 0x0404 and length == 4:
            result[name] = struct.unpack('<I', raw)[0]
        else:
            result[name] = raw.hex()
    return result

def entry_indices(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r'0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\d+)\s+(?:\d+\s+)?(\w+)\s*', line)
        if match:
            result[match[2]] = int(match[1])
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', required=True, choices=['vulkan-probe', 'runtime-probe', 'dawn-probe', 'signal-probe', 'storage-probe', 'loader-probe', 'boot-probe', 'game'])
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--oelf', type=Path, required=True)
    parser.add_argument('--eboot', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--verify-existing',action='store_true',help='Verify a previously assembled output; do not rebuild it')
    parser.add_argument('--extractor',type=Path,help='Pinned project-owned extractor for large unsigned PFS catalogs')
    parser.add_argument('--private-artwork', type=Path)
    asset_inputs = parser.add_mutually_exclusive_group()
    asset_inputs.add_argument('--private-assets', type=Path)
    asset_inputs.add_argument('--private-assets-dir', type=Path)
    parser.add_argument('--candidate-profile', choices=['performance-v0021','original-hd-v0022','remastered-lite-v0023','remastered-enhanced-v0024','consolidated','consolidated-original-hd'])
    args = parser.parse_args()
    modules=startup_modules(json.loads((ROOT/'upstream.lock.json').read_text()))
    if not re.fullmatch(r'\d{2}\.\d{2}', args.version):
        parser.error('--version must use NN.NN')
    mapping = verify(args.elf, args.oelf, args.eboot)
    out = args.out.resolve()
    if out.exists() and not args.verify_existing:
        parser.error('Output already exists; select a new package version/directory')
    if args.verify_existing and not out.is_dir():parser.error('Existing package output is missing')
    bundle = Path(os.environ.get('PRIME_SDK_BUNDLE', '/opt/orbis-sdk'))
    sdk = bundle / 'sdk'
    helper = bundle / 'orbis-compat/scripts/ps4/make-pkg.sh'
    pkgtool = sdk / 'bin/linux/PkgTool.Core'
    openssl = Path(os.environ.get('PS4_PKGTOOL_OPENSSL_LIB', '/opt/openssl11/lib'))
    if not (openssl / 'libssl.so.1.1').is_file():
        parser.error('Set PS4_PKGTOOL_OPENSSL_LIB to the existing PkgTool host compatibility libraries')
    kind = {
        'game': ('PRME00001', 'Metroid Prime - Development', 'PROJECTPRIME0001'),
        'vulkan-probe': ('PRME00002', 'Project Prime - Vulkan Diagnostic', 'PRIMEVKPROBE0001'),
        'runtime-probe': ('PRME00003', 'Project Prime - Runtime Diagnostic', 'PRIMERTPROBE0001'),
        'dawn-probe': ('PRME00004', 'Project Prime - Dawn Diagnostic', 'PRIMEDAWNPROBE01'),
        'signal-probe': ('PRME00005', 'Project Prime - Signal Diagnostic', 'PRIMESIGPROBE001'),
        'storage-probe': ('PRME00006', 'Project Prime - Storage Diagnostic', 'PRIMEDBPROBE0001'),
        'loader-probe': ('PRME00007', 'Project Prime - Loader Diagnostic', 'PRIMELOADPROBE01'),
        'boot-probe': ('PRME00008', 'Project Prime - Native Startup Diagnostic', 'PRIMEBOOTPROBE01'),
    }[args.kind]
    title_id, title, label = kind
    candidate_profiles = {
        'consolidated': ('00.25','PRME00013','Metroid Prime','PROJECTPRIME0025'),
        'consolidated-original-hd': ('00.25','PRME00013','Metroid Prime','PROJECTPRIME0025'),
        'performance-v0021': ('00.21','PRME00009','Metroid Prime - Performance Candidate','PRIMEPERF0021'),
        'original-hd-v0022': ('00.22','PRME00010','Metroid Prime - Original HD Candidate','PRIMEHD0022'),
        'remastered-lite-v0023': ('00.23','PRME00011','Metroid Prime - Remastered Lite Test','PRIMELITE0023'),
        'remastered-enhanced-v0024': ('00.24','PRME00012','Metroid Prime - Enhanced Experiment','PRIMEENH0024'),
    }
    if args.candidate_profile:
        version,title_id,title,label=candidate_profiles[args.candidate_profile]
        consolidated=args.candidate_profile in ('consolidated','consolidated-original-hd')
        if args.kind!='game' or (not consolidated and args.version!=version) or (consolidated and tuple(map(int,args.version.split('.')))<(0,25)):
            parser.error('Candidate profile requires its matching game package version '+version)
    private_assets=None
    if args.private_assets:
        private_assets=args.private_assets.resolve()
        if args.candidate_profile!='consolidated' or not private_assets.is_relative_to(ROOT/'private'):
            parser.error('Private test assets require the consolidated profile and private input path')
        pinned=json.loads((ROOT/'evidence/consolidation/asset-bundle.json').read_text())
        if sha(private_assets)!=pinned['sha256']:parser.error('Private asset bundle hash differs from verified input')
    private_dir = None
    private_manifest = None
    if args.private_assets_dir:
        private_dir = args.private_assets_dir.resolve()
        if args.candidate_profile != 'consolidated' or not private_dir.is_relative_to(ROOT/'private'):
            parser.error('Installed private assets require the consolidated profile and private input path')
        private_manifest = verify_private_assets(private_dir)
        if private_manifest['profile'] != 'remastered-consolidated':
            parser.error('Expected the verified consolidated Remastered assets')
    artwork = {}
    provenance = None
    if args.private_artwork:
        art = args.private_artwork.resolve()
        if not art.is_relative_to(ROOT / 'private'):
            parser.error('Keep private artwork inside this workspace private/ directory')
        provenance = json.loads((art / 'provenance.json').read_text())
        for name, dimensions in [('icon0.png', (512, 512)), ('pic1.png', (1920, 1080))]:
            file = art / name
            if name == 'pic1.png' and not file.exists():
                continue
            data = file.read_bytes()
            if data[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack_from('>II', data, 16) != dimensions:
                raise ValueError('Invalid artwork dimensions: ' + name)
            if provenance['files'].get(name) != sha(file):
                raise ValueError('Artwork does not match private provenance: ' + name)
            artwork[name] = file
    out.mkdir(parents=True,exist_ok=args.verify_existing)
    previous_receipt = json.loads((out/'receipt.json').read_text()) if args.verify_existing and (out/'receipt.json').is_file() else None
    inputs = out / 'inputs'
    inputs.mkdir(exist_ok=args.verify_existing)
    # Preserve the actual manifests with each package. A later workspace pin
    # update must not turn an old receipt into an unresolved hash reference.
    for original in (ROOT/'upstream.lock.json',ROOT/'evidence/sdk-inputs.json'):
        if args.verify_existing:
            if sha(original)!=sha(inputs/original.name):raise RuntimeError('Existing package source/SDK pin differs')
        else:shutil.copyfile(original,inputs/original.name)
    if provenance:
        (inputs / 'artwork-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    descriptor = None
    if args.candidate_profile:
        descriptor=inputs/'prime-profile.txt'
        expected_descriptor='PROJECT_PRIME_PROFILE_V1\n'+args.candidate_profile+'\n'
        if args.verify_existing:
            if descriptor.read_text()!=expected_descriptor:raise RuntimeError('Existing profile descriptor differs')
        else:descriptor.write_text(expected_descriptor)
    env = os.environ.copy()
    env['DOTNET_SYSTEM_GLOBALIZATION_INVARIANT'] = '1'
    env['PS4_PKGTOOL_OPENSSL_LIB'] = str(openssl)
    env['LD_LIBRARY_PATH'] = str(openssl) + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    extractor=pkgtool;extractor_inputs={}
    if args.extractor:
        extractor=args.extractor.resolve()
        pinned_reader=json.loads((extractor.parent/'receipt.json').read_text())
        for name,digest in pinned_reader['files'].items():
            path=extractor.parent/name
            if sha(path)!=digest:raise RuntimeError('Package reader differs from its build receipt')
            extractor_inputs[str(path)]=digest
        env['DOTNET_ROOT']=pinned_reader['dotnet_root']

    def run(label, command, cwd=ROOT):
        completed = subprocess.run(list(map(str, command)), env=env, cwd=cwd,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (label + '.log')).write_bytes(completed.stdout)
        if completed.returncode:
            raise RuntimeError(f'{label} failed ({completed.returncode}); see {out / (label + ".log")}')
        return completed.stdout.decode('utf-8', errors='replace')

    command = ['bash', helper, '--eboot', args.eboot.resolve(), '--out-dir', out,
               '--title-id', title_id, '--title', title, '--version', args.version,
               '--content-label', label, '--sdk', sdk]
    if 'icon0.png' in artwork:
        command += ['--icon', artwork['icon0.png']]
    if 'pic1.png' in artwork:
        command += ['--extra', str(artwork['pic1.png']) + ':sce_sys/pic1.png']
    for name,path in sorted(modules.items()):
        command += ['--extra', str(path)+':sce_module/'+name]
    if descriptor:
        command += ['--extra',str(descriptor)+':prime-profile.txt']
    if private_assets:
        command += ['--extra',str(private_assets)+':remastered.primebundle']
    if not args.verify_existing:run('assemble', command)
    installed_assets = {}
    if private_dir:
        manifest = json.loads((private_dir/'profile.json').read_text())
        names = [row['path'] for row in manifest['files']] + [
            'mods/remastered-models/.import-version', 'profile.json',
            'prime-assets.txt', 'prime-assets-index.txt']
        gp4 = out/'pkg-stage/pkg.gp4'
        installed_assets = {'remastered/'+name:private_dir/name for name in names} if args.verify_existing else add_asset_tree(gp4, private_dir, names)
        # Reuse the SDK-created metadata, modules, artwork and GP4 recipe.
        # Only this project's staging tree and final output are modified.
        if not args.verify_existing:
            run('assemble-installed-assets', [pkgtool, 'pkg_build', 'pkg.gp4', '.'], cwd=gp4.parent)
            package_name = f'IV0000-{title_id}_00-{label:0<16}.pkg'
            shutil.move(gp4.parent/package_name, out/package_name)
    packages = [p for p in out.glob('*.pkg') if not p.is_symlink()]
    if len(packages) != 1:
        raise RuntimeError('Expected exactly one assembled package')
    package = packages[0]
    validation = run('validate', [pkgtool, 'pkg_validate', package])
    checks = [line for line in validation.splitlines() if line.strip()]
    if len(checks) < 20 or any(not line.startswith('[OK]') for line in checks):
        raise RuntimeError('PkgTool reported a validation problem; inspect validate.log')
    listing = run('entries', [pkgtool, 'pkg_listentries', package])
    indices = entry_indices(listing)
    expected = {'PARAM_SFO': out / 'pkg-stage/sce_sys/param.sfo'}
    expected.update({name.upper().replace('.', '_'): file for name, file in artwork.items()})
    readback = out / 'readback'
    readback.mkdir(exist_ok=args.verify_existing)
    entries = {}
    for name, original in expected.items():
        if name not in indices:
            raise RuntimeError('Packaged entry is missing: ' + name)
        destination = readback / original.name
        run('readback-' + name, [pkgtool, 'pkg_extractentry', package, indices[name], destination])
        if not destination.is_file():
            raise RuntimeError('Package extractor did not create entry: ' + name)
        if name == 'PARAM_SFO':
            before, after = sfo_values(original.read_bytes()), sfo_values(destination.read_bytes())
            # LibOrbisPkg appends its build date/tool revision while building.
            if any(after.get(key) != value for key, value in before.items()) or \
                    after.keys() - before.keys() - {'PUBTOOLINFO', 'PUBTOOLVER'}:
                raise RuntimeError('Packager changed application metadata')
        elif sha(destination) != sha(original):
            raise RuntimeError('Extracted entry differs from input: ' + name)
        entries[name] = sha(destination)
    sfo = sfo_values((readback / 'param.sfo').read_bytes())
    content_id = f'IV0000-{title_id}_00-{label:0<16}'
    for key, value in {'TITLE_ID': title_id, 'APP_VER': args.version, 'TITLE': title,
                       'CATEGORY': 'gd', 'CONTENT_ID': content_id}.items():
        if sfo.get(key) != value:
            raise RuntimeError('Incorrect packaged SFO field: ' + key)
    run('extract', [extractor, 'pkg_extract', package, out / 'extracted'])
    extracted = out / 'extracted/uroot'
    if sha(extracted / 'eboot.bin') != sha(args.eboot):
        raise RuntimeError('Extracted executable differs from verified input')
    allowed = {'eboot.bin', 'sce_sys/param.sfo', 'sce_sys/icon0.png', 'sce_sys/pic1.png', 'sce_sys/keystone'}
    if descriptor:
        allowed.add('prime-profile.txt')
        if sha(extracted/'prime-profile.txt')!=sha(descriptor):
            raise RuntimeError('Extracted candidate profile differs from input')
    for name,path in modules.items():
        allowed.add('sce_module/'+name)
        if sha(extracted/'sce_module'/name)!=sha(path):
            raise RuntimeError('Extracted native startup module differs from pin: '+name)
    if private_assets:
        allowed.add('remastered.primebundle')
        if sha(extracted/'remastered.primebundle')!=sha(private_assets):
            raise RuntimeError('Extracted private assets differ from verified bundle')
    for name, original in installed_assets.items():
        allowed.add(name)
        if sha(extracted/name) != sha(original):
            raise RuntimeError('Extracted installed asset differs from verified input: '+name)
    files = {str(p.relative_to(extracted)): sha(p) for p in extracted.rglob('*') if p.is_file()}
    if files.keys() - allowed:
        raise RuntimeError('Unexpected files in package: ' + str(files.keys() - allowed))
    receipt = {
        'schema': 1, 'time_utc': datetime.now(timezone.utc).isoformat(),
        'kind': args.kind, 'candidate_profile': args.candidate_profile, 'stage': 'packaged_and_host_verified',
        'hardware_tested': False, 'playable_verified': False, 'release_accepted': False,
        'package': package.name, 'package_sha256': sha(package), 'bytes': package.stat().st_size,
        'metadata': sfo, 'elf_mapping': mapping, 'eboot_sha256': sha(args.eboot),
        'extracted_files': files, 'extracted_metadata': entries,
        'source_lock_sha256': sha(ROOT / 'upstream.lock.json'),
        'project_source_files': {
            name: sha(ROOT / name)
            for name in subprocess.check_output(
                ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
            ).decode().split('\0')
            if name and not name.startswith('evidence/') and (ROOT / name).is_file()
        },
        'native_runtime_archives': {
            str(p.relative_to(ROOT)): sha(p)
            for p in sorted((ROOT / 'build/modern-runtime/lib').glob('*.a'))
        },
        'native_startup_modules': {name:sha(path) for name,path in modules.items()},
        'native_graphics_archives': {
            name:sha(ROOT/name) for name in (
                'build/dawn-modern/src/dawn/native/libwebgpu_dawn.a',
                'build/dawn-bridge/libprime_gpu_wait.a',
                'build/mesa-native-memory/libvulkan_radeon.a')
        } if args.kind=='game' else {},
        'sdk_receipt_sha256': sha(ROOT / 'evidence/sdk-inputs.json'),
        'packaging_inputs': {str(p): sha(p) for p in [helper, pkgtool, sdk / 'bin/linux/create-gp4', ROOT/'tools/package_assets.py']},
        'extraction_inputs':extractor_inputs,
        'private_artwork_included': bool(artwork), 'private_artwork_provenance': provenance,
        'commercial_game_data_included': bool(private_assets or private_dir),
        'private_testing_only': bool(private_assets or private_dir or provenance),
        'asset_install_layout': 'direct_app0_files' if private_dir else 'first_launch_bundle' if private_assets else 'external',
        'installed_asset_profile': private_manifest,
        'remastered_bundle_sha256': sha(private_assets) if private_assets else None,
    }
    if previous_receipt:
        # Re-verifying an old executable must not relabel it with today's
        # workspace or native-library inputs.
        for key in ('project_source_files','source_lock_sha256','native_runtime_archives',
                    'native_startup_modules','native_graphics_archives','sdk_receipt_sha256',
                    'packaging_inputs'):
            receipt[key] = previous_receipt[key]
        if previous_receipt.get('source_archive_files'):
            receipt['source_archive_files'] = archive_sources(ROOT, inputs/'project',
                previous_receipt['source_archive_files'], verify=True)
    else:
        receipt['source_archive_files'] = archive_sources(ROOT, inputs/'project', receipt['project_source_files'])
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'package': str(package), 'receipt': str(out / 'receipt.json'),
                      'stage': receipt['stage'], 'hardware_tested': False}, indent=2))

if __name__ == '__main__':
    main()

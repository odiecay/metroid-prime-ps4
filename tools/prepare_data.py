#!/usr/bin/env python3
"""Prepare a user-supplied GameCube image locally; never invoked by packaging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NOD_SHA = 'ee8345c004248eb82a86dd0c6068e0fcf3dc75baf68d18fe949bf4589822f5a5'
# Same pinned nodtool release; the portable Windows helper bundles its native build.
NOD_WINDOWS_SHA = 'f65de62c44f0ad5d179eff85bd1e92099713ffacbf717f651fb761129a25c7bc'
NOD_HASHES = frozenset((NOD_SHA, NOD_WINDOWS_SHA))

def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def inspect_iso(path):
    path = Path(path)
    size = path.stat().st_size
    with path.open('rb') as f:
        header = f.read(0x440)
        if len(header) != 0x440 or header[0x1c:0x20] != b'\xc2\x33\x9f\x3d':
            raise ValueError('Not an uncompressed GameCube image')
        disc_id = header[:6].decode('ascii')
        disc, revision = header[6], header[7]
        if disc != 0 or not ((disc_id == 'GM8E01' and revision <= 2) or
                            (disc_id == 'GM8P01' and revision == 0)):
            raise ValueError(f'Unsupported disc {disc_id} disc={disc} revision={revision}')
        dol, fst, fst_size = struct.unpack_from('>III', header, 0x420)
        if dol < 0x440 or dol + 0x100 > size or fst < 0x440 or fst_size < 12 or fst_size > 32*1024*1024 or fst + fst_size > size:
            raise ValueError('DOL/FST bounds invalid or image truncated')
        f.seek(fst)
        table = f.read(fst_size)
        count = struct.unpack_from('>I', table, 8)[0]
        if table[0] != 1 or count < 1 or count*12 > len(table):
            raise ValueError('Invalid FST root')
        names = table[count*12:]
        for i in range(1, count):
            kind, offset, length = struct.unpack_from('>III', table, i*12)
            name = kind & 0xffffff
            if name >= len(names) or b'\0' not in names[name:]:
                raise ValueError(f'Invalid FST name at {i}')
            if kind >> 24 == 0:
                if offset > size or length > size-offset:
                    raise ValueError(f'Truncated FST file at {i}')
            elif kind >> 24 == 1:
                if not (offset < i < length <= count):
                    raise ValueError(f'Invalid FST directory at {i}')
            else:
                raise ValueError(f'Invalid FST type at {i}')
        f.seek(dol)
        dh = f.read(0x100)
        for i in range(18):
            off = struct.unpack_from('>I', dh, 4*i)[0]
            length = struct.unpack_from('>I', dh, 0x90+4*i)[0]
            if length and (dol+off > size or length > size-dol-off):
                raise ValueError(f'Truncated DOL section {i}')
    return {'disc_id': disc_id, 'disc_number': disc, 'revision': revision,
            'bytes': size, 'fst_entries': count, 'fst_sha256': hashlib.sha256(table).hexdigest()}

def verified_redump_record(text):
    title=re.search(r'^Redump:\s+(Metroid Prime[^\r\n]*?)\s+✅\s*$',text,re.M)
    digest=re.search(r'^SHA-1\s*:\s*([0-9a-fA-F]{40})\s+✅\s*$',text,re.M)
    if not title or not digest or '❌' in text:
        raise ValueError('No complete clean Metroid Prime Redump match; corrupt or unrecognized inputs are not imported')
    return {'title':title[1], 'sha1':digest[1].lower(), 'database':'embedded in pinned nodtool v2.0.0-alpha.12'}

def verify_disc(path,nodtool,log_path):
    if sha(nodtool) not in NOD_HASHES:
        raise ValueError('nodtool does not match the pinned release digest')
    with Path(log_path).open('w') as log:
        subprocess.run([str(nodtool),'--no-color','verify',str(path)],stdout=log,stderr=subprocess.STDOUT,check=True)
    return verified_redump_record(Path(log_path).read_text(encoding='utf-8'))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--output', type=Path, default=ROOT/'private/game-data')
    p.add_argument('--nodtool', type=Path, default=ROOT/'.tools/nodtool')
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    destination = output/'MetroidPrime.iso'
    if destination.exists() or (output/'import-receipt.json').exists():
        raise SystemExit('Existing import retained; choose a fresh --output directory')
    source = args.input.resolve()
    source_sha = sha(source)
    if source.suffix.lower() == '.zip':
        with zipfile.ZipFile(source) as z:
            images = [i for i in z.infolist() if Path(i.filename).suffix.lower() in ('.rvz', '.iso', '.gcm')]
            if len(images) != 1:
                raise ValueError('ZIP must contain exactly one supported image')
            item = images[0]
            if item.file_size > 2*1024**3:
                raise ValueError('GameCube image exceeds import limit')
            extracted = output/('source'+Path(item.filename).suffix.lower())
            with z.open(item) as src, extracted.open('xb') as dst:
                shutil.copyfileobj(src, dst, 1024*1024)
                dst.flush(); os.fsync(dst.fileno())
            # ZipExtFile checks the CRC while streaming. The original ZIP is retained.
        source = extracted
    # nod infers the output format from the final suffix.
    temporary = output/'MetroidPrime.partial.iso'
    if temporary.exists():
        raise ValueError('Interrupted import exists; select a fresh directory')
    if source.suffix.lower() == '.rvz':
        if sha(args.nodtool) not in NOD_HASHES:
            raise ValueError('nodtool does not match the pinned release digest')
        with (output/'nod-convert.log').open('w') as log:
            subprocess.run([str(args.nodtool), '--no-color', 'convert', str(source), str(temporary)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    else:
        with source.open('rb') as src, temporary.open('xb') as dst:
            shutil.copyfileobj(src, dst, 1024*1024)
    identity = inspect_iso(temporary)
    redump=verify_disc(temporary,args.nodtool,output/'nod-verify.log')
    identity.update(sha256=sha(temporary), source_sha256=source_sha, source_path=str(args.input.resolve()),
                    verification='ZIP CRC when applicable, full-image nod/embedded Redump match, full SHA256, disc revision, FST and DOL bounds',
                    redump=redump,nodtool_sha256=sha(args.nodtool),
                    verification_log_sha256=sha(output/'nod-verify.log'),
                    copyrighted_data_included_in_packages=False)
    with temporary.open('r+b') as f: os.fsync(f.fileno())
    temporary.rename(destination)
    (output/'import-receipt.json').write_text(json.dumps(identity, indent=2)+'\n',encoding='utf-8')
    print(json.dumps(identity, indent=2))

if __name__ == '__main__':
    main()

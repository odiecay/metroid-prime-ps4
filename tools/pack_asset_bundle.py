#!/usr/bin/env python3
"""Pack verified preconverted private files for one bulk FTP transfer."""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'private/consolidated-remastered-v1'
manifest=json.loads((source/'profile.json').read_text())
rows=[(r['path'],r['sha256']) for r in manifest['files']]
for name in ('mods/remastered-models/.import-version','profile.json','prime-assets.txt','prime-assets-index.txt'):
    rows.append((name,hashlib.file_digest((source/name).open('rb'),'sha256').hexdigest()))
output=ROOT/'private/consolidated-remastered-v1.primebundle'
temp=output.with_suffix(output.suffix+'.partial')
if output.exists() or temp.exists():raise SystemExit('Existing bundle preserved')
total=sum((source/name).stat().st_size for name,_ in rows)
with temp.open('xb') as out:
    out.write(b'PRIMEASSET1\n'+struct.pack('<IQ',len(rows),total))
    for name,expected in rows:
        encoded=name.encode();p=source/name;length=p.stat().st_size;at=out.tell()
        out.write(struct.pack('<HQI',len(encoded),length,0)+encoded)
        crc=0;h=hashlib.sha256()
        with p.open('rb') as f:
            while block:=f.read(1024*1024):out.write(block);crc=zlib.crc32(block,crc);h.update(block)
        if h.hexdigest()!=expected:raise RuntimeError('Asset changed during bundle creation: '+name)
        end=out.tell();out.seek(at+10);out.write(struct.pack('<I',crc));out.seek(end)
temp.rename(output)
with output.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
receipt=dict(bundle=str(output),sha256=digest,bytes=output.stat().st_size,files=len(rows),payload_bytes=total,format='PRIMEASSET1; uncompressed preconverted data; per-file CRC32',proprietary_assets=True,keys_included=False)
(ROOT/'evidence/consolidation/asset-bundle.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))

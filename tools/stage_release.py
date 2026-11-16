#!/usr/bin/env python3
"""Stage verified consolidated inputs to NEW FTP paths only. Never install or launch."""
from pathlib import Path
from ftplib import FTP,error_perm
import argparse,hashlib,json,time,uuid,re
ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--assets',type=Path);ap.add_argument('--bundle',type=Path);ap.add_argument('--package',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--host',default='192.168.0.188');ap.add_argument('--port',type=int,default=2121)
    ap.add_argument('--verify',choices=('sha256','size'),default='sha256',help='Size checks do not establish a remote SHA256 match')
    args=ap.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    receipt=dict(host=args.host,port=args.port,operation='upload_only',installed=False,launched=False,verification=args.verify,files=[],state='uploading')
    def save(): (args.out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    f=FTP();f.connect(receipt['host'],receipt['port'],timeout=120);f.login();f.voidcmd('TYPE I')
    def size(path):
        try:return f.size(path)
        except error_perm as e:
            if str(e).startswith('550'):return None
            raise
    def directory(path):
        current=''
        for part in path.split('/'):
            if not part:continue
            current+='/'+part
            try:f.mkd(current)
            except error_perm:f.cwd(current)
        f.cwd('/')
    def reconnect():
        nonlocal f
        try:f.close()
        except OSError:pass
        f=FTP();f.connect(receipt['host'],receipt['port'],timeout=120);f.login();f.voidcmd('TYPE I')
    def remote_hash(path):
        h=hashlib.sha256();received=0;last_read=time.monotonic()
        for attempt in range(5):
            try:
                def receive(block):
                    nonlocal received,last_read
                    h.update(block);received+=len(block)
                    if time.monotonic()-last_read>=5:
                        print(f'Readback verified stream: {received/1048576:.1f} MiB',flush=True);last_read=time.monotonic()
                f.retrbinary('RETR '+path,receive,1024*1024,rest=received or None)
                return h.hexdigest()
            except (OSError,EOFError) as error:
                print(f'Reconnecting readback at {received} bytes: {error}',flush=True);reconnect()
        raise RuntimeError('Readback retries exhausted; final destination preserved')
    total=0;finished=0;last=time.monotonic()
    def upload(local,remote,expected):
        nonlocal total,finished,last
        length=local.stat().st_size
        if size(remote) is not None:
            if size(remote)!=length or remote_hash(remote)!=expected:raise RuntimeError('Existing destination differs; preserved: '+remote)
        else:
            directory(remote.rsplit('/',1)[0]);temporary=remote+'.upload-'+uuid.uuid4().hex+'.partial'
            if sha(local)!=expected:raise RuntimeError('Local upload input changed')
            sent=0
            for attempt in range(5):
                try:
                    sent=size(temporary) or 0
                    if sent>length:raise RuntimeError('Invalid partial transfer size')
                    if sent==length:break
                    def progress(block):
                        nonlocal sent,last
                        sent+=len(block)
                        if time.monotonic()-last>=5:
                            print(f'{local.name}: {sent/1048576:.1f}/{length/1048576:.1f} MiB uploaded',flush=True);last=time.monotonic()
                    with local.open('rb') as stream:
                        stream.seek(sent);f.storbinary('STOR '+temporary,stream,1024*1024,progress,rest=sent or None)
                    break
                except (OSError,EOFError) as error:
                    print(f'Reconnecting upload at {sent} bytes: {error}',flush=True);reconnect()
            if size(temporary)!=length:raise RuntimeError('Transferred size differs: '+remote)
            if args.verify=='sha256' and remote_hash(temporary)!=expected:raise RuntimeError('Readback SHA256 mismatch: '+remote)
            if size(remote) is not None:raise RuntimeError('Destination appeared; preserved: '+remote)
            f.rename(temporary,remote)
        total+=length;finished+=1
        receipt['files'].append(dict(path=remote,size=length,local_sha256=expected,size_verified=True,remote_sha256_verified=args.verify=='sha256'))
        if time.monotonic()-last>=5:
            print(f'Staged {finished} files / {total/1048576:.1f} MiB ({args.verify} verification)',flush=True);save();last=time.monotonic()
    save()
    try:
        if args.bundle:
            bundle=args.bundle.resolve()
            if not bundle.is_relative_to(ROOT/'private'):raise ValueError('Private asset bundle required')
            r=json.loads((ROOT/'evidence/consolidation/asset-bundle.json').read_text())
            assert str(bundle)==r['bundle']
            upload(bundle,'/data/project-prime/assets/remastered-v1.primebundle',r['sha256'])
            receipt['asset_bundle_staged']=True;save()
        if args.assets:
            source=args.assets.resolve()
            if not source.is_relative_to(ROOT/'private'):raise ValueError('Assets must remain private')
            manifest=json.loads((source/'profile.json').read_text());assert manifest['profile']=='remastered-consolidated'
            destination='/data/project-prime/assets/remastered-v1'
            staging=destination+'.upload-'+uuid.uuid4().hex
            directory(staging)
            rows=[(r['path'],r['sha256']) for r in manifest['files']]
            rows += [(name,sha(source/name)) for name in ['mods/remastered-models/.import-version','prime-assets.txt','prime-assets-index.txt','profile.json']]
            receipt['asset_destination']=destination;receipt['asset_staging']=staging;save()
            # Refuse replacing any existing immutable set.
            try:f.cwd(destination)
            except error_perm:pass
            else:raise RuntimeError('Asset destination already exists; preserved')
            f.cwd('/')
            for rel,digest in rows:upload(source/rel,staging+'/'+rel,digest)
            try:f.cwd(destination)
            except error_perm:pass
            else:raise RuntimeError('Asset destination appeared; preserved')
            f.cwd('/');f.rename(staging,destination);receipt['assets_committed']=True;save()
            print('Asset set committed with '+args.verify+' verification',flush=True)
        if args.package:
            folder=args.package.resolve();r=json.loads((folder/'receipt.json').read_text())
            version=r['metadata']['APP_VER']
            if r['stage']!='packaged_and_host_verified' or r['metadata']['TITLE_ID']!='PRME00013' or not re.fullmatch(r'\d{2}\.\d{2}',version) or tuple(map(int,version.split('.')))<(0,25):
                raise ValueError('Expected a verified consolidated package receipt')
            suffix='Original-HD' if r.get('candidate_profile')=='consolidated-original-hd' else 'Private-Test' if r.get('private_testing_only') else 'Consolidated'
            upload(folder/r['package'],f'/data/pkg/Metroid-Prime-PS4-{version}-{suffix}.pkg',r['package_sha256'])
            receipt['package_staged']=True
        receipt['state']='upload_complete_hash_verified' if args.verify=='sha256' else 'upload_complete_size_verified';save();f.quit()
    except Exception as exc:
        receipt['state']='failed';receipt['error']=str(exc);save();f.close();raise
if __name__=='__main__':main()

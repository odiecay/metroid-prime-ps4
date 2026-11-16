#!/usr/bin/env python3
"""Read native Project Prime logs from an explicitly authorized console session.

Read-only FTP operations. No install, launch, chmod, upload or console control.
"""
import argparse
from datetime import datetime,timezone
from ftplib import FTP,error_perm
import hashlib
import json
from pathlib import Path
import time

def optional_metadata(ftp, remote):
    try:
        return (ftp.sendcmd('MDTM '+remote), ftp.size(remote))
    except error_perm as error:
        if str(error).startswith('550'): return None
        raise


def read_profile(ftp, remote, size, cached=None):
    """Read the append/rotate writer's log with verified suffix continuity.

    Rotation metadata is checked before and after transfer. A rotation, failed
    overlap check or unsupported REST falls back to a complete read.
    """
    rotation=remote+'.previous';wire_bytes=0
    for attempt in range(3):
        generation=optional_metadata(ftp,rotation)
        prefix=cached['data'] if cached and cached['rotation']==generation and size>len(cached['data']) else b''
        overlap=min(4096,len(prefix));offset=len(prefix)-overlap
        chunks=[];mode='full'
        try:
            ftp.retrbinary('RETR '+remote,chunks.append,1024*1024,rest=offset if prefix else None)
        except error_perm as error:
            if not prefix or str(error)[:3] not in ('500','501','502','504'):raise
            prefix=b'';chunks=[]
            ftp.retrbinary('RETR '+remote,chunks.append,1024*1024)
            mode='full-rest-fallback'
        transferred=b''.join(chunks);wire_bytes+=len(transferred)
        if prefix:
            if transferred[:overlap]==prefix[-overlap:]:
                data=prefix+transferred[overlap:];mode='append-rest'
            else:
                chunks=[];ftp.retrbinary('RETR '+remote,chunks.append,1024*1024)
                data=b''.join(chunks);wire_bytes+=len(data);mode='full-overlap-fallback'
        else:data=transferred
        if optional_metadata(ftp,rotation)==generation:
            return data,{'data':data,'rotation':generation},mode,wire_bytes
        cached=None
    raise RuntimeError('Profiler rotated repeatedly during read; retry on next poll')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True)
    parser.add_argument('--port',type=int,default=2121)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--follow-seconds',type=int,default=0)
    parser.add_argument('--candidate-profile',choices=['performance-v0021','original-hd-v0022','remastered-lite-v0023','remastered-enhanced-v0024','consolidated'])
    args=parser.parse_args()
    if not 0<=args.follow_seconds<=600:parser.error('Follow duration must be 0..600 seconds')
    args.out.mkdir(parents=True,exist_ok=False)
    deadline=time.monotonic()+args.follow_seconds
    log_root=('/data/project-prime/candidates/'+args.candidate_profile+'/logs/' if args.candidate_profile else '/data/project-prime/logs/')
    if args.candidate_profile=='consolidated': log_root='/data/project-prime/release/logs/'
    previous={};records=[];sequence=0;profile_cache=None
    while True:
        try:
            ftp=FTP();ftp.connect(args.host,args.port,timeout=8);ftp.login();ftp.voidcmd('TYPE I')
            try:
                for name in ('boot.log','crash.log','startup.log','shutdown.log',
                             'boot.previous.log','crash.previous.log','shutdown.log.previous',
                             'frame-profile.jsonl','frame-profile.jsonl.previous'):
                    remote=log_root+name
                    try:
                        metadata=(ftp.sendcmd('MDTM '+remote),ftp.size(remote))
                        if previous.get(name)==metadata:continue
                        if name=='frame-profile.jsonl':
                            data,profile_cache,mode,wire_bytes=read_profile(ftp,remote,metadata[1],profile_cache)
                        else:
                            chunks=[];ftp.retrbinary('RETR '+remote,chunks.append,1024*1024)
                            data=b''.join(chunks);mode='full';wire_bytes=len(data)
                        sequence+=1;local=args.out/f'{sequence:04d}-{name}'
                        local.write_bytes(data)
                        record={'time_utc':datetime.now(timezone.utc).isoformat(),
                            'remote':remote,'metadata':metadata,'file':local.name,
                            'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),
                            'transfer_mode':mode,'transferred_bytes':wire_bytes}
                        records.append(record);previous[name]=metadata
                        (args.out/'receipt.json').write_text(json.dumps(records,indent=2)+'\n')
                        print(json.dumps(record),flush=True)
                        if name!='startup.log' and not name.startswith('frame-profile.'):print(data.decode(errors='replace')[-6000:],flush=True)
                    except error_perm as error:
                        print(name,str(error),flush=True)
                    except RuntimeError as error:
                        print(name,str(error),flush=True)
            finally:
                ftp.close()
        except OSError as error:
            print(type(error).__name__,str(error),flush=True)
        if time.monotonic()>=deadline:break
        time.sleep(min(10,max(0,deadline-time.monotonic())))

if __name__=='__main__':main()

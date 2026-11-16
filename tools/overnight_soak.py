#!/usr/bin/env python3
"""Run pinned host tests serially for a bounded overnight soak.

No compilation, code agents, installation, console launch or remote writes.
An optional console observation reads only existing diagnostic files by FTP.
"""
import argparse
from datetime import datetime, timezone, timedelta
import fcntl
from ftplib import FTP
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    temporary=path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n')
    temporary.replace(path)


def verify_jobs(manifest, snapshot):
    if not manifest.get('jobs'):
        raise ValueError('No pinned host jobs')
    names=set()
    for job in manifest['jobs']:
        if not re.fullmatch(r'[a-z0-9_-]+',job['name']) or job['name'] in names:
            raise ValueError('Invalid or duplicate job name')
        names.add(job['name'])
        path=Path(job['path']).resolve()
        if not path.is_relative_to(snapshot.resolve()) or not path.is_file() or sha(path)!=job['sha256']:
            raise ValueError('Pinned executable changed: '+job['name'])
        if not 1<=job['timeout_seconds']<=120:
            raise ValueError('Job timeout must be 1..120 seconds')
        if not all(isinstance(arg,str) for arg in job.get('args',[])):
            raise ValueError('Job arguments must be strings')
    for dependency in manifest.get('dependencies',[]):
        if sha(dependency['path'])!=dependency['sha256']:
            raise ValueError('Pinned host dependency changed: '+dependency['path'])


def observe_console(host, port, out, cycle):
    """Read at most 64 KiB per diagnostic file; remote bytes are never changed."""
    records=[]
    ftp=FTP()
    try:
        ftp.connect(host,port,timeout=5);ftp.login();ftp.voidcmd('TYPE I')
        for name in ('boot.log','crash.log','shutdown.log'):
            remote='/data/project-prime/logs/'+name
            size=ftp.size(remote)
            modified=ftp.sendcmd('MDTM '+remote)
            offset=max(0,size-65536)
            pieces=[]
            ftp.retrbinary('RETR '+remote,pieces.append,blocksize=65536,rest=offset if offset else None)
            data=b''.join(pieces)
            if len(data)>131072:
                raise RuntimeError('Unexpected diagnostic tail size')
            (out/('console-'+name)).write_bytes(data)
            text=data.decode(errors='replace')
            frames=[line for line in text.splitlines() if line.startswith('orbis: frame times:')]
            records.append({'remote':remote,'full_remote_bytes':size,'tail_offset':offset,
                            'metadata':modified,'tail_sha256':hashlib.sha256(data).hexdigest(),
                            'tail_bytes':len(data),'latest_frame_samples':frames[-3:],
                            'fatal_observed_in_tail':'[fatal]' in text or 'fatal signal=' in text})
        return {'cycle':cycle,'state':'read_only_observation','files':records}
    except Exception as error:
        return {'cycle':cycle,'state':'unavailable','reason':type(error).__name__+': '+str(error)}
    finally:
        ftp.close()


def run(args):
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    manifest=json.loads(args.manifest.read_text())
    verify_jobs(manifest,out/'snapshot')
    stop=threading.Event()
    for value in (signal.SIGTERM,signal.SIGINT):
        signal.signal(value,lambda *_:stop.set())
    started=datetime.now(timezone.utc)
    state={'schema':1,'scope':'host tests; not native PS4 acceptance','pid':os.getpid(),
           'state':'running','started_utc':started.isoformat(),
           'deadline_utc':(started+timedelta(seconds=args.duration_seconds)).isoformat(),
           'manifest_sha256':sha(args.manifest),'cycles_completed':0,'jobs_passed':0,
           'gtest_case_executions_passed':0,'console_modified':False}
    result=out/'result.json'
    deadline=time.monotonic()+args.duration_seconds
    lock=args.lock or out.parent/'project-prime-overnight.lock'
    lock.parent.mkdir(parents=True,exist_ok=True)
    with lock.open('a+') as ownership:
        fcntl.flock(ownership,fcntl.LOCK_EX|fcntl.LOCK_NB)
        ownership.seek(0);ownership.truncate();ownership.write(str(os.getpid())+'\n');ownership.flush()
        write_json(result,state)
        try:
            while not stop.is_set() and time.monotonic()<deadline:
                cycle=state['cycles_completed']+1
                cycle_start=time.monotonic()
                verify_jobs(manifest,out/'snapshot')
                for job in manifest['jobs']:
                    if stop.is_set():break
                    command=[job['path'],*job.get('args',[])]
                    if job.get('shuffle'):
                        command+=['--gtest_shuffle','--gtest_random_seed='+str(cycle%99999+1)]
                    log=out/(job['name']+'-latest.log')
                    began=time.monotonic()
                    try:
                        with log.open('wb') as output:
                            process=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=output,
                                                   stderr=subprocess.STDOUT,timeout=job['timeout_seconds'])
                        code=process.returncode
                    except subprocess.TimeoutExpired:
                        code='timeout'
                    record={'cycle':cycle,'job':job['name'],'result':code,
                            'seconds':time.monotonic()-began,'output_sha256':sha(log),
                            'output_bytes':log.stat().st_size,'time_utc':datetime.now(timezone.utc).isoformat()}
                    with (out/'journal.jsonl').open('a') as journal:
                        journal.write(json.dumps(record)+'\n')
                    if code!=0:
                        state.update(state='failed',failed_job=job['name'],failure=record)
                        return 1
                    text=log.read_text(errors='replace')
                    state['jobs_passed']+=1
                    state['gtest_case_executions_passed']+=sum(int(n) for n in re.findall(r'\[==========\] (\d+) tests? from .* ran\.',text))
                    write_json(result,state)
                if stop.is_set():break
                state['cycles_completed']=cycle
                if args.console_host and (cycle==1 or cycle%5==0):
                    observation=observe_console(args.console_host,args.console_port,out,cycle)
                    observation['time_utc']=datetime.now(timezone.utc).isoformat()
                    with (out/'console-observations.jsonl').open('a') as journal:
                        journal.write(json.dumps(observation)+'\n')
                    state['latest_console_observation']=observation
                write_json(result,state)
                if args.once:break
                remaining=min(args.interval_seconds-(time.monotonic()-cycle_start),deadline-time.monotonic())
                if remaining>0:stop.wait(remaining)
            state['state']='stopped' if stop.is_set() else 'completed'
            return 0
        except Exception as error:
            state.update(state='failed',reason=type(error).__name__+': '+str(error))
            return 1
        finally:
            state['ended_or_updated_utc']=datetime.now(timezone.utc).isoformat()
            write_json(result,state)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--lock',type=Path)
    parser.add_argument('--duration-seconds',type=int,default=28800)
    parser.add_argument('--interval-seconds',type=int,default=60)
    parser.add_argument('--once',action='store_true')
    parser.add_argument('--console-host')
    parser.add_argument('--console-port',type=int,default=2121)
    args=parser.parse_args()
    if not 1<=args.duration_seconds<=43200:parser.error('Duration must be 1..43200 seconds')
    if not 10<=args.interval_seconds<=3600:parser.error('Interval must be 10..3600 seconds')
    return run(args)

if __name__=='__main__':raise SystemExit(main())

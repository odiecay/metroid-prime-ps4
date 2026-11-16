"""Host verification of the same C++ reader compiled for Orbis; no GPU evidence."""
import concurrent.futures
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
READ=c.CFUNCTYPE(c.c_int64,c.c_void_p,c.c_uint64,c.c_void_p,c.c_size_t)
LENGTH=c.CFUNCTYPE(c.c_int64,c.c_void_p)
CLOSE=c.CFUNCTYPE(None,c.c_void_p)
VISIT=c.CFUNCTYPE(c.c_uint32,c.c_uint32,c.c_int,c.c_char_p,c.c_uint32,c.c_void_p)
class Stream(c.Structure):
    _fields_=[('user_data',c.c_void_p),('read_at',READ),('stream_len',LENGTH),('close',CLOSE)]
class Blob(c.Structure): _fields_=[('data',c.c_void_p),('size',c.c_size_t)]
class Meta(c.Structure):
    _fields_=[(n,Blob) for n in ('boot','bi2','apploader','dol','fst','ticket','tmd','cert','h3')]
class CacheMetrics(c.Structure):
    _fields_=[(n,c.c_uint64) for n in ('block_hits','block_misses','source_read_calls','source_read_bytes',
                                     'retained_bytes','exact_fallbacks','bulk_bypass_requests')]

def cache_metrics(lib):
    result=CacheMetrics()
    lib.prime_iso_read_cache_metrics(c.byref(result))
    return result

def api(path):
    lib=c.CDLL(str(path))
    lib.nod_disc_open_stream.argtypes=[c.POINTER(Stream),c.c_void_p,c.POINTER(c.c_void_p)]
    lib.nod_disc_open_partition_kind.argtypes=[c.c_void_p,c.c_uint,c.c_void_p,c.POINTER(c.c_void_p)]
    lib.nod_partition_open_file.argtypes=[c.c_void_p,c.c_uint,c.POINTER(c.c_void_p)]
    lib.nod_partition_find_file.argtypes=[c.c_void_p,c.c_char_p,c.POINTER(c.c_int),c.POINTER(c.c_uint)]
    lib.nod_partition_find_file.restype=c.c_uint
    lib.nod_partition_iterate_fst.argtypes=[c.c_void_p,VISIT,c.c_void_p]
    lib.nod_partition_meta.argtypes=[c.c_void_p,c.POINTER(Meta)]
    lib.nod_read.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t]; lib.nod_read.restype=c.c_int64
    lib.nod_seek.argtypes=[c.c_void_p,c.c_int64,c.c_int]; lib.nod_seek.restype=c.c_int64
    lib.nod_free.argtypes=[c.c_void_p]
    if hasattr(lib,'prime_iso_read_cache_metrics'):
        lib.prime_iso_read_cache_metrics.argtypes=[c.POINTER(CacheMetrics)]
    return lib

class Source:
    def __init__(self, data, max_read=None, fail_at=None):
        self.data=data; self.closed=0
        self.reads=[];self.reject_reads_larger_than=None
        self.file=data.open('rb') if isinstance(data,Path) else None
        self.size=data.stat().st_size if self.file else len(data)
        @READ
        def read(user, offset, output, length):
            self.reads.append((offset,length))
            if self.reject_reads_larger_than is not None and length>self.reject_reads_larger_than:return -1
            if fail_at is not None and offset>=fail_at: return -1
            if max_read: length=min(length,max_read)
            if self.file:
                self.file.seek(offset); part=self.file.read(length)
            else: part=bytes(self.data[offset:offset+length])
            c.memmove(output,part,len(part));return len(part)
        @LENGTH
        def size(user):return self.size
        @CLOSE
        def close(user):
            self.closed+=1
            if self.file:self.file.close()
        self.callbacks=(read,size,close)
        self.stream=Stream(None,*self.callbacks)
    def open(self,lib):
        disc=c.c_void_p()
        status=lib.nod_disc_open_stream(c.byref(self.stream),None,c.byref(disc))
        return status,disc

def fixture():
    b=bytearray(0x4000);b[:6]=b'GM8E01';struct.pack_into('>I',b,0x1c,0xc2339f3d)
    names=b'\0dir\0leaf.bin\0root.bin\0'
    nodes=[(0x1000000,0,4),(0x1000001,0,3),(5,0x3000,64),(14,0x3100,3)]
    fst=b''.join(struct.pack('>III',*n) for n in nodes)+names
    struct.pack_into('>III',b,0x420,0x1000,0x2000,len(fst));b[0x2000:0x2000+len(fst)]=fst
    struct.pack_into('>I',b,0x1000,0x100);struct.pack_into('>I',b,0x1090,4)
    b[0x1100:0x1104]=b'DOL!';b[0x3000:0x3040]=bytes(range(64));b[0x3100:0x3103]=b'abc'
    return b

def partition(lib,disc):
    p=c.c_void_p()
    if lib.nod_disc_open_partition_kind(disc,0,None,c.byref(p)):raise RuntimeError('partition open failed')
    return p

def entries(lib,p):
    result=[]
    @VISIT
    def visit(index,kind,name,size,user):
        result.append((index,kind,name.decode(),size));return index+1
    lib.nod_partition_iterate_fst(p,visit,None)
    return result

def read_sample(lib,p,index,size):
    f=c.c_void_p()
    if lib.nod_partition_open_file(p,index,c.byref(f)):raise RuntimeError('file open failed')
    digest=hashlib.sha256()
    try:
        for offset in sorted(set([0,size//2,max(0,size-65536)])):
            if lib.nod_seek(f,offset,0)!=offset:raise RuntimeError('seek failed')
            n=min(65536,size-offset);buf=c.create_string_buffer(n)
            total=0
            while total<n:
                read=lib.nod_read(f,c.byref(buf,total),n-total)
                if read<=0:raise RuntimeError('short file read')
                total+=read
            digest.update(buf.raw)
    finally:lib.nod_free(f)
    return digest.hexdigest()

class IsoReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sanitizers=os.environ.get('PRIME_READER_SANITIZERS')
        path=ROOT/('build/libprime_gc_iso_host_sanitized.so' if sanitizers else 'build/libprime_gc_iso_host.so')
        path.parent.mkdir(exist_ok=True)
        compiler=os.environ.get('PRIME_READER_CXX','clang++-19')
        flags=['-fsanitize='+sanitizers,'-fno-omit-frame-pointer'] if sanitizers else []
        if sanitizers and 'clang' in compiler:flags+=['-shared-libasan']
        compiler_env=os.environ.copy();compiler_env.pop('LD_PRELOAD',None)
        subprocess.run([compiler,'-std=c++20','-O1','-g','-fPIC','-shared','-pthread',*flags,
            '-I'+str(ROOT/'external/nod-host-package/include'),
            str(ROOT/'platform/ps4/disc/gc_iso.cpp'),'-o',str(path)],check=True,env=compiler_env)
        cls.lib=api(path)
    def test_real_reads_and_parent_lifetime(self):
        source=Source(fixture(),max_read=7);r,disc=source.open(self.lib);self.assertEqual(r,0)
        p=partition(self.lib,disc);self.lib.nod_free(disc)
        self.assertEqual(source.closed,0)
        kind=c.c_int();length=c.c_uint()
        index=self.lib.nod_partition_find_file(p,b'/dir/leaf.bin',c.byref(kind),c.byref(length))
        self.assertEqual((index,kind.value,length.value),(2,0,64))
        f=c.c_void_p();self.assertEqual(self.lib.nod_partition_open_file(p,index,c.byref(f)),0)
        self.lib.nod_free(p);self.assertEqual(source.closed,0)
        buf=c.create_string_buffer(80)
        self.assertEqual(self.lib.nod_read(f,buf,80),64);self.assertEqual(buf.raw[:64],bytes(range(64)))
        self.assertEqual(self.lib.nod_read(f,buf,80),0)
        self.assertEqual(self.lib.nod_seek(f,-65,2),-1)
        self.assertEqual(self.lib.nod_seek(f,-(1<<63),0),-1)
        self.lib.nod_free(f);self.assertEqual(source.closed,1)
    def test_corrupt_inputs_fail_and_close_once(self):
        cases=[]
        b=fixture();b[0x1c]=0;cases.append(b)
        b=fixture();b[:6]=b'BAD001';cases.append(b)
        cases.append(fixture()[:0x3005])
        b=fixture();struct.pack_into('>I',b,0x2014,999);cases.append(b)
        b=fixture();struct.pack_into('>I',b,0x2010,3);cases.append(b)
        for b in cases:
            with self.subTest(digest=hashlib.sha256(b).hexdigest()):
                source=Source(b);r,disc=source.open(self.lib)
                self.assertNotEqual(r,0);self.assertFalse(disc.value);self.assertEqual(source.closed,1)
    def test_io_errors_are_not_successful_reads(self):
        source=Source(fixture(),fail_at=0x2000);r,disc=source.open(self.lib)
        self.assertNotEqual(r,0);self.assertEqual(source.closed,1)
    def test_independent_concurrent_file_positions(self):
        source=Source(fixture());r,disc=source.open(self.lib);self.assertEqual(r,0)
        p=partition(self.lib,disc)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            hashes=list(pool.map(lambda _:read_sample(self.lib,p,2,64),range(64)))
        self.assertEqual(len(set(hashes)),1)
        self.lib.nod_free(p);self.lib.nod_free(disc);self.assertEqual(source.closed,1)
    def test_small_resource_reads_share_blocks_across_file_handles(self):
        evidence={}
        before_env=os.environ.get('MP_DVD_READ_CACHE')
        try:
            for enabled in (False,True):
                os.environ['MP_DVD_READ_CACHE']='1' if enabled else '0'
                source=Source(fixture());r,disc=source.open(self.lib);self.assertEqual(r,0)
                p=partition(self.lib,disc);source.reads.clear()
                baseline=cache_metrics(self.lib)
                for offset in (0,3,17,32,5,21,9,0):
                    f=c.c_void_p();self.assertEqual(self.lib.nod_partition_open_file(p,2,c.byref(f)),0)
                    self.assertEqual(self.lib.nod_seek(f,offset,0),offset)
                    buf=c.create_string_buffer(16)
                    self.assertEqual(self.lib.nod_read(f,buf,16),16)
                    self.assertEqual(buf.raw,bytes(range(offset,offset+16)))
                    self.lib.nod_free(f)
                after=cache_metrics(self.lib)
                evidence['cache_on' if enabled else 'cache_off']={
                    'stream_calls':len(source.reads),'stream_bytes':sum(x[1] for x in source.reads),
                    'hits':after.block_hits-baseline.block_hits,'misses':after.block_misses-baseline.block_misses}
                self.assertEqual(len(source.reads),1 if enabled else 8)
                self.lib.nod_free(p);self.lib.nod_free(disc)
                self.assertEqual(cache_metrics(self.lib).retained_bytes,0)
        finally:
            if before_env is None:os.environ.pop('MP_DVD_READ_CACHE',None)
            else:os.environ['MP_DVD_READ_CACHE']=before_env
        evidence.update(stage='host_tested',workload='8 overlapping 16-byte reads using independent handles',
                        limitation='Synthetic byte correctness and actual stream call counts; no PS4 frame-time claim')
        (ROOT/'evidence/consolidation/original-iso-read-cache-calls.json').write_text(json.dumps(evidence,indent=2)+'\n')
    def test_read_ahead_failure_falls_back_to_exact_read_without_poisoning_cache(self):
        source=Source(fixture());r,disc=source.open(self.lib);self.assertEqual(r,0)
        p=partition(self.lib,disc);f=c.c_void_p()
        self.assertEqual(self.lib.nod_partition_open_file(p,2,c.byref(f)),0)
        before=cache_metrics(self.lib)
        source.reject_reads_larger_than=16
        buf=c.create_string_buffer(16)
        self.assertEqual(self.lib.nod_read(f,buf,16),16)
        self.assertEqual(buf.raw,bytes(range(16)))
        self.assertEqual(self.lib.nod_seek(f,0,1),16)
        source.reject_reads_larger_than=0
        self.assertEqual(self.lib.nod_read(f,buf,16),-1)
        self.assertEqual(self.lib.nod_seek(f,0,1),16) # failed read does not advance
        source.reject_reads_larger_than=None
        self.assertEqual(self.lib.nod_read(f,buf,16),16)
        self.assertEqual(buf.raw,bytes(range(16,32)))
        after=cache_metrics(self.lib)
        self.assertEqual(after.exact_fallbacks-before.exact_fallbacks,2)
        self.assertEqual(after.block_hits-before.block_hits,0)
        self.lib.nod_free(f);self.lib.nod_free(p);self.lib.nod_free(disc)
        self.assertEqual(cache_metrics(self.lib).retained_bytes,0)
    def test_cache_cross_block_reads_eviction_and_bulk_bypass(self):
        block=65536
        b=fixture();offset=0x10000;size=130*block+32
        b.extend(bytes((i&255 for i in range(len(b),offset+size))))
        struct.pack_into('>II',b,0x201c,offset,size)
        source=Source(b);r,disc=source.open(self.lib);self.assertEqual(r,0)
        p=partition(self.lib,disc);f=c.c_void_p()
        self.assertEqual(self.lib.nod_partition_open_file(p,2,c.byref(f)),0)
        buf=c.create_string_buffer(block+32)
        for at,length in [(block-17,40)]+[(i*block+3,29) for i in range(130)]+[(3,29),(size-32,64)]:
            self.assertEqual(self.lib.nod_seek(f,at,0),at)
            count=min(length,size-at)
            self.assertEqual(self.lib.nod_read(f,buf,length),count)
            self.assertEqual(buf.raw[:count],bytes(b[offset+at:offset+at+count]))
            self.assertLessEqual(cache_metrics(self.lib).retained_bytes,8*1024*1024)
        before=cache_metrics(self.lib)
        self.lib.nod_seek(f,0,0)
        self.assertEqual(self.lib.nod_read(f,buf,block+32),block+32)
        self.assertEqual(buf.raw,bytes(b[offset:offset+block+32]))
        self.assertEqual(cache_metrics(self.lib).bulk_bypass_requests-before.bulk_bypass_requests,1)
        self.lib.nod_free(f);self.lib.nod_free(p);self.lib.nod_free(disc)
        self.assertEqual(cache_metrics(self.lib).retained_bytes,0)
    def test_supplied_disc_against_pinned_nod(self):
        iso=ROOT/'private/prepared/MetroidPrime.iso';reference=ROOT/'external/nod-host-package/lib/libnod.so'
        if not iso.exists() or not reference.exists():self.skipTest('optional supplied disc/reference library absent')
        ref=api(reference)
        ours=Source(iso);theirs=Source(iso)
        ro,do=ours.open(self.lib);rr,dr=theirs.open(ref)
        self.assertEqual((ro,rr),(0,0));po=partition(self.lib,do);pr=partition(ref,dr)
        eo=entries(self.lib,po);er=entries(ref,pr);self.assertEqual(eo,er)
        files=[e for e in er if e[1]==0]
        for index,kind,name,size in files:
            self.assertEqual(read_sample(self.lib,po,index,size),read_sample(ref,pr,index,size),name)
        mo=Meta();mr=Meta();self.lib.nod_partition_meta(po,c.byref(mo));ref.nod_partition_meta(pr,c.byref(mr))
        for name in ('boot','bi2','apploader','fst','dol'):
            o=getattr(mo,name);r=getattr(mr,name)
            self.assertEqual(hashlib.sha256(c.string_at(o.data,o.size)).digest(),hashlib.sha256(c.string_at(r.data,r.size)).digest(),name)
        self.lib.nod_free(po);self.lib.nod_free(do);ref.nod_free(pr);ref.nod_free(dr)
        self.assertEqual((ours.closed,theirs.closed),(1,1))
        receipt={'stage':'host_tested','target_reader':'same C++ source as Orbis target',
                 'reference':'nod v2.0.0-alpha.12','files_compared':len(files),'fst_entries':len(eo)+1,
                 'comparison':'first/middle/last 64 KiB of every file; complete boot, bi2, apploader, FST and DOL hashes',
                 'hardware_tested':False}
        (ROOT/'evidence/disc-reader-host.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':unittest.main()

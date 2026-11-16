from pathlib import Path
from ftplib import error_perm
import importlib.util
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fetch',ROOT/'tools/fetch_logs.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class FTPFixture:
    def __init__(self,data):
        self.data=data;self.generation=1;self.rest_supported=True;self.after_read=None
    def sendcmd(self,cmd):return '213 '+str(self.generation)
    def size(self,path):return 100+self.generation
    def retrbinary(self,cmd,callback,blocksize,rest=None):
        if rest is not None and not self.rest_supported:raise error_perm('502 REST unsupported')
        callback(self.data[rest or 0:])
        if self.after_read:
            action=self.after_read;self.after_read=None;action()

class FetchLogTests(unittest.TestCase):
    def test_append_is_exact_and_transfers_only_suffix_plus_overlap(self):
        f=FTPFixture(bytes(range(256))*1024)
        first,cache,_,_=m.read_profile(f,'/profile',len(f.data))
        f.data+=b'new profiler window\n'
        data,_,mode,wire=m.read_profile(f,'/profile',len(f.data),cache)
        self.assertEqual(data,f.data);self.assertEqual(mode,'append-rest')
        self.assertEqual(wire,4096+len(f.data)-len(first))
    def test_rotation_cannot_mix_old_prefix_with_larger_new_file(self):
        f=FTPFixture(b'old'*4096);_,cache,_,_=m.read_profile(f,'/profile',len(f.data))
        f.data=b'new'*8192;f.generation+=1
        data,_,mode,_=m.read_profile(f,'/profile',len(f.data),cache)
        self.assertEqual(data,f.data);self.assertEqual(mode,'full')
    def test_rotation_during_transfer_restarts_full_read(self):
        f=FTPFixture(b'old'*4096);_,cache,_,_=m.read_profile(f,'/profile',len(f.data))
        f.data+=b'append'
        def rotate():f.data=b'new'*8192;f.generation+=1
        f.after_read=rotate
        data,_,mode,_=m.read_profile(f,'/profile',len(f.data),cache)
        self.assertEqual(data,f.data);self.assertEqual(mode,'full')
    def test_unsupported_rest_and_overlap_mismatch_fall_back(self):
        for unsupported in (False,True):
            f=FTPFixture(b'old'*4096);_,cache,_,_=m.read_profile(f,'/profile',len(f.data))
            f.data=b'changed'*4096;f.rest_supported=not unsupported
            data,_,mode,_=m.read_profile(f,'/profile',len(f.data),cache)
            self.assertEqual(data,f.data);self.assertTrue(mode.startswith('full-'))
    def test_same_size_rewrite_is_read_in_full(self):
        f=FTPFixture(b'old'*4096);_,cache,_,_=m.read_profile(f,'/profile',len(f.data))
        f.data=b'new'*4096
        data,_,mode,_=m.read_profile(f,'/profile',len(f.data),cache)
        self.assertEqual(data,f.data);self.assertEqual(mode,'full')

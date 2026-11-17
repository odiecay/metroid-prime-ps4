"""Contract tests for the opt-in native tour; no game or console is simulated."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class BenchmarkTourTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=Path(os.environ.get('PRIME_BENCHMARK_BUILD_DIR',str(ROOT/'build/benchmark-tour-tests')))
        cls.build.mkdir(parents=True,exist_ok=True)
        cls.exe=cls.build/'tour'
        cmd=[os.environ.get('CXX','/usr/lib/llvm-19/bin/clang++'),'-std=c++20','-O2',
             '-DPRIME_BENCHMARK_TEST','-Wall','-Wextra','-Werror',
             '-I'+str(ROOT/'platform/ps4'),str(ROOT/'tests/benchmark_tour.cpp'),
             str(ROOT/'platform/ps4/benchmark_tour.cpp'),'-o',str(cls.exe)]
        cls.sanitized=os.environ.get('PRIME_BENCHMARK_SANITIZE')=='1'
        if cls.sanitized:
            headers=cls.build/'headers';headers.mkdir(exist_ok=True)
            if not (headers/'sanitizer').exists():
                (headers/'sanitizer').symlink_to('/usr/lib/gcc/x86_64-linux-gnu/14/include/sanitizer')
            cmd+=['-O1','-g','-isystem',str(headers),'-fsanitize=address,undefined',
                  '-fno-sanitize-link-runtime','-fno-omit-frame-pointer',
                  '-L/usr/lib/gcc/x86_64-linux-gnu/14','-lasan','-lubsan']
        subprocess.run(cmd,check=True,capture_output=True,text=True)
    def run_mode(self,mode):
        with tempfile.TemporaryDirectory() as tmp:
            env=dict(os.environ)
            if self.sanitized:
                env.update(LD_PRELOAD='/usr/lib/gcc/x86_64-linux-gnu/14/libasan.so',
                           ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            result=subprocess.run([str(self.exe),mode,tmp],env=env,capture_output=True,text=True,check=True)
            p=Path(tmp)/'benchmarks'/('play-v2' if mode.startswith('play_') else 'tour-v1')/'logs/benchmark-tour.jsonl'
            rows=[json.loads(l) for l in p.read_text().splitlines()] if p.is_file() else []
            return rows,result.stderr
    def test_missing_request_has_no_effect(self):self.assertEqual(self.run_mode('absent')[0],[])
    def test_bad_request_cannot_change_live_paths(self):self.assertEqual(self.run_mode('invalid')[0],[])
    def test_symlink_cannot_redirect_namespace_into_live_save(self):self.assertEqual(self.run_mode('symlink')[0],[])
    def test_complete_tour_has_exact_pairing_and_untouched_saves(self):
        rows,_=self.run_mode('complete')
        begins=[r for r in rows if r['event']=='measure_begin']
        ends=[r for r in rows if r['event']=='measure_end']
        self.assertEqual(len(begins),48);self.assertEqual(len(ends),48)
        self.assertEqual(rows[-1]['event'],'complete')
        self.assertEqual({r['session_id'] for r in rows},{'host-contract-session'})
        self.assertEqual(len({r['run_id'] for r in rows}),1)
        for a,b in zip(begins,ends):
            self.assertEqual((a['visit'],a['view']),(b['visit'],b['view']))
            self.assertGreaterEqual(b['monotonic_ns']-a['monotonic_ns'],12_000_000_000)
            self.assertGreater(b['frame'],a['frame']);self.assertEqual(b['status'],'ok')
    def test_lost_gameplay_invalidates_measurement_and_completion(self):
        rows,_=self.run_mode('lost_ready')
        self.assertTrue(any(r['event']=='measure_end' and r['status']=='lost_ready' for r in rows))
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='complete' for r in rows))
    def test_unready_rooms_timeout_without_fake_measurements(self):
        rows,_=self.run_mode('timeout')
        self.assertEqual(sum(r['event']=='scene_error' for r in rows),12)
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='measure_begin' for r in rows))
    def test_full_queue_aborts(self):
        rows,_=self.run_mode('queue_failure');self.assertEqual(rows[-1]['event'],'aborted')
        self.assertFalse(any(r['event']=='complete' for r in rows))
    def test_v2_prefers_separate_optin_and_requires_observed_actions(self):
        rows,_=self.run_mode('play_complete')
        ends=[r for r in rows if r['event']=='measure_end']
        self.assertEqual(len(ends),48);self.assertEqual(rows[-1]['event'],'complete')
        self.assertTrue(all(r['tour_version']==2 and r['dawn_call_profile'] for r in rows))
        self.assertTrue(all(r['movement_distance_m']>=1 for r in ends))
        self.assertTrue(all(r['aim_degrees']>=5 for r in ends if r['view']==0))
        self.assertTrue(all(r['power_projectiles']>0 and r['airborne_frames']>0 for r in ends if r['view']==1))
        self.assertTrue(all(r['missile_projectiles']>0 for r in ends if r['view']==2))
        self.assertTrue(all(r['bomb_objects']>0 and r['morphed_frames']>=30 for r in ends if r['view']==3))
    def test_v2_queued_inputs_cannot_substitute_for_observed_actions(self):
        rows,_=self.run_mode('play_no_actions')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='complete' for r in rows))
        self.assertTrue(any(r['status']=='insufficient_action' for r in rows))
    def test_v2_no_safe_terrain_never_reaches_measurement(self):
        rows,_=self.run_mode('play_bad_pose')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='measure_begin' for r in rows))
    def test_v2_guard_failure_invalidates_phase(self):
        rows,_=self.run_mode('play_guard')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertTrue(any(r['event']=='measure_end' and r['status']=='route_guard' for r in rows))
    def test_v2_invalid_marker_cannot_modify_paths(self):self.assertEqual(self.run_mode('play_invalid')[0],[])
    def test_failed_marker_write_stops_without_acceptance(self):
        rows,err=self.run_mode('log_failure')
        self.assertFalse(any(r['event']=='complete' for r in rows))
        self.assertIn('marker write failed',err)

if __name__=='__main__':unittest.main()

"""Contract tests for the opt-in native tour; no game or console is simulated."""
import json
import os
import re
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
             '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
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
            p=Path(tmp)/'benchmarks'/('coverage-v3' if mode.startswith('coverage_') else 'play-v2' if mode.startswith('play_') else 'tour-v1')/'logs/benchmark-tour.jsonl'
            rows=[json.loads(l) for l in p.read_text().splitlines()] if p.is_file() else []
            return rows,result.stderr
    def test_missing_request_has_no_effect(self):self.assertEqual(self.run_mode('absent')[0],[])
    def test_bad_request_cannot_change_live_paths(self):self.assertEqual(self.run_mode('invalid')[0],[])
    def test_symlink_cannot_redirect_namespace_into_live_save(self):self.assertEqual(self.run_mode('symlink')[0],[])
    def test_relaunch_replaces_settings_when_permission_changes_are_denied(self):
        rows,_=self.run_mode('play_restart')
        self.assertEqual(rows[-1]['event'],'complete')
        self.assertEqual(sum(r['event']=='measure_end' for r in rows),24)
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
    def test_shader_gate_drains_queued_background_and_installing_work(self):
        rows,_=self.run_mode('shader_busy')
        begins=[r for r in rows if r['event']=='measure_begin']
        self.assertEqual(len(begins),48)
        self.assertGreaterEqual(begins[0]['monotonic_ns'],11_000_000_000)
        for row in begins:
            self.assertTrue(row['shader_gate_required'])
            self.assertGreaterEqual(row['shader_stable_frames'],8)
            self.assertEqual(row['shader_completed'],6)
            self.assertTrue(all(row[k]==0 for k in ('shaders_queued','shaders_background','shaders_active','shaders_pending','shader_drop_pending','shader_failures','shader_prefetch_queued','shader_prefetch_active')))
    def test_compile_during_measure_cancels_and_retries_without_pooling(self):
        rows,_=self.run_mode('shader_retry')
        failed=[r for r in rows if r['event']=='measure_end' and r['status']=='pipeline_compile_during_measure']
        self.assertEqual(len(failed),1);self.assertEqual(failed[0]['measure_attempt'],0)
        success=next(r for r in rows if r['event']=='measure_end' and r['visit']==1 and r['view']==0 and r['status']=='ok')
        self.assertEqual(success['measure_attempt'],1)
        self.assertEqual(rows[-1]['event'],'complete')
    def test_unstable_failed_or_timed_out_shaders_never_report_complete(self):
        for mode,status in (('shader_unstable','shader_unstable'),('shader_failure','shader_compile_failed'),('shader_timeout','shader_timeout')):
            with self.subTest(mode=mode):
                rows,_=self.run_mode(mode)
                self.assertEqual(rows[-1]['event'],'incomplete')
                self.assertTrue(any(r['status']==status for r in rows))
                self.assertFalse(any(r['event']=='complete' for r in rows))
    def test_v2_prefers_separate_optin_and_requires_observed_actions(self):
        rows,_=self.run_mode('play_complete')
        ends=[r for r in rows if r['event']=='measure_end']
        self.assertEqual(len(ends),24);self.assertEqual(rows[-1]['event'],'complete')
        self.assertTrue(all(r['tour_version']==2 and r['dawn_call_profile'] for r in rows))
        self.assertTrue(all(r['movement_distance_m']>=1 for r in ends))
        self.assertTrue(all(r['aim_degrees']>=5 for r in ends if r['view']==0))
        self.assertTrue(all(r['power_projectiles']>0 and r['airborne_frames']>0 for r in ends if r['view']==1))
        self.assertTrue(all(r['missile_projectiles']>0 for r in ends if r['view']==2))
        self.assertTrue(all(r['bomb_objects']>0 and r['morphed_frames']>=30 for r in ends if r['view']==3))
        self.assertEqual(sum(r['event']=='rehearse_begin' for r in rows),24)
        self.assertEqual(sum(r['event']=='rehearse_end' for r in rows),24)
        self.assertTrue(all(r['pass']==1 for r in ends))
    def test_v2_queued_inputs_cannot_substitute_for_observed_actions(self):
        rows,_=self.run_mode('play_no_actions')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='complete' for r in rows))
        self.assertTrue(any(r['status']=='prewarm_insufficient_action' for r in rows))
    def test_v2_no_safe_terrain_never_reaches_measurement(self):
        rows,_=self.run_mode('play_bad_pose')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertFalse(any(r['event']=='measure_begin' for r in rows))
    def test_v2_guard_failure_invalidates_phase(self):
        rows,_=self.run_mode('play_guard')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertTrue(any(r['event']=='scene_error' and r['status']=='route_guard' for r in rows))
        self.assertFalse(any(r['event']=='measure_begin' for r in rows))
    def test_v2_invalid_marker_cannot_modify_paths(self):self.assertEqual(self.run_mode('play_invalid')[0],[])
    def test_v3_covers_every_region_with_real_table_ids_and_unchanged_actions(self):
        rows,_=self.run_mode('coverage_complete')
        plan=rows[0]['coverage_plan']
        self.assertEqual(len(plan),24)
        source=(ROOT/'game/MetroidPrimePort/platform/port_ap_world_data.inc').read_text()
        real={(m[2],m[3],m[5]) for m in re.finditer(r'\{(\d+), 0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+), "([^"]+)"\}',source)}
        for scene in plan:
            self.assertIn((scene['world'],scene['mrea'],scene['scene']),real)
        self.assertEqual(len({r['region'] for r in plan}),7)
        self.assertEqual(len({(r['world'],r['mrea']) for r in plan}),24)
        begins=[r for r in rows if r['event']=='measure_begin']
        ends=[r for r in rows if r['event']=='measure_end']
        self.assertEqual(len(begins),96);self.assertEqual(len(ends),96)
        self.assertEqual(rows[-1]['event'],'complete')
        self.assertEqual({r['visit'] for r in ends},set(range(1,25)))
        self.assertTrue(all(r['tour_version']==3 and r['pass']==1 for r in rows))
        self.assertTrue(all(r['coverage_scope']=='assisted_room_actions' for r in rows))
        self.assertTrue(all(r['planned_rooms']==24 and r['planned_phases']==96 for r in rows))
        self.assertTrue(all(r['steering']=='closed_loop_stop_turn_v5' for r in rows))
        self.assertTrue(all(r['stick_aim_pixels_per_second']==600 and r['walk_speed_percent']==100
                            and r['pointer_aim'] is False and r['mouse_aim'] is False
                            and r['gyro_mode']==0 and r['turn_policy']=='grounded_stop_aim_go' for r in rows))
        self.assertTrue(all(r['input_ownership']=='exclusive' for r in rows))
        self.assertEqual(rows[0]['world'],'158EFE17');self.assertEqual(rows[0]['mrea'],'D1241219')
        self.assertTrue(all(r['shader_stable_frames']>=8 for r in begins))
        self.assertEqual(sum(r['event']=='rehearse_end' for r in rows),96)
        self.assertGreaterEqual(rows[-1]['monotonic_ns']-rows[0]['monotonic_ns'],2580_000_000_000)
    def test_v3_failures_stop_at_the_failed_room_without_skipping(self):
        for mode,reason in [('coverage_bad_pose','no_safe_route'),('coverage_no_actions','prewarm_insufficient_action'),('coverage_timeout','timeout')]:
            with self.subTest(mode=mode):
                rows,_=self.run_mode(mode)
                self.assertEqual(rows[-1]['event'],'incomplete')
                self.assertEqual(sum(r['event']=='scene_error' and r['status']==reason for r in rows),1)
                self.assertEqual({r['visit'] for r in rows},{1})
                self.assertFalse(any(r['event']=='measure_begin' for r in rows))
    def test_v3_guard_invalidates_sample_and_stops_without_next_warp(self):
        rows,_=self.run_mode('coverage_guard')
        self.assertEqual(rows[-1]['event'],'incomplete')
        self.assertEqual({r['visit'] for r in rows},{1})
        self.assertEqual(sum(r['event']=='scene_error' for r in rows),1)
        self.assertTrue(any(r['event']=='measure_end' and r['status']=='route_guard' for r in rows))
        self.assertFalse(any(r['event']=='measure_end' and r['status']=='ok' for r in rows))
    def test_v3_refills_after_world_reset_and_across_the_full_missile_budget(self):
        rows,_=self.run_mode('coverage_finite_ammo')
        self.assertEqual(rows[-1]['event'],'complete')
        missiles=[r for r in rows if r['event']=='measure_end' and r['view']==2]
        self.assertEqual(len(missiles),24)
        self.assertTrue(all(r['missile_projectiles']>0 for r in missiles))
    def test_v3_invalid_request_preserves_current_settings(self):self.assertEqual(self.run_mode('coverage_invalid')[0],[])
    def test_failed_marker_write_stops_without_acceptance(self):
        rows,err=self.run_mode('log_failure')
        self.assertFalse(any(r['event']=='complete' for r in rows))
        self.assertIn('marker write failed',err)

if __name__=='__main__':unittest.main()

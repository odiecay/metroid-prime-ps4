from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class TraceTests(unittest.TestCase):
    def test_zero_tls_registration_and_concurrent_retired_reuse(self):
        out=ROOT/'build/frame-trace-tests';out.mkdir(exist_ok=True)
        binary=out/'tls-registration'
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-pthread','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace_tls_registration.cpp'),'-o',str(binary)],check=True)
        for _ in range(10):
            run=subprocess.run([binary],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_spsc_order_wrap_and_frame_metrics(self):
        out=ROOT/'build/frame-trace-tests';out.mkdir(exist_ok=True)
        binary=out/'trace'
        subprocess.run(['g++-14','-std=c++20','-O2','-pthread','-fsanitize=address,undefined',
            '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace.cpp'),'-o',str(binary)],check=True)
        run=subprocess.run([binary],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_real_writer_async_summary_and_frame_correlations(self):
        import json
        out=ROOT/'build/frame-trace-tests';out.mkdir(exist_ok=True)
        binary=out/'writer';log=out/'summary.jsonl'
        log.unlink(missing_ok=True)
        subprocess.run(['g++-14','-std=c++20','-O1','-pthread','-DPRIME_TRACE_TEST=1',
            '-fsanitize=address,undefined','-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace_writer.cpp'),str(ROOT/'platform/ps4/frame_trace_writer.cpp'),
            '-o',str(binary)],check=True)
        result=subprocess.run([binary,log],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        window=json.loads(log.read_text())
        self.assertTrue(window['session_id'])
        self.assertEqual(window['dropped_events'],0)
        self.assertEqual(window['bucket_collisions'],0)
        self.assertEqual(window['unknown_stages'],0)
        stages={(x['lane'],x['stage']):x for x in window['stages']}
        sim=stages['main','simulation']
        self.assertEqual((sim['frames'],sim['sum_ns'],sim['median_ns'],sim['p95_ns']),
                         (2,30000000,10000000,20000000))
        self.assertEqual(stages['render','Dawn encoding (inclusive)']['sum_ns'],6000000)
        self.assertEqual(stages['render','Dawn Vulkan command translation (inclusive)']['sum_ns'],2000000)
        self.assertEqual(stages['render','Dawn device tick (inclusive)']['sum_ns'],400000)
        self.assertEqual(stages['render','Dawn pending command submission (inclusive)']['frames'],2)
        self.assertEqual(stages['render','Dawn resource preparation (inclusive)']['sum_ns'],600000)
        self.assertEqual(stages['render','Dawn render-pass replay (inclusive)']['sum_ns'],3000000)
        self.assertEqual(stages['render','Dawn render-pass setup (inclusive)']['sum_ns'],200000)
        self.assertEqual(window['stage_capacity'],192)
        self.assertLess(window['stage_count'],window['stage_capacity'])
        self.assertTrue(window['vulkan_call_sampling']['enabled'])
        self.assertEqual(window['vulkan_call_sampling']['period'],64)
        self.assertEqual(stages['render','Vulkan draw calls']['counter'],256)
        self.assertEqual(stages['render','Vulkan draw wall samples']['counter'],4)
        self.assertEqual(stages['render','Vulkan draw observed sampled wall ns']['counter'],2000)
        self.assertEqual(stages['render','Vulkan pipeline wall samples']['counter'],0)
        self.assertEqual(stages['render','Vulkan call profiled submissions']['counter'],2)
        self.assertEqual(stages['render','Vulkan call invalid wall clocks']['counter'],1)
        self.assertEqual(stages['render','Dawn tracker descriptor bind calls']['counter'],4)
        self.assertEqual(stages['render','Dawn tracker descriptor sets']['counter'],6)
        self.assertEqual(stages['render','Dawn bind-group usage cache hits']['counter'],10)
        self.assertEqual(stages['render','Dawn bind-group usage cache misses']['counter'],4)
        self.assertEqual(stages['main','burst work']['frames'],600)
        self.assertEqual(stages['main','complete frame wall']['frames'],603)
        self.assertEqual(stages['main','registry-stage-95']['frames'],1)

        self.assertEqual(window['slow_frames'][0]['frame_id'],2)
        self.assertEqual(window['slow_frames'][0]['lane'],'render')
        self.assertEqual(window['slow_frames'][0]['wall_ns'],30000000)

        # Launching the same binary again appends a distinct process identity.
        again=subprocess.run([binary,log],capture_output=True,text=True,timeout=30)
        self.assertEqual(again.returncode,0,again.stdout+again.stderr)
        windows=[json.loads(line) for line in log.read_text().splitlines()]
        self.assertEqual(len(windows),2)
        self.assertNotEqual(windows[0]['session_id'],windows[1]['session_id'])

    def test_real_writer_preserves_cross_window_frames_without_stale_samples(self):
        import json
        out=ROOT/'build/frame-trace-tests';out.mkdir(exist_ok=True)
        binary=out/'window-reset';log=out/'window-reset.jsonl'
        log.unlink(missing_ok=True)
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-pthread','-DPRIME_TRACE_TEST=1',
            '-fsanitize=address,undefined','-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace_window_reset.cpp'),'-o',str(binary)],check=True)
        run=subprocess.run([binary,log],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        windows=[json.loads(line)for line in log.read_text().splitlines()]
        self.assertEqual(len(windows),3)
        self.assertEqual(len({row['session_id']for row in windows}),1)
        for row in windows:
            self.assertEqual((row['dropped_events'],row['bucket_collisions'],row['unknown_stages']),(0,0,0))
        rows=[{r['stage']:r for r in row['stages']}for row in windows]
        self.assertEqual((rows[0]['old-window']['frames'],rows[0]['old-window']['sum_ns']), (2,21000000))
        self.assertEqual(rows[0]['window-counter']['counter'],7)
        self.assertNotIn('old-window',rows[1])
        self.assertEqual((rows[1]['pending-span']['frames'],rows[1]['pending-span']['sum_ns']), (1,70000000))
        self.assertEqual(rows[1]['fresh-span']['median_ns'],5000000)
        self.assertEqual(rows[1]['window-counter']['counter'],3)
        self.assertEqual(rows[2],{})
        self.assertEqual(windows[2]['slow_frames'],[])

    def test_stage_registry_is_bounded_and_reports_overflow(self):
        import json
        out=ROOT/'build/frame-trace-tests';out.mkdir(parents=True,exist_ok=True)
        program=out/'registry-overflow';log=out/'overflow.jsonl';log.unlink(missing_ok=True)
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-pthread','-DPRIME_TRACE_TEST=1',
            '-fsanitize=address,undefined','-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace_registry_overflow.cpp'),str(ROOT/'platform/ps4/frame_trace_writer.cpp'),
            '-o',str(program)],check=True)
        result=subprocess.run([program,log],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        window=json.loads(log.read_text())
        self.assertEqual(window['stage_capacity'],192)
        self.assertEqual(window['stage_count'],192)
        self.assertEqual(window['unknown_stages'],16)
        self.assertEqual(len(window['stages']),192)
        self.assertEqual(window['dropped_events'],0)

    def test_stage_lookup_owns_labels_and_rejects_stale_pointer_hits(self):
        out=ROOT/'build/frame-trace-tests';out.mkdir(parents=True,exist_ok=True)
        binary=out/'stage-lookup'
        subprocess.run(['g++-14','-std=c++20','-O1','-g','-pthread','-DPRIME_TRACE_TEST=1',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I'+str(ROOT/'game/MetroidPrimePort/extern/aurora/include'),
            str(ROOT/'tests/frame_trace_stage_lookup.cpp'),'-o',str(binary)],check=True)
        run=subprocess.run([binary],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

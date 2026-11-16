import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('analyse',ROOT/'tools/analyse_frametimes.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class AnalysisTests(unittest.TestCase):
    @staticmethod
    def draw_window(end=1, cpu=True, **changes):
        counters={'draws':640,'samples':10,'sampled early returns':1,
                  'sampled wall ns':80000,'wall clock reads':100,
                  'CPU clock reads':20 if cpu else 0,
                  'CPU samples':10 if cpu else 0,'sampled CPU ns':60000 if cpu else 0}
        counters.update({name+' ns':10000 for name,_ in m.DRAW_PREP_STAGES})
        counters.update(changes)
        return dict(end_ns=end,stages=[dict(lane='main',stage='GX prep '+name,kind=2,counter=value)
                                      for name,value in counters.items()])

    def test_draw_samples_rank_partition_without_extrapolating_frame_time(self):
        text='\n'.join(m.draw_prep_report([self.draw_window()]))
        self.assertIn('10 sampled draws / 640 observed draws',text)
        self.assertIn('0.001000 | 12.5%',text)
        self.assertIn('wall mean: 0.008000 ms',text)
        self.assertIn('CPU mean: 0.006000 ms (10 / 10',text)
        self.assertIn('not frame percentiles',text)
        self.assertIn('must not be added',text)

    def test_draw_split_counter_windows_are_rejected_individually(self):
        rows=[self.draw_window(end=1,**{'config ns':9000}),
              self.draw_window(end=2,**{'config ns':11000}),self.draw_window(end=3)]
        text='\n'.join(m.draw_prep_report(rows))
        self.assertIn('Excluded 2 inconsistent',text)
        self.assertIn('10 sampled draws / 640 observed draws',text)

    def test_draw_cpu_unavailable_does_not_report_zero_cpu_cost(self):
        text='\n'.join(m.draw_prep_report([self.draw_window(cpu=False)]))
        self.assertIn('Per-thread CPU timing unavailable',text)
        self.assertNotIn('CPU mean:',text)

    def test_cpu_query_boundary_versions_remain_distinct(self):
        old='\n'.join(m.draw_prep_report([self.draw_window()]))
        new='\n'.join(m.draw_prep_report([self.draw_window(**{'CPU queries outside wall':20})]))
        self.assertIn('CPU-query observer bias in build 00.39',old)
        self.assertIn('CPU queries are excluded',new)
        self.assertNotIn('CPU-query observer bias',new)

    def test_draw_invalid_clocks_and_dropped_events_are_not_timings(self):
        damaged=self.draw_window(end=2);damaged['dropped_events']=1
        rows=[self.draw_window(**{'invalid wall clocks':1}),damaged,
              self.draw_window(end=3,**{'CPU samples':11}),
              self.draw_window(end=4,**{'wall clock reads':19})]
        text='\n'.join(m.draw_prep_report(rows))
        self.assertIn('No complete valid sampled draw measurements',text)
        self.assertIn('Excluded 4 inconsistent',text)
        self.assertNotIn('| Sampled stage',text)

    def test_existing_capture_without_draw_sampling_has_no_sample_claim(self):
        self.assertEqual(m.draw_prep_report([self.decoder_window()]),[])

    def test_orphan_durations_are_not_pooled_into_valid_sample_counts(self):
        orphan=self.draw_window(end=2,**{'draws':0,'samples':0,'sampled early returns':0,
            'CPU samples':0,'CPU clock reads':0,'wall clock reads':0})
        text='\n'.join(m.draw_prep_report([self.draw_window(),orphan]))
        self.assertIn('Excluded 1 inconsistent',text)
        self.assertIn('wall mean: 0.008000 ms',text)
        self.assertIn('CPU mean: 0.006000 ms',text)

    @staticmethod
    def decoder_window(batches=10, samples=10, unavailable=0, frames=2, end=1):
        counters={'GX FIFO decoder batches':batches,
                  'GX FIFO decoder CPU samples':samples,
                  'GX FIFO decoder CPU unavailable batches':unavailable,
                  'GX FIFO decoder wall ns':8000000,
                  'GX FIFO decoder CPU ns':6000000}
        stages=[dict(lane='main',stage=name,kind=2,counter=value)
                for name,value in counters.items()]
        stages.append(dict(lane='main',kind=3,stage='complete frame wall',frames=frames))
        return dict(end_ns=end,stages=stages)

    def test_decoder_means_are_separate_worker_cpu_and_wall(self):
        text='\n'.join(m.decoder_report([self.decoder_window()]))
        self.assertIn('decoder wall 4.000 ms; worker CPU 3.000 ms',text)
        self.assertIn('wall 0.800 ms; CPU 0.600 ms',text)
        self.assertIn('not frame percentiles',text)
        self.assertIn('main drain waits overlap',text)

    def test_decoder_inconsistent_windows_do_not_create_fake_costs(self):
        text='\n'.join(m.decoder_report([self.decoder_window(samples=15000,end=40703890322)]))
        self.assertIn('Excluded 1 inconsistent counter windows',text)
        self.assertIn('40703890322',text)
        self.assertNotIn('mean per main frame',text)

    def test_decoder_missing_cpu_is_explicit_and_excluded(self):
        text='\n'.join(m.decoder_report([self.decoder_window(samples=0,unavailable=10)]))
        self.assertIn('CPU timing unavailable for 10 batches',text)
        self.assertNotIn('worker CPU 0.000',text)

    def test_decoder_coverage_is_window_local_not_pooled(self):
        rows=[self.decoder_window(samples=9,end=1),self.decoder_window(samples=11,end=2),self.decoder_window(end=3)]
        text='\n'.join(m.decoder_report(rows))
        self.assertIn('1 consistent, fully CPU-sampled windows',text)
        self.assertIn('Excluded 2 inconsistent',text)
        self.assertIn('10 batches / 2 completed main frames',text)

    def test_legacy_launch_reset_is_not_sorted_into_previous_launch(self):
        rows=[dict(end_ns=t) for t in (100,200,10,20)]
        self.assertEqual(m.sessions(rows),[rows[:2],rows[2:]])

    def test_explicit_launch_ids_separate_even_increasing_timestamps(self):
        rows=[dict(session_id='one',end_ns=100),dict(session_id='one',end_ns=200),
              dict(session_id='two',end_ns=300)]
        self.assertEqual(m.sessions(rows),[rows[:2],rows[2:]])

    def test_same_launch_and_rotation_keep_order(self):
        rows=[dict(session_id='one',end_ns=t) for t in (100,200,300)]
        self.assertEqual(m.sessions(rows),[rows])

    def test_disc_cache_totals_use_latest_window_without_double_counting(self):
        def cache(hits):
            return dict(block_hits=hits,block_misses=2,source_read_calls=3,
                        source_read_bytes=65536,retained_bytes=65536,
                        exact_fallbacks=0,bulk_bypass_requests=1)
        text=m.report([dict(disc_read_cache=cache(4)),dict(disc_read_cache=cache(7))])
        self.assertIn('7 / 2',text)
        self.assertIn('current retained bytes: 65536',text)
        self.assertIn('do not establish I/O latency',text)

    def test_wait_ownership_report_labels_service_as_wall_time(self):
        wait=dict(samples=3,untracked=1,waiting=0,max_wait_ns=10000000,
                  worst_queue_ns=2000000,worst_service_ns=5000000,
                  worst_callback_ns=1000000,worst_other_ns=2000000,
                  worst_entry=42,worst_offset=256,worst_length=512,worst_overlay=False)
        text=m.report([dict(dvd_wait=wait)])
        self.assertIn('10.000 ms (queue 2.000 ms, service 5.000 ms',text)
        self.assertIn('rather than measured physical disk time',text)
        self.assertIn('1 untracked',text)

    def test_window_percentiles_are_not_misrepresented_as_pooled(self):
        stage=dict(lane='main',stage='complete frame wall',kind=3,frames=10,sum_ns=200000000,
            median_ns=18000000,p95_ns=30000000,p99_ns=35000000,max_ns=35000000,over_1667=8)
        text=m.report([dict(stages=[stage],dropped_events=2)])
        self.assertIn('20.000',text)
        self.assertIn('not a pooled percentile',text)
        self.assertIn('2 dropped events',text)
        self.assertIn('8 / 0 / 0 / 0',text)

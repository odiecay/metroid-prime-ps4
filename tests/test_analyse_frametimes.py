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

class VulkanCallAnalysisTests(unittest.TestCase):
    @staticmethod
    def call_window():
        values={'Vulkan call profiled submissions':2,'Vulkan call invalid wall clocks':1}
        for prefix,name,_ in m.VULKAN_CALL_FAMILIES:
            values[name]=128
            values[f'Vulkan {prefix} wall samples']=2
            values[f'Vulkan {prefix} observed sampled wall ns']=1000
            values[f'Vulkan {prefix} cap skipped samples']=0
        values['Vulkan pipeline wall samples']=0
        values['Vulkan pipeline observed sampled wall ns']=0
        values['Vulkan draw cap skipped samples']=3
        values['Vulkan call observation checksum']=sum(values.values()) & ((1<<64)-1)
        return dict(stages=[dict(lane='render',kind=2,stage=k,counter=v)for k,v in values.items()])

    def test_only_observed_wall_means_and_explicit_cap_are_reported(self):
        text='\n'.join(m.vulkan_call_report([self.call_window()]))
        self.assertIn('| Draw / indexed / indirect | 128 | 2 | 3 | 0.500 |',text)
        self.assertIn('| Pipeline binds | 128 | 0 | 0 | unavailable |',text)
        self.assertIn('| Push constants | 128 | 2 | 0 | 0.500 |',text)
        self.assertIn('| Dynamic state | 128 | 2 | 0 | 0.500 |',text)
        self.assertIn('Invalid wall-clock intervals excluded:1'.replace(':1',': 1'),text)
        self.assertIn('not complete category cost',text)
        self.assertIn('possible early-region bias',text)

    def test_split_missing_or_dropped_bundles_are_rejected(self):
        row=self.call_window();split=dict(row)
        split['stages']=row['stages'][:-1]
        ending=dict(stages=row['stages'][-1:])
        text='\n'.join(m.vulkan_call_report([split,ending]))
        self.assertIn('Excluded2'.replace('2',' 2'),text)
        self.assertNotIn('0.500',text)
        row['dropped_events']=1
        self.assertIn('No complete valid','\n'.join(m.vulkan_call_report([row])))

    def test_disabled_old_logs_have_no_fake_call_report(self):
        self.assertEqual(m.vulkan_call_report([dict(stages=[])]),[])
        text='\n'.join(m.vulkan_call_report([dict(stages=[],vulkan_call_sampling={'enabled':True})]))
        self.assertIn('no call timing is available',text)

    def test_old_four_family_capture_remains_valid_without_fake_new_calls(self):
        row=self.call_window()
        row['stages']=[s for s in row['stages'] if not s['stage'].startswith(('Vulkan push constant','Vulkan dynamic state'))
                       and s['stage']!='Vulkan call observation checksum']
        row['stages'].append(dict(lane='render',kind=2,stage='Vulkan call observation checksum',
                                  counter=sum(s['counter'] for s in row['stages']) & ((1<<64)-1)))
        text='\n'.join(m.vulkan_call_report([row]))
        self.assertIn('| Draw / indexed / indirect | 128 | 2 | 3 | 0.500 |',text)
        self.assertNotIn('| Push constants |',text);self.assertNotIn('| Dynamic state |',text)
        self.assertNotIn('Excluded',text)

    def test_appended_category_split_observation_is_rejected(self):
        row=self.call_window()
        row['stages']=[s for s in row['stages'] if s['stage']!='Vulkan dynamic state wall samples']
        self.assertIn('No complete valid','\n'.join(m.vulkan_call_report([row])))
class DawnFrontendCallAnalysisTests(unittest.TestCase):
    @staticmethod
    def call_window():
        values={'Dawn frontend profiled batches':2,'Dawn frontend invalid wall clocks':1,
                'Dawn frontend cap skipped samples':3}
        for prefix,_ in m.DAWN_FRONTEND_FAMILIES:
            values[f'Dawn frontend {prefix} calls']=128
            values[f'Dawn frontend {prefix} wall samples']=2
            values[f'Dawn frontend {prefix} observed sampled wall ns']=1000
        values['Dawn frontend finish wall samples']=0
        values['Dawn frontend finish observed sampled wall ns']=0
        values['Dawn frontend observation checksum']=sum(values.values())&((1<<64)-1)
        return dict(stages=[dict(lane='render',kind=2,stage=k,counter=v)for k,v in values.items()])

    def test_frontend_means_have_distinct_scope_and_no_cpu_gpu_claim(self):
        text='\n'.join(m.dawn_frontend_call_report([self.call_window()]))
        self.assertIn('| DrawIndexed | 128 | 2 | 0.500 |',text)
        self.assertIn('| CommandEncoder Finish | 128 | 0 | unavailable |',text)
        self.assertIn('unseparated lock wait',text)
        self.assertIn('no complete family cost',text)
        self.assertNotIn('Vulkan driver calls',text)

    def test_frontend_split_checksum_invalid_and_dropped_windows_rejected(self):
        row=self.call_window();split=dict(row,stages=row['stages'][:-1]);ending=dict(stages=row['stages'][-1:])
        self.assertIn('Excluded 2','\n'.join(m.dawn_frontend_call_report([split,ending])))
        row=self.call_window();row['dropped_events']=1
        self.assertIn('Excluded 1','\n'.join(m.dawn_frontend_call_report([row])))
        row=self.call_window();row['stages'][2]['counter']=-1
        self.assertIn('Excluded 1','\n'.join(m.dawn_frontend_call_report([row])))

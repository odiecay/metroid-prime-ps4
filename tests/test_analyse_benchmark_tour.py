import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tour_analysis', ROOT / 'tools/analyse_benchmark_tour.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
S = 1_000_000_000


def marker(event, seconds, session='launch-a', run='tour-a', visit=0, view=0, **changes):
    return dict(schema=1, type='benchmark_tour', session_id=session, run_id=run,
                event=event, monotonic_ns=int(seconds * S), frame=10, simulation_tick=10,
                visit=visit, scene='Tallon Canyon', world='39F2DE28', mrea='2043C96E',
                **{'pass': 1, 'view': view, 'status': 'ok', **changes})


def window(start, end, session='launch-a', duration=16, **changes):
    stage = dict(lane='main', stage='complete frame wall', kind=3, frames=100,
                 sum_ns=duration * 100 * 1_000_000, median_ns=duration * 1_000_000,
                 p95_ns=(duration + 5) * 1_000_000, p99_ns=(duration + 8) * 1_000_000,
                 max_ns=(duration + 10) * 1_000_000, over_1667=20, over_20=10,
                 over_25=2, over_3333=0)
    return dict(schema=1, type='cpu_frame_window', session_id=session,
                start_ns=int(start * S), end_ns=int(end * S), stages=[stage]) | changes


def tour(session='launch-a', run='tour-a', visit=0, base=0):
    rows = [marker('start', base, session, run, visit)]
    for view in range(4):
        rows.extend([marker('measure_begin', base + 1 + view * 20, session, run, visit, view),
                     marker('measure_end', base + 13 + view * 20, session, run, visit, view)])
    rows.append(marker('complete', base + 74, session, run, visit))
    return rows


class BenchmarkTourAnalysisTests(unittest.TestCase):
    def test_only_contained_full_windows_and_existing_report_are_used(self):
        frames = [window(t, t + 5) for t in range(0, 80, 5)]
        with patch.object(m.analyse_frametimes, 'report', wraps=m.analyse_frametimes.report) as old_report:
            result = m.analyse(frames, tour())
        visit = result['runs'][0]['visits'][0]
        self.assertEqual(result['counts']['accepted_visits'], 1)
        self.assertEqual(result['counts']['selected_windows'], 4)
        self.assertEqual(result['counts']['selected_frames'], 400)
        self.assertEqual([v['boundary_windows_excluded'] for v in visit['views']], [2] * 4)
        self.assertEqual(visit['main']['mean_ms'], 16)
        self.assertEqual(visit['main']['worst_window_p95_ms'], 21)
        old_report.assert_called_once()
        self.assertEqual([r['start_ns'] for r in old_report.call_args.args[0]], [5 * S, 25 * S, 45 * S, 65 * S])
        self.assertIn('not a pooled percentile', visit['frame_report'])

    def test_exact_boundaries_allowed_short_forced_flush_excluded(self):
        markers = [marker('start', 0), marker('measure_begin', 5), marker('measure_end', 10), marker('complete', 11)]
        result = m.analyse([window(5, 9), window(5, 10)], markers, 1)
        self.assertEqual(result['counts']['selected_windows'], 1)
        self.assertEqual(result['diagnostics']['short_window'], 1)

    def test_delayed_poll_is_not_rejected_as_a_slow_window(self):
        markers = [marker('start', 0), marker('measure_begin', 1), marker('measure_end', 14), marker('complete', 15)]
        result = m.analyse([window(2, 13, duration=50)], markers, 1)
        self.assertEqual(result['counts']['selected_windows'], 1)
        self.assertEqual(result['runs'][0]['visits'][0]['main']['mean_ms'], 50)

    def test_appended_launches_with_identical_clocks_do_not_pool(self):
        frames = [window(5, 10, session='old', duration=40), window(5, 10, duration=16)]
        markers = [marker('start', 0), marker('measure_begin', 1), marker('measure_end', 13), marker('complete', 14)]
        result = m.analyse(frames, markers, 1)
        self.assertEqual(result['counts']['selected_frames'], 100)
        self.assertEqual(result['runs'][0]['visits'][0]['main']['mean_ms'], 16)

    def test_missing_exact_session_is_unavailable_not_timestamp_matched(self):
        markers = [marker('start', 0), marker('measure_begin', 1), marker('measure_end', 13)]
        result = m.analyse([window(5, 10, session='other')], markers, 1)
        self.assertEqual(result['counts']['accepted_visits'], 0)
        self.assertIn('missing_frame_session', result['runs'][0]['issues'])

    def test_missing_marker_session_never_infers_a_launch(self):
        rows = tour()
        for row in rows:
            row.pop('session_id')
        result = m.analyse([window(5, 10)], rows)
        self.assertEqual(result['counts']['runs'], 0)
        self.assertEqual(result['diagnostics']['invalid_marker'], len(rows))

    def test_errors_timeouts_incomplete_views_and_missing_end_exclude_visit(self):
        for alteration in ('error', 'timeout', 'missing_view', 'missing_end'):
            with self.subTest(alteration=alteration):
                rows = tour()
                if alteration in ('error', 'timeout'):
                    rows.insert(-1, marker('scene_error', 73.5, status='timeout' if alteration == 'timeout' else 'lost_ready'))
                elif alteration == 'missing_view':
                    rows = [r for r in rows if not(r['event'].startswith('measure_') and r['view'] == 3)]
                else:
                    rows = [r for r in rows if not(r['event'] == 'measure_end' and r['view'] == 3)]
                result = m.analyse([window(t, t + 5) for t in range(0, 80, 5)], rows)
                self.assertEqual(result['counts']['selected_windows'], 0)
                self.assertEqual(result['counts']['excluded_visits'], 1)
                self.assertIsNone(result['runs'][0]['visits'][0]['frame_report'])

    def test_completed_rooms_survive_a_later_aborted_visit_without_whole_tour_claim(self):
        rows = tour()[:-1] + [marker('measure_begin', 81, visit=1),
                              marker('scene_error', 90, visit=1, status='timeout'),
                              marker('aborted', 91, visit=1, status='timeout')]
        result = m.analyse([window(t, t + 5) for t in range(0, 100, 5)], rows)
        self.assertEqual(result['counts']['accepted_visits'], 1)
        self.assertEqual(result['counts']['excluded_visits'], 1)
        self.assertEqual(result['counts']['accepted_complete_runs'], 0)
        self.assertEqual(result['runs'][0]['status'], 'timeout')

    def test_duplicate_identity_and_clock_reset_do_not_merge_runs(self):
        result = m.analyse([window(t, t + 5) for t in range(0, 80, 5)], tour() + tour())
        self.assertEqual(result['counts']['accepted_visits'], 0)
        self.assertIn('duplicate_start_identity', result['runs'][0]['issues'])
        self.assertIn('marker_clock_regression', result['runs'][0]['issues'])

    def test_reused_frame_session_after_clock_reset_is_ambiguous(self):
        frames = [window(5, 10), window(0, 5)]
        result = m.analyse(frames, tour())
        self.assertEqual(result['counts']['accepted_visits'], 0)
        self.assertIn('ambiguous_frame_session', result['runs'][0]['issues'])

    def test_overlapping_summaries_and_measurements_are_not_double_counted(self):
        markers = [marker('start', 0), marker('measure_begin', 1), marker('measure_end', 13), marker('complete', 14)]
        result = m.analyse([window(5, 10), window(6, 11)], markers, 1)
        self.assertEqual(result['counts']['selected_windows'], 0)
        self.assertEqual(result['diagnostics']['overlapping_frame_windows'], 2)
        rows = [marker('start', 0), marker('measure_begin', 1, view=0), marker('measure_begin', 2, view=1),
                marker('measure_end', 13, view=0), marker('measure_end', 14, view=1), marker('complete', 15)]
        result = m.analyse([window(5, 10)], rows, 2)
        self.assertEqual(result['counts']['selected_windows'], 0)
        self.assertIn('overlapping_measurements', result['runs'][0]['visits'][0]['reasons'])

    def test_damaged_invalid_and_empty_windows_do_not_establish_measurements(self):
        bad_stage = window(5, 10)
        bad_stage['stages'][0]['sum_ns'] = 'bad'
        missing_end = window(5, 10)
        missing_end['end_ns'] = None
        rows = [window(5, 10, dropped_events=1), bad_stage, missing_end, window(5, 10, stages=[])]
        markers = [marker('start', 0), marker('measure_begin', 1), marker('measure_end', 13)]
        result = m.analyse(rows, markers, 1)
        self.assertEqual(result['counts']['accepted_visits'], 0)
        for key in ('trace_integrity', 'invalid_stages', 'invalid_interval', 'no_completed_main_frames'):
            self.assertEqual(result['diagnostics'][key], 1)

    def test_native_cpu_time_kind_five_is_valid_and_unknown_kinds_are_rejected(self):
        # Exact native CpuTime stage schema captured from the running v45 tour.
        # Keep this regression self-contained; private hardware logs are not
        # required by the source release or represented as synthetic timings.
        cpu = {'thread': 3, 'lane': 'main', 'stage': 'thread CPU execution', 'kind': 5,
               'frames': 25, 'sum_ns': 50214000, 'median_ns': 1099000, 'p95_ns': 6403000,
               'p99_ns': 7772000, 'max_ns': 7772000, 'counter': 0, 'unattributed_ns': 0,
               'over_1667': 0, 'over_20': 0, 'over_25': 0, 'over_3333': 0}
        row = window(5, 10)
        row['stages'].append(cpu)
        markers = [marker('start', 0), marker('measure_begin', 1),
                   marker('measure_end', 13), marker('complete', 14)]
        result = m.analyse([row], markers, 1)
        self.assertEqual(result['counts']['accepted_visits'], 1)
        self.assertIn('main: thread CPU execution', result['runs'][0]['visits'][0]['frame_report'])
        for unknown in (6, 255, -1, True):
            with self.subTest(kind=unknown):
                row['stages'][-1] = dict(cpu, kind=unknown)
                self.assertEqual(m.window_problem(row), 'invalid_stages')

    def test_window_medians_and_worst_percentiles_are_explicit(self):
        markers = [marker('start', 0), marker('measure_begin', 0), marker('measure_end', 20), marker('complete', 21)]
        result = m.analyse([window(0, 5, duration=10), window(5, 10, duration=30)], markers, 1)
        main = result['runs'][0]['visits'][0]['main']
        self.assertEqual(main['mean_ms'], 20)
        self.assertEqual(main['median_of_window_medians_ms'], 20)
        self.assertEqual(main['worst_window_p95_ms'], 35)
        self.assertIn('never pooled frame percentiles', m.report(result))
        self.assertNotIn('locked', m.report(result))

    def test_waiting_and_queue_aborted_visits_are_visible_without_measurements(self):
        rows = [marker('start', 0), marker('wait_ready', 0),
                marker('scene_error', 120, status='timeout'),
                marker('aborted', 121, visit=1, status='queue_full')]
        result = m.analyse([window(5, 10)], rows)
        self.assertEqual(result['counts']['visits'], 2)
        self.assertEqual(result['counts']['excluded_visits'], 2)
        self.assertIn('scene_error:queue_full', result['runs'][0]['visits'][1]['reasons'])

    def test_observed_room_and_frame_markers_cannot_claim_a_different_scene(self):
        rows = [marker('start', 0), marker('measure_begin', 1, actual_world='WRONG'),
                marker('measure_end', 13, observed_frames=0), marker('complete', 14)]
        result = m.analyse([window(5, 10)], rows, 1)
        self.assertEqual(result['counts']['accepted_visits'], 0)
        self.assertIn('observed_room_mismatch', result['runs'][0]['visits'][0]['reasons'])
        self.assertIn('no_observed_frames', result['runs'][0]['visits'][0]['reasons'])

    @unittest.skipUnless((ROOT / 'build/benchmark-tour-tests/tour').exists(), 'existing native tour contract binary required')
    def test_actual_tour_marker_schema_matches_with_synthetic_window_inputs(self):
        # The real state machine emits markers; only profiler windows below are
        # synthetic. This verifies schema/identity pairing, not game performance.
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([str(ROOT / 'build/benchmark-tour-tests/tour'), 'complete', directory],
                           check=True, capture_output=True, text=True)
            rows, errors = m.load(Path(directory) / 'benchmarks/tour-v1/logs/benchmark-tour.jsonl', 'benchmark_tour')
            self.assertFalse(errors)
            start = rows[0]['monotonic_ns'] // S
            end = rows[-1]['monotonic_ns'] // S
            frames = [window(t, t + 5, session=rows[0]['session_id']) for t in range(start, end + 5, 5)]
            result = m.analyse(frames, rows)
            self.assertEqual(result['counts']['accepted_visits'], 12)
            self.assertEqual(result['counts']['accepted_complete_runs'], 1)
            self.assertGreaterEqual(result['counts']['selected_windows'], 48)
            self.assertEqual(result['runs'][0]['configuration']['cache_policy'], 'isolated_persistent')

    def test_cli_latest_run_is_file_order_and_requires_exact_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            markers, frames, output = root / 'tour.jsonl', root / 'frames.jsonl', root / 'out.json'
            markers.write_text('\n'.join(map(json.dumps, tour(session='old', run='old', base=100) + tour())))
            frames.write_text('\n'.join(map(json.dumps,
                [window(t, t + 5, session='old', duration=40) for t in range(100, 180, 5)]
                + [window(t, t + 5) for t in range(0, 80, 5)])) + '\n{"truncated"')
            command = [sys.executable, str(ROOT / 'tools/analyse_benchmark_tour.py'), str(markers), str(frames), '--json-out', str(output)]
            run = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            result = json.loads(output.read_text())
            self.assertEqual([r['run_id'] for r in result['runs']], ['tour-a'])
            self.assertEqual(result['counts']['accepted_visits'], 1)
            self.assertTrue(result['parse_errors']['frames'])
            self.assertIn('Ignored malformed/truncated lines', run.stdout)
            all_runs = subprocess.run(command + ['--all-runs'], text=True, capture_output=True)
            self.assertEqual(all_runs.returncode, 0, all_runs.stderr)
            self.assertEqual(json.loads(output.read_text())['counts']['runs'], 2)


if __name__ == '__main__':
    unittest.main()

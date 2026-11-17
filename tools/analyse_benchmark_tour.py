#!/usr/bin/env python3
"""Match automated room-tour visits to complete native profiler windows."""
import argparse
from collections import Counter, defaultdict
import importlib.util
import json
import math
from pathlib import Path
import statistics

try:
    from . import analyse_frametimes
except ImportError:
    spec = importlib.util.spec_from_file_location('prime_frametimes', Path(__file__).with_name('analyse_frametimes.py'))
    analyse_frametimes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(analyse_frametimes)


WINDOW_NS = 5_000_000_000
# aurora::trace::Kind: Phase, Inclusive, Counter, Complete, Gpu, CpuTime.
STAGE_KINDS = frozenset(range(6))
FAIL_EVENTS = {'scene_error', 'error', 'timeout', 'aborted'}
TERMINAL_EVENTS = {'complete', 'aborted', 'incomplete'}
COVERAGE_REGIONS = ('Frigate Orpheon', 'Tallon Overworld', 'Chozo Ruins', 'Magmoor Caverns',
                    'Phendrana Drifts', 'Phazon Mines', 'Impact Crater')
ACTION_FIELDS = ('movement_distance_m', 'aim_degrees', 'power_projectiles', 'charged_projectiles',
                 'missile_projectiles', 'bomb_objects', 'airborne_frames', 'morphed_frames',
                 'enemy_lock_frames', 'unexpected_warps', 'jump_safe')


def coverage_manifest(start, expected_views):
    if start.get('tour_version') != 3:
        return None
    plan = start.get('coverage_plan')
    valid = (expected_views == 4 and start.get('coverage_scope') == 'assisted_room_actions'
             and start.get('planned_rooms') == 24 and start.get('planned_phases') == 96
             and isinstance(plan, list) and len(plan) == 24)
    if not valid:
        return []
    for index, row in enumerate(plan, 1):
        if (not isinstance(row, dict) or not integer(row.get('visit'))
                or row['visit'] != index or row.get('region') not in COVERAGE_REGIONS):
            return []
        if any(not isinstance(row.get(k), str) or not row[k] for k in ('scene', 'visual_targets')):
            return []
        if any(not isinstance(row.get(k), str) or len(row[k]) != 8
               or any(c not in '0123456789ABCDEF' for c in row[k]) for k in ('world', 'mrea')):
            return []
    if len({(r['world'], r['mrea']) for r in plan}) != len(plan):
        return []
    if {r['region'] for r in plan} != set(COVERAGE_REGIONS):
        return []
    return plan


def action_problem(marker, view):
    if (marker.get('workload') != 'scripted_native_input_v2'
            or marker.get('input_source') != 'virtual_gamecube_pad'):
        return 'unverified_native_action_workload'
    for name in ('movement_distance_m', 'aim_degrees'):
        value = marker.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            return 'invalid_action_observations'
    if any(not integer(marker.get(k)) for k in ACTION_FIELDS[2:-1]):
        return 'invalid_action_observations'
    if not isinstance(marker.get('jump_safe'), bool):
        return 'invalid_action_observations'
    moved = marker['movement_distance_m'] >= (2 if view == 0 else 1)
    action = (marker['aim_degrees'] >= 5 if view == 0 else
              marker['power_projectiles'] > 0 and (not marker['jump_safe'] or marker['airborne_frames'] > 0)
              if view == 1 else marker['missile_projectiles'] > 0 if view == 2 else
              marker['morphed_frames'] >= 30 and marker['bomb_objects'] > 0)
    return None if moved and action and marker['unexpected_warps'] == 0 else 'insufficient_action_evidence'


def coverage_summary(plan, visits, status, issues):
    if plan is None:
        return None
    by_id = {v['visit']: v for v in visits}
    regions = []
    for region in COVERAGE_REGIONS:
        planned = [row for row in plan if row['region'] == region]
        accepted = [row['scene'] for row in planned if by_id.get(row['visit'], {}).get('status') == 'accepted']
        regions.append({'region': region, 'planned_rooms': len(planned), 'accepted_rooms': len(accepted),
                        'status': 'measured' if planned and len(accepted) == len(planned) else
                                  'partial' if accepted else 'not_measured',
                        'pending_rooms': [row['scene'] for row in planned if row['scene'] not in accepted]})
    accepted = sum(v['status'] == 'accepted' for v in visits)
    return {'scope': 'assisted_room_actions', 'planned_rooms': len(plan), 'accepted_rooms': accepted,
            'planned_action_phases': len(plan) * 4,
            'accepted_action_phases': sum(len(v['views']) for v in visits if v['status'] == 'accepted'),
            'room_workloads_complete': bool(plan) and accepted == len(plan) and status == 'complete' and not issues,
            'regions': regions, 'visual_targets': 'planned stress categories; visual correctness not verified',
            'not_exercised': ['boss encounters and boss deaths', 'normal progression and puzzles',
                              'save/load and upgrade persistence', 'cinematics and ending',
                              'elevator and frigate escape sequence', 'visual accuracy comparisons',
                              'controller reconnect and audio synchronisation'],
            'release_acceptance': 'pending; room-action coverage is not a complete-game playthrough'}


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def identity(row):
    values = row.get('session_id'), row.get('run_id')
    return values if all(isinstance(v, str) and v for v in values) else None


def load(path, record_type):
    rows, errors = [], []
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        try:
            row = json.loads(line)
            if not isinstance(row, dict):
                raise ValueError('record must be an object')
            if row.get('type') == record_type:
                rows.append(row)
        except (ValueError, TypeError):
            errors.append(number)
    return rows, errors


def window_problem(row):
    if row.get('schema') != 1 or row.get('type') != 'cpu_frame_window':
        return 'unsupported_frame_schema'
    start, end = row.get('start_ns'), row.get('end_ns')
    if not integer(start) or not integer(end) or end <= start:
        return 'invalid_interval'
    if end - start < WINDOW_NS:
        return 'short_window'
    if any(not integer(row.get(k, 0)) or row.get(k, 0) for k in
           ('dropped_events', 'bucket_collisions', 'unknown_stages')):
        return 'trace_integrity'
    stages = row.get('stages')
    if not isinstance(stages, list):
        return 'invalid_stages'
    for stage in stages:
        if not isinstance(stage, dict) or stage.get('lane') not in ('main', 'render'):
            return 'invalid_stages'
        if not integer(stage.get('kind')) or stage['kind'] not in STAGE_KINDS or not isinstance(stage.get('stage'), str):
            return 'invalid_stages'
        if not integer(stage.get('frames')):
            return 'invalid_stages'
        fields = ('counter',) if stage['kind'] == 2 else ('sum_ns', 'median_ns', 'p95_ns', 'max_ns')
        if any(not integer(stage.get(k)) for k in fields):
            return 'invalid_stages'
        if any(not integer(stage.get(k, 0)) for k in
               ('p99_ns', 'unattributed_ns', 'over_1667', 'over_20', 'over_25', 'over_3333')):
            return 'invalid_stages'
    if not any(s['lane'] == 'main' and s['kind'] == 3 and s['frames'] for s in stages):
        return 'no_completed_main_frames'
    return None


def main_summary(rows):
    values = [s for row in rows for s in row['stages'] if s['lane'] == 'main' and s['kind'] == 3]
    count = sum(s['frames'] for s in values)
    if not count:
        return None
    measured = [s for s in values if s['frames']]
    return {
        'frames': count,
        'mean_ms': sum(s['sum_ns'] for s in values) / count / 1e6,
        'median_of_window_medians_ms': statistics.median(s['median_ns'] for s in measured) / 1e6,
        'worst_window_p95_ms': max(s['p95_ns'] for s in measured) / 1e6,
        'worst_window_p99_ms': (max(s['p99_ns'] for s in measured) / 1e6
                                if all('p99_ns' in s for s in measured) else None),
        'maximum_ms': max(s['max_ns'] for s in measured) / 1e6,
        **{k: sum(s.get(k, 0) for s in values)
           for k in ('over_1667', 'over_20', 'over_25', 'over_3333')},
    }


def analyse(frame_rows, marker_rows, expected_views=4):
    """Keep each launch/run/visit distinct; never split a profiler window."""
    if not integer(expected_views) or not 1 <= expected_views <= 16:
        raise ValueError('expected_views must be between 1 and 16')
    diagnostics = Counter()
    runs = {}
    for index, marker in enumerate(marker_rows):
        key = identity(marker)
        if (marker.get('schema') != 1 or marker.get('type') != 'benchmark_tour' or key is None
                or not integer(marker.get('monotonic_ns')) or not isinstance(marker.get('event'), str)):
            diagnostics['invalid_marker'] += 1
            continue
        run = runs.setdefault(key, {'session_id': key[0], 'run_id': key[1], 'markers': [],
                                    'visits': {}, 'issues': [], 'first_index': index})
        if run['markers'] and marker['monotonic_ns'] < run['markers'][-1]['monotonic_ns']:
            run['issues'].append('marker_clock_regression')
        run['markers'].append(marker)

    # Repeated launch IDs or a backwards clock make a frame capture ambiguous,
    # even when a malformed marker happens to reuse that same ID.
    captures = defaultdict(list)
    sequenced = []
    for row in frame_rows:
        if not isinstance(row.get('session_id'), str) or not row['session_id']:
            diagnostics['frame_missing_session_id'] += 1
        elif not integer(row.get('end_ns')):
            diagnostics['invalid_interval'] += 1
        else:
            sequenced.append(row)
    for capture in analyse_frametimes.sessions(sequenced):
        session_ids = {w.get('session_id') for w in capture}
        if len(session_ids) != 1 or not next(iter(session_ids), None):
            diagnostics['frame_missing_session_id'] += len(capture)
            continue
        captures[next(iter(session_ids))].append(capture)
    frames = {}
    for session_id, groups in captures.items():
        if len(groups) != 1:
            diagnostics['ambiguous_frame_session'] += 1
            frames[session_id] = []
            continue
        candidates = []
        for row in groups[0]:
            reason = window_problem(row)
            if reason:
                diagnostics[reason] += 1
            else:
                candidates.append(row)
        # Duplicate/overlapping summaries would double count frames. Reject
        # both sides; do not silently deduplicate a damaged capture.
        overlap = set()
        ordered = sorted(enumerate(candidates), key=lambda item: item[1]['start_ns'])
        for pos, (i, row) in enumerate(ordered):
            for j, other in ordered[pos + 1:]:
                if other['start_ns'] >= row['end_ns']:
                    break
                overlap.update((i, j))
        diagnostics['overlapping_frame_windows'] += len(overlap)
        frames[session_id] = [row for i, row in enumerate(candidates) if i not in overlap]

    intervals = []
    for key, run in runs.items():
        starts = [m for m in run['markers'] if m['event'] == 'start']
        terminals = [m for m in run['markers'] if m['event'] in TERMINAL_EVENTS]
        if len(starts) != 1:
            run['issues'].append('missing_start' if not starts else 'duplicate_start_identity')
        if len(terminals) > 1:
            run['issues'].append('multiple_terminal_events')
        if key[0] not in captures:
            run['issues'].append('missing_frame_session')
        elif len(captures[key[0]]) != 1:
            run['issues'].append('ambiguous_frame_session')
        run['status'] = terminals[-1]['event'] if terminals else 'incomplete'
        if terminals and terminals[-1].get('status', 'ok') != 'ok':
            run['status'] = terminals[-1].get('status') or 'error'
        plan = coverage_manifest(starts[0], expected_views) if starts else None
        run['coverage_plan'] = plan
        if plan == []:
            run['issues'].append('invalid_coverage_manifest')
        for marker in run['markers']:
            event = marker['event']
            if plan is not None and (marker.get('tour_version') != 3
                    or marker.get('coverage_scope') != 'assisted_room_actions'
                    or marker.get('planned_rooms') != 24 or marker.get('planned_phases') != 96):
                run['issues'].append('changed_coverage_metadata')
            if event not in {'wait_ready', 'ready', 'warm_begin', 'measure_begin', 'measure_end', *FAIL_EVENTS}:
                continue
            visit_id = marker.get('visit')
            if not integer(visit_id):
                run['issues'].append('invalid_visit_identity')
                continue
            visit = run['visits'].setdefault(visit_id, {
                'visit': visit_id, 'scene': marker.get('scene'), 'pass': marker.get('pass'),
                'world': marker.get('world'), 'mrea': marker.get('mrea'), 'views': {}, 'issues': [],
                'region': marker.get('region'), 'visual_targets': marker.get('visual_targets'),
            })
            if any(visit[field] != marker.get(field) for field in ('scene', 'pass', 'world', 'mrea')):
                visit['issues'].append('changed_visit_identity')
            if plan is not None and any(visit[field] != marker.get(field) for field in ('region', 'visual_targets')):
                visit['issues'].append('changed_coverage_visit_metadata')
            if not isinstance(visit['scene'], str) or not visit['scene'] or not integer(visit['pass']) or visit['pass'] < 1:
                visit['issues'].append('invalid_visit_metadata')
            retry_end = event == 'measure_end' and marker.get('status') == 'pipeline_compile_during_measure'
            if event in FAIL_EVENTS or (marker.get('status', 'ok') != 'ok' and not retry_end):
                status = marker.get('status')
                visit['issues'].append('scene_error:' + str(status if status and status != 'ok' else event))
                continue
            if event not in {'measure_begin', 'measure_end'}:
                continue
            if any(field in marker and marker[field] != marker.get(target)
                   for field, target in (('actual_world', 'world'), ('actual_mrea', 'mrea'))):
                visit['issues'].append('observed_room_mismatch')
            if event == 'measure_end' and not retry_end and 'observed_frames' in marker and not marker['observed_frames']:
                visit['issues'].append('no_observed_frames')
            view = marker.get('view')
            if not integer(view) or view >= expected_views:
                visit['issues'].append('invalid_view')
                continue
            attempt = marker.get('measure_attempt', 0)
            if not integer(attempt) or attempt > 2:
                visit['issues'].append('invalid_measure_attempt')
                continue
            pair = visit['views'].setdefault(view, {}).setdefault(attempt, {'attempt': attempt})
            if plan is not None and event == 'measure_end' and not retry_end:
                pair['action_evidence'] = {name: marker.get(name) for name in ACTION_FIELDS}
                problem = action_problem(marker, view)
                if problem:
                    visit['issues'].append(problem)
            if retry_end:
                pair['failed_status'] = marker['status']
            if marker.get('shader_gate_required'):
                pair['shader_gate_required'] = True
                gate = {name: marker.get(name) for name in
                        ('shaders_queued', 'shaders_background', 'shaders_active', 'shaders_pending', 'shader_prefetch_queued', 'shader_prefetch_active',
                         'shader_drop_pending', 'shader_failures', 'shader_epoch', 'shader_completed', 'shader_stable_frames')}
                pair['shader_begin' if event == 'measure_begin' else 'shader_end'] = gate
            endpoint = 'begin_ns' if event == 'measure_begin' else 'end_ns'
            if endpoint in pair:
                visit['issues'].append('duplicate_' + event)
            pair[endpoint] = marker['monotonic_ns']
        if plan:
            expected = {row['visit']: row for row in plan}
            for visit in run['visits'].values():
                row = expected.get(visit['visit'])
                if row is None or visit['pass'] != 1 or any(visit.get(k) != row.get(k) for k in
                        ('scene', 'world', 'mrea', 'region', 'visual_targets')):
                    visit['issues'].append('coverage_plan_mismatch')
            for row in plan:
                run['visits'].setdefault(row['visit'], {**row, 'pass': 1, 'views': {}, 'issues': ['not_visited']})
        for visit in run['visits'].values():
            for view, attempts in list(visit['views'].items()):
                # Only the final attempt is eligible; never cherry-pick an old
                # success or pool windows from a cancelled compilation interval.
                last = max(attempts)
                pair = attempts[last]
                pair['discarded_attempts'] = [dict(attempt=number, begin_ns=old.get('begin_ns'),
                    end_ns=old.get('end_ns'), status=old.get('failed_status', 'superseded'))
                    for number, old in sorted(attempts.items()) if number != last]
                visit['views'][view] = pair
                if pair.get('failed_status'):
                    visit['issues'].append('failed_final_attempt:' + pair['failed_status'])
                if pair.get('shader_gate_required'):
                    begin, end = pair.get('shader_begin', {}), pair.get('shader_end', {})
                    zeros = ('shaders_queued', 'shaders_background', 'shaders_active', 'shaders_pending', 'shader_prefetch_queued', 'shader_prefetch_active',
                             'shader_drop_pending', 'shader_failures')
                    valid = all(integer(row.get(name)) and row[name] == 0 for row in (begin, end) for name in zeros)
                    valid &= integer(begin.get('shader_stable_frames')) and begin.get('shader_stable_frames', 0) >= 8
                    valid &= all(integer(begin.get(name)) and begin.get(name) == end.get(name)
                                 for name in ('shader_epoch', 'shader_completed'))
                    if not valid:
                        visit['issues'].append('shader_gate_unverified')
                if 'begin_ns' not in pair or 'end_ns' not in pair or pair.get('end_ns', 0) <= pair.get('begin_ns', 0):
                    visit['issues'].append('incomplete_measurement')
                    continue
                if starts and pair['begin_ns'] < starts[0]['monotonic_ns']:
                    visit['issues'].append('measurement_before_start')
                if terminals and pair['end_ns'] > terminals[0]['monotonic_ns']:
                    visit['issues'].append('measurement_after_terminal')
                intervals.append((key, visit, view, pair))
            if set(visit['views']) != set(range(expected_views)):
                visit['issues'].append('incomplete_views')

    # Runs may share one process, but their measuring intervals may not share
    # time. Mark both visits invalid instead of assigning their windows twice.
    for i, (key, visit, _, pair) in enumerate(intervals):
        for other_key, other_visit, _, other_pair in intervals[i + 1:]:
            if key[0] == other_key[0] and max(pair['begin_ns'], other_pair['begin_ns']) < min(pair['end_ns'], other_pair['end_ns']):
                visit['issues'].append('overlapping_measurements')
                other_visit['issues'].append('overlapping_measurements')

    output, used = [], set()
    for key, run in runs.items():
        visits = []
        for visit in run['visits'].values():
            selected = []
            per_view = []
            for view, pair in sorted(visit['views'].items()):
                chosen = []
                boundary_windows = 0
                if 'begin_ns' in pair and 'end_ns' in pair:
                    chosen = [row for row in frames.get(key[0], [])
                              if pair['begin_ns'] <= row['start_ns'] and row['end_ns'] <= pair['end_ns']]
                    boundary_windows = sum(max(pair['begin_ns'], row['start_ns']) < min(pair['end_ns'], row['end_ns'])
                                           and row not in chosen for row in frames.get(key[0], []))
                per_view.append({'view': view, **pair, 'contained_windows': len(chosen),
                                 'boundary_windows_excluded': boundary_windows})
                if not chosen:
                    visit['issues'].append('view_without_complete_window')
                selected.extend(chosen)
            reasons = sorted(set(run['issues'] + visit['issues']))
            accepted = not reasons
            if accepted:
                used.update((key[0], row['start_ns'], row['end_ns']) for row in selected)
            else:
                selected = []
            visits.append({
                'visit': visit['visit'], 'scene': visit['scene'], 'pass': visit['pass'],
                'world': visit['world'], 'mrea': visit['mrea'],
                'region': visit.get('region'), 'visual_targets': visit.get('visual_targets'),
                'status': 'accepted' if accepted else 'excluded', 'reasons': reasons,
                'views': per_view, 'selected_windows': len(selected),
                'selected_window_duration_ns': sum(row['end_ns'] - row['start_ns'] for row in selected),
                'window_intervals': [{'start_ns': row['start_ns'], 'end_ns': row['end_ns']} for row in selected],
                'main': main_summary(selected) if selected else None,
                # Keep the established analyser authoritative for stage costs,
                # counters, GPU limitations, and overlapping/inclusive stages.
                'frame_report': analyse_frametimes.report(selected) if selected else None,
            })
        output.append({
            'session_id': key[0], 'run_id': key[1], 'status': run['status'],
            'issues': sorted(set(run['issues'])), 'first_index': run['first_index'], 'visits': visits,
            'configuration': {name: run['markers'][0][name] for name in
                              ('workload', 'cache_policy', 'assisted_inventory', 'invulnerable',
                               'tour_version', 'coverage_scope', 'planned_rooms', 'planned_phases', 'nominal_minimum_seconds')
                              if name in run['markers'][0]},
            'coverage': coverage_summary(run.get('coverage_plan'), visits, run['status'], run['issues']),
        })
    counts = {
        'runs': len(output), 'terminal_complete_runs': sum(run['status'] == 'complete' for run in output),
        'accepted_complete_runs': sum(run['status'] == 'complete' and not run['issues'] and bool(run['visits'])
                                      and all(v['status'] == 'accepted' for v in run['visits']) for run in output),
        'visits': sum(len(run['visits']) for run in output),
        'accepted_visits': sum(v['status'] == 'accepted' for run in output for v in run['visits']),
        'selected_windows': len(used), 'input_frame_windows': len(frame_rows),
        'input_markers': len(marker_rows),
        'selected_frames': sum(v['main']['frames'] for run in output for v in run['visits'] if v['main']),
        'coverage_runs': sum(run['coverage'] is not None for run in output),
        'coverage_complete_regions': sum(r['status'] == 'measured' for run in output if run['coverage']
                                         for r in run['coverage']['regions']),
    }
    counts['excluded_visits'] = counts['visits'] - counts['accepted_visits']
    return {'schema': 1, 'counts': counts, 'diagnostics': dict(sorted(diagnostics.items())), 'runs': output}


def report(result, details=False):
    counts = result['counts']
    lines = [f"Tour capture: {counts['runs']} run(s), {counts['accepted_visits']} accepted / "
             f"{counts['visits']} room/pass visits, {counts['selected_windows']} complete windows.", '']
    for run in result['runs']:
        if run.get('coverage'):
            coverage = run['coverage']
            lines.append(f"Coverage: {coverage['accepted_rooms']}/{coverage['planned_rooms']} planned room workloads accepted; "
                         f"{sum(r['status'] == 'measured' for r in coverage['regions'])}/7 regions fully sampled.")
            lines.append('Bosses, progression, saves, cinematics and visual accuracy remain pending; this is assisted room-action coverage.')
        lines.extend([f"Run {run['run_id']} / session {run['session_id']}: {run['status']}.",
                      '| Room / pass | Status | Windows / frames | Mean ms | Window median ms | Worst window P95 ms | Worst ms | >20 ms |',
                      '|---|---|---:|---:|---:|---:|---:|---:|'])
        for visit in run['visits']:
            label = str(visit['scene']).replace('|', '\\|').replace('\n', ' ') + ' / ' + str(visit['pass'])
            m = visit['main']
            if m:
                lines.append(f"| {label} | accepted | {visit['selected_windows']} / {m['frames']} | "
                             f"{m['mean_ms']:.3f} | {m['median_of_window_medians_ms']:.3f} | "
                             f"{m['worst_window_p95_ms']:.3f} | {m['maximum_ms']:.3f} | {m['over_20']} |")
            else:
                lines.append(f"| {label} | excluded | 0 | — | — | — | — | — |")
                lines.append('Excluded visit ' + str(visit['visit']) + ': ' + ', '.join(visit['reasons']) + '.')
        if run['issues']:
            lines.append('Run issues: ' + ', '.join(run['issues']) + '.')
        if details:
            for visit in run['visits']:
                if visit['frame_report']:
                    lines.extend(['', f"{visit['scene']} / pass {visit['pass']} / visit {visit['visit']}:", visit['frame_report']])
        lines.append('')
    if result['diagnostics']:
        lines.append('Excluded/unusable input: ' + ', '.join(f'{name}={n}' for name, n in result['diagnostics'].items()) + '.')
    lines.extend([
        'Only intact profiler windows of at least five seconds wholly inside a successful measurement interval are used. '
        'Every expected view must have a contained window; failed, timed-out and incomplete visits are excluded.',
        'Medians are medians of window medians; P95/P99 are worst window percentiles, never pooled frame percentiles. '
        'Window durations can exceed five seconds when polling is delayed. '
        'Stages and asynchronous frame completions can cross poll boundaries; inclusive CPU stages overlap.',
        'Results cover only the recorded room-tour workload, including scripted actions when declared. '
        'They do not establish normal controller operation, audio synchronisation, progression or complete-game reliability. '
        'Its main frame times measure throughput, not input/display latency; CPU submission time is not GPU execution time.',
        'Passes identify repeat order, not proven cold/warm shader-cache state. '
        'Assisted inventory, invulnerability and cache policy are preserved in the machine report when the markers declare them.',
    ])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('markers', type=Path, help='benchmark-tour.jsonl')
    parser.add_argument('frames', type=Path, help='frame-profile.jsonl from the same benchmark launch')
    parser.add_argument('--run-id')
    parser.add_argument('--session-id')
    parser.add_argument('--all-runs', action='store_true', help='Report each run separately; never pool launches')
    parser.add_argument('--expected-views', type=int, default=4)
    parser.add_argument('--details', action='store_true', help='Include the established per-stage profiler report')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--json-out', type=Path)
    args = parser.parse_args()
    markers, marker_errors = load(args.markers, 'benchmark_tour')
    frames, frame_errors = load(args.frames, 'cpu_frame_window')
    if args.run_id:
        markers = [m for m in markers if m.get('run_id') == args.run_id]
    if args.session_id:
        markers = [m for m in markers if m.get('session_id') == args.session_id]
    identities = list(dict.fromkeys(key for m in markers if (key := identity(m)) is not None))
    if not identities:
        parser.error('no tour markers with exact session_id and run_id; timestamp-only matching is not supported')
    if args.run_id and not args.session_id and len(identities) > 1:
        parser.error('run_id occurs in multiple sessions; also select --session-id')
    if not args.all_runs:
        latest = identities[-1]
        markers = [m for m in markers if identity(m) == latest]
    try:
        result = analyse(frames, markers, args.expected_views)
    except ValueError as exc:
        parser.error(str(exc))
    result['parse_errors'] = {'markers': marker_errors, 'frames': frame_errors}
    text = report(result, args.details)
    if marker_errors or frame_errors:
        text += f'Ignored malformed/truncated lines: markers {marker_errors}; frames {frame_errors}.\n'
    if args.json_out:
        args.json_out.write_text(json.dumps(result, indent=2) + '\n')
    if args.out:
        args.out.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()

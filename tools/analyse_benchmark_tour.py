#!/usr/bin/env python3
"""Match automated room-tour visits to complete native profiler windows."""
import argparse
from collections import Counter, defaultdict
import importlib.util
import json
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
        for marker in run['markers']:
            event = marker['event']
            if event not in {'wait_ready', 'ready', 'warm_begin', 'measure_begin', 'measure_end', *FAIL_EVENTS}:
                continue
            visit_id = marker.get('visit')
            if not integer(visit_id):
                run['issues'].append('invalid_visit_identity')
                continue
            visit = run['visits'].setdefault(visit_id, {
                'visit': visit_id, 'scene': marker.get('scene'), 'pass': marker.get('pass'),
                'world': marker.get('world'), 'mrea': marker.get('mrea'), 'views': {}, 'issues': [],
            })
            if any(visit[field] != marker.get(field) for field in ('scene', 'pass', 'world', 'mrea')):
                visit['issues'].append('changed_visit_identity')
            if not isinstance(visit['scene'], str) or not visit['scene'] or not integer(visit['pass']) or visit['pass'] < 1:
                visit['issues'].append('invalid_visit_metadata')
            if event in FAIL_EVENTS or marker.get('status', 'ok') != 'ok':
                status = marker.get('status')
                visit['issues'].append('scene_error:' + str(status if status and status != 'ok' else event))
                continue
            if event not in {'measure_begin', 'measure_end'}:
                continue
            if any(field in marker and marker[field] != marker.get(target)
                   for field, target in (('actual_world', 'world'), ('actual_mrea', 'mrea'))):
                visit['issues'].append('observed_room_mismatch')
            if event == 'measure_end' and 'observed_frames' in marker and not marker['observed_frames']:
                visit['issues'].append('no_observed_frames')
            view = marker.get('view')
            if not integer(view) or view >= expected_views:
                visit['issues'].append('invalid_view')
                continue
            pair = visit['views'].setdefault(view, {})
            endpoint = 'begin_ns' if event == 'measure_begin' else 'end_ns'
            if endpoint in pair:
                visit['issues'].append('duplicate_' + event)
            pair[endpoint] = marker['monotonic_ns']
        for visit in run['visits'].values():
            for view, pair in visit['views'].items():
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
                              ('workload', 'cache_policy', 'assisted_inventory', 'invulnerable')
                              if name in run['markers'][0]},
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
    }
    counts['excluded_visits'] = counts['visits'] - counts['accepted_visits']
    return {'schema': 1, 'counts': counts, 'diagnostics': dict(sorted(diagnostics.items())), 'runs': output}


def report(result, details=False):
    counts = result['counts']
    lines = [f"Tour capture: {counts['runs']} run(s), {counts['accepted_visits']} accepted / "
             f"{counts['visits']} room/pass visits, {counts['selected_windows']} complete windows.", '']
    for run in result['runs']:
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
        'This fixed-camera room tour does not establish combat, transition, audio, controls or complete-game reliability. '
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

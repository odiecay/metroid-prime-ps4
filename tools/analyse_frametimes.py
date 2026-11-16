#!/usr/bin/env python3
"""Summarise native profiler JSONL. Never infer GPU time from CPU duration."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics


def load(path):
    rows=[];errors=[]
    for number,line in enumerate(Path(path).read_text().splitlines(),1):
        try:
            row=json.loads(line)
            if row.get('type')=='cpu_frame_window': rows.append(row)
        except (ValueError,TypeError): errors.append(number)
    return rows,errors


def sessions(rows):
    """Keep appended launches separate, including legacy captures without IDs.

    Orbis monotonic time is process relative. A later launch can have smaller
    timestamps and repeated frame IDs, so sorting or pooling by end_ns mixes
    unrelated gameplay. New writers identify launches explicitly; old logs
    can only be split where their monotonic clock moves backwards.
    """
    result=[]
    for row in rows:
        if result:
            previous=result[-1][-1]
            previous_id=previous.get('session_id')
            current_id=row.get('session_id')
            changed_id=current_id is not None and current_id!=previous_id
            clock_reset=row.get('end_ns',0)<previous.get('end_ns',0)
        else:
            changed_id=clock_reset=False
        if not result or changed_id or clock_reset:
            result.append([])
        result[-1].append(row)
    return result


def decoder_report(rows):
    """Window-aligned worker costs, accepting only internally consistent totals.

    The consumer can split a frame's separate counter events across two polls.
    Reject mismatched windows instead of inventing coverage or percentiles.
    """
    accepted=[]; rejected=[]; unavailable=0
    for window in rows:
        counters=defaultdict(int)
        for stage in window.get('stages',[]):
            if stage['kind']==2 and stage['lane']=='main':
                counters[stage['stage']]+=stage.get('counter',0)
        batches=counters['GX FIFO decoder batches']
        if not batches: continue
        samples=counters['GX FIFO decoder CPU samples']
        missing=counters['GX FIFO decoder CPU unavailable batches']
        wall=counters['GX FIFO decoder wall ns']
        cpu=counters['GX FIFO decoder CPU ns']
        frames=sum(stage['frames'] for stage in window.get('stages',[])
                   if stage['kind']==3 and stage['lane']=='main')
        if samples+missing!=batches or counters['GX FIFO decoder invalid wall batches']:
            rejected.append(window.get('end_ns'))
            continue
        unavailable+=missing
        if samples==batches and frames:
            accepted.append((frames,batches,wall,cpu))
    if not accepted and not rejected and not unavailable: return []
    lines=['','FIFO decoder worker (separate thread; inclusive wall and CPU must not be added):']
    if accepted:
        frames,batches,wall,cpu=map(sum,zip(*accepted))
        lines += [f'- {len(accepted)} consistent, fully CPU-sampled windows: {batches} batches / {frames} completed main frames.',
                  f'- Approximate window-aligned mean per main frame: decoder wall {wall/frames/1e6:.3f} ms; worker CPU {cpu/frames/1e6:.3f} ms.',
                  f'- Mean per decoder batch: wall {wall/batches/1e6:.3f} ms; CPU {cpu/batches/1e6:.3f} ms.']
    if unavailable:
        lines.append(f'- CPU timing unavailable for {unavailable} batches in otherwise consistent windows; those windows are excluded from CPU/frame means.')
    if rejected:
        lines.append(f'- Excluded {len(rejected)} inconsistent counter windows (end_ns: '+', '.join(map(str,rejected))+'). Separate counter events can straddle polls; persistent mismatch requires investigation.')
    lines.append('These are means, not frame percentiles. Batch timing excludes FIFO mutex acquisition and draw-done callbacks; main drain waits overlap decoder work.')
    return lines


DRAW_PREP_STAGES = (
    ('vertex index', 'Vertex/index preparation'),
    ('arrays', 'Resident arrays and uploads'),
    ('config', 'Pipeline configuration and shader analysis'),
    ('pipeline lookup', 'Pipeline lookup/touch'),
    ('texture resolve', 'Texture resolution'),
    ('bind groups', 'Bind-group lookup/creation'),
    ('uniforms', 'Uniform and fog packing'),
    ('recording', 'Command recording'),
)


def draw_prep_report(rows):
    """Rank sampled decoder work without inventing per-frame percentiles.

    The producer emits each counter separately after draining the decoder.
    A consumer poll can split that set. Require the sampled stage partition
    and sample/clock counts to agree within each window; never pool two broken
    windows into a seemingly valid measurement.
    """
    totals=defaultdict(int); rejected=[]; observed=False
    for window in rows:
        counters=defaultdict(int)
        for stage in window.get('stages',[]):
            if stage['kind']==2 and stage['lane']=='main' and stage['stage'].startswith('GX prep '):
                counters[stage['stage'][8:]]+=stage.get('counter',0)
        if not counters: continue
        observed=True
        draws,samples=counters['draws'],counters['samples']
        stage_sum=sum(counters[name+' ns'] for name,_ in DRAW_PREP_STAGES)
        valid=(all(value>=0 for value in counters.values()) and samples<=draws and
               counters['sampled early returns']<=samples and counters['CPU samples']<=samples and
               counters['sampled wall ns']==stage_sum and not counters['invalid wall clocks'] and
               counters['wall clock reads']>=samples*2 and
               counters['CPU clock reads'] in (0,samples*2) and
               counters['CPU queries outside wall'] in (0,counters['CPU clock reads']) and
               (samples>0 or (stage_sum==0 and counters['wall clock reads']==0)) and
               (counters['CPU samples']>0 or counters['sampled CPU ns']==0) and
               (counters['CPU clock reads']>0 or counters['CPU samples']==0) and
               not any(window.get(key,0) for key in ('dropped_events','bucket_collisions','unknown_stages')))
        if not valid:
            rejected.append(window.get('end_ns'));continue
        for name,value in counters.items(): totals[name]+=value
    if not observed: return []
    lines=['','Sampled GX draw preparation (FIFO worker):']
    draws,samples=totals['draws'],totals['samples']
    if samples:
        wall=totals['sampled wall ns']
        lines += [f'- {samples} sampled draws / {draws} observed draws; '
                  f'{totals["sampled early returns"]} sampled early returns.',
                  '', '| Sampled stage | Mean ms / sampled draw | Sampled wall share |',
                  '|---|---:|---:|']
        for name,label in sorted(DRAW_PREP_STAGES,key=lambda item:totals[item[0]+' ns'],reverse=True):
            cost=totals[name+' ns']
            share=f'{cost/wall*100:.1f}%' if wall else 'unavailable'
            lines.append(f'| {label} | {cost/samples/1e6:.6f} | {share} |')
        lines += [f'- Whole sampled draw wall mean: {wall/samples/1e6:.6f} ms.',
                  f'- Clock calls: {totals["wall clock reads"]} monotonic; {totals["CPU clock reads"]} per-thread CPU.']
        cpu_samples=totals['CPU samples']
        if cpu_samples:
            lines.append(f'- Whole sampled draw CPU mean: {totals["sampled CPU ns"]/cpu_samples/1e6:.6f} ms '
                         f'({cpu_samples} / {samples} samples available).')
        else:
            lines.append('- Per-thread CPU timing unavailable for these sampled draws.')
        if totals['CPU clock reads'] and totals['CPU queries outside wall']!=totals['CPU clock reads']:
            lines.append('CPU-query observer bias in build 00.39: its two per-draw CPU queries are inside '
                         'the wall interval. Merged primitives stay in vertex/index preparation, so both '
                         'query costs are charged there. Treat these raw shares as instrumented observations, '
                         'not a production hotspot ranking. Full decoder-batch CPU and GPU-query measurements '
                         'are independent of this attribution bias.')
        elif totals['CPU clock reads']:
            lines.append('Opt-in CPU interval surrounds the sampled wall interval and includes additional '
                         'observer work. CPU queries are excluded from the wall-stage partition.')
    else:
        lines.append('- No complete valid sampled draw measurements in the selected windows.')
    if rejected:
        lines.append(f'- Excluded {len(rejected)} inconsistent or damaged counter windows (end_ns: '+
                     ', '.join(map(str,rejected))+').')
    lines.append('One draw in 64 is sampled with a rotating frame offset. Values include instrumentation overhead '
                 'and OS descheduling; stage rows partition sampled wall time, not GPU time. '
                 'These are sampled draw means, not frame percentiles or a measured whole-frame saving. '
                 'The CPU interval overlaps wall stages and must not be added to them.')
    return lines


def report(rows):
    stages=defaultdict(list)
    for window in rows:
        for stage in window.get('stages',[]):
            stages[(stage['lane'],stage['stage'],stage['kind'])].append(stage)
    lines=['| Thread / stage | Mean ms | Window median ms | Worst window P95 ms | Worst ms | Frames |',
           '|---|---:|---:|---:|---:|---:|']
    for (lane,name,kind),values in sorted(stages.items()):
        if kind==4: lane='GPU sample'
        count=sum(v['frames'] for v in values)
        if kind==2 or not count: continue
        mean=sum(v['sum_ns'] for v in values)/count/1e6
        median=statistics.median(v['median_ns'] for v in values if v['frames'])/1e6
        p95=max(v['p95_ns'] for v in values)/1e6
        worst=max(v['max_ns'] for v in values)/1e6
        lines.append(f'| {lane}: {name} | {mean:.3f} | {median:.3f} | {p95:.3f} | {worst:.3f} | {count} |')
    lines+=['','Medians are medians of window medians; P95 is the worst window P95, not a pooled percentile.',
            'Phase rows partition a thread’s wall time; inclusive rows overlap them and each other.',
            'Render lifetime includes producer waits. Thread wall time includes OS descheduling; CPU execution rows use the native per-thread counter when accepted by Orbis. Unsupported counters are reported explicitly.',
            'Main frame wall time measures loop throughput, not end-to-end input/display latency.',
            'GPU rows contain fresh query-window averages sampled periodically; their percentiles describe windows, not individual GPU frames. CPU submit time is not GPU time.']
    drops=sum(w.get('dropped_events',0) for w in rows)
    collisions=sum(w.get('bucket_collisions',0) for w in rows)
    unknown=sum(w.get('unknown_stages',0) for w in rows)
    lines.append(f'Trace integrity: {drops} dropped events; {collisions} overwritten frame buckets; {unknown} unknown stages.')
    for (lane,name,kind),values in sorted(stages.items()):
        if kind==3:
            counts=[sum(v.get(k,0) for v in values) for k in ('over_1667','over_20','over_25','over_3333')]
            lines.append(f'{lane} frames over 16.67 / 20 / 25 / 33.33 ms: '+ ' / '.join(map(str,counts))+'.')
    counters=[];background=[]
    for (lane,name,kind),values in sorted(stages.items()):
        if kind==2:
            counters.append(f"{lane}: {name} = {sum(v.get('counter',0) for v in values)}")
        work=sum(v.get('unattributed_ns',0) for v in values)
        if work: background.append(f"{lane}: {name} = {work/1e6:.3f} ms")
    if counters: lines+=['','Observed counters:']+['- '+x for x in counters]
    lines+=decoder_report(rows)
    lines+=draw_prep_report(rows)
    if background: lines+=['','Work without a main/render frame ID (not a per-frame cost):']+['- '+x for x in background]
    cache=next((w['disc_read_cache'] for w in reversed(rows) if 'disc_read_cache' in w),None)
    if cache:
        lines+=['', 'Original-disc read cache (latest process totals):',
            f"- Block hits / misses: {cache['block_hits']} / {cache['block_misses']}; source calls: {cache['source_read_calls']}.",
            f"- Source bytes: {cache['source_read_bytes']}; current retained bytes: {cache['retained_bytes']}; exact fallbacks: {cache['exact_fallbacks']}; bulk bypass requests: {cache['bulk_bypass_requests']}.",
            'Block counts are not file requests. Source bytes include read-ahead; these counters do not establish I/O latency.']
    dvd=next((w['dvd_wait'] for w in reversed(rows) if 'dvd_wait' in w),None)
    if dvd:
        parts=', '.join(f"{label} {dvd['worst_'+key+'_ns']/1e6:.3f} ms" for key,label in
                       (('queue','queue'),('service','service'),('callback','callback'),('other','wake/other')))
        lines+=['','Mandatory DVD wait ownership (latest process totals):',
            f"- {dvd['samples']} tracked samples; {dvd['untracked']} untracked; {dvd['waiting']} currently waiting.",
            f"- Worst completed wait: {dvd['max_wait_ns']/1e6:.3f} ms ({parts}).",
            f"- Entry {dvd['worst_entry']}, offset {dvd['worst_offset']}, length {dvd['worst_length']}, overlay {dvd['worst_overlay']}.",
            'Service includes stream locks and read work, rather than measured physical disk time. Queue/service/callback/other partition the completed wait. Untracked waits have no trusted ownership breakdown.']
    slow=sorted((f for w in rows for f in w.get('slow_frames',[])),key=lambda f:f['wall_ns'],reverse=True)
    if slow:
        lines+=['','Worst captured frames (inclusive stages overlap):']
        for frame in slow[:8]:
            costs=sorted((s for s in frame['stages'] if s['kind'] in (0,1)),key=lambda s:s['value'],reverse=True)
            detail=', '.join(s['stage']+f" {s['value']/1e6:.3f} ms" for s in costs[:4])
            lines.append(f"- Frame {frame['frame_id']} / {frame['lane']}: {frame['wall_ns']/1e6:.3f} ms; {detail}")
        omitted=sum(w.get('slow_frames_omitted',0) for w in rows)
        lines.append(f'Capture is bounded to the worst 16 frames per window; {omitted} additional slow frame details omitted.')
    return '\n'.join(lines)+'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('jsonl',type=Path)
    parser.add_argument('--out',type=Path)
    parser.add_argument('--last-windows',type=int,help='Report only the latest N completed capture windows')
    parser.add_argument('--all-sessions',action='store_true',help='Explicitly pool appended launches instead of the latest detected launch')
    args=parser.parse_args()
    rows,errors=load(args.jsonl)
    if not rows: parser.error('no native cpu_frame_window records; this file does not establish measured stage costs')
    captures=sessions(rows)
    if not args.all_sessions:
        rows=captures[-1]
    if args.last_windows is not None:
        if args.last_windows < 1: parser.error('--last-windows must be positive')
        rows=rows[-args.last_windows:]
    selection='all detected launches pooled' if args.all_sessions else 'latest detected launch only'
    text=f'Capture: {len(captures)} detected launch(es); {selection}; {len(rows)} window(s).\n\n'+report(rows)
    if not all(w.get('session_id') is not None for w in rows):
        text+='Legacy capture: launch boundaries are inferred from monotonic-clock resets; a reset between unobserved windows cannot always be detected.\n'
    if errors: text+=f'Ignored malformed/truncated lines: {errors}\n'
    if args.out: args.out.write_text(text)
    else: print(text,end='')

if __name__=='__main__': main()

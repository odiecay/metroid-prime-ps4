#!/usr/bin/env python3
"""Evaluate real probe JSONL without promoting baseline checks to gameplay proof."""
import argparse
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def evaluate(rows, requirements=None):
    req = requirements or json.loads((ROOT/'renderer/requirements.json').read_text())
    missing = []
    device = next((r for r in rows if 'device_name' in r), None)
    end = next((r for r in reversed(rows) if r.get('stage') == 'probe_end'), None)
    metadata = next((r for r in rows if r.get('schema') == 1), {})
    if metadata.get('target') != 'orbis': missing.append('native Orbis target evidence')
    if not end or not end.get('passed'): missing.append('complete successful native probe')
    if not any(r.get('stage') == 'transfer_readback' and r.get('passed') for r in rows):
        missing.append('GPU submission/fence/readback')
    if device:
        v = device.get('api_version', 0)
        if (v >> 22 & 0x7f, v >> 12 & 0x3ff) < tuple(req['minimum_vulkan']): missing.append('Vulkan 1.1')
        features = device.get('features', {})
        missing += ['feature:'+f for f in req['required_features'] if features.get(f) is not True]
        missing += ['core_feature:'+f for f in req['core_features'] if features.get(f) is not True]
        if not any(all(features.get(f) is True for f in option) for option in req['compression_any']):
            missing.append('supported texture-compression combination')
        for name, minimum in req['minimum_limits'].items():
            if device.get('limits', {}).get(name, 0) < minimum: missing.append('limit:'+name)
        for kind in ('instance', 'device'):
            extensions = set(next((r[kind+'_extensions'] for r in rows if kind+'_extensions' in r), []))
            missing += ['extension:'+e for e in req['required_'+kind+'_extensions'] if e not in extensions]
    else: missing.append('physical device capabilities')
    return {'schema': 1, 'stage': 'baseline_failed' if missing else 'baseline_passed',
            'baseline_failures': missing, 'dawn_device_tested': False,
            'aurora_rendered': False, 'gameplay_tested': False, 'release_accepted': False,
            'pending': req['unproven_until_device_tests']}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path, nargs='?')
    p.add_argument('--output', type=Path, default=ROOT/'evidence/graphics-compatibility.json')
    a=p.parse_args()
    if a.input:
        rows = [json.loads(line) for line in a.input.read_text().splitlines() if line.strip()]
        report = evaluate(rows)
        report['probe_source'] = str(a.input.resolve())
    else:
        report = evaluate([])
        report['stage'] = 'awaiting_native_probe'
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()

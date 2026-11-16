#!/usr/bin/env python3
"""Pack small converted resources for one bounded immutable startup read.

Proprietary payloads stay in the supplied private mod directory. This optional
acceleration file changes no resources and leaves loose fallbacks untouched.
The runtime uses it only for immutable, indexed installation data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

MAGIC = b'MPRC0001'
LIMIT = 32 * 1024 * 1024
HEADER = struct.Struct('<8sIIQ')
ENTRY = struct.Struct('<IIQQQ')
CORE = {'PART', 'SWHC', 'CRSC', 'DPSC', 'ELSC', 'WPSC', 'ANCS', 'CSKR'}
NAME = re.compile(r'[0-9a-fA-F]{8}\.([a-zA-Z0-9]{4})\Z')


def fnv64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def align32(value):
    return (value + 31) & ~31


def prepare(mod, budget=LIMIT):
    mod = Path(mod)
    if not 24 <= budget <= LIMIT:
        raise ValueError('Cache budget must be between 24 bytes and 32 MiB')
    candidates = []
    for path in mod.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Symlink refused: {path.relative_to(mod)}')
        match = NAME.fullmatch(path.name)
        if not path.is_file() or not match or 'reports' in path.relative_to(mod).parts:
            continue
        kind = match[1].upper()
        size = path.stat().st_size
        if size <= 0 or (kind not in CORE and (kind not in {'TXTR', 'CMDL'} or size > 262144)):
            continue
        relative = path.relative_to(mod).as_posix()
        encoded = relative.encode('utf-8')
        if len(encoded) > 1024:
            continue
        # Include the whole particle graph and animation metadata first. Then
        # tiny texture proxies/materials before larger, less numerous models.
        rank = 0 if kind in CORE else 1 if kind == 'TXTR' else 2
        candidates.append((rank, size, relative, path, encoded))
    selected = []
    table_bytes = HEADER.size
    data_bytes = 0
    for candidate in sorted(candidates):
        _, size, relative, path, encoded = candidate
        next_table = table_bytes + ENTRY.size + len(encoded)
        next_data = data_bytes + align32(size)
        if align32(next_table) + next_data > budget:
            continue
        selected.append(candidate)
        table_bytes, data_bytes = next_table, next_data
    selected.sort(key=lambda item: item[2])
    total = align32(table_bytes) + data_bytes
    output = bytearray(total)
    HEADER.pack_into(output, 0, MAGIC, len(selected), table_bytes, total)
    table_at, data_at = HEADER.size, align32(table_bytes)
    counts = {}
    payload_bytes = 0
    input_hash = hashlib.sha256()
    for _, size, relative, path, encoded in selected:
        data = path.read_bytes()
        if len(data) != size:
            raise ValueError(f'Source size changed while packing: {relative}')
        digest = hashlib.sha256(data).hexdigest()
        input_hash.update(f'{size} {digest} {relative}\n'.encode())
        ENTRY.pack_into(output, table_at, len(encoded), 0, size, data_at, fnv64(data))
        table_at += ENTRY.size
        output[table_at:table_at+len(encoded)] = encoded
        table_at += len(encoded)
        output[data_at:data_at+size] = data
        data_at += align32(size)
        payload_bytes += size
        kind = path.suffix[1:].upper()
        counts[kind] = counts.get(kind, 0) + 1
    assert table_at == table_bytes and data_at == total
    destination = mod / 'prime-resource-cache.bin'
    temporary = destination.with_suffix('.tmp')
    temporary.write_bytes(output)
    temporary.replace(destination)
    return dict(schema=1, format=MAGIC.decode(), file=destination.name,
                files=len(selected), payload_bytes=payload_bytes, retained_bytes=total,
                budget_bytes=budget, types=dict(sorted(counts.items())),
                sha256=hashlib.sha256(output).hexdigest(),
                source_index_sha256=input_hash.hexdigest(), proprietary_assets=True,
                purpose='Immutable optional read cache; original loose files preserved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mod', type=Path, required=True)
    parser.add_argument('--budget-mib', type=int, default=32)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    report = prepare(args.mod, args.budget_mib * 1024 * 1024)
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

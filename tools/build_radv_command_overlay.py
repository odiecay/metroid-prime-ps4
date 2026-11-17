#!/usr/bin/env python3
"""Build the pinned RADV command TU privately; never modify the SDK archive.

An untuned build must reproduce the SDK member (only .comment may differ)
before an optional PS4 ISA build is accepted. Source blobs come from pinned Git
objects; generated Mesa headers are the only vendored build inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / 'renderer/orbis/radv-command-generated'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(name):
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or '..' in path.parts or '\\' in name:
        raise ValueError(f'Unsafe manifest path: {name!r}')
    return Path(*path.parts)


def verify_files(base, entries):
    for entry in entries:
        path = base / relative(entry['path'])
        if not path.resolve().is_relative_to(base.resolve()) or not path.is_file():
            raise ValueError(f'Missing or escaping pinned input: {path}')
        if sha(path) != entry['sha256']:
            raise ValueError(f'Pinned input hash differs: {path}')


def materialise(repo, commit, entries, output):
    """Use object IDs, never files from a possibly modified Mesa worktree."""
    names = [f"{commit}:{relative(e['path']).as_posix()}" for e in entries]
    proc = subprocess.run(['git', '-C', str(repo), 'cat-file', '--batch'],
                          input=('\n'.join(names)+'\n').encode(), capture_output=True)
    if proc.returncode:
        raise ValueError(f'Pinned Mesa Git objects unavailable: {proc.stderr.decode(errors="replace")}')
    data = proc.stdout; position = 0
    for entry in entries:
        end = data.find(b'\n', position)
        header = data[position:end].decode().split()
        if len(header) != 3 or header[1] != 'blob' or header[0] != entry['git_blob']:
            raise ValueError(f"Missing pinned Mesa blob {commit}:{entry['path']}; fetch that commit")
        size = int(header[2]); blob = data[end+1:end+1+size]
        if len(blob) != size or hashlib.sha256(blob).hexdigest() != entry['sha256']:
            raise ValueError(f"Mesa blob hash differs: {entry['path']}")
        path = output / relative(entry['path']); path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob); position = end + size + 2
    if position != len(data):
        raise ValueError('Unexpected trailing Git object data')


def elf(path):
    data = Path(path).read_bytes()
    head = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if head[0][:6] != b'\x7fELF\x02\x01' or head[2] != 62:
        raise ValueError('Expected a little-endian x86-64 ELF object')
    raw = [struct.unpack_from('<IIQQQQIIQQ', data, head[6]+i*head[11]) for i in range(head[12])]
    table = raw[head[13]]; names = data[table[4]:table[4]+table[5]]
    sections = []
    for row in raw:
        sections.append(dict(name=names[row[0]:names.find(b'\0', row[0])].decode(),
            kind=row[1], flags=row[2], size=row[5], link=row[6], info=row[7], align=row[8], entsize=row[9],
            data=data[row[4]:row[4]+row[5]] if row[1] != 8 else b''))
    symtab = next(s for s in sections if s['kind'] == 2); strings = sections[symtab['link']]['data']
    symbols = []
    for offset in range(0, len(symtab['data']), 24):
        row = struct.unpack_from('<IBBHQQ', symtab['data'], offset)
        symbols.append(dict(name=strings[row[0]:strings.find(b'\0', row[0])].decode(),
            binding=row[1] >> 4, kind=row[1] & 15, other=row[2], section=row[3], value=row[4], size=row[5]))
    relocations = {}
    for section in sections:
        if section['kind'] != 4:
            continue
        records = []
        for offset in range(0, len(section['data']), 24):
            address, info, addend = struct.unpack_from('<QQq', section['data'], offset)
            symbol = symbols[info >> 32]; index = symbol['section']
            name = symbol['name'] or (sections[index]['name'] if 0 < index < len(sections) else '')
            records.append((address, info & 0xffffffff, name, symbol['value'], addend))
        relocations[sections[section['info']]['name']] = records
    return {s['name']: s for s in sections}, relocations, symbols


def compare_elf(base, candidate):
    left, lr, ls = elf(base); right, rr, rs = elf(candidate); differences = []; alloc = text = size = 0
    for name, section in left.items():
        other = right.get(name)
        equal = bool(other and all(section[key] == other[key] for key in
                     ('kind', 'flags', 'size', 'align', 'entsize', 'data')) and lr.get(name, []) == rr.get(name, []))
        alloc += bool(section['flags'] & 2); text += name.startswith('.text.')
        if name.startswith('.text.'):
            size += section['size']
        if not equal:
            differences.append(name)
    extra = sorted(set(right)-set(left))
    comment_safe = True
    if '.comment' in differences:
        a, b = left.get('.comment'), right.get('.comment')
        comment_safe = bool(a and b and not (a['flags'] & 2 or b['flags'] & 2) and
                            all(a[k] == b[k] for k in ('kind', 'flags', 'align', 'entsize')))
    return dict(allocated_sections=alloc, text_sections=text, text_bytes=size,
                differing_sections=differences, extra_sections=extra, same_symbol_records=ls == rs,
                exact_except_comment=not extra and ls == rs and comment_safe and
                                     all(n == '.comment' for n in differences))


def require_baseline(base, candidate):
    report = compare_elf(base, candidate)
    if not report['exact_except_comment']:
        raise ValueError(f'SDK baseline code/ABI reproduction failed: {report}')
    return report


def build(args):
    generated = GENERATED.resolve(); manifest_path = generated / 'manifest.json'
    manifest = json.loads(manifest_path.read_text()); sdk = args.sdk.resolve(); repo = args.mesa_repo.resolve()
    output = args.out.resolve(); compiler = Path(shutil.which(str(args.compiler)) or args.compiler).resolve()
    for readonly in (sdk, repo, generated):
        if output.is_relative_to(readonly) or readonly.is_relative_to(output):
            raise ValueError(f'Output must be separate from read-only inputs: {output}')
    verify_files(generated, manifest['generated_headers']); verify_files(sdk, manifest['sdk_headers'])
    archive = sdk / relative(manifest['sdk_archive'])
    if sha(archive) != manifest['sdk_archive_sha256'] or sha(compiler) != manifest['compiler_sha256']:
        raise ValueError('Pinned SDK archive or clang-18 compiler hash differs')
    resource = Path(subprocess.check_output([compiler, '-print-resource-dir'], text=True).strip()).resolve()
    verify_files(resource, manifest.get('compiler_headers', []))
    output.mkdir(parents=True, exist_ok=True); source = output / 'source'
    materialise(repo, manifest['mesa_commit'], manifest['sources'], source)
    raw = output / 'sdk-radv_cmd_buffer.c.o'
    raw.write_bytes(subprocess.check_output([args.ar, 'p', str(archive), manifest['sdk_member']]))
    if sha(raw) != manifest['sdk_member_sha256']:
        raise ValueError('Raw SDK command-buffer member hash differs')

    def compile_one(directory, flags):
        directory.mkdir(parents=True, exist_ok=True)
        obj = directory / 'radv_cmd_buffer.c.o'; dep = directory / 'radv_cmd_buffer.c.o.d'
        values = dict(source=str(source), generated=str(generated), sdk=str(sdk), compiler=str(compiler),
                      object=str(obj), depfile=str(dep))
        command = [arg.format(**values) for arg in manifest['command_template']] + flags
        (directory / 'command.json').write_text(json.dumps(command, indent=2)+'\n')
        with (directory / 'compile.log').open('w') as log:
            run = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        if run.returncode:
            raise ValueError(f'Native compile failed; see {directory / "compile.log"}')
        # Catch accidental fallbacks to mutable/unpinned build headers.
        import shlex
        paths = shlex.split(dep.read_text().replace('\\\n', ' ').split(':', 1)[1])
        pinned = {str((sdk / relative(x['path'])).resolve()): x['sha256'] for x in manifest['sdk_headers']}
        pinned.update({str((generated / relative(x['path'])).resolve()): x['sha256'] for x in manifest['generated_headers']})
        pinned.update({str((source / relative(x['path'])).resolve()): x['sha256'] for x in manifest['sources']})
        pinned.update({str((resource / relative(x['path'])).resolve()): x['sha256'] for x in manifest.get('compiler_headers', [])})
        inputs = {}
        for name in paths:
            path = Path(name).resolve(); digest = sha(path)
            if pinned.get(str(path)) != digest:
                raise ValueError(f'Unexpected or changed compile dependency: {path}')
            inputs[str(path)] = digest
        return obj, command, inputs

    baseline, command, inputs = compile_one(output / 'baseline', [])
    comparison = require_baseline(raw, baseline)
    selected = baseline; commands = [command]
    if args.tune_ps4:
        selected, command, tuned_inputs = compile_one(output / 'ps4', manifest['ps4_flags'])
        # ISA feature defines can enable a pinned compiler intrinsic header.
        # Mesa/source/generated/SDK inputs must stay identical, never drop.
        if any(tuned_inputs.get(name) != digest for name, digest in inputs.items()) or any(
                not Path(name).is_relative_to(resource) for name in set(tuned_inputs)-set(inputs)):
            raise ValueError('Tuned build used different source/header inputs')
        inputs = tuned_inputs
        commands.append(command)
    expected = args.expected_object_sha256 or manifest['tuned_object_sha256' if args.tune_ps4 else 'baseline_reproduction_object_sha256']
    if sha(selected) != expected:
        raise ValueError(f'Compiled object hash differs: expected {expected}, got {sha(selected)}')
    result = output / 'radv_cmd_buffer.c.o'; shutil.copyfile(selected, result)
    receipt = dict(schema=1, mode='ps4_native_isa' if args.tune_ps4 else 'reproduced_sdk_baseline',
        mesa_commit=manifest['mesa_commit'], manifest_sha256=sha(manifest_path), build_tool_sha256=sha(__file__),
        sdk_archive_sha256=sha(archive), sdk_member_sha256=sha(raw), compiler_sha256=sha(compiler),
        baseline_sha256=sha(baseline), baseline_comparison=comparison, object_sha256=sha(result),
        commands=commands, dependency_hashes=inputs, modifies_sdk=False,
        purpose='Single exact-source RADV command-buffer TU; preserves shader, ownership and synchronization source')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'object': str(result), 'sha256': receipt['object_sha256'],
                      'mode': receipt['mode'], 'baseline': comparison}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/radv-command-overlay')
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('PRIME_SDK_BUNDLE',
        '/opt/orbis-sdk')))
    parser.add_argument('--mesa-repo', type=Path, default=ROOT / 'external/mesa-ps4')
    parser.add_argument('--compiler', type=Path, default=Path('/usr/bin/clang-18'))
    parser.add_argument('--ar', default='/usr/lib/llvm-19/bin/llvm-ar')
    parser.add_argument('--tune-ps4', action='store_true')
    parser.add_argument('--expected-object-sha256')
    try:
        build(parser.parse_args())
    except (OSError, ValueError, subprocess.CalledProcessError, struct.error) as error:
        parser.exit(1, f'RADV command overlay: {error}\n')


if __name__ == '__main__':
    main()

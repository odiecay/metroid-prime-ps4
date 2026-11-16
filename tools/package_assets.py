"""Add a verified private asset tree to an existing SDK-generated GP4.

The SDK and its packaging helper remain read-only. Files are installed by the
PS4 package installer into /app0/remastered; the application never extracts them.
"""
from pathlib import Path, PurePosixPath
import shutil
import hashlib
import tempfile
import xml.etree.ElementTree as ET


def archive_sources(root, destination, hashes, verify=False):
    """Freeze exact root sources/vendor patches alongside their build receipt."""
    root, destination = Path(root), Path(destination)
    planned = []
    for name, digest in hashes.items():
        rel = PurePosixPath(name)
        if rel.is_absolute() or '..' in rel.parts or str(rel) != name:
            raise ValueError('Unsafe source archive path')
        source = destination/name if verify else root/name
        if not source.is_file():
            raise ValueError('Missing archived build input: '+name)
        with source.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != digest:
                raise ValueError('Build input hash mismatch: '+name)
        planned.append((source, destination/name))
    if not verify:
        if destination.exists():
            raise ValueError('Source archive already exists')
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.source-archive-',dir=destination.parent) as staging:
            for source, target in planned:
                staged=Path(staging)/target.relative_to(destination)
                staged.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, staged)
                with staged.open('rb') as stream:
                    if hashlib.file_digest(stream,'sha256').hexdigest()!=hashes[target.relative_to(destination).as_posix()]:
                        raise ValueError('Source changed during archival')
            Path(staging).rename(destination)
    return dict(hashes)


def add_asset_tree(gp4, source, names):
    gp4, source = Path(gp4), Path(source)
    document = ET.parse(gp4)
    files = document.getroot().find('files')
    rootdir = document.getroot().find('rootdir')
    if files is None or rootdir is None:
        raise ValueError('SDK GP4 is missing its files or root directory')
    existing = {node.attrib['targ_path'] for node in files.findall('file')}
    planned = []
    for name in names:
        rel = PurePosixPath(name)
        if rel.is_absolute() or not rel.parts or any(p in ('.', '..') for p in rel.parts) or str(rel) != name:
            raise ValueError('Invalid installed asset path')
        path = source
        for part in rel.parts:
            path /= part
            if path.is_symlink():
                raise ValueError('Linked asset paths are unsupported')
        target = 'remastered/' + name
        if target in existing or not path.is_file():
            raise ValueError('Duplicate or missing installed asset')
        existing.add(target)
        planned.append((path, target))
    if (gp4.parent / 'remastered').exists():
        raise ValueError('Installed asset stage already exists')
    # Validate all paths before copying; do not accept symlinked metadata either.
    for path, target in planned:
        output = gp4.parent / target
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, output)
        ET.SubElement(files, 'file', targ_path=target, orig_path=target)
    # Include every ancestor in the PFS directory tree. The SDK's fixed root
    # tree does not know about asset subdirectories, and duplicate nodes fail.
    rootdir.clear()
    dirs = {'': rootdir}
    for name in sorted(existing):
        parent = ''
        for part in PurePosixPath(name).parts[:-1]:
            key = parent + '/' + part if parent else part
            if key not in dirs:
                dirs[key] = ET.SubElement(dirs[parent], 'dir', targ_name=part)
            parent = key
    ET.indent(document, space='\t')
    document.write(gp4, encoding='utf-8', xml_declaration=True)
    return {target: path for path, target in planned}

"""Verify the populated Aurora archive dependencies used by the actual build."""
import hashlib
from pathlib import Path

def tree_digest(directory):
    root=Path(directory).resolve();digest=hashlib.sha256()
    for path in sorted(root.rglob('*')):
        relative=path.relative_to(root)
        if any(part in ('.git','__pycache__') for part in relative.parts) or path.suffix in ('.orig','.rej'):
            continue
        if path.is_symlink():
            if not path.resolve().is_relative_to(root):
                raise ValueError('Dependency symlink escapes source tree: '+str(relative))
            value='link:'+str(path.readlink())
        elif path.is_file():
            with path.open('rb') as f:value=hashlib.file_digest(f,'sha256').hexdigest()
        else:continue
        digest.update(str(relative).encode()+b'\0'+value.encode()+b'\n')
    return digest.hexdigest()

def populated_catalog(root):
    result=[];root=Path(root)
    for stamp in sorted((root/'build/game-native/_deps').glob('*-subbuild/*/src/*/*-urlinfo.txt')):
        fields=dict(line.split('=',1) for line in stamp.read_text().splitlines() if '=' in line and not line.startswith('#'))
        if fields.get('method')!='url':continue
        algorithm,digest=fields['hash'].split('=',1)
        if algorithm!='SHA256' or len(digest)!=64:
            raise ValueError('Unpinned archive dependency: '+str(stamp))
        source=Path(fields['source_dir']);name=stamp.name.removesuffix('-populate-urlinfo.txt')
        result.append({'name':name,'source_path':str(source.relative_to(root)),
                       'url':fields['url(s)'],'archive_sha256':digest,
                       'source_tree_sha256':tree_digest(source)})
    return result

def verify_populated(root,catalog):
    root=Path(root);verified=0
    for part in catalog:
        source=root/part['source_path']
        if not source.exists():continue # CMake restores it when the game is configured.
        if tree_digest(source)!=part['source_tree_sha256']:
            raise ValueError('Populated dependency differs from recorded native build input: '+part['name'])
        verified+=1
    return verified

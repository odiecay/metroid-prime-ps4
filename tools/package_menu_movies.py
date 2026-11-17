"""Validate the exact PC-converted menu payload before native packaging."""
import hashlib
import json
from pathlib import Path
import struct

NAMES = (
    '00_first_start.thp', '01_startloop.thp',
    '02_start_fileselect_A.thp', '02_start_fileselect_B.thp', '02_start_fileselect_C.thp',
    '03_fileselectloop.thp',
    '04_fileselect_playgame_A.thp', '04_fileselect_playgame_B.thp', '04_fileselect_playgame_C.thp',
)
TARGETS = frozenset('menus/hd-menu/Video/' + name for name in NAMES)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_menu_movies(folder):
    folder = Path(folder)
    if folder.is_symlink() or (folder/'menu-movies.json').is_symlink():
        raise ValueError('Linked menu payload is unsupported')
    receipt = json.loads((folder/'menu-movies.json').read_text())
    if receipt.get('scale') != 1.5 or receipt.get('width') != 960 or receipt.get('height') != 720 or \
       receipt.get('all_movies_decoded') is not True or receipt.get('display_framing_unchanged') is not True:
        raise ValueError('Expected fully decoded 1.5x menu movies with unchanged framing')
    base = folder/'hd-menu'
    if base.is_symlink() or (base/'Video').is_symlink():
        raise ValueError('Linked menu folder is unsupported')
    actual = set()
    for path in base.rglob('*'):
        if path.is_symlink():
            raise ValueError('Linked menu file is unsupported')
        if path.is_file():
            actual.add(path.relative_to(base).as_posix())
    if actual != {'Video/'+name for name in NAMES}:
        raise ValueError('Menu payload must contain exactly the nine original front-end movies')
    rows = receipt.get('clips', [])
    if len(rows) != len(NAMES) or {row.get('output') for row in rows} != {'hd-menu/Video/'+n for n in NAMES}:
        raise ValueError('Missing or duplicate converted menu receipt')
    files = {}
    frames = total = 0
    for row in rows:
        path = folder/row['output']
        size = path.stat().st_size
        if not 76 <= size <= 64*1024*1024 or sha(path) != row.get('output_sha256') or size != row.get('file_bytes'):
            raise ValueError('Converted menu movie differs from its decoder receipt')
        with path.open('rb') as stream:
            header = stream.read(76)
        magic, version, _, audio_max, fps_bits, count = struct.unpack_from('>6I', header)
        component = struct.unpack_from('>I', header, 32)[0]
        if magic != 0x54485000 or version != 0x10000 or audio_max != 0 or component != 48 or \
           struct.unpack_from('>I', header, 48)[0] != 1 or header[52] != 0 or \
           struct.unpack_from('>II', header, 68) != (960, 720):
            raise ValueError('Invalid converted menu THP header')
        if row.get('all_frames_decoded') is not True or row.get('plane_guards_intact') is not True or \
           row.get('frames') != count or count < 1 or row.get('fps_bits') != fps_bits or \
           row.get('audio_components') != 0 or row.get('audio_bytes') != 0 or row.get('audio_crc32') != '00000000' or \
           row.get('source_letterbox_preserved') is not True or row.get('aspect_stretched') is not False:
            raise ValueError('Incomplete menu decode/framing/audio proof')
        files['menus/'+row['output']] = path
        total += size
        frames += count
    if total != receipt.get('total_movie_bytes') or total > 384*1024*1024 or frames != receipt.get('total_frames'):
        raise ValueError('Menu aggregate receipt differs from payload')
    return files, {'scale': 1.5, 'width': 960, 'height': 720, 'frames': frames,
                   'bytes': total, 'files': {name: sha(path) for name, path in sorted(files.items())},
                   'source_manifest_sha256': sha(folder/'menu-movies.json'),
                   'read_directly_from_app0': True, 'first_launch_extraction': False}

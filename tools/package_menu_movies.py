"""Validate the exact PC-converted menu payload before native packaging."""
import hashlib
import json
from pathlib import Path
import struct
import math

NAMES = (
    '00_first_start.thp', '01_startloop.thp',
    '02_start_fileselect_A.thp', '02_start_fileselect_B.thp', '02_start_fileselect_C.thp',
    '03_fileselectloop.thp',
    '04_fileselect_playgame_A.thp', '04_fileselect_playgame_B.thp', '04_fileselect_playgame_C.thp',
)
TARGETS = frozenset('menus/hd-menu/Video/' + name for name in NAMES)
FONT_TARGET = 'menus/hd-menu/font/deface.sdfont'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_hd_font(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or not 68 <= path.stat().st_size <= 4*1024*1024:
        raise ValueError('Invalid HD font input')
    data = path.read_bytes()
    magic, version, width, height, padding, per_pixel, count = struct.unpack_from('<4sIIIffI', data)
    if magic != b'SDFT' or version != 1 or not 1 <= width <= 2048 or not 1 <= height <= 2048 or \
       not 1 <= count <= 4096 or len(data) != 28+40*count+width*height or \
       not math.isfinite(padding) or not math.isfinite(per_pixel) or padding < 0 or per_pixel <= 0:
        raise ValueError('Invalid HD font header or atlas')
    previous = -1
    characters = set()
    for i in range(count):
        character, *metrics = struct.unpack_from('<I9f', data, 28+40*i)
        if character <= previous or character > 0xffff or not all(math.isfinite(n) for n in metrics):
            raise ValueError('Invalid HD font glyph order or metrics')
        previous = character
        characters.add(character)
    if ord('H') not in characters:
        raise ValueError('HD font needs the reference capital for original-layout fitting')
    return {'file': FONT_TARGET, 'sha256': sha(path), 'bytes': len(data),
            'atlas': [width, height], 'glyphs': count,
            'original_layout_preserved': True, 'renderer': 'existing distance-field font renderer'}


def verify_menu_movies(folder):
    folder = Path(folder)
    if folder.is_symlink() or (folder/'menu-movies.json').is_symlink():
        raise ValueError('Linked menu payload is unsupported')
    receipt = json.loads((folder/'menu-movies.json').read_text())
    scale = receipt.get('scale')
    if scale not in (1.5, 2, 3, 4):
        raise ValueError('Unsupported original-menu preparation scale')
    width, height = int(640*scale), int(480*scale)
    if receipt.get('width') != width or receipt.get('height') != height or \
       receipt.get('all_movies_decoded') is not True or receipt.get('display_framing_unchanged') is not True:
        raise ValueError('Expected fully decoded menu movies with unchanged framing')
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
        if not 76 <= size <= 192*1024*1024 or sha(path) != row.get('output_sha256') or size != row.get('file_bytes'):
            raise ValueError('Converted menu movie differs from its decoder receipt')
        with path.open('rb') as stream:
            header = stream.read(76)
        magic, version, _, audio_max, fps_bits, count = struct.unpack_from('>6I', header)
        component = struct.unpack_from('>I', header, 32)[0]
        if magic != 0x54485000 or version != 0x10000 or audio_max != 0 or component != 48 or \
           struct.unpack_from('>I', header, 48)[0] != 1 or header[52] != 0 or \
           struct.unpack_from('>II', header, 68) != (width, height):
            raise ValueError('Invalid converted menu THP header')
        if row.get('all_frames_decoded') is not True or row.get('plane_guards_intact') is not True or \
           row.get('frames') != count or count < 1 or row.get('fps_bits') != fps_bits or \
           row.get('audio_components') != 0 or row.get('audio_bytes') != 0 or row.get('audio_crc32') != '00000000' or \
           row.get('source_letterbox_preserved') is not True or row.get('aspect_stretched') is not False:
            raise ValueError('Incomplete menu decode/framing/audio proof')
        files['menus/'+row['output']] = path
        total += size
        frames += count
    if total != receipt.get('total_movie_bytes') or total > 1024*1024*1024 or frames != receipt.get('total_frames'):
        raise ValueError('Menu aggregate receipt differs from payload')
    return files, {'scale': scale, 'width': width, 'height': height, 'frames': frames,
                   'bytes': total, 'files': {name: sha(path) for name, path in sorted(files.items())},
                   'source_manifest_sha256': sha(folder/'menu-movies.json'),
                   'read_directly_from_app0': True, 'first_launch_extraction': False}

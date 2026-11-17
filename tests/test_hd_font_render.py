"""Run actual SDF/layout/render functions with a recording GX boundary.

Private compatibility checks take an existing .sdfont and extracted own-disc
FONT files via HD_FONT_ASSET / HD_FONT_DISC_DIR; no asset enters the source tree.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'game/MetroidPrimePort'


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class HdFontRenderTests(unittest.TestCase):
    def test_actual_render_layout_and_lifetimes(self):
        asset = Path(os.environ.get('HD_FONT_ASSET', ROOT / 'private/remastered-conversion-v1/mods/remastered-models/font/deface.sdfont'))
        fonts = Path(os.environ.get('HD_FONT_DISC_DIR', ROOT / 'build/final-ui-font-proof/private'))
        if not asset.is_file() or not list(fonts.glob('*.FONT')):
            self.skipTest('private converted font and own-disc FONT fixtures required')
        out = ROOT / 'build/final-ui-font-proof/render'
        out.mkdir(parents=True, exist_ok=True)
        sources = {}
        def read(path):
            path = GAME / path
            sources[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
            return path.read_text()
        backend = read('platform/port_hd_font.cpp')
        # Includes alone are replaced by a recording GX/device boundary. Every
        # function body (Load, PaletteColor, Begin/DrawGlyph/End, Reset) is intact.
        (out / 'actual_hd_font.inc').write_text(re.sub(r'^#include[^\n]*\n', '', backend, flags=re.M))
        header = read('include/Kyoto/Text/CRasterFont.hpp')
        pieces = [header[header.index('class CFontInfo {'):header.index('CHECK_SIZEOF(CFontInfo')],
                  header[header.index('class CKernPair {'):header.index('// TODO: breaks')],
                  header[header.index('class CGlyph {'):header.index('CHECK_SIZEOF(CGlyph')],
                  header[header.index('enum EFontMode {'):header.index('class CRasterFont {')]]
        (out / 'actual_font_types.inc').write_text('\n'.join(pieces))
        raster = read('src/Kyoto/Text/CRasterFont.cpp')
        # Select production's TARGET_PC branches before taking functions (the
        # matching/nonmatching loop alternatives each open the same brace).
        raster = subprocess.check_output(['g++-14', '-E', '-P', '-x', 'c++', '-DTARGET_PC', '-'],
            input=re.sub(r'^#include[^\n]*\n', '', raster, flags=re.M), text=True)
        methods = ['CRasterFont::CRasterFont(', 'EFontMode CRasterFont::GetMode(',
                   'void CRasterFont::GetSize(', 'int CRasterFont::KernLookup(',
                   'void CRasterFont::SinglePassDrawString(',
                   'const CGlyph* CRasterFont::InternalGetGlyph(']
        (out / 'actual_raster_layout.inc').write_text('\n'.join(function(raster, s) for s in methods))
        render = read('src/Kyoto/Text/CTextRenderBuffer.cpp')
        (out / 'actual_text_render.inc').write_text(function(render, 'void CTextRenderBuffer::Render('))
        convert = read('extern/aurora/lib/gfx/texture_convert.hpp')
        texture = read('extern/aurora/lib/gfx/texture.cpp')
        (out / 'actual_texture_gate.inc').write_text('\n'.join([
            function(convert, 'inline bool uses_direct_texture_upload('),
            function(convert, 'inline wgpu::TextureFormat to_wgpu('),
            function(texture, 'bool setup_swizzle(')]))
        shader = read('extern/aurora/lib/gx/shader.cpp')
        (out / 'actual_sdf_shader.inc').write_text(function(shader, 'if (config.sdf != 0)'))
        file_source = GAME / 'platform/port_hd_font_file.cpp'
        sources[str(file_source.relative_to(ROOT))] = hashlib.sha256(file_source.read_bytes()).hexdigest()
        rows = []
        for sanitized in (False, True):
            binary = out / ('sanitized' if sanitized else 'normal')
            command = ['taskset', '-c', '0,1,4,5', 'g++-14', '-std=c++20', '-O1', '-g',
                       '-DTARGET_PC', '-DFMT_HEADER_ONLY', '-I'+str(out),
                       '-I'+str(GAME / 'platform/include'),
                       '-I'+str(GAME / 'extern/aurora/include'),
                       '-I'+str(ROOT / 'build/pc-asset-importer/_deps/fmt-src/include'),
                       str(ROOT / 'tests/hd_font_render.cpp'), str(file_source), '-o', str(binary)]
            if sanitized:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            env = os.environ | {'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
            result = subprocess.run([binary, asset, fonts], capture_output=True, text=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('HD_FONT actual_render=1', result.stdout)
            print(('SANITIZED ' if sanitized else 'NORMAL ') + result.stdout, end='')
            rows.append({'sanitized':sanitized, 'command':command, 'stdout':result.stdout})
        (out / 'receipt.json').write_text(json.dumps({'production_files':sources,
            'asset_sha256':hashlib.sha256(asset.read_bytes()).hexdigest(),
            'disc_fonts':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(fonts.glob('*.FONT'))},
            'runs':rows, 'scope':'actual CPU source and recorded GX calls; no hardware visual claim'}, indent=2)+'\n')


if __name__ == '__main__':
    unittest.main()

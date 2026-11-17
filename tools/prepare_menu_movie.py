#!/usr/bin/env python3
"""PC-only1.5x/2x/3x original-menu upscale; uses the existing C++ THP importer.

No game data is downloaded or bundled. Audio-bearing THPs are refused, never
stripped. The original picture including embedded black bars is preserved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from types import SimpleNamespace


def prepare(source,output,scale,tool=None,ffmpeg='ffmpeg',cleanup=False):
    root=Path(__file__).resolve().parents[1]
    a=SimpleNamespace(source=Path(source),output=Path(output),scale=float(scale),
                      tool=Path(tool or root/'build/pc-asset-importer/prime_menu_movie'),
                      ffmpeg=ffmpeg,cleanup=cleanup)
    if a.scale not in (1.5,2,3):raise ValueError('Scale must be1.5,2 or3')
    if a.output.exists():raise ValueError('Output exists; refusing to overwrite it')
    # Width and height are parsed/validated again by the C++ tool. Multiplying
    # both dimensions retains the exact source aspect and all embedded bars.
    factor=f'{a.scale:g}'
    filters=('bilateral=sigmaS=0.6:sigmaR=0.035:planes=1,'if a.cleanup else'')+f'scale=iw*{factor}:ih*{factor}:flags=lanczos:out_color_matrix=bt601:out_range=pc'
    args=[a.ffmpeg,'-hide_banner','-v','warning','-nostdin','-threads','2','-i',str(a.source),
          '-an','-vf',filters,'-fps_mode','passthrough','-c:v','mjpeg','-pix_fmt','yuvj420p',
          '-huffman','default','-q:v','2','-threads','2','-f','image2pipe','-']
    a.output.parent.mkdir(parents=True,exist_ok=True)
    started=time.perf_counter()
    with tempfile.TemporaryDirectory(prefix='.movie-',dir=a.output.parent)as temp:
        target=Path(temp)/'converted.thp'
        with(a.output.with_suffix(a.output.suffix+'.ffmpeg.log')).open('w')as log:
            decode=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=log)
            try:
                converted=subprocess.run([str(a.tool),'encode',str(a.source),str(target),factor],
                                         stdin=decode.stdout,capture_output=True,text=True)
                decode.stdout.close();code=decode.wait()
            finally:
                if decode.poll()is None:decode.kill();decode.wait()
        if converted.returncode or code:
            raise RuntimeError(converted.stderr or f'ffmpeg exited{code}')
        receipt=json.loads(converted.stdout)
        # Atomic no-replace publication on this PC only. A concurrent writer
        # cannot cause an existing file to be overwritten.
        a.output.hardlink_to(target)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    receipt.update(source=str(a.source),source_sha256=sha(a.source),output=str(a.output),
                   output_sha256=sha(a.output),scale=a.scale,cleanup=a.cleanup,
                   source_letterbox_preserved=True,aspect_stretched=False,
                   source_timestamps_resampled=False,upscale_method='Lanczos; optional mild spatial denoise',
                   recovered_original_detail=False,host_conversion_elapsed_seconds=time.perf_counter()-started,
                   ffmpeg_argv=args,tool_sha256=sha(a.tool),ps4_performance_measured=False)
    receipt_path=a.output.with_suffix(a.output.suffix+'.json')
    receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--scale',type=float,choices=(1.5,2,3),required=True)
    p.add_argument('--cleanup',action='store_true',help='Optional mild spatial cleanup; default is resolution only')
    p.add_argument('--tool',type=Path);p.add_argument('--ffmpeg',default='ffmpeg')
    a=p.parse_args()
    print(json.dumps(prepare(a.source,a.output,a.scale,a.tool,a.ffmpeg,a.cleanup),indent=2))


if __name__=='__main__':main()

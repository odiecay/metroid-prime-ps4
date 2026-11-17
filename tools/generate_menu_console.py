#!/usr/bin/env python3
"""Original PS4 menu sculpture, encoded as a texture-free Prime CMDL.

Case geometry follows Sony CUH-1000 launch dimensions and product photographs.
Authentic PS/SONY/PS4 outlines are embedded below with their vector provenance.
No commercial model, texture, font or game data is read. Generated data uses Prime's documented
big-endian CMDL/material/display-list interfaces and normal CModel rendering.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path
import struct

U32=lambda n:struct.pack('>I',n)
F32=lambda n:struct.pack('>f',n)

def pad(data):return data+b'\0'*((-len(data))%32)

def normal(a,b,c):
    u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
    n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
    length=math.sqrt(sum(x*x for x in n))
    assert length>1e-14
    return tuple(x/length for x in n)

class Mesh:
    def __init__(self):self.vertices=[];self.lookup={};self.triangles=[[],[]]
    def polygon(self,points,colour,material=0):
        n=normal(*points[:3])
        ids=[]
        for p in points:
            key=tuple(round(x,8) for x in p+n)+tuple(colour)
            if key not in self.lookup:
                self.lookup[key]=len(self.vertices);self.vertices.append((p,n,colour))
            ids.append(self.lookup[key])
        for i in range(1,len(ids)-1):self.triangles[material].extend((ids[0],ids[i],ids[i+1]))
    def box(self,lo,hi,colour,material=0):
        x,y,z=lo;X,Y,Z=hi
        for points in [((x,y,z),(x,Y,z),(X,Y,z),(X,y,z)),
                       ((x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)),
                       ((x,y,z),(X,y,z),(X,y,Z),(x,y,Z)),
                       ((X,Y,z),(x,Y,z),(x,Y,Z),(X,Y,Z)),
                       ((x,Y,z),(x,y,z),(x,y,Z),(x,Y,Z)),
                       ((X,y,z),(X,Y,z),(X,Y,Z),(X,y,Z))]:self.polygon(points,colour,material)

def outline(w,d,r):
    # CUH-1000 has crisp, mitred corners rather than the rounded Slim outline.
    return [(w/2,d/2-r),(w/2-r,d/2),(-w/2+r,d/2),(-w/2,d/2-r),
            (-w/2,-d/2+r),(-w/2+r,-d/2),(w/2-r,-d/2),(w/2,-d/2+r)]

def clipped(poly,xcut,less):
    out=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        ina=(a[0]<=xcut) if less else (a[0]>=xcut)
        inb=(b[0]<=xcut) if less else (b[0]>=xcut)
        if ina:out.append(a)
        if ina!=inb:
            t=(xcut-a[0])/(b[0]-a[0]);out.append((xcut,a[1]+t*(b[1]-a[1])))
    return out

class Ps4Mesh(Mesh):
    def polygon(self,points,colour,material=0):
        # CUH-1000 rises away from its front controls and overhangs its rear I/O.
        # Negative Y is the front: the top front edge must recede (+Y).
        # Width/height/depth follow Sony's 275 x 53 x 305 mm launch dimensions.
        super().polygon([(x,y+.60*z,z)for x,y,z in points],colour,material)

def slab(mesh,w,d,z0,z1,upper):
    bevel=.014;outer=outline(w,d,.018);inner=outline(w-2*bevel,d-2*bevel,.012)
    for left,cut in [(True,-.43),(False,-.40)]:
        o=clipped(outer,cut,left);i=clipped(inner,cut,left)
        rings=[[(x,y,z0)for x,y in i],[(x,y,z0+bevel)for x,y in o],
               [(x,y,z1-bevel)for x,y in o],[(x,y,z1)for x,y in i]]
        top=(30,37,48,255)if upper and left else(53,57,64,255)if upper else(36,40,47,255)
        mesh.polygon(list(reversed(rings[0])),(18,22,28,255))
        for layer,(low,high)in enumerate(zip(rings,rings[1:])):
            for k in range(len(low)):
                j=(k+1)%len(low);front=(low[k][1]+low[j][1])*.5<-d/2+.04
                rear=abs(low[k][1]-d/2)<1e-6 and abs(low[j][1]-d/2)<1e-6
                # Rear fascia is rebuilt below as an actual perforated panel.
                if rear and layer==1:continue
                seam=abs(low[k][0]-cut)<.001 and abs(low[j][0]-cut)<.001
                colour=(8,12,18,255)if seam else(28,33,41,255)if upper and left else(65,69,77,255)if front else(38,43,51,255)
                if layer==2:colour=tuple(min(255,c+9)for c in top[:3])+(255,)
                mesh.polygon([low[k],low[j],high[j],high[k]],colour)
        mesh.polygon(rings[-1],top)

def stroke(mesh,points,width,z=None,front=False,colour=(8,12,18,255),material=0):
    for a,b in zip(points,points[1:]):
        dx=b[0]-a[0];dy=b[1]-a[1];length=math.hypot(dx,dy)
        px=-dy/length*width/2;py=dx/length*width/2
        q=[(a[0]-px,a[1]-py),(b[0]-px,b[1]-py),(b[0]+px,b[1]+py),(a[0]+px,a[1]+py)]
        mesh.polygon([(x,-1.3705,y)if front else(x,y,z)for x,y in (list(reversed(q))if front else q)],colour,material)

# Authentic marks, not a substitute typeface or hand-drawn monogram. The SVGs
# are tagged PD-textlogo by Wikimedia Commons; the marks remain Sony's brands.
# Sources: https://commons.wikimedia.org/wiki/File:PlayStation_logo.svg
#          https://commons.wikimedia.org/wiki/File:Sony_logo.svg
#          https://commons.wikimedia.org/wiki/File:PlayStation_4_logo_and_wordmark.svg
# Case references: https://blog.playstation.com/tachyon/2013/10/ps-suite.jpg
# https://manuals.playstation.net/document/pdf/CUH-1000A-1.5_1.pdf
# Sony's CUH-1000 specification is 275 x 53 x 305 mm; one unit is 100 mm.
PS_MARK='m 197.23914,117.96194 c -3.8677,4.8796 -13.34356,8.36053 -13.34356,8.36053 0,0 -70.49109,25.31994 -70.49109,25.31994 0,0 0,-18.67289 0,-18.67289 0,0 51.87665,-18.48401 51.87665,-18.48401 5.887,-2.10924 6.79096,-5.09097 2.00581,-6.65604 -4.77616,-1.56957 -13.42451,-1.11983 -19.31601,0.99841 0,0 -34.56645,12.17426 -34.56645,12.17426 0,0 0,-19.37898 0,-19.37898 0,0 1.99232,-0.6746 1.99232,-0.6746 0,0 9.98856,-3.534896 24.03371,-5.09097 14.04515,-1.547081 31.24291,0.211374 44.74389,5.32933 15.21445,4.80764 16.92793,11.89543 13.06473,16.77502 z M 120.11451,86.165853 c 0,0 0,-47.752601 0,-47.752601 0,-5.608163 -1.03439,-10.771093 -6.29626,-12.232725 -4.0296,-1.290734 -6.53012,2.45104 -6.53012,8.054706 0,0 0,119.583887 0,119.583887 0,0 -32.250314,-10.23591 -32.250314,-10.23591 0,0 0,-142.58321 0,-142.58321 13.712343,2.54549 33.689454,8.56291 44.429074,12.18326 27.31226,9.376917 36.57225,21.047482 36.57225,47.343343 0,25.630256 -15.82159,35.344478 -35.92463,25.63925 z M 15.862004,131.01768 C 0.24279269,126.6193 -2.3566614,117.45375 4.7626047,112.17389 c 6.5795883,-4.8751 17.7689333,-8.54492 17.7689333,-8.54492 0,0 46.241498,-16.442224 46.241498,-16.442224 0,0 0,18.744854 0,18.744854 0,0 -33.275709,11.90892 -33.275709,11.90892 -5.878004,2.10924 -6.781967,5.09547 -2.005807,6.66054 4.780657,1.56506 13.433512,1.11983 19.320511,-0.99391 0,0 15.961005,-5.79256 15.961005,-5.79256 0,0 0,16.77053 0,16.77053 -1.011893,0.17989 -2.140724,0.35978 -3.184104,0.53518 -15.965505,2.60845 -32.969893,1.5201 -49.726928,-4.00262 z m 171.105246,7.42508 c 2.0193,0 3.91267,0.78254 5.33832,2.22618 1.42566,1.42115 2.21269,3.31903 2.21269,5.33383 0,2.02379 -0.78703,3.91267 -2.21269,5.33383 -1.42565,1.43464 -3.31902,2.21718 -5.33832,2.21718 -2.0193,0 -3.90818,-0.78254 -5.33833,-2.21718 -1.42565,-1.42116 -2.20818,-3.31004 -2.20818,-5.33383 0,-4.16453 3.38198,-7.56001 7.54651,-7.56001 z m -6.27827,7.56001 c 0,1.6775 0.65211,3.25606 1.83941,4.43436 1.18279,1.19629 2.76585,1.8439 4.43886,1.8439 3.46743,0 6.27826,-2.81532 6.27826,-6.27826 0,-1.682 -0.64761,-3.26056 -1.8394,-4.44336 -1.1828,-1.19629 -2.76586,-1.83941 -4.43886,-1.83941 -1.67301,0 -3.25607,0.64312 -4.43886,1.83941 -1.1873,1.1828 -1.83941,2.76136 -1.83941,4.44336 z m 8.55841,-4.07008 c 0.82751,0.36428 1.24576,1.06586 1.24576,2.06427 0,0.5127 -0.10794,0.94444 -0.3283,1.28174 -0.15741,0.24285 -0.38228,0.44074 -0.63413,0.61163 0.19788,0.11694 0.37328,0.25635 0.50371,0.41826 0.17988,0.23386 0.28332,0.60713 0.29682,1.11533 0,0 0.0405,1.07486 0.0405,1.07486 0.0135,0.28783 0.0315,0.5082 0.0765,0.64312 0.045,0.19788 0.13042,0.32381 0.23835,0.36429 0,0 0.11244,0.054 0.11244,0.054 0,0 0,0.12143 0,0.12143 0,0 0,0.18439 0,0.18439 0,0 0,0.18439 0,0.18439 0,0 -0.18439,0 -0.18439,0 0,0 -1.33571,0 -1.33571,0 0,0 -0.10793,0 -0.10793,0 0,0 -0.054,-0.0944 -0.054,-0.0944 -0.045,-0.0899 -0.0764,-0.19338 -0.10793,-0.3283 -0.0225,-0.12143 -0.045,-0.3328 -0.0585,-0.65661 0,0 -0.0675,-1.33571 -0.0675,-1.33571 -0.018,-0.46322 -0.1754,-0.75105 -0.47222,-0.90396 -0.18439,-0.0854 -0.49021,-0.12592 -0.90396,-0.12592 0,0 -2.28914,0 -2.28914,0 0,0 0,3.26056 0,3.26056 0,0 0,0.18439 0,0.18439 0,0 -0.18889,0 -0.18889,0 0,0 -1.08836,0 -1.08836,0 0,0 -0.18438,0 -0.18438,0 0,0 0,-0.18439 0,-0.18439 0,0 0,-8.03672 0,-8.03672 0,0 0,-0.18439 0,-0.18439 0,0 0.18438,0 0.18438,0 0,0 3.71929,0 3.71929,0 0.63863,0 1.17381,0.0944 1.58756,0.28782 z m -4.0296,3.38648 c 0,0 2.32961,0 2.32961,0 0.46772,0 0.841,-0.0855 1.10634,-0.26084 0.24286,-0.1754 0.35979,-0.49471 0.35979,-0.95793 0,-0.5037 -0.1664,-0.83201 -0.51719,-1.0074 -0.19338,-0.0944 -0.46323,-0.14841 -0.80503,-0.14841 0,0 -2.47352,0 -2.47352,0 0,0 0,2.37458 0,2.37458 z'
SONY_MARK='m 456.0129,225.27997 c -46.41279,0 -89.42079,-13.82399 -118.09279,-39.60319 A 96.895989,96.895989 0 0 1 305.79212,112.48639 98.329588,98.329588 0 0 1 337.92011,39.551995 C 364.54411,15.359998 411.64811,0 456.0129,0 c 49.07519,0 88.37119,12.364799 118.39999,39.603195 a 97.407989,97.407989 0 0 1 31.69279,72.883195 101.52959,101.52959 0 0 1 -31.69279,73.19039 c -27.9808,25.9328 -71.9104,39.60319 -118.39999,39.60319 v -29.64479 c 24.6016,0 47.43679,-8.4992 63.38559,-24.3712 15.9488,-15.872 23.1936,-35.1232 23.1936,-58.87999 0,-22.681601 -7.9616,-43.878398 -23.1936,-58.879996 -15.744,-15.487998 -39.11679,-24.217597 -63.38559,-24.217597 -24.2688,0 -47.74399,8.652799 -63.48799,24.217597 -15.1808,15.027198 -23.04,36.300795 -23.04,58.879996 a 82.35519,82.35519 0 0 0 23.04,58.87999 c 15.744,15.6928 39.06559,24.3712 63.48799,24.3712 z M 117.12014,0 C 92.313748,0 64.128151,4.6591995 40.320154,15.359998 18.099355,25.292797 1.5754749e-4,41.241595 1.5754749e-4,67.686392 A 54.220794,54.220794 0 0 0 14.720156,104.95999 c 6.425599,5.9392 16.793599,16.0256 43.878396,21.9648 12.108798,2.55999 37.990395,6.68159 63.769588,9.39519 25.7792,2.7136 50.7648,5.12 61.0048,7.8592 8.1408,2.0736 21.83679,4.8896 21.83679,20.2496 0,15.36 -14.41279,19.968 -16.92159,20.9664 -2.5088,0.9984 -19.8144,8.9344 -50.8928,8.9344 a 216.42237,216.42237 0 0 1 -60.59519,-10.4192 c -11.596799,-4.1472 -23.756797,-9.6 -35.097596,-23.4496 a 40.268795,40.268795 0 0 1 -7.295999,-22.2208 H 6.2465567 v 78.84799 H 37.529754 v -10.67519 a 4.4543995,4.4543995 0 0 1 6.7584,-3.84 246.42557,246.42557 0 0 0 45.772794,14.79679 c 16.435202,3.4304 27.059192,5.9136 47.487992,5.9136 a 202.62398,202.62398 0 0 0 63.6416,-8.9856 111.07839,111.07839 0 0 0 37.81119,-18.66239 51.814394,51.814394 0 0 0 20.24959,-41.4976 58.060793,58.060793 0 0 0 -16.35839,-40.80639 72.012792,72.012792 0 0 0 -20.1728,-13.798402 148.60798,148.60798 0 0 0 -24.88319,-8.678399 c -16.2304,-3.967999 -52.6848,-8.934399 -70.1184,-10.675198 -18.27839,-1.8944 -49.99679,-4.5312 -62.668789,-8.447999 -3.839999,-1.2032 -11.673599,-4.9152 -11.673599,-14.003199 0,-6.476799 3.584,-11.955198 10.649599,-16.383998 C 75.26415,34.303996 97.945747,29.926396 121.60014,29.926396 a 166.98878,166.98878 0 0 1 66.7136,13.030399 72.857591,72.857591 0 0 1 15.87199,9.471999 47.718394,47.718394 0 0 1 15.6416,26.163197 h 25.2672 V 9.9583988 h -28.16 v 7.9615992 c 0,2.56 -2.56,5.939199 -7.68,3.1488 C 196.55694,14.463998 160.87054,0.17919998 117.12014,0 Z m 618.21433,12.595199 137.62558,124.185581 -1.408,-83.609586 c -0.1536,-10.982399 -2.1504,-15.564798 -14.0288,-15.564798 H 831.66726 V 12.595199 h 117.75998 v 25.011197 h -25.26719 c -12.0832,0 -12.8,3.891199 -13.0048,15.564798 l 2.1248,159.769586 h -40.32 L 714.41927,71.475192 V 171.85278 c 0.128,10.9312 0.64,16.0768 11.8784,16.0768 h 28.16 v 25.0112 H 639.02728 v -25.0112 h 27.0336 c 10.08639,0 9.67679,-9.6256 9.67679,-16.64 V 54.118394 c 0,-7.679999 -1.0752,-16.486398 -16.89599,-16.486398 h -21.9136 V 12.595199 Z M 1083.776,187.87838 a 55.859193,55.859193 0 0 0 6.9632,-0.4352 8.627199,8.627199 0 0 0 5.4272,-4.8128 28.006397,28.006397 0 0 0 0.5376,-5.4016 v -39.552 c 0,-1.3312 0,-1.3568 -1.6896,-3.456 -1.6896,-2.0992 -72.0896,-81.919986 -75.2896,-85.503986 -3.9936,-4.351999 -11.008,-11.084798 -21.68316,-11.084798 h -24.448 V 12.595199 h 137.98396 v 24.985597 h -16.64 c -3.84,0 -6.4,3.660799 -3.1232,7.679999 0,0 46.4384,55.551995 46.8736,56.140795 0.4352,0.5888 0.8192,0.7168 1.408,0.1792 0.5888,-0.5376 47.5904,-55.807995 47.9488,-56.319995 a 4.7871994,4.7871994 0 0 0 -4.096,-7.679999 h -17.0752 V 12.595199 H 1280 v 25.036797 h -25.2672 c -9.1648,0 -12.8,1.689599 -19.7888,9.471998 l -76.16,86.886386 a 5.3759994,5.3759994 0 0 0 -0.9216,3.6864 v 39.5264 a 28.159997,28.159997 0 0 0 0.5632,5.4016 8.524799,8.524799 0 0 0 5.4016,4.8128 50.611194,50.611194 0 0 0 6.912,0.4352 h 25.8304 v 25.0368 h -137.2672 v -25.0368 z'
PS4_MARK=['M335.361,71.101V41.126c0-6.115,2.854-10.494,8.941-10.494h37.547c0.23,0,0.451-0.228,0.451-0.454v-5.964    c0-0.052-0.015-0.103-0.035-0.152h-45.909c-9.951,1.265-14.812,8.2-14.812,17.064v29.974c0,6.115-2.934,10.495-8.94,10.495    h-39.652c-0.225,0-0.451,0.151-0.451,0.453v5.965c0,0.247,0.152,0.436,0.33,0.499h45.019    C329.584,88.204,335.361,80.713,335.361,71.101z', 'M181.104,88.513h13.183c0.178-0.063,0.33-0.252,0.33-0.499V68.911c0-6.115,3.454-10.344,8.486-10.344h54.301    c12.242,0,18.176-7.55,18.176-17.441c0-8.864-4.857-15.799-14.813-17.064H180.86c-0.023,0.05-0.062,0.089-0.062,0.152v5.964    c0,0.302,0.15,0.454,0.45,0.454h71.574c6.084,0,8.938,4.379,8.938,10.494c0,6.116-2.854,10.495-8.938,10.495h-56.628    c-8.787,0-15.396,7.476-15.396,17.29v19.103C180.797,88.261,180.903,88.449,181.104,88.513z', 'M382.08,81.596h72.1c0.221,0,0.371,0.151,0.371,0.302v6.116c0,0.302,0.23,0.453,0.451,0.453h12.996     c0.23,0,0.449-0.151,0.449-0.453v-5.965c0-0.227,0.23-0.453,0.451-0.453h12.916c0.301,0,0.451-0.227,0.451-0.452v-6.041     c0-0.227-0.15-0.453-0.451-0.453h-12.916c-0.221,0-0.451-0.227-0.451-0.453V30.632c0-3.555-1.321-5.819-3.542-6.57h-3.76     c-1.172,0.301-2.446,0.84-3.812,1.661l-77.656,47.945c-3.004,1.887-4.057,4-3.305,5.586     C376.972,80.614,378.695,81.596,382.08,81.596z M393.343,73.593L453.5,36.37c0.379-0.302,1.131-0.226,1.131,0.604v37.223     c0,0.227-0.23,0.453-0.451,0.453h-60.465c-0.521,0-0.752-0.15-0.822-0.302C392.812,74.046,392.962,73.819,393.343,73.593z']

def svg_contours(path, tolerance=0.7):
    """Flatten the embedded SVG commands without a font/raster dependency."""
    tokens=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?',path)
    contours=[];points=[];p=(0.,0.);start=p;i=0;command=None
    def cubic(a,b,c,d):
        # Bound curvature error using control-polygon flatness.
        chord=math.hypot(d[0]-a[0],d[1]-a[1])
        flat=max(abs((d[0]-a[0])*(q[1]-a[1])-(d[1]-a[1])*(q[0]-a[0]))for q in(b,c))
        if flat<=tolerance*max(chord,tolerance):points.append(d);return
        ab=tuple((a[k]+b[k])*.5 for k in(0,1));bc=tuple((b[k]+c[k])*.5 for k in(0,1));cd=tuple((c[k]+d[k])*.5 for k in(0,1))
        abc=tuple((ab[k]+bc[k])*.5 for k in(0,1));bcd=tuple((bc[k]+cd[k])*.5 for k in(0,1));mid=tuple((abc[k]+bcd[k])*.5 for k in(0,1))
        cubic(a,ab,abc,mid);cubic(mid,bcd,cd,d)
    while i<len(tokens):
        if tokens[i].isalpha():command=tokens[i];i+=1
        relative=command.islower();c=command.upper()
        if c=='Z':
            if points and points[-1]!=start:points.append(start)
            if points:contours.append(points)
            points=[];p=start;command=None;continue
        count={'M':2,'L':2,'H':1,'V':1,'C':6,'A':7}[c]
        a=list(map(float,tokens[i:i+count]));i+=count
        def point(x,y):return(x+p[0],y+p[1])if relative else(x,y)
        if c=='M':
            if points:contours.append(points)
            p=point(*a);start=p;points=[p];command='l'if relative else'L'
        elif c=='L':p=point(*a);points.append(p)
        elif c=='H':p=(p[0]+a[0]if relative else a[0],p[1]);points.append(p)
        elif c=='V':p=(p[0],p[1]+a[0]if relative else a[0]);points.append(p)
        elif c=='C':
            b=point(*a[:2]);d=point(*a[2:4]);end=point(*a[4:]);cubic(p,b,d,end);p=end
        elif c=='A':
            # SVG endpoint-to-centre elliptical-arc conversion (Sony's O).
            rx,ry,rotation,large,sweep,x,y=a;end=point(x,y);rx=abs(rx);ry=abs(ry)
            phi=math.radians(rotation);co=math.cos(phi);si=math.sin(phi)
            dx=(p[0]-end[0])*.5;dy=(p[1]-end[1])*.5
            xp=co*dx+si*dy;yp=-si*dx+co*dy
            scale=max(1.,math.sqrt(xp*xp/(rx*rx)+yp*yp/(ry*ry)));rx*=scale;ry*=scale
            numerator=max(0.,rx*rx*ry*ry-rx*rx*yp*yp-ry*ry*xp*xp)
            denominator=rx*rx*yp*yp+ry*ry*xp*xp
            factor=(-1 if bool(large)==bool(sweep)else 1)*math.sqrt(numerator/denominator)
            cxp=factor*rx*yp/ry;cyp=-factor*ry*xp/rx
            cx=co*cxp-si*cyp+(p[0]+end[0])*.5;cy=si*cxp+co*cyp+(p[1]+end[1])*.5
            theta=math.atan2((yp-cyp)/ry,(xp-cxp)/rx)
            stop=math.atan2((-yp-cyp)/ry,(-xp-cxp)/rx);delta=(stop-theta)%(2*math.pi)
            if not sweep:delta-=2*math.pi
            segments=max(1,math.ceil(abs(delta)/max(.01,2*math.acos(max(-1.,1-tolerance/max(rx,ry))))))
            for k in range(1,segments+1):
                t=theta+delta*k/segments;points.append((cx+co*rx*math.cos(t)-si*ry*math.sin(t),cy+si*rx*math.cos(t)+co*ry*math.sin(t)))
            points[-1]=end;p=end
    if points:contours.append(points)
    return contours

def vector_mark(mesh, paths, width, origin, front=False, colour=(155,158,164,255)):
    """Tessellate authentic outlines with holes, using exact nonzero fill bands."""
    if isinstance(paths,str):paths=[paths]
    contours=[c for path in paths for c in svg_contours(path,tolerance=0.7 if width<.25 else 1.)]
    xmin=min(x for c in contours for x,y in c);xmax=max(x for c in contours for x,y in c)
    ymin=min(y for c in contours for x,y in c);scale=width/(xmax-xmin)
    edges=[(a,b)for c in contours for a,b in zip(c,c[1:]+c[:1])if abs(a[1]-b[1])>1e-9]
    levels=sorted(set(round(y,8)for c in contours for x,y in c))
    def project(q):
        x,y=q;x=origin[0]+(x-xmin)*scale;y=(y-ymin)*scale
        return(x,-1.3708,origin[1]-y)if front else(x,origin[1]-y,.2666)
    for lo,hi in zip(levels,levels[1:]):
        if hi-lo<1e-7:continue
        mid=(lo+hi)*.5
        active=[(a,b)for a,b in edges if min(a[1],b[1])<mid<max(a[1],b[1])]
        def at(edge,y):
            a,b=edge;return a[0]+(b[0]-a[0])*(y-a[1])/(b[1]-a[1])
        active.sort(key=lambda e:at(e,mid));winding=0;left=None
        for edge in active:
            before=winding;winding+=1 if edge[1][1]>edge[0][1]else-1
            if not before and winding:left=edge
            if before and not winding:
                # SVG y points down; reverse for outward +Z on top / -Y in front.
                q=[(at(left,lo),lo),(at(edge,lo),lo),(at(edge,hi),hi),(at(left,hi),hi)]
                q.reverse()
                q=[project(x)for x in q]
                clean=[]
                for v in q:
                    if not clean or math.dist(v,clean[-1])>1e-9:clean.append(v)
                if len(clean)>1 and math.dist(clean[0],clean[-1])<1e-9:clean.pop()
                if len(clean)>=3:
                    # Skip numerical slivers at curve extrema, not real contours.
                    u=[clean[1][j]-clean[0][j]for j in range(3)];v=[clean[2][j]-clean[0][j]for j in range(3)]
                    if math.sqrt(sum(x*x for x in(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])))>1e-13:
                        mesh.polygon(clean,colour)

def rear_polygon(mesh,points,y,colour):
    mesh.polygon([(x,y,z)for x,z in reversed(points)],colour)

def rear_panel(mesh,z0,z1,holes):
    """Real grille/connector openings with inward walls, not painted sockets."""
    front=1.3675;back=1.326
    for xmin,xmax in [(-1.357,-.43),(-.40,1.357)]:
        levels=sorted(set([z0,z1]+[max(z0,min(z1,z))for a,b,c,d in holes for z in(c,d)]))
        for lo,hi in zip(levels,levels[1:]):
            if hi-lo<1e-8:continue
            mid=(lo+hi)*.5
            cuts=sorted((max(xmin,a),min(xmax,b))for a,b,c,d in holes if c<mid<d and b>xmin and a<xmax)
            x=xmin
            for a,b in cuts+[(xmax,xmax)]:
                if a>x:rear_polygon(mesh,[(x,lo),(a,lo),(a,hi),(x,hi)],front,(38,43,51,255))
                x=max(x,b)
    # Cut edges descend into the recess; the interior box remains behind them.
    for a,b,c,d in holes:
        if c<z0 or d>z1:continue
        for points in [((a,front,c),(a,back,c),(b,back,c),(b,front,c)),
                       ((a,front,d),(b,front,d),(b,back,d),(a,back,d)),
                       ((a,front,c),(a,front,d),(a,back,d),(a,back,c)),
                       ((b,front,c),(b,back,c),(b,back,d),(b,front,d))]:
            mesh.polygon(points,(13,17,22,255))

def rear_socket(mesh,outer,inner,colour=(67,70,75,255)):
    # Four matching boundary edges preserve the trapezoidal HDMI mouth.
    for i in range(4):
        j=(i+1)%4;rear_polygon(mesh,[outer[i],outer[j],inner[j],inner[i]],1.353,colour)
    rear_polygon(mesh,inner,1.330,(2,4,6,255))

def rear_io(mesh):
    # Positions follow the CUH-1000 manual rear photograph, viewed from behind:
    # DIGITAL OUT / HDMI OUT / LAN / AUX across the left; AC IN below left.
    sockets=[(.61,.80,.070,.212),(.395,.575,.085,.161),(.145,.325,.065,.212),(-.115,.070,.086,.173)]
    ac=(.83,1.115,-.211,-.092)
    upper=list(sockets);lower=[ac]
    for k in range(31):
        x=-1.32+k*.086
        for z in(.071,.129,.187):
            hole=(x,x+.061,z,z+.038)
            if not any(x<b+.018 and x+.061>a-.018 for a,b,c,d in sockets):upper.append(hole)
        for z in(-.236,-.180,-.124):
            hole=(x,x+.061,z,z+.035)
            if not(x<ac[1]+.014 and x+.061>ac[0]-.014):lower.append(hole)
    rear_panel(mesh,.059,.251,upper);rear_panel(mesh,-.251,-.059,lower)
    # Optical square flap; HDMI trapezoid; shielded RJ45; narrow AUX socket.
    rear_socket(mesh,[(.61,.070),(.80,.070),(.80,.212),(.61,.212)],
                [(.628,.087),(.782,.087),(.782,.195),(.628,.195)],(34,38,43,255))
    rear_polygon(mesh,[(.638,.096),(.772,.096),(.772,.185),(.638,.185)],1.342,(16,20,24,255))
    rear_socket(mesh,[(.395,.085),(.575,.085),(.575,.161),(.395,.161)],
                [(.424,.099),(.546,.099),(.560,.145),(.410,.145)])
    rear_socket(mesh,[(.145,.065),(.325,.065),(.325,.212),(.145,.212)],
                [(.160,.078),(.310,.078),(.310,.198),(.160,.198)])
    for k in range(8):
        x=.173+k*.017
        mesh.box((x,1.339,.177),(x+.006,1.345,.190),(123,111,77,255))
    rear_socket(mesh,[(-.115,.086),(.070,.086),(.070,.173),(-.115,.173)],
                [(-.101,.100),(.056,.100),(.056,.159),(-.101,.159)])
    # Figure-eight AC inlet and its two recessed contact pins.
    for x in(.918,1.025):
        outer=[(x+.052*math.cos(t),-.1515+.045*math.sin(t))for t in[k*math.pi/8 for k in range(16)]]
        inner=[(x+.037*math.cos(t),-.1515+.032*math.sin(t))for t in[k*math.pi/8 for k in range(16)]]
        for i in range(16):
            j=(i+1)%16;rear_polygon(mesh,[outer[i],outer[j],inner[j],inner[i]],1.350,(28,32,38,255))
        rear_polygon(mesh,inner,1.331,(1,3,5,255))
        pin=[(x+.007*math.cos(t),-.1515+.007*math.sin(t))for t in[k*math.pi/6 for k in range(12)]]
        rear_polygon(mesh,pin,1.340,(112,117,123,255))

def build_mesh():
    m=Ps4Mesh()
    # Four sharp case blocks with a recessed horizontal seam and vertical panel split.
    # Shear expands the 2.735 deep body to the published 3.05 total depth.
    slab(m,2.75,2.735,-.265,-.045,False)
    slab(m,2.75,2.735,.045,.265,True)
    m.box((-1.31,-1.325,-.05),(1.31,1.325,.05),(9,13,19,255))
    m.box((-1.33,1.318,-.248),(1.33,1.325,.248),(3,5,8,255))
    # A narrow illuminated seam, set below the lid rather than painted over it.
    m.box((-.429,-1.345,.254),(-.401,1.342,.261),(9,24,42,255))
    m.box((-.424,-1.340,.263),(-.406,1.337,.267),(27,133,255,255),1)
    m.box((-.420,-1.338,.2672),(-.410,1.335,.268),(124,202,255,255),1)
    # CUH-1000: 120 mm loading slot left, then two close USB-A sockets.
    # All sit behind the front lip; the metal rims do not protrude through it.
    m.box((-1.225,-1.348,.014),(-.015,-1.330,.032),(1,2,4,255))
    for x in (.125,.365):
        # USB-A shell: 14 x 5.5 mm opening, four thin separate rim strips.
        for lo,hi in [((x-.008,-1.351,-.031),(x+.148,-1.337,-.025)),
                      ((x-.008,-1.351,.025),(x+.148,-1.337,.031)),
                      ((x-.008,-1.351,-.025),(x,-1.337,.025)),
                      ((x+.14,-1.351,-.025),(x+.148,-1.337,.025))]:
            m.box(lo,hi,(66,68,72,255))
        m.box((x,-1.341,-.025),(x+.14,-1.334,.025),(1,2,3,255))
        m.box((x+.014,-1.349,-.011),(x+.126,-1.343,-.002),(39,42,47,255))
        # Four contact pads inside each socket, visible only at close range.
        for k in range(4):
            xx=x+.028+k*.026
            m.box((xx,-1.350,-.003),(xx+.009,-1.345,.000),(115,108,85,255))
    # Separate capacitive power/eject keys in the vertical front-panel split.
    for z in (.045,-.245):
        m.box((-.429,-1.372,z),(-.401,-1.368,z+.19),(6,9,13,255))
    # Small true power symbol above, eject triangle below (not silver bars).
    ring=[(-.415+.006*math.cos(t),.070+.006*math.sin(t))for t in [math.radians(a)for a in range(-50,231,28)]]
    stroke(m,ring,.0015,front=True,colour=(123,128,136,255))
    stroke(m,[(-.415,.069),(-.415,.079)],.0015,front=True,colour=(123,128,136,255))
    m.polygon([(-.422,-1.3708,-.205),(-.408,-1.3708,-.205),(-.415,-1.3708,-.194)],(123,128,136,255))
    stroke(m,[(-.422,-.209),(-.408,-.209)],.0015,front=True,colour=(123,128,136,255))
    # Smooth recessed sides match Sony's side photograph; ventilation is
    # inside the interspace rather than teeth protruding beyond the case.
    rear_io(m)
    # Actual Sony marks at the photo's locations: SONY upper-left fascia,
    # PS4 upper-right, small PS monogram beside the rear of the glossy lid seam.
    vector_mark(m,SONY_MARK,.235,(-1.255,.195),front=True,colour=(117,121,130,255))
    vector_mark(m,PS4_MARK,.350,(.855,.193),front=True,colour=(10,13,18,255))
    vector_mark(m,PS_MARK,.177,(-.665,1.205),colour=(143,150,161,255))
    return m

def material(index,lit):
    # Indexed POS/NRM/CLR0, vertex-colour material source, no textures/UVs.
    channel=0x3005 if lit else 0x4
    colour_args=15|(15<<5)|(15<<10)|(10<<15) # TEV d = raster colour
    alpha_args=7|(7<<5)|(7<<10)|(5<<15) # TEV d = raster alpha
    words=[0x83,0,0x3f,0x50533400+index,0x00000001,1,channel,1,
           colour_args,alpha_args,0x100,0x100,4,0x0000ffff,0,4,0]
    return b''.join(U32(x)for x in words)

def encode(mesh):
    positions=b''.join(F32(x)for p,n,c in mesh.vertices for x in p)
    normals=b''.join(F32(x)for p,n,c in mesh.vertices for x in n)
    colours=bytes(x for p,n,c in mesh.vertices for x in c)
    mats=[material(0,True),material(1,False)]
    matset=U32(0)+U32(len(mats))+b''.join(U32(sum(map(len,mats[:i+1])))for i in range(len(mats)))+b''.join(mats)
    surfaces=[]
    for mat,indices in enumerate(mesh.triangles):
        assert len(indices)<65536
        dl=pad(bytes([0x90])+struct.pack('>H',len(indices))+b''.join(struct.pack('>HHH',i,i,i)for i in indices))
        coords=[mesh.vertices[i][0]for i in set(indices)]
        center=[sum(p[c]for p in coords)/len(coords)for c in range(3)]
        surface=b''.join(F32(x)for x in center)+U32(mat)+U32(0x80000000|len(dl))+U32(0)*3+F32(0)+F32(0)+F32(1)
        surfaces.append(pad(surface)+dl)
    table=U32(len(surfaces))+b''.join(U32(len(s))for s in surfaces)
    secs=[pad(matset),pad(positions),pad(normals),pad(colours),b'',b'',pad(table),*surfaces]
    bounds=[min(p[c]for p,n,col in mesh.vertices)for c in range(3)]+[max(p[c]for p,n,col in mesh.vertices)for c in range(3)]
    header=U32(0xdeadbabe)+U32(2)+U32(4)+b''.join(F32(x)for x in bounds)+U32(len(secs))+U32(1)+b''.join(U32(len(s))for s in secs)
    return pad(header)+b''.join(secs),bounds

def header(data):
    rows=[', '.join('0x%02x'%x for x in data[i:i+16])for i in range(0,len(data),16)]
    return ('// Generated by tools/generate_menu_console.py. Authored case geometry / source-linked PD-textlogo marks; SPDX-License-Identifier: MIT\n'
            '#pragma once\n#include <cstddef>\nnamespace prime::menu {\n'
            'alignas(32) inline constexpr unsigned char Ps4ModelBytes[] = {\n  '+',\n  '.join(rows)+'\n};\n'
            'inline constexpr std::size_t Ps4ModelSize = sizeof(Ps4ModelBytes);\n}\n')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--mesh-json',type=Path);ap.add_argument('--cmdl',type=Path)
    args=ap.parse_args();m=build_mesh();data,bounds=encode(m)
    args.out.write_text(header(data))
    if args.cmdl:args.cmdl.write_bytes(data)
    if args.mesh_json:args.mesh_json.write_text(json.dumps(dict(vertices=m.vertices,triangles=m.triangles)))
    print(json.dumps(dict(bytes=len(data),vertices=len(m.vertices),triangles=sum(map(len,m.triangles))//3,
          materials=2,bounds=bounds,sha256=hashlib.sha256(data).hexdigest()),indent=2))
if __name__=='__main__':main()

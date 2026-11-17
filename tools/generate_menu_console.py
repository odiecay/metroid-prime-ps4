#!/usr/bin/env python3
"""Original PS4 menu sculpture, encoded as a texture-free Prime CMDL.

All geometry, colours and stroke lettering are authored here. No commercial
model, image, font or game data is read. Generated data uses Prime's documented
big-endian CMDL/material/display-list interfaces and normal CModel rendering.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

U32=lambda n:struct.pack('>I',n)
F32=lambda n:struct.pack('>f',n)

def pad(data):return data+b'\0'*((-len(data))%32)

def normal(a,b,c):
    u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
    n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
    length=math.sqrt(sum(x*x for x in n))
    assert length>1e-9
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
        # Original PS4's front and rear faces lean forwards as they rise.
        # Width/height/depth follow Sony's 275 x 53 x 305 mm launch dimensions.
        super().polygon([(x,y-.60*z,z)for x,y,z in points],colour,material)

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
        mesh.polygon([(x,-1.3705,y)if front else(x,y,z)for x,y in q],colour,material)

def build_mesh():
    m=Ps4Mesh()
    # Four sharp case blocks with a recessed horizontal seam and vertical panel split.
    # Shear expands the 2.735 deep body to the published 3.05 total depth.
    slab(m,2.75,2.735,-.265,-.045,False)
    slab(m,2.75,2.735,.045,.265,True)
    m.box((-1.31,-1.325,-.05),(1.31,1.325,.05),(9,13,19,255))
    # A narrow illuminated seam, set below the lid rather than painted over it.
    m.box((-.429,-1.345,.254),(-.401,1.342,.261),(9,24,42,255))
    m.box((-.424,-1.340,.263),(-.406,1.337,.267),(27,133,255,255),1)
    m.box((-.420,-1.338,.2672),(-.410,1.335,.268),(124,202,255,255),1)
    # Slot and both USB sockets sit in the actual recessed interspace.
    m.box((-1.22,-1.347,.000),(-.57,-1.326,.022),(2,4,7,255))
    for x in (.12,.49):
        m.box((x-.018,-1.352,-.037),(x+.188,-1.327,.037),(53,58,67,255))
        m.box((x,-1.355,-.026),(x+.17,-1.350,.026),(2,5,9,255))
        m.box((x+.018,-1.357,-.008),(x+.15,-1.354,.004),(71,88,111,255))
    # The power/eject touch keys follow the split down the slanted front fascia.
    for z in (.085,-.205):
        m.box((-.429,-1.372,z),(-.401,-1.368,z+.095),(9,14,22,255))
        m.box((-.421,-1.375,z+.040),(-.409,-1.372,z+.063),(145,157,171,255))
    # Fine side and rear ventilation retains the sharp two-layer silhouette.
    for side in (-1,1):
        for k in range(18):
            y=-1.12+k*.13
            x=side*1.376
            lo,hi=sorted((x,x+side*.007))
            m.box((lo,y,-.033),(hi,y+.075,.030),(5,9,14,255))
    for k in range(23):
        x=-1.25+k*.11;m.box((x,1.368,-.196),(x+.062,1.374,-.090),(6,10,16,255))
    # Recessed rear I/O is authored detail, not imported texture imagery.
    for x,width in [(-1.12,.20),(-.72,.19),(-.36,.17),(.10,.22),(.78,.32)]:
        m.box((x,1.342,-.018),(x+width,1.349,.027),(4,7,12,255))
    # Authored thin-stroke PS4 wordmark on the front-right face, as on CUH-1000.
    glyphs=[[(0,0),(0,.095),(.105,.095),(.105,.048),(0,.048)],
            [(.105,.095),(0,.095),(0,.048),(.105,.048),(.105,0),(0,0)],
            [(0,.095),(0,.045),(.105,.045),(.105,.095),(.105,0)]]
    for g,points in enumerate(glyphs):
        stroke(m,[(.79+g*.15+x,.115+y)for x,y in points],.010,front=True)
    # Small, independently drawn P/S monogram on the glossy lid.
    stroke(m,[(-.92,.91),(-.92,1.15),(-.80,1.15),(-.80,1.03),(-.92,1.03)],.025,z=.2665,colour=(121,133,150,255))
    stroke(m,[(-.77,1.01),(-.86,.98),(-.73,.94),(-.84,.91)],.020,z=.2665,colour=(121,133,150,255))
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
    return ('// Generated by tools/generate_menu_console.py. Original-authored geometry; SPDX-License-Identifier: MIT\n'
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

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
    out=[]
    for x,y,start in [(w/2-r,d/2-r,0),(-w/2+r,d/2-r,90),(-w/2+r,-d/2+r,180),(w/2-r,-d/2+r,270)]:
        for i in range(4):
            a=math.radians(start+i*30);out.append((x+r*math.cos(a),y+r*math.sin(a)))
    return out

def clipped(poly,xcut,less):
    out=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        ina=(a[0]<=xcut) if less else (a[0]>=xcut)
        inb=(b[0]<=xcut) if less else (b[0]>=xcut)
        if ina:out.append(a)
        if ina!=inb:
            t=(xcut-a[0])/(b[0]-a[0]);out.append((xcut,a[1]+t*(b[1]-a[1])))
    return out

def slab(mesh,w,d,z0,z1):
    bevel=.045;outer=outline(w,d,.09);inner=outline(w-2*bevel,d-2*bevel,.05)
    rings=[[(x,y,z0)for x,y in inner],[(x,y,z0+bevel)for x,y in outer],
           [(x,y,z1-bevel)for x,y in outer],[(x,y,z1)for x,y in inner]]
    mesh.polygon(list(reversed(rings[0])),(31,35,43,255))
    for low,high in zip(rings,rings[1:]):
        colour=(65,72,84,255) if high is rings[-1] else (43,49,59,255)
        for i in range(len(low)):
            j=(i+1)%len(low);mesh.polygon([low[i],low[j],high[j],high[i]],colour)
    for less,colour in [(True,(43,49,61,255)),(False,(66,73,84,255))]:
        mesh.polygon([(x,y,z1)for x,y in clipped(inner,-.38,less)],colour)

def build_mesh():
    m=Mesh()
    slab(m,2.72,2.82,-.255,-.035)
    slab(m,2.72,2.82,.035,.255)
    # Recessed middle band, slot, paired USB sockets and thin top status strip.
    m.box((-1.315,-1.365,-.04),(1.315,1.365,.04),(12,16,22,255))
    m.box((-1.02,-1.414,.065),(-.10,-1.409,.105),(3,5,8,255))
    for x in (.21,.68):
        m.box((x,-1.414,-.175),(x+.265,-1.409,-.075),(7,10,15,255))
        m.box((x+.025,-1.418,-.147),(x+.24,-1.415,-.125),(92,103,118,255))
    m.box((-.397,-1.2,.2555),(-.363,1.2,.259),(27,120,255,255),1)
    m.box((-1.22,-1.416,.075),(-1.11,-1.411,.11),(40,145,255,255),1)
    # Rear ventilation slots are shallow authored recesses, not transparent faces.
    for i in range(13):
        x=-1.16+i*.18;m.box((x,1.409,-.19),(x+.10,1.414,-.09),(8,11,17,255))
    # Original monoline P/S/4 letter strokes; no imported font or logo image.
    glyphs=[[(0,0,.04,.34),(0,.30,.19,.34),(.15,.16,.19,.34),(0,.15,.19,.19)],
            [(0,.30,.19,.34),(0,.15,.04,.34),(0,.15,.19,.19),(.15,0,.19,.19),(0,0,.19,.04)],
            [(0,.15,.04,.34),(.15,0,.19,.34),(0,.15,.19,.19)]]
    for g,strokes in enumerate(glyphs):
        for x,y,X,Y in strokes:
            m.box((.43+g*.26+x,.70+y,.256),(.43+g*.26+X,.70+Y,.259),(150,161,178,255))
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

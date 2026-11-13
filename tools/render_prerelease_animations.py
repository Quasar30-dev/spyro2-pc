"""Render the original decoded meshes/textures for animation RE comparison.

Orthographic inspection views, not generated artwork. Retains every decoded
frame in JSON and exports contact sheets from several inspection angles.
"""
from pathlib import Path
import sys,struct,json
import numpy as np
from PIL import Image,ImageDraw,ImageFont
from spyrodisc import wad_index,u32
from probe_spyro_model import decode
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/analysis/prerelease-20260930/animation-comparison';OUT.mkdir(exist_ok=True)
builds=[('Retail','data/disc/SCUS_944.25','data/disc/WAD.WAD',15,0x80062674),
 ('July 13','data/builds/july-1999/disc/SPYRO2.EXE','data/builds/july-1999/disc/WAD.WAD',6,0x800649d8),
 ('Winter Jampack','data/builds/winter-jampack-1999/disc/SPYRO2/DEMO2.EXE','data/builds/winter-jampack-1999/disc/SPYRO2/WAD.WAD',29,0x80069134)]
def assets(exefile,wadfile,slot,code):
 exe=(ROOT/exefile).read_bytes();wad=(ROOT/wadfile).read_bytes();idx=wad_index(wad[:0x10000],len(wad));e=idx[0];g=wad[e.offset:e.offset+e.size];e=idx[slot];level=wad[e.offset:e.offset+e.size];tex=level[u32(level,0):u32(level,0)+0x80000]
 return g,struct.unpack_from('<125I',exe,code-u32(exe,24)+0x800),np.frombuffer(tex,dtype='<u2').reshape(512,512)
def clip(g,lookup,n):
 at=0x800+u32(g,0x800+60+n*4);common=0x800+u32(g,0x800+56);p=common+u32(g,at+8);palette=common+u32(g,at+12)
 faces=[]
 for x in range(p+4,p+4+u32(g,p),20):
  vs=[u32(g,x)>>s&255 for s in [24,16,8,0]];cs=[u32(g,x+4)>>s&127 for s in [24,17,10,3]]
  colors=[[(u32(g,palette+c*4)>>s&255)/128 for s in [0,8,16]] for c in cs]
  uv=[list(g[x+k:x+k+2]) for k in [8,12,16,18]];clut,page=struct.unpack_from('<H',g,x+10)[0],struct.unpack_from('<H',g,x+14)[0]
  for t in ([(0,1,2),(1,3,2)] if vs[2]!=vs[3] else [(0,1,2)]):faces.append(([vs[i] for i in t],np.array([uv[i] for i in t]),np.array([colors[i] for i in t]),clut,page))
 frames=[]
 for f in range(g[at]):
  v,_=decode(g,n,f,lookup);a=np.array(v,float)/1024;frames.append(a[:,[1,2,0]]*[-1,1,1])
 return frames,faces
def render(vertices,faces,vram,angle,size=280):
 eye=np.array(angle,dtype=float);forward=-eye/np.linalg.norm(eye);right=np.cross(forward,[0,1,0]);right/=np.linalg.norm(right);up=np.cross(right,forward)
 center=np.array([0,-.13,0]);v=vertices-center;screen=np.column_stack((v@right,-(v@up)))*230+size/2;depth=v@forward
 rgb=np.empty((size,size,3),np.uint8);rgb[:]=[30,33,46];zbuf=np.full((size,size),np.inf)
 for ids,uv,colors,clut,page in faces:
  xy=screen[ids];x0,y0=np.maximum(0,np.floor(xy.min(0))).astype(int);x1,y1=np.minimum(size-1,np.ceil(xy.max(0))).astype(int)
  if x1<x0 or y1<y0:continue
  xx,yy=np.meshgrid(np.arange(x0,x1+1)+.5,np.arange(y0,y1+1)+.5);a,b,c=xy
  den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
  if abs(den)<1e-8:continue
  w0=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den;w1=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den;w2=1-w0-w1;w=np.stack([w0,w1,w2],-1)
  z=w@depth[ids];mask=(w.min(-1)>=-1e-7)&(z<zbuf[y0:y1+1,x0:x1+1]);coords=(w@uv).astype(int)&255;u,t=coords[...,0],coords[...,1]
  px=(page&15)*64+u//4-512;py=((page>>4)&1)*256+t
  if px.min()<0 or px.max()>=512:continue
  pal=(vram[py,px]>>((u%4)*4))&15;packed=vram[(clut>>6)&511,(clut&63)*16+pal-512];mask &= packed!=0
  color=np.stack([((packed>>s)&31)*255/31 for s in [0,5,10]],-1);color=np.clip(color*(w@colors),0,255).astype(np.uint8)
  rgb[y0:y1+1,x0:x1+1][mask]=color[mask];zbuf[y0:y1+1,x0:x1+1][mask]=z[mask]
 return Image.fromarray(rgb)
loaded=[];report={}
for name,exe,wad,slot,code in builds:
 g,l,vram=assets(exe,wad,slot,code);clips={}
 for id in [16,24,10,11]:
  if u32(g,0x800+60+id*4)!=0xffffffff:clips[id]=clip(g,l,id)
 loaded.append((name,clips,vram));report[name]={str(i):{'frames':[v.tolist() for v in c[0]],'triangles':[f[0] for f in c[1]]} for i,c in clips.items()}
(OUT/'decoded.json').write_text(json.dumps(report))
sheet=Image.new('RGB',(5*280,3*310),(20,22,32));d=ImageDraw.Draw(sheet)
for row,(name,clips,vram) in enumerate(loaded):
 frames,faces=clips[16];d.text((12,row*310+6),name+' / global clip 16 / all five keyframes',fill='white')
 for f,v in enumerate(frames):sheet.paste(render(v,faces,vram,(1,0.65,2)),(f*280,row*310+28))
sheet.save(OUT/'glide-all-frames.png')
views=[(0,.15,-2),(2,.3,0),(0,1.8,.6)]
for view,angle in zip(['rear','side','above'],views):
 still=Image.new('RGB',(840,310),(20,22,32));d=ImageDraw.Draw(still)
 for i,(name,clips,vram) in enumerate(loaded):
  frames,faces=clips[16];still.paste(render(frames[1],faces,vram,angle),(i*280,28));d.text((i*280+10,7),name,fill='white')
 still.save(OUT/f'glide-{view}.png')
name,clips,vram=loaded[1];roll=Image.new('RGB',(6*280,2*310),(20,22,32));d=ImageDraw.Draw(roll)
for row,id in enumerate([10,11]):
 frames,faces=clips[id];d.text((10,row*310+6),f'July clip {id} / recovered roll',fill='white')
 for i,f in enumerate([0,4,8,12,16,19]):roll.paste(render(frames[f],faces,vram,(.5,.35,-2)),(i*280,row*310+28))
roll.save(OUT/'july-rolls.png')
print(OUT)

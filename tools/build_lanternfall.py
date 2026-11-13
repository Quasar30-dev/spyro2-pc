"""Assemble the reproducible Gaussian module test level and gameplay scaffold."""
from pathlib import Path
import json,shlex,struct
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates,distance_transform_edt,binary_dilation

ROOT=Path(__file__).resolve().parents[1]
WORLD=ROOT/'data/custom-levels/lanternfall'

def coverage_surface(module,offset,connected):
    src=WORLD/'source';rgb=np.array(Image.open(src/f'{module}-blank-sky.png').convert('RGB'))
    h,w=rgb.shape[:2];key=(rgb[...,0]>180)&(rgb[...,2]>170)&(rgb[...,1]<90)
    key=binary_dilation(key,iterations=1)
    indices=distance_transform_edt(key,return_distances=False,return_indices=True)
    rgba=np.empty((h,w,4),dtype=np.uint8);rgba[:,:,:3]=rgb[tuple(indices)];rgba[:,:,3]=np.where(key,0,255)
    depth=np.load(src/f'{module}-splats/aligned-depth.npy')
    nx,ny=384,192;yy,xx=np.mgrid[:ny+1,:nx+1];u=xx/nx;v=np.clip(yy/ny,.00005,.99995)
    yaw=(u-.5)*2*np.pi;pitch=(.5-v)*np.pi
    directions=np.stack([np.sin(yaw)*np.cos(pitch),np.sin(pitch),np.cos(yaw)*np.cos(pitch)],-1)
    coords=np.stack([np.clip(v*h-.5,0,h-1),(u*w-.5)%w]);r=map_coordinates(depth,coords,order=1,mode='grid-wrap')
    position=directions*(r[...,None]+.025);position[...,1]+=1.6;position[...,1]=np.maximum(position[...,1],-.015)
    position+=offset
    data=np.concatenate([position,u[...,None],v[...,None]],axis=-1).astype('<f4')
    mesh=[]
    for j in range(ny):
        for i in range(nx):
            for ids in [[(j,i),(j+1,i),(j+1,i+1)],[(j,i),(j+1,i+1),(j,i+1)]]:
                verts=np.array([data[a,b] for a,b in ids]);center=verts[:,:3].mean(0)
                # Leave an actual opening between the two modules.
                if connected:
                    if module=='exterior' and center[2]>16 and abs(center[0])<2.15 and center[1]<4.6:continue
                    if module=='interior' and center[2]<21:continue
                verts[:,4]=(verts[:,4]+(module=='interior'))/2
                mesh.append(verts)
    return np.concatenate(mesh),rgba

def pack(parts,path,modules=None):
    arrays=[]
    for d,offset in parts:
        p=d['positions'].copy();p+=offset;c=d['covariance'];a=np.zeros((len(p),16),dtype='<f4')
        a[:,:3]=p;a[:,3]=np.clip(d['opacity'],.00001,1)
        a[:,4:7]=np.clip(d['colors'],0,1)
        a[:,8:11]=np.diagonal(c,axis1=1,axis2=2)+1e-8
        a[:,12:15]=c[:,[0,0,1],[1,2,2]]
        arrays.append(a)
    a=np.concatenate(arrays)
    if modules:
        surfaces=[];images=[]
        for module,offset in modules:
            mesh,rgba=coverage_surface(module,np.array(offset),len(modules)>1);surfaces.append(mesh);images.append(rgba)
        if len(images)==1:images.append(images[0].copy())
        mesh=np.concatenate(surfaces).astype('<f4');rgba=np.concatenate(images);h,w=rgba.shape[:2]
        with path.open('wb') as f:
            f.write(b'S2GS0002');f.write(struct.pack('<IIII',len(a),len(mesh),w,h));f.write(a.tobytes());f.write(mesh.tobytes());f.write(rgba.tobytes())
    else:
        with path.open('wb') as f:f.write(b'S2GS0001');f.write(struct.pack('<I',len(a)));f.write(a.tobytes())
    return len(a)

def actors():
    t=shlex.split((ROOT/'data/custom-levels/hollowbloom-springs/pc/hollowbloom.s2world').read_text())
    i=10;nm=int(t[i]);i+=1+nm*3;nt=int(t[i]);i+=1+nt*26;n=int(t[i]);i+=1
    refs={}
    for j in range(n):
        row=t[i+j*7:i+(j+1)*7];refs[int(row[0])]=int(row[1])
    return refs

def world_file(path,splat_name,interior=False):
    faces=[]
    def tri(a,b,c,visible=False):
        faces.append((3 if visible else 2,[a,b,c]))
    def quad(a,b,c,d,visible=False):tri(a,b,c,visible);tri(a,c,d,visible)
    # Separate collision is deliberately simple and editable. The first test
    # uses a broad connected ground lane, with rail bounds outside the lane.
    end=38 if interior else 12
    quad((-7,0,-7),(-7,0,end),(7,0,end),(7,0,-7))
    for x in [-7,7]:quad((x,0,-7),(x,4,-7),(x,4,end),(x,0,end))
    for z in [-7,end]:quad((-7,0,z),(7,0,z),(7,4,z),(-7,4,z))
    if interior:
        # Short mesh vestibule explicitly bridges the independent modules.
        quad((-2,0,16),(-2,0,21),(2,0,21),(2,0,16),True)
        for x in [-2,2]:quad((x,0,16),(x,4.5,16),(x,4.5,21),(x,0,21),True)
        quad((-2,4.5,16),(2,4.5,16),(2,4.5,21),(-2,4.5,21),True)
    refs=actors();objects=[]
    if 1 in refs:
        for x,z in [(-1,2),(0,3),(1,4),(-1,6),(1,8)]+([(0,23),(1,27),(-1,30)] if interior else []):objects.append([1,refs[1],x,.2,z,0,1])
    if 0xc8 in refs:objects.append([0xc8,refs[0xc8],3,.1,5,0,5])
    if 0x1aa in refs:objects.append([0x1aa,refs[0x1aa],0,0,35 if interior else 10,3.141593,1])
    lines=['S2WORLD 2','17','"Lanternfall Court - Gaussian test"','0 0.08 0 0 0.25 0.34 0.53','1','33 7 0',str(len(faces))]
    for flags,face in faces:
        lines.append(f'{flags} 0')
        for j,(x,y,z) in enumerate(face):lines.append(f'{x} {y} {z} .88 .8 .68 {int(j==1)} {int(j==2)}')
    lines.append(str(len(objects)));lines.extend(' '.join(map(str,o)) for o in objects)
    lines.extend(['3','0 0 0','0 0 7',f'0 0 {30 if interior else 10}','0','0','0','0','SPLATS 1',json.dumps(splat_name)])
    path.write_text('\n'.join(lines)+'\n')

def main():
    src=WORLD/'source';pc=WORLD/'pc';pc.mkdir(exist_ok=True)
    aligned=(src/'exterior-splats/aligned.npz').exists()
    exterior=dict(np.load(src/('exterior-splats/aligned.npz' if aligned else 'exterior-splats/module.npz')))
    count=pack([(exterior,[0,0,0])],pc/'exterior.s2gs',[('exterior',[0,0,0])] if aligned else None);world_file(pc/'exterior.s2world','exterior.s2gs')
    report={'exterior_gaussians':count}
    if (src/'interior-splats/module.npz').exists():
        interior=dict(np.load(src/('interior-splats/aligned.npz' if aligned else 'interior-splats/module.npz')))
        # Editable cut planes at each threshold remove overlapping distant
        # predictions. Full, uncropped source PLYs remain available in Blender.
        p=exterior['positions'];keep=(p[:,2]<16)|(np.abs(p[:,0])>2.15)|(p[:,1]>4.6)
        exterior={k:v[keep] for k,v in exterior.items()}
        p=interior['positions'];keep=(p[:,2]>-9)&(np.abs(p[:,0])<18)&(p[:,2]<22)
        interior={k:v[keep] for k,v in interior.items()}
        report['connected_gaussians']=pack([(exterior,[0,0,0]),(interior,[0,0,30])],pc/'lanternfall.s2gs',[('exterior',[0,0,0]),('interior',[0,0,30])] if aligned else None)
        world_file(pc/'lanternfall.s2world','lanternfall.s2gs',True)
    (pc/'assembly.json').write_text(json.dumps(report,indent=2));print(report)

if __name__=='__main__':main()

"""Author a flat PS1 portal panorama from Mesa's actual far terrain in Blender."""
import bpy
import bmesh
import sys
import json
import struct
import hashlib
import math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
import argparse
ap=argparse.ArgumentParser();ap.add_argument('--archive',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--mode',choices=('grid','mesh','adaptive'),default='grid')
a=ap.parse_args(sys.argv[sys.argv.index('--')+1:]);a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
raw=a.archive.read_bytes();scene_at,scene_size=struct.unpack_from('<II',raw,8)
s=raw[scene_at:scene_at+scene_size];region_at=struct.unpack_from('<I',s,0)[0];r=s[region_at:region_at+struct.unpack_from('<I',s,region_at)[0]]
directory=4+struct.unpack_from('<I',r,4)[0];count=struct.unpack_from('<I',r,directory)[0]
vertices=[];faces=[];colours=[]
for i in range(count):
    at=4+struct.unpack_from('<I',r,directory+4+i*4)[0];h=r[at:at+28]
    y,x,_,z=struct.unpack_from('<4H',h,8);nv,nc,nf=h[16:19];vbase=len(vertices)
    for j in range(nv):
        word=struct.unpack_from('<I',r,at+28+j*4)[0]
        vertices.append(((x*16+(word>>21)*16)/1024,(y*16+((word>>10)&2047)*16)/1024,(z*16+(word&1023)*32)/1024))
    palette=r[at+28+nv*4:at+28+(nv+nc)*4];p=at+28+(nv+nc)*4
    for j in range(nf):
        vw,cw=struct.unpack_from('<II',r,p+j*8)
        vi=[(vw>>shift)&127 for shift in (24,17,10,3)];ci=[(cw>>shift)&127 for shift in (25,18,11,4)]
        slots=(0,1,2) if vi[2]==vi[3] else (0,1,3,2)
        assert all(vi[k]<nv and ci[k]<nc for k in slots)
        faces.append(tuple(vbase+vi[k] for k in slots))
        cols=[[palette[ci[k]*4+c] for c in range(3)] for k in slots]
        colours.append(tuple(min(255,round(sum(c[k] for c in cols)/len(cols)/16)*16)/255 for k in range(3)))
mesh=bpy.data.meshes.new('Actual Mesa far terrain');mesh.from_pydata(vertices,[],faces);mesh.update()
obj=bpy.data.objects.new('Actual terrain panorama source',mesh);bpy.context.collection.objects.link(obj)
palette=sorted(set(colours));indices={c:i for i,c in enumerate(palette)}
for i,c in enumerate(palette):
    mat=bpy.data.materials.new('Source B shade '+str(i))
    linear=lambda v:v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
    mat.diffuse_color=(*(linear(v) for v in c),1);mesh.materials.append(mat)
for poly,c in zip(mesh.polygons,colours):poly.material_index=indices[c]
master=obj.copy();master.data=mesh.copy();master.name='Editable unchanged far terrain';bpy.context.collection.objects.link(master)
master.hide_render=True;master.hide_viewport=True
if a.mode=='mesh':
    # Original regions duplicate boundary vertices. Weld only the derived
    # picture mesh so decimation can cross those coincident chart boundaries;
    # the retained editable master remains the exact original far geometry.
    bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.00001)
    bm.to_mesh(mesh);bm.free();mesh.update()
tri=obj.modifiers.new('Original quad triangulation','TRIANGULATE');bpy.context.view_layer.objects.active=obj
bpy.ops.object.modifier_apply(modifier=tri.name)
decimate=obj.modifiers.new('Portal picture polygon budget','DECIMATE');decimate.ratio=.025
bpy.ops.object.modifier_apply(modifier=decimate.name)
obj.hide_render=True;master.hide_render=False
lo=Vector(tuple(min(v[k] for v in vertices) for k in range(3)));hi=Vector(tuple(max(v[k] for v in vertices) for k in range(3)))
target=(lo+hi)/2;extent=max(hi[k]-lo[k] for k in range(3))
cam_data=bpy.data.cameras.new('Mesa portal calibrated orthographic');cam=bpy.data.objects.new(cam_data.name,cam_data);bpy.context.collection.objects.link(cam)
cam.location=target+Vector((.7,-1.3,1.1))*extent
cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam_data.type='ORTHO';cam_data.ortho_scale=extent*1.45
scene=bpy.context.scene;scene.camera=cam;scene.render.resolution_x=1024;scene.render.resolution_y=432;scene.render.resolution_percentage=100
scene.world.color=(.31,.25,.40);bpy.context.view_layer.update()
projections=[world_to_camera_view(scene,cam,Vector(v)) for v in vertices]
cam_data.ortho_scale*=max(max(abs(p.x-.5),abs(p.y-.5)) for p in projections)*2*1.08
bpy.context.view_layer.update()
screen=[]
for v in obj.data.vertices:
    p=world_to_camera_view(scene,cam,obj.matrix_world@v.co)
    screen.append([round(p.x*512),round(12+(1-p.y)*216)])
assert all(0<=x<=512 and 12<=y<=228 for x,y in screen)
polygons=[]
for poly in obj.data.polygons:
    ids=list(poly.vertices)
    for i in range(1,len(ids)-1):polygons.append(dict(vertices=[ids[0],ids[i],ids[i+1]],rgb=[min(255,max(0,round(v*255))) for v in palette[poly.material_index]]))
scene.render.engine='BLENDER_WORKBENCH';scene.display.shading.light='FLAT';scene.display.shading.color_type='MATERIAL'
scene.display.shading.background_type='WORLD';scene.display.shading.show_shadows=False
scene.display.shading.show_cavity=False;scene.display.shading.show_specular_highlight=False
scene.view_settings.view_transform='Standard'
scene.render.filepath=str(a.out/'panorama-source-render.png');bpy.ops.render.render(write_still=True)
render=bpy.data.images.load(str(a.out/'panorama-source-render.png'));pixels=list(render.pixels)
width,height=render.size
screen=[];vertex_rgb=[];polygons=[]
# A freshly loaded PNG exposes its stored channel values through pixels here.
# Applying another linear-to-sRGB curve washed out the portal (PNG151/137/170
# became202/194/213). Keep the authored PNG colours; verify against its bytes.
nx,ny=19,10
for row in range(ny+1):
    for col in range(nx+1):
        x=round(col/nx*512);y=round(12+row/ny*216);screen.append([x,y])
        px=min(width-1,round(col/nx*(width-1)));py=min(height-1,round((1-row/ny)*(height-1)))
        offset=(py*width+px)*4
        vertex_rgb.append([round(max(0,min(1,v))*255) for v in pixels[offset:offset+3]])
for row in range(ny):
    for col in range(nx):
        q=row*(nx+1)+col
        polygons.extend([dict(vertices=[q,q+1,q+nx+2]),dict(vertices=[q,q+nx+2,q+nx+1])])
if a.mode=='mesh':
    # The regular20x11 colour grid blurred thin bridges into diagonal bands.
    # Keep the actual Blender-decimated silhouettes and flat far-terrain shades.
    # Project once, then painter-sort from the retained calibrated camera.
    screen=[];vertex_rgb=[];polygons=[]
    for v in obj.data.vertices:
        p=world_to_camera_view(scene,cam,obj.matrix_world@v.co)
        screen.append([round(p.x*512),round(12+(1-p.y)*216)])
    inverse=cam.matrix_world.inverted()
    depth=lambda p:sum((inverse@(obj.matrix_world@obj.data.vertices[i].co)).z for i in p.vertices)/len(p.vertices)
    for poly in sorted(obj.data.polygons,key=depth):
        rgb=[min(255,max(0,round(v*255))) for v in palette[poly.material_index]]
        ids=list(poly.vertices)
        for i in range(1,len(ids)-1):polygons.append(dict(vertices=[ids[0],ids[i],ids[i+1]],rgb=rgb))
    assert len(screen)<=1024 and len(polygons)<=389,(len(screen),len(polygons))
    assert all(0<=x<=512 and 12<=y<=228 for x,y in screen)
if a.mode=='adaptive':
    import numpy as np
    from mathutils.geometry import delaunay_2d_cdt
    # Select380 image points by edge importance and distance, keeping the
    # source-camera image as the reference. Blender performs the triangulation;
    # the actual picture remains an editable, separately named mesh in the file.
    rgb=np.asarray(pixels,np.float32).reshape(height,width,4)[::-1,:,:3]
    rgb=rgb[::2,::2];hh,ww=rgb.shape[:2]
    edge=np.zeros((hh,ww),np.float32)
    edge[:,1:]+=np.linalg.norm(rgb[:,1:]-rgb[:,:-1],axis=2)
    edge[1:]+=np.linalg.norm(rgb[1:]-rgb[:-1],axis=2)
    yy,xx=np.mgrid[:hh,:ww];distance=np.full((hh,ww),np.inf)
    chosen=[]
    def point(x,y):
        chosen.append((int(x),int(y)))
        np.minimum(distance,(xx-x)**2+(yy-y)**2,out=distance)
    for y in np.linspace(0,hh-1,5,dtype=int):
        for x in np.linspace(0,ww-1,9,dtype=int):point(x,y)
    for _ in range(380-len(chosen)):
        score=(edge+0.015)*distance
        y,x=np.unravel_index(np.argmax(score),score.shape);point(x,y)
    coordinates=[Vector((x/(ww-1)*512,12+y/(hh-1)*216)) for x,y in chosen]
    result=delaunay_2d_cdt(coordinates,[],[],0,0.00001)
    screen=[[round(v.x),round(v.y)] for v in result[0]];vertex_rgb=[];polygons=[]
    for ids in result[2]:
        assert len(ids)==3
        points=np.array([[screen[i][0]/512*(ww-1),(screen[i][1]-12)/216*(hh-1)] for i in ids])
        samples=[]
        for weights in [(1/3,1/3,1/3),(.6,.2,.2),(.2,.6,.2),(.2,.2,.6)]:
            x,y=np.sum(points*np.array(weights)[:,None],axis=0)
            samples.append(rgb[min(hh-1,round(y)),min(ww-1,round(x))])
        colour=np.clip(np.round(np.mean(samples,axis=0)*255/8)*8,0,255).astype(int).tolist()
        polygons.append(dict(vertices=list(ids),rgb=colour))
    assert len(screen)<=440 and len(polygons)<=767,(len(screen),len(polygons))
    picture_mesh=bpy.data.meshes.new('Editable adaptive original-format portal picture')
    picture_mesh.from_pydata([(x,y,0) for x,y in screen],[],[f['vertices'] for f in polygons]);picture_mesh.update()
    picture=bpy.data.objects.new(picture_mesh.name,picture_mesh);bpy.context.collection.objects.link(picture)
    picture.hide_render=True;picture.hide_viewport=True
    colours2=sorted({tuple(f['rgb']) for f in polygons});assert len(colours2)<=512
    for c in colours2:
        mat=bpy.data.materials.new('Portal picture RGB '+','.join(map(str,c)))
        mat.diffuse_color=(*(linear(v/255) for v in c),1);picture_mesh.materials.append(mat)
    for face,f in zip(picture_mesh.polygons,polygons):face.material_index=colours2.index(tuple(f['rgb']))
blend=a.out/'market-mesa-portal-panorama.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend))
payload=dict(source_archive=str(a.archive.resolve()),source_sha256=hashlib.sha256(raw).hexdigest(),source_far_faces=len(faces),
             blend=str(blend),blend_sha256=hashlib.sha256(blend.read_bytes()).hexdigest(),vertices=screen,vertex_rgb=vertex_rgb,faces=polygons,
             source_render=str(a.out/'panorama-source-render.png'),source_render_sha256=hashlib.sha256((a.out/'panorama-source-render.png').read_bytes()).hexdigest(),
             sky_rgb=vertex_rgb[0] if vertex_rgb else [round(max(0,min(1,v))*255) for v in pixels[:3]],colour_encoding='Stored PNG RGB, no second transfer curve',
             camera=dict(position=list(cam.location),rotation=list(cam.rotation_euler),ortho_scale=cam_data.ortho_scale),
             limits=['Flat original-format portal artwork derived/decimated from actual Mesa far mesh; not a new level mesh.',
                     'Original PS1 clipping/draw and final portal-framing review still required.'])
if a.mode in ('mesh','adaptive'):
    # Retain the original PNG's sky, with explicit flat per-face source colours.
    payload.update(sky_rgb=[round(max(0,min(1,v))*255) for v in pixels[:3]],
                   vertex_rgb=[],face_shading='flat',colour_encoding='Original far palette RGB; flat original portal face flag',
                   projection_order='Calibrated Blender camera, descending distance / far first' if a.mode=='mesh' else 'Blender Delaunay on edge-weighted source image samples; flat triangles')
(a.out/'panorama.json').write_text(json.dumps(payload,indent=2)+'\n')
print('MESA PORTAL ART',len(screen),'screen vertices',len(polygons),'faces from actual',len(faces),'far faces;',payload['source_sha256'])

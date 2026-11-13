"""Bake registered view detail per surface chart, retaining reusable sheet UVs.

Unlike assigning a different photo to each triangle, visibility is resolved per
texel and feathered within a coherent chart. Source images remain unchanged.
"""
from pathlib import Path
import json,math,struct,time,sys
import numpy as np
import cv2
from PIL import Image
from scipy.ndimage import distance_transform_edt
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/custom-levels/lanternfall/complete-hall';SOURCE=OUT.parent/'source/multiview'
polished='--polished' in sys.argv
floor_only='--floor-only' in sys.argv
assert not floor_only or polished,'Partial floor bake requires the polished asset'
prefix='hall-polished' if polished else 'hall-detail-baked'
records=json.loads((OUT/('geometry-polished.json' if polished else 'geometry-refined.json')).read_text());cameras=json.loads((OUT/'cameras.json').read_text())
materials={m['id']:m for m in json.loads((SOURCE/'materials.json').read_text())['materials']}
sheets=[np.asarray(Image.open(SOURCE/(c+'-material-atlas.png')).convert('RGB')) for c in ['exterior','interior']]
views=[]
fits={v['view']:v for v in json.loads((OUT/'refinement.json').read_text())['views']}
for camera in cameras:
    matrix=np.asarray(camera['camera_to_world_blender']);ref=np.load(OUT/'depth-reference'/(camera['id']+'.npz'))
    pixels=np.asarray(Image.open(OUT/'generated-views'/camera.get('generated_image',camera['id']+'.png')).convert('RGB'))
    registration=OUT/'registration'/(camera['id']+'.npz')
    offset=np.load(registration)['uv_offset'] if polished else None
    if offset is not None:assert np.isfinite(offset).all()
    predicted=np.load(OUT/'moge-detail'/(camera['id']+'.npz'))['depth']
    fit=fits[camera['id']]
    inverse=fit['inverse_depth_scale']/np.maximum(predicted,.01)+fit['inverse_depth_offset']
    aligned=np.where(np.isfinite(inverse)&(inverse>0),1/np.maximum(inverse,1e-6),0).astype(np.float32)
    rgb=pixels.astype(float)
    purple=(rgb[:,:,2]>rgb[:,:,1]*1.18)&(rgb[:,:,0]<rgb[:,:,2]*1.5)
    gold=(rgb[:,:,0]>rgb[:,:,2]*1.2)&(rgb[:,:,1]>rgb[:,:,2]*1.12)&(rgb[:,:,1]>rgb[:,:,0]*.65)
    _,labels,stats,_=cv2.connectedComponentsWithStats(gold.astype(np.uint8),8)
    small=np.array([False]+[s[cv2.CC_STAT_AREA]<1800 for s in stats[1:]])
    vault=(purple|small[labels]).astype(np.uint8)
    foliage=(rgb[:,:,1]>rgb[:,:,0]*1.08)&(rgb[:,:,1]>rgb[:,:,2]*1.08)
    stone_mask=(1-cv2.dilate((purple|foliage).astype(np.uint8),np.ones((21,21),np.uint8)))
    # Long gold components are architectural ribs/lanterns, not surface motifs.
    # They must come from actual trim/prop meshes, never from the ceiling bake.
    views.append({'camera':camera,'origin':matrix[:3,3],'rot':matrix[:3,:3], 'depth':ref['depth'],'predicted':aligned,'pixels':pixels,'vault_mask':vault,'stone_mask':stone_mask,'offset':offset,'tan':math.tan(math.radians(camera['horizontal_fov_degrees']/2))})

def project(points,view):
    local=(points-view['origin'])@view['rot'];z=-local[...,2];h,w=view['pixels'].shape[:2]
    u=.5+local[...,0]/np.maximum(z,.001)/(2*view['tan']);v=.5-local[...,1]/np.maximum(z,.001)/(2*view['tan']*h/w)
    return u,v,z

def paving_color(positions):
    """One world-space tile grid; retain the floor reference's actual stone detail.

    Samples are bounded by grout centres in the accepted overhead image. The
    clean stone row supplies nine variants. Six central cells reconstruct its
    single compass without any second projected compass or mismatched overlay.
    """
    tile=1.2
    gx=(positions[:,:,0]+tile/2)/tile;gy=(positions[:,:,1]-9)/tile
    ix=np.floor(gx).astype(np.int32);iy=np.floor(gy).astype(np.int32)
    fu=gx-ix;fv=gy-iy
    # Grout positions vary slightly with image height. Rectify each source cell
    # rather than carrying its perspective skew or a second grout edge into UVs.
    xlines=np.array([99,239,379,519,662,801,951,1088,1225,1365],np.float32)
    slopes=np.array([-2,-3,-2,-1,-1,0,1,2,2,3],np.float32)/131.5
    ylines=np.array([142,277,406,547,685],np.float32)
    column=np.mod(np.bitwise_xor(ix*73,iy*151),9)
    row=np.zeros(ix.shape,np.int32)
    compass=(np.abs(ix)<=1)&(iy>=-1)&(iy<=0)
    column=np.where(compass,ix+4,column)
    row=np.where(compass,2-iy,row)
    my=ylines[row+1]*(1-fv)+ylines[row]*fv
    left=xlines[column]+slopes[column]*(my-210)
    right=xlines[column+1]+slopes[column+1]*(my-210)
    mx=left*(1-fu)+right*fu
    rgb=cv2.remap(views[9]['pixels'],mx.astype(np.float32),my.astype(np.float32),cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE).astype(float)
    # Different source cells have differently lit grout. A shared narrow mortar
    # centre makes both limits agree, including exactly at mesh/texture borders.
    distance=np.minimum.reduce([fu,1-fu,fv,1-fv])
    # The compass spans several cells: its gold lines cross their internal grout.
    # Only its outer material boundary participates in the mortar transition.
    distance=np.where(compass,np.minimum.reduce([gx+1,2-gx,gy+1,1-gy]),distance)
    blend=np.clip(distance/.035,0,1);blend=blend*blend*(3-2*blend)
    return rgb*blend[:,:,None]+np.array([151.,112.,79.])*(1-blend[:,:,None])

objects=[];charts=[];pad=8
for obj in records:
    p=np.asarray(obj['vertices']);tri=p[obj['faces']];uv=np.asarray(obj['triangle_uvs'])
    cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);area=np.linalg.norm(cross,axis=1);valid=area>1e-9
    tri=tri[valid];uv=uv[valid];normal=cross[valid]/area[valid,None]
    normal[np.sum((tri.mean(1)-p.mean(0))*normal,axis=1)<0]*=-1
    item={'record':obj,'tri':tri,'uv':uv,'normal':normal,'charts':[]};oi=len(objects);objects.append(item)
    dominant=np.argmax(abs(normal),axis=1);group=dominant*2+(normal[np.arange(len(normal)),dominant]>0)
    for side in range(6):
        if polished and obj['name'].startswith('Lantern'):continue
        faces=np.flatnonzero(group==side)
        if not len(faces):continue
        axis=side//2;axes=[a for a in range(3) if a!=axis];q=tri[faces][:,:,axes];lo=q.min((0,1));hi=q.max((0,1));extent=hi-lo
        n=normal[faces].mean(0);n/=max(np.linalg.norm(n),1e-6);center=tri[faces].mean((0,1))
        # Exclude completely hidden sides from the unique detail bake.
        density=0
        for view in views:
            u,v,z=project(center,view);d=view['origin']-center;facing=n@d/max(np.linalg.norm(d),.001)
            if .005<u<.995 and .005<v<.995 and z>.1 and facing>.12:
                rh,rw=view['depth'].shape;depth=view['depth'][min(rh-1,int(v*rh)),min(rw-1,int(u*rw))]
                if abs(depth-z)<.3+.03*z:density=max(density,view['pixels'].shape[1]/(2*view['tan']*z)*math.sqrt(facing))
        if polished and obj['material'] in ['interior.paving','interior.compass'] and side==5:
            # A hidden centre does not make a paving tile wholly invisible.
            # Every upward floor surface shares the continuous material,
            # including vestibules and pieces partially beneath furniture.
            density=120
        if density==0:continue
        density=np.clip(density,80,240)
        width=max(4,int(math.ceil(extent[0]*density)));height=max(4,int(math.ceil(extent[1]*density)))
        if max(width,height)>1800:
            factor=1800/max(width,height);width=max(4,int(width*factor));height=max(4,int(height*factor))
        chart={'object':oi,'faces':faces,'axes':axes,'lo':lo,'extent':np.maximum(extent,1e-8),'w':width,'h':height,'normal':n}
        item['charts'].append(len(charts));charts.append(chart)

def pack(scale):
    sizes=[(max(4,int(c['w']*scale))+2*pad,max(4,int(c['h']*scale))+2*pad,i) for i,c in enumerate(charts)]
    sizes +=[(p.shape[1]+2*pad,p.shape[0]+2*pad,len(charts)+i) for i,p in enumerate(sheets)]
    sizes +=[(32,32,len(charts)+2)] # Neutral patch for true lantern materials.
    x=y=rowh=0;locations={};width=8192
    for w,h,i in sorted(sizes,key=lambda s:-s[1]):
        if x+w>width:x=0;y+=rowh;rowh=0
        locations[i]=(x+pad,y+pad,w-2*pad,h-2*pad);x+=w;rowh=max(rowh,h)
    height=math.ceil((y+rowh)/16)*16
    return locations,width,height
scale=1.;locations,width,height=pack(scale)
while width*height>32*1024*1024:
    scale*=.9;locations,width,height=pack(scale)
print('Charts',len(charts),'atlas',width,height,'density scale',scale,flush=True)
previous=json.loads((OUT/(prefix+'.json')).read_text()) if floor_only else None
if floor_only:
    assert previous['atlas_dimensions']==[width,height] and previous['charts']==len(charts)
    atlas=np.asarray(Image.open(OUT/(prefix+'-atlas.png')).convert('RGBA')).copy()
else:
    atlas=np.zeros((height,width,4),np.uint8);atlas[:,:,3]=255
for i,pixels in enumerate(sheets):
    x,y,w,h=locations[len(charts)+i];atlas[y-pad:y+h+pad,x-pad:x+w+pad,:3]=np.pad(pixels,((pad,pad),(pad,pad),(0,0)),mode='edge')
x,y,w,h=locations[len(charts)+2];atlas[y-pad:y+h+pad,x-pad:x+w+pad,:3]=255
photo_texels=surface_texels=0;chart_reports=[]
for ci,chart in enumerate(charts):
    item=objects[chart['object']];obj=item['record'];tri=item['tri'][chart['faces']];uv=item['uv'][chart['faces']]
    if floor_only and obj['material'] not in ['interior.paving','interior.compass']:
        old=previous['objects'][ci]
        assert old['object']==obj['name'] and old['faces']==len(chart['faces'])
        chart_reports.append(old)
        continue
    x,y,w,h=locations[ci];q=(tri[:,:,chart['axes']]-chart['lo'])/chart['extent']*np.array([w-1,h-1])
    positions=np.zeros((h,w,3),np.float32);sheet_uv=np.zeros((h,w,2),np.float32);mask=np.zeros((h,w),bool)
    for t,qu,texture_uv in zip(tri,q,uv):
        xmin,ymin=np.maximum(np.floor(qu.min(0)).astype(int),0);xmax,ymax=np.minimum(np.ceil(qu.max(0)).astype(int),[w-1,h-1])
        if xmin>xmax or ymin>ymax:continue
        yy,xx=np.mgrid[ymin:ymax+1,xmin:xmax+1];r=np.stack([xx-qu[0,0],yy-qu[0,1]],-1)
        a=qu[1]-qu[0];b=qu[2]-qu[0];det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-7:continue
        beta=(r[:,:,0]*b[1]-r[:,:,1]*b[0])/det;gamma=(a[0]*r[:,:,1]-a[1]*r[:,:,0])/det;alpha=1-beta-gamma
        inside=(alpha>=-1e-4)&(beta>=-1e-4)&(gamma>=-1e-4)
        bary=np.stack([alpha,beta,gamma],-1)
        positions[ymin:ymax+1,xmin:xmax+1][inside]=(bary@t)[inside]
        sheet_uv[ymin:ymax+1,xmin:xmax+1][inside]=(bary@texture_uv)[inside]
        mask[ymin:ymax+1,xmin:xmax+1]|=inside
    material=materials['interior.brass' if obj['name'].startswith('Transverse rib') else obj['material']];category=0 if obj['material'].startswith('exterior') else 1;sheet=sheets[category]
    sh,sw=sheet.shape[:2];col,row=material['cell']
    mapx=(col*sw/4+2+sheet_uv[:,:,0]*(sw/4-4)).astype(np.float32);mapy=(row*sh/2+2+(1-sheet_uv[:,:,1])*(sh/2-4)).astype(np.float32)
    base=cv2.remap(sheet,mapx,mapy,cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE).astype(float)
    shade=np.clip(.76+.22*(chart['normal']@np.array([-.351,-.281,.895])),.52,1)
    base*=shade*np.asarray(obj['color']);samples=[];weights=[];feathers=[]
    # Chart packing boundaries are not visibility boundaries. Extrapolate only
    # sampling positions outside the triangle footprint, so a five-pixel fade
    # cannot create a repeated pale stripe at every narrow architectural piece.
    nearest=distance_transform_edt(~mask,return_distances=False,return_indices=True)
    positions[~mask]=positions[nearest[0][~mask],nearest[1][~mask]]
    for view in views:
        u,v,z=project(positions,view);rh,rw=view['depth'].shape;ph,pw=view['pixels'].shape[:2]
        reference=cv2.remap(view['depth'],(u*rw-.5).astype(np.float32),(v*rh-.5).astype(np.float32),cv2.INTER_NEAREST,borderMode=cv2.BORDER_CONSTANT)
        su,sv=u,v
        if view['offset'] is not None:
            fh,fw=view['offset'].shape[:2]
            shift=cv2.remap(view['offset'],(u*fw-.5).astype(np.float32),(v*fh-.5).astype(np.float32),cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE)
            su=u+shift[:,:,0];sv=v+shift[:,:,1]
        rgb=cv2.remap(view['pixels'],(su*pw-.5).astype(np.float32),(sv*ph-.5).astype(np.float32),cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE).astype(float)
        toward=view['origin']-positions;dist=np.linalg.norm(toward,axis=-1)
        facing=np.sum(toward*chart['normal'],axis=-1)/np.maximum(dist,.001)
        valid=(u>.003)&(u<.997)&(v>.003)&(v<.997)&(z>.1)&(facing>.15)&(np.abs(reference-z)<.14+.012*z)
        dh,dw=view['predicted'].shape
        observed=cv2.remap(view['predicted'],(su*dw-.5).astype(np.float32),(sv*dh-.5).astype(np.float32),cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
        # The edited photograph may have moved foreground objects. Blockout
        # visibility alone cannot stop their silhouettes being painted on walls.
        valid&=(observed>0)&(observed>z-(.35+.045*z))
        valid&=rgb.min(-1)<247
        green=(rgb[:,:,1]>rgb[:,:,0]*1.08)&(rgb[:,:,1]>rgb[:,:,2]*1.08)
        valid&=green if obj['name'].startswith('Topiary ') else ~green
        if obj['material'] in ['interior.wall','interior.carved_trim','interior.oak']:
            stone=cv2.remap(view['stone_mask'],(su*pw-.5).astype(np.float32),(sv*ph-.5).astype(np.float32),cv2.INTER_NEAREST,borderMode=cv2.BORDER_CONSTANT)
            valid&=stone>0
        if obj['name'].startswith('Vault panel'):
            ceiling=cv2.remap(view['vault_mask'],(su*pw-.5).astype(np.float32),(sv*ph-.5).astype(np.float32),cv2.INTER_NEAREST,borderMode=cv2.BORDER_CONSTANT)
            valid&=ceiling>0
        # Feather around visibility/mask boundaries, rather than hard per-triangle
        # switches between unrelated photos and generic materials.
        feather=np.ones(valid.shape) if valid.all() else np.clip(distance_transform_edt(np.pad(valid,8,mode='edge'))[8:-8,8:-8]/5,0,1)
        weight=np.where(valid,facing**2/np.maximum(dist,1)**2,0)*feather
        samples.append(rgb);weights.append(weight);feathers.append(feather)
    weights=np.asarray(weights);coverage=np.sum(weights,axis=0)>0
    dominant=int(np.argmax(weights.sum((1,2))))
    # Adjacent architectural pieces share source priority. Choosing independently
    # by triangle/chart score produces doubled ribs and discontinuous ornaments.
    order=[0,1,2,3,4,5,6,7,8,9]
    if obj['material'] in ['interior.paving','interior.compass']:order=[9]+order[:-1]
    color=base.copy();remaining=np.ones((h,w))
    accumulated=np.zeros_like(color)
    for vi in order:
        strength=feathers[vi]
        contribution=remaining*strength;accumulated+=samples[vi]*contribution[:,:,None];remaining*=1-strength
    color=accumulated+base*remaining[:,:,None]
    if polished and obj['material']=='interior.banner':
        # A unique reusable ornament sheet keeps the fleur-de-lis intact instead
        # of shearing it between mutually inconsistent perspective banners.
        color=base;coverage=np.zeros_like(mask)
    if polished and obj['material'] in ['interior.paving','interior.compass']:
        color=paving_color(positions);coverage=mask.copy()
    if polished and obj['name'].startswith('Vault panel'):
        # Generated perspectives disagree on the vault's motif phase and rib
        # position. The accepted ceiling reference is the authoring authority.
        # Map its unobscured left field continuously around each complete bay,
        # mirror across the ridge, and hide repeats under actual transverse ribs.
        # Excluding the central lamp and edge ribs prevents photographic props
        # from becoming part of the ceiling material; their meshes remain separate.
        ceiling=views[8]['pixels'];ch,cw=ceiling.shape[:2]
        cu=.035+.39*(1-np.clip(abs(positions[:,:,0])/4.5,0,1))
        cv=.23+.54*np.mod(positions[:,:,1],6)/6
        color=cv2.remap(ceiling,(cu*cw-.5).astype(np.float32),(cv*ch-.5).astype(np.float32),cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE).astype(float)
        coverage=mask.copy()
    # Extend island edge colours into the gutter; UVs never sample an empty region.
    if mask.any():
        nearest=distance_transform_edt(~mask,return_distances=False,return_indices=True);color[~mask]=color[nearest[0][~mask],nearest[1][~mask]]
    pixels=np.clip(color,0,255).astype(np.uint8)
    atlas[y-pad:y+h+pad,x-pad:x+w+pad,:3]=np.pad(pixels,((pad,pad),(pad,pad),(0,0)),mode='edge')
    override='Accepted ceiling reference mapped continuously per bay' if polished and obj['name'].startswith('Vault panel') else 'Continuous 1.2 m world-space paving and single compass from accepted floor reference' if polished and obj['material'] in ['interior.paving','interior.compass'] else None
    chart_reports.append({'object':obj['name'],'faces':len(chart['faces']),'pixels':[w,h],'photographic_fraction':float((coverage&mask).sum()/max(1,mask.sum())),'highest_scoring_view':cameras[dominant]['id'],'authoring_override':override})
    photo_texels+=int((coverage&mask).sum());surface_texels+=int(mask.sum())
    if ci%150==0:print('Baked',ci,'/',len(charts),flush=True)

vertices=[];assignments=[]
for item in objects:
    obj=item['record'];tri=item['tri'];uv=item['uv'];material=materials[obj['material']];category=0 if obj['material'].startswith('exterior') else 1
    sx,sy,sw,sh=locations[len(charts)+category];col,row=material['cell'];final_uv=uv.copy()
    final_uv[:,:,0]=(sx+col*sw/4+2+uv[:,:,0]*(sw/4-4))/width;final_uv[:,:,1]=(sy+row*sh/2+2+(1-uv[:,:,1])*(sh/2-4))/height
    shade=np.clip(.76+.22*(item['normal']@np.array([-.351,-.281,.895])),.52,1);colors=shade[:,None]*np.asarray(obj['color'])
    for ci in item['charts']:
        chart=charts[ci];faces=chart['faces'];x,y,w,h=locations[ci]
        local=(tri[faces][:,:,chart['axes']]-chart['lo'])/chart['extent']
        final_uv[faces,:,0]=(x+.5+local[:,:,0]*(w-1))/width;final_uv[faces,:,1]=(y+.5+local[:,:,1]*(h-1))/height;colors[faces]=1
    if polished and obj['name'].startswith('Lantern'):
        wx,wy,ww,wh=locations[len(charts)+2];final_uv[:]=[(wx+ww/2)/width,(wy+wh/2)/height]
        name=obj['name'].lower()
        if 'glass' in name:colors[:]=[1,.92,.68]
        elif 'core' in name:colors[:]=[1,.78,.32]
        elif 'recessed' in name:colors[:]=np.array([.30,.16,.045])*shade[:,None]
        else:colors[:]=np.array([.95,.66,.24])*shade[:,None]
    vertices.append(np.concatenate([tri[:,:,[0,2,1]],final_uv,np.repeat(colors[:,None,:],3,axis=1)],-1).reshape(-1,8))
    assignments.append({'name':obj['name'],'triangle_uvs':final_uv.tolist(),'colors':colors.tolist()})
mesh=np.concatenate(vertices).astype('<f4');assert len(mesh)<=1500000
with (OUT/(prefix+'.s2scene')).open('wb') as f:
    f.write(b'S2GS0003');f.write(struct.pack('<IIII',0,len(mesh),width,height));f.write(mesh.tobytes());f.write(atlas.tobytes())
(OUT/(prefix+'.s2world')).write_text((OUT/'hall-refined.s2world').read_text().replace('hall-refined.s2scene',prefix+'.s2scene'))
Image.fromarray(atlas).save(OUT/(prefix+'-atlas.png'))
(OUT/(prefix+'-uv.json')).write_text(json.dumps(assignments,separators=(',',':')))
report={'status':'Per-surface appearance bake; geometry still requires detail matching','atlas_dimensions':[width,height],'charts':len(charts),'density_scale':scale,'photographic_fraction_of_baked_texels':None if floor_only else photo_texels/max(surface_texels,1),'partial_floor_rebake':floor_only,'objects':chart_reports}
(OUT/(prefix+'.json')).write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='objects'}),flush=True)

"""Append a Blender-authored Mesa portal to original Summer Forest assets.

Existing terrain records, geometry, collision, portal records, actors and model
banks are retained. New frame regions, closed-frame collision, a special portal
surface and a name controller use the original format/consumers.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
import s2scene
import s2scene_build
import s2regions
import s2collision
import mesa_visibility
from mesa_ps1 import pack_vertex,pack_tri,rgbx
from spyrodisc import wad_index

ROOT=Path(__file__).resolve().parents[2]
def sha(b):return hashlib.sha256(b).hexdigest()
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def put(b,o,n):struct.pack_into('<I',b,o,n)


def visible_append(part,indices):
    old=bytes(part);out=bytearray(part);size,mask=part[4:6]
    added=bytes([255,*indices]);assert size+len(added)<=255
    end=4+size;out[end:end]=added;out[4]=size+len(added)
    if mask:struct.pack_into('<H',out,6,struct.unpack_from('<H',old,6)[0]+len(added))
    out+=bytes((-len(out))%4);put(out,0,len(out))
    rng=np.random.default_rng(20261105)
    for pos in rng.integers(0,256,(4096,3)):
        before,status=mesa_visibility.decode(old,pos);after,other=mesa_visibility.decode(out,pos)
        assert status==other and all(after[i]==before[i] for i in range(160))
        assert all(after[i]==255 for i in indices)
    return bytes(out)


def regions(faces,first_index,atlas):
    records=[]
    for begin in range(0,len(faces),10):
        batch=faces[begin:begin+10];vertices=[];index={};A=[];B=[];near=[];far=[]
        for f in batch:
            p=np.asarray(f['vertices']);p=np.round(p/[16,16,32])*[16,16,32]
            ids=[]
            for q in p:
                key=tuple(int(x) for x in q)
                if key not in index:index[key]=len(vertices);vertices.append(key)
                ids.append(index[key])
            c=len(A);a=np.asarray(f['rgb']);m=f['material'];assert m==53
            tile=atlas[m//16*64:m//16*64+64,m%16*64:m%16*64+64,:3]
            b=np.mean(tile,axis=(0,1))*a/128;A.append(a);B.append(b)
            if len(ids)==4:
                v3,v2,v0,v1=ids[0],ids[1],ids[3],ids[2]
                near.append(bytes([v0,v1,v2,v3,c,c,c,c])+struct.pack('<II',0,m))
                strip=(v3,v2,v0,v1)
            else:
                v3,v2,v1=ids
                near.append(bytes([v1,v1,v2,v3,0,c,c,c])+struct.pack('<II',0,m))
                strip=(v3,v2,v1,v1)
            # Original far format uses7-bit indices in bitfields, not the
            # near format's individual index bytes (original80028be4).
            va,vb,vc,vd=strip
            far.append(struct.pack('<II',(va<<24)|(vb<<17)|(vc<<10)|(vd<<3),
                                   (c<<25)|(c<<18)|(c<<11)|(c<<4)))
        p=np.asarray(vertices);origin=np.floor(p.min(0)/[16,16,32])*[16,16,32]
        packed=b''.join(struct.pack('<I',pack_vertex(q,origin)) for q in p)
        centre=(p.min(0)+p.max(0))/2;radius=np.linalg.norm(p-centre,axis=1).max()+32
        count=len(batch);nv=len(vertices);nc=len(A);assert nv+nc+2*count<=255
        header=struct.pack('<8H',int(centre[1]/16),int(centre[0]/16),int(radius/16)+1,int(centre[2]/16),
                           int(origin[1]/16),int(origin[0]/16),first_index+begin,int(origin[2]/16))
        header+=bytes([nv,nc,count,0,nv,nc,count,nv+nc+2*count])+struct.pack('<I',0xffffffff)
        colors=b''.join(rgbx(a) for a in A)+b''.join(rgbx(b) for b in B)
        records.append(s2regions.Record(header,packed,b''.join(rgbx(b) for b in B),b''.join(far),packed,colors,b''.join(near)))
        for face in far:
            vw,cw=struct.unpack('<II',face)
            assert all(((vw>>shift)&127)<nv for shift in (24,17,10,3))
            assert all(((cw>>shift)&127)<nc for shift in (25,18,11,4))
    return records


def label(gameplay,centre):
    g=bytearray(gameplay);at=0x2c
    for _ in range(8):at+=u32(g,at)
    size,count=struct.unpack_from('<II',g,at);end=at+size;rec=lambda i:at+8+88*i
    labels=[i for i in range(count) if struct.unpack_from('<H',g,rec(i)+54)[0]==0x40a]
    assert labels;original=bytes(g[rec(labels[0]):rec(labels[0])+88])
    assert not any(g[rec(count):rec(count+1)]) and end-rec(count+1)>=32*112
    psize=u32(g,end);insert=end+psize;assert struct.unpack_from('<II',g,insert)==(8,0)
    reloc=insert+8;nr=u32(g,reloc);refs=list(struct.unpack_from('<'+'I'*nr,g,reloc+4))
    assert not any(g[reloc+4+nr*4:])
    g[insert:insert]=bytes(8);put(g,end,psize+8)
    newrefs=[]
    for field in refs:
        moved=field+8 if field>=insert else field;value=u32(g,moved)
        if value>=insert:put(g,moved,value+8)
        newrefs.append(moved)
    record=bytearray(original);put(record,0,insert)
    struct.pack_into('<3i',record,12,centre[0]-64,centre[1],centre[2]+768)
    record[72]=0;record[50:52]=bytes(2);record[63]=0;record[62]=0
    record[68:71]=bytes(3)
    g[rec(count):rec(count+1)]=record;struct.pack_into('<II',g,insert,29,1);newrefs.append(rec(count))
    put(g,at+4,count+1);reloc+=8;put(g,reloc,len(newrefs))
    needed=reloc+4+len(newrefs)*4
    if len(g)<needed:g.extend(bytes(needed-len(g)))
    struct.pack_into('<'+'I'*len(newrefs),g,reloc+4,*newrefs)
    return bytes(g),dict(index=count,old_count=count,count=count+1,parameter=insert,source=labels[0],remaining_dynamic_pool=end-rec(count+1))


def panorama_bytes(art,original_sky):
    assert sha(Path(art['blend']).read_bytes())==art['blend_sha256']
    assert sha(Path(art['source_archive']).read_bytes())==art['source_sha256']
    flat=art.get('face_shading')=='flat'
    # Original Summer pictures reach381 vertices/677 faces; Autumn reaches
    # 440 vertices/767 faces. Keep within those actual original consumers.
    assert len(art['vertices'])<=440 and len(art['faces'])<=767
    if not flat:assert len(art['vertices'])==len(art['vertex_rgb'])
    background=bytearray(original_sky);assert len(background)==60
    assert struct.unpack_from('<6I',background)==(4,24,1,40,2,44)
    rgb=art['sky_rgb'];put(background,40,rgb[0]|rgb[1]<<8|rgb[2]<<16)
    vertices=[]
    for x,y in art['vertices']:
        assert 0<=x<=512 and 12<=y<=228
        vertices.append(struct.pack('<I',(((y<<16)|x)<<5)&0xffffffff))
    rgb_values=sorted({tuple(f['rgb']) for f in art['faces']}) if flat else art['vertex_rgb']
    assert 0<len(rgb_values)<=512,'original fog palette buffer is512 words before80068bf0'
    colours=[struct.pack('<I',r|(g<<8)|(b<<16)) for r,g,b in rgb_values]
    colour_index={tuple(rgb):i for i,rgb in enumerate(rgb_values)}
    faces=[]
    for face in art['faces']:
        va,vb,vc=face['vertices'];assert min(va,vb,vc)>=0 and max(va,vb,vc)<len(vertices)
        indices=va<<20|vb<<10|vc
        palette=0x80000000|(colour_index[tuple(face['rgb'])]<<20) if flat else indices
        faces.append(struct.pack('<II',indices,palette))
    header=struct.pack('<6I',len(vertices),24,len(colours),24+len(vertices)*4,len(faces),24+(len(vertices)+len(colours))*4)
    return bytes(background)+header+b''.join(vertices+colours+faces)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('mesh',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--panorama',type=Path)
    a=ap.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
    author=json.loads(a.mesh.read_text());assert sha(Path(author['blend']).read_bytes())==author['blend_sha256']
    sc=s2scene.read_scene(ROOT/'data/disc',15);old=sc.archive;ch=wad_index(old[:65536],len(old))
    scene=s2scene_build.split(sc.scene);mesh=s2regions.decode(scene['parts'][1]);assert len(mesh['records'])==160
    first=1+sum(r.header[22] for r in mesh['records']);assert first+len(author['faces'])<8192
    added=regions(author['faces'],first,s2scene.atlas(sc));ids=list(range(160,160+len(added)))
    assert len(mesh['records'])+len(added)<255
    scene['parts'][1]=s2regions.encode(mesh['moving'],mesh['records']+added,mesh['order']+ids,mesh['head_gap'])
    assert s2regions.decode(scene['parts'][1])['records'][:160]==mesh['records']
    scene['parts'][2]=visible_append(scene['parts'][2],ids)
    portal=bytearray(scene['tail']);psize,count=struct.unpack_from('<II',portal,0)
    assert count==7;insert=8+count*68
    new=bytearray(68)
    for i,p in enumerate(author['aperture']):struct.pack_into('<3i',new,i*12,*p)
    # Retain the original Glimmer panorama provisionally; final Mesa-specific
    # portal artwork has its own completion gate, never silently represented.
    picture=u32(portal,8+60)+68;put(new,60,picture);new[64:68]=bytes([44,1,14,0])
    for i in range(count):put(portal,8+i*68+60,u32(portal,8+i*68+60)+68)
    portal[insert:insert]=new;put(portal,0,psize+68);put(portal,4,count+1)
    art_proof=None
    if a.panorama:
        art=json.loads(a.panorama.read_text());picture_data=panorama_bytes(art,portal[picture:picture+60])
        picture_at=psize+68;portal[picture_at:picture_at]=picture_data
        put(portal,0,picture_at+len(picture_data));put(portal,insert+60,picture_at)
        art_proof=dict(path=str(a.panorama.resolve()),sha256=sha(a.panorama.read_bytes()),picture_sha256=sha(picture_data),
                       vertices=len(art['vertices']),faces=len(art['faces']),source_archive_sha256=art['source_sha256'],blend_sha256=art['blend_sha256'])
    scene['tail']=bytes(portal)
    surfaces=bytearray(scene['parts'][4]);ns=u32(surfaces,4);assert ns==9
    payload=bytes(surfaces[8+ns*4:]);offsets=[u32(surfaces,8+4*i)+4 for i in range(ns)]
    part=bytearray(struct.pack('<II',len(surfaces)+12,ns+1))
    part+=b''.join(struct.pack('<I',offset) for offset in offsets)+struct.pack('<I',len(surfaces))
    part+=payload+struct.pack('<II',6,count);assert len(part)==len(surfaces)+12
    scene['parts'][4]=bytes(part)
    collision=s2collision.decode(scene['collision']);special=len(collision.flags)
    points=np.round(np.asarray(author['aperture'])/16).astype(int)
    plane=b''.join(pack_tri(points[[0,i,i+1]]) for i in range(1,4))
    solid=[]
    for face in author['faces']:
        p=np.round(np.asarray(face['vertices'])/16).astype(int)
        for i in range(1,len(p)-1):solid.append(pack_tri(p[[0,i+1,i]]))
    tri=collision.tri[:special*12]+plane+collision.tri[special*12:]+b''.join(solid)
    cells={k:[i if i<special else i+3 for i in v] for k,v in collision.cells.items()}
    extra_cells=s2collision.cells_conservative(plane+b''.join(solid))
    for cell,indices in extra_cells.items():
        mapped=[special+i if i<3 else collision.count+i for i in indices]
        cells.setdefault(cell,[]).extend(mapped)
    flags2=collision.flags2
    if len(flags2)>special:flags2=flags2[:special]+bytes(3)+flags2[special:]
    scene['collision']=s2collision.encode(tri,collision.flags+bytes([ns])*3,flags2,cells,collision.extra)
    after=s2collision.decode(scene['collision'])
    assert after.tri[:special*12]+after.tri[(special+3)*12:(collision.count+3)*12]==collision.tri
    gameplay=old[ch[3].offset:ch[3].offset+ch[3].size];gameplay,label_proof=label(gameplay,author['centre'])
    children=[old[ch[0].offset:ch[0].offset+ch[0].size],s2scene_build.join(scene),old[ch[2].offset:ch[2].offset+ch[2].size],gameplay]
    output=bytearray(old[:ch[0].offset]);offsets=[]
    for i,data in enumerate(children):
        output+=bytes((-len(output))%2048);offsets.append(len(output));struct.pack_into('<II',output,8*i,len(output),(len(data)+2047)&~2047)
        output+=data;output+=bytes((-len(output))%2048)
    for at in range(64,0x140,4):
        ptr=u32(old,at)
        if not ptr:break
        assert ch[2].offset<=ptr<=ch[2].offset+ch[2].size
        put(output,at,ptr+offsets[2]-ch[2].offset)
    path=a.out/'slot15-summer-with-mesa-portal.bin';path.write_bytes(output)
    report=dict(archive=str(path.resolve()),archive_sha256=sha(output),original_archive_sha256=sha(old),
                mesh=str(a.mesh.resolve()),mesh_sha256=sha(a.mesh.read_bytes()),blend_sha256=author['blend_sha256'],
                added_regions=ids,added_near_faces=len(author['faces']),old_near_faces=first-1,
                original_terrain_records_exact=True,original_collision_triangles_exact=True,
                appended_collision_triangles=3+len(solid),portal_index=count,scene_id=14,aperture=author['aperture'],
                label=label_proof,visibility_old_regions_exact_4096_samples=True,release_eligible=False,
                panorama=art_proof,
                limits=['New Mesa panorama derived from actual terrain; original PS1 clipping/frame review required.' if art_proof else 'New portal panorama currently reuses original Glimmer artwork; Mesa artwork pending.',
                        'Placement/support/normal-flow original PS1 travel and return checks required.',
                        'Native extra-scene integration remains lead-owned.'])
    (a.out/'portal-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('SUMMER PORTAL ARCHIVE',path.resolve(),'sha256',sha(output),flush=True)
    print('SUMMER PORTAL STRUCTURE PASS',sha(output),'160 original regions exact;',len(added),'new frame regions; original collision triangles exact; original portal/actor indices retained; original bank/VRAM bytes retained',flush=True)
    print('LIMIT',json.dumps(report['limits']),flush=True)


if __name__=='__main__':main()

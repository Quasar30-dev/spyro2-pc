"""Hash-bound source mapping, original UV consumer and complete texel checks.

Runs the actual resident 8002654c..800265a4 instruction slice with branch/load
delays, without calling the packer's orientation or image sampling functions.
This is an offline consumer test, not a complete engine/emulator execution.
"""
import argparse
import hashlib
import json
import struct
from collections import Counter
from collections import defaultdict
from pathlib import Path

import numpy as np

import mesa_ps1 as mesa
import mesa_vram
import mipsdis
from mesa_oracle_check import near_polygons
from spyrodisc import wad_index
import s2regions


def consumer(descriptor):
    base,code = mipsdis.exe_image(); r = [0]*32; r[9],r[10] = struct.unpack('<II',descriptor)
    r[30] = 0x80100000; mem = {}; pc,npc = 0x8002654c,0x80026550; delayed = None
    signed = lambda v:v if v<0x80000000 else v-0x100000000
    def read(a):
        return struct.unpack_from('<I',code,a-base)[0]
    for step in range(100):
        if pc==0x800265a8:
            return [(mem[0x80100000+p]&255,(mem[0x80100000+p]>>8)&255) for p in (12,24,36,48)]
        w = read(pc); op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sh=(w>>6)&31
        imm=w&65535; si=imm if imm<32768 else imm-65536; following=npc+4; writes={}; pending=None
        if op==0:
            fn=w&63
            if fn==0: writes[rd]=r[rt]<<sh
            elif fn==2: writes[rd]=r[rt]>>sh
            elif fn==3: writes[rd]=signed(r[rt])>>sh
            elif fn in (32,33): writes[rd]=r[rs]+r[rt]
            else: raise AssertionError((hex(pc),hex(w)))
        elif op in (8,9): writes[rt]=r[rs]+si
        elif op==12: writes[rt]=r[rs]&imm
        elif op==15: writes[rt]=imm<<16
        elif op==4:
            if r[rs]==r[rt]: following=pc+4+(si<<2)
        elif op==35: pending=(rt,read((r[rs]+si)&0xffffffff))
        elif op==43: mem[(r[rs]+si)&0xffffffff]=r[rt]
        else: raise AssertionError((hex(pc),hex(w)))
        if delayed and delayed[0] not in writes:r[delayed[0]]=delayed[1]
        for k,v in writes.items():r[k]=v&0xffffffff
        r[0]=0; delayed=pending; pc,npc=npc,following&0xffffffff
    raise AssertionError('original UV consumer instruction bound')


def decode(d,vram):
    corners = np.array(consumer(d),float)
    s,t = np.meshgrid(np.arange(32)/31,np.arange(32)/31)
    uv = corners[0]+s[...,None]*(corners[1]-corners[0])+t[...,None]*(corners[2]-corners[0])
    assert np.allclose(corners[3],corners[1]+corners[2]-corners[0])
    u,v = np.round(uv[...,0]).astype(int),np.round(uv[...,1]).astype(int)
    ppw = 2 if d[6]&128 else 4
    x = (d[6]&15)*64+u//ppw-512; y=((d[6]>>4)&1)*256+v
    words = vram[y,x].astype(np.uint32)
    index = (words>>((u%ppw)*(8 if ppw==2 else 4)))&((1<<(8 if ppw==2 else 4))-1)
    clut = int.from_bytes(d[2:4],'little')
    return vram[(clut>>6)&511,(clut&63)*16-512+index]


def image_expected(image,uv):
    # Independently interpolate the authored 4-corner UV map at every texel.
    result = np.empty((64,64),np.uint16)
    original = mesa.to15(image)
    for y in range(64):
        for x in range(64):
            s,t=x/63,y/63
            if s+t<=1: coord=(1-s-t)*uv[0]+s*uv[1]+t*uv[2]
            else: coord=(1-t)*uv[1]+(1-s)*uv[2]+(s+t-1)*uv[3]
            u,v=np.clip(coord,0,1)
            result[y,x]=original[int(round(v*(original.shape[0]-1))),int(round(u*(original.shape[1]-1)))]
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive',type=Path); ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--colour-reference',type=Path)
    a=ap.parse_args(); arc=a.archive.read_bytes(); digest=hashlib.sha256(arc).hexdigest()
    report=json.loads(a.archive.with_name('mesa-ps1-report.json').read_text()); proof=report['uv_polish']
    assert report['archive_sha256']==digest
    print('ARTIFACT',a.archive.resolve(),'sha256',digest,flush=True)
    print('ORACLE',mipsdis.EXE,'sha256',hashlib.sha256(mipsdis.EXE.read_bytes()).hexdigest(),flush=True)
    baseline=a.baseline.read_bytes()
    print('BASELINE',a.baseline.resolve(),'sha256',hashlib.sha256(baseline).hexdigest(),flush=True)
    assert near_polygons(arc)==near_polygons(baseline)
    print('CHECK PASS near positions/winding/polygon multiplicities equal test05',flush=True)
    if a.colour_reference:
        def attributes(data):
            children=wad_index(data[:65536],len(data))
            scene=mesa.s2scene_build.split(data[children[1].offset:children[1].offset+children[1].size])
            near=Counter(); far=Counter()
            for reg in s2regions.decode(scene['parts'][1])['records']:
                y,x,_,z=struct.unpack_from('<4H',reg.header,8);origin=(x*16,y*16,z*16)
                def positions(buf):
                    return [(origin[0]+(w>>21)*16,origin[1]+((w>>10)&2047)*16,origin[2]+(w&1023)*32)
                            for (w,) in struct.iter_unpack('<I',buf)]
                hp=positions(reg.hp_verts);lp=positions(reg.lp_verts);n=reg.header[21]
                for k in range(reg.header[22]):
                    f=reg.hp_faces[k*16:k*16+16];tri=f[0]==f[1];order=(3,2,1) if tri else (3,2,1,0)
                    loop=tuple((hp[f[i]],reg.hp_colors[f[i+4]*4:f[i+4]*4+4],
                                reg.hp_colors[(n+f[i+4])*4:(n+f[i+4])*4+4]) for i in order)
                    loop=min(loop[j:]+loop[:j] for j in range(len(loop)))
                    near[(f[12]&127,(f[13]>>4)&3 if tri else 0,loop)]+=1
                for k in range(reg.header[18]):
                    w0,w1=struct.unpack_from('<II',reg.lp_faces,k*8)
                    vi=[(w0>>s)&127 for s in (24,17,10,3)];ci=[(w1>>s)&127 for s in (25,18,11,4)]
                    loop=tuple((lp[v],reg.lp_colors[c*4:c*4+4]) for v,c in zip(vi,ci))
                    loop=min(loop[j:]+loop[:j] for j in range(len(loop)))
                    far[loop]+=1
            return near,far
        reference=a.colour_reference.read_bytes()
        print('COLOUR REFERENCE',a.colour_reference.resolve(),'sha256',hashlib.sha256(reference).hexdigest(),flush=True)
        actual_near,actual_far=attributes(arc);expected_near,expected_far=attributes(reference)
        assert actual_near==expected_near and actual_far==expected_far
        print('CHECK PASS compaction preserves every near position/winding/material/UV rotation/A/B byte and far position/winding/colour byte',
              sum(actual_near.values()),sum(actual_far.values()),flush=True)
    original,own,terrain,safe=mesa_vram.inventory()
    ov=np.frombuffer(original,'<u2',512*512,struct.unpack_from('<I',original)[0]).reshape(512,512)
    vram=np.frombuffer(arc,'<u2',512*512,struct.unpack_from('<I',arc)[0]).reshape(512,512)
    assert not np.any((vram!=ov)&~safe)
    print('CHECK PASS every changed VRAM word lies in conservative terrain allocation; known original model/Spyro/font pages excluded',flush=True)
    print('LIMITATION full symbolic inventory of global/overlay VRAM consumers remains incomplete; unclassified non-terrain reserved',flush=True)
    ch=wad_index(arc[:65536],len(arc)); sc=mesa.s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size])
    desc=sc['parts'][0][8:]; total=0; se=0.; mx=0; transparent=0; raw_modes=Counter()
    for mapping in proof['mappings']:
        m=mapping['material']; key=mapping['key']; key=tuple(key) if isinstance(key,list) else key
        image=mesa.tile_rgba(key)
        assert hashlib.sha256(image.tobytes()).hexdigest()==proof['source_textures'][str(key)]
        expected=image_expected(image,np.asarray(mapping['uv_corners']))
        actual=np.zeros((64,64),np.uint16)
        for q in range(4):
            d=desc[m*48+16+q*8:m*48+24+q*8]
            assert d[4]-d[0]==31 and d[5]-d[1]==31
            raw_modes[(bool(d[6]&128),d[7])]+=1
            actual[q//2*32:q//2*32+32,q%2*32:q%2*32+32]=decode(d,vram)
        transparent+=int(np.count_nonzero((expected==0)!=(actual==0)))
        delta=np.stack([((actual>>k)&31).astype(float)-((expected>>k)&31) for k in (0,5,10)],-1)
        total+=delta.size; se+=float(np.square(delta).sum()); mx=max(mx,float(np.abs(delta).max()))
    assert transparent==0
    print('CHECK PASS all',len(proof['mappings'])*4096,'near texels covered through original UV instruction slice; transparency mismatches',transparent,flush=True)
    print('MEASUREMENT source colour RMS RGB5',np.sqrt(se/total),'maximum channel error',mx,'descriptor modes',dict(raw_modes),flush=True)
    assert np.sqrt(se/total)<1.5
    assert len(proof['faces'])==report['ps1_faces']
    top=[f for f in proof['faces'] if f['kind']=='top']
    for f in proof['faces']:
        mapping=proof['mappings'][f['material']]
        uv=np.array(mapping['uv_corners'])[f['square_corners']]
        err=float(np.abs(uv-np.array(f['source_uv'])).max())
        assert abs(err-f['max_uv_error'])<1e-9
        if f['kind']=='top':assert err<=1e-6
    assert len(top)==2041
    print('CHECK PASS all',len(top),'top faces preserve authored source UV corners; continuous shared source coordinates are retained',flush=True)
    print('MEASUREMENT source exact faces',proof['source_exact_faces'],'/',len(proof['faces']),'; approximate',proof['source_approximated_faces'],flush=True)
    # Bind to original authoring data independently of the report's per-face UV
    # claims. Match decoded serialized vertex positions to source polygons, then
    # compare source UVs in the actual binary strip order and rotation.
    source=mesa.MB.build_model(mesa.PROJECT/'data/soi/levels/06-market-mesa/level.json',stone_polish=bool(report.get('stone_polish')))
    faces,_=mesa.ps1_faces(source); P=np.asarray(source['P'])
    used=sorted({v for f in faces for v in f['vs']}); frame=mesa.Frame(P[used]); R=frame(P)
    R[:,:2]=np.round(R[:,:2]/16)*16;R[:,2]=np.round(R[:,2]/32)*32
    source_polys=defaultdict(list)
    for f in faces:
        positions=[tuple(int(x) for x in p) for p in R[f['vs']]]
        source_polys[tuple(sorted(positions))].append((f,dict(zip(positions,f['uv']))))
    exact=0; near_count=0; actual_top=0; actual_top_errors=0; exact_panels=0; errors=[]; source_edges=defaultdict(list)
    for region in s2regions.decode(sc['parts'][1])['records']:
        y,x,_,z=struct.unpack_from('<4H',region.header,8); origin=(x*16,y*16,z*16)
        positions=[]
        for (w,) in struct.iter_unpack('<I',region.hp_verts):
            positions.append((origin[0]+(w>>21)*16,origin[1]+((w>>10)&2047)*16,origin[2]+(w&1023)*32))
        for offset in range(0,len(region.hp_faces),16):
            raw=region.hp_faces[offset:offset+16]; triangle=raw[0]==raw[1]
            inds=(3,2,1) if triangle else (3,2,0,1)
            ps=[positions[raw[k]] for k in inds]
            matches=source_polys[tuple(sorted(ps))]
            assert matches,'serialized face has no source polygon'
            material=raw[12]&127; mapping=proof['mappings'][material]
            key=mapping['key']; key=tuple(key) if isinstance(key,list) else key
            square=np.array([(0,0),(1,0),(0,1)] if triangle else [(0,0),(1,0),(0,1),(1,1)])
            if triangle:
                for _ in range((raw[13]>>4)&3):square=np.column_stack((1-square[:,1],square[:,0]))
            uv=np.array(mapping['uv_corners'])[square[:,0]+2*square[:,1]]
            candidates=[]
            for j,(f,p_uv) in enumerate(matches):
                if mesa.MB.MAT[f['mat']]!=key:continue
                expected_uv=np.array([p_uv[p] for p in ps]);delta=uv-expected_uv
                candidates.append((float(np.square(delta).sum()),j,f,expected_uv,delta))
            assert candidates,'serialized material uses a different source texture'
            _,j,f,expected_uv,delta=min(candidates,key=lambda x:x[0]);matches.pop(j)
            err=float(np.abs(delta).max());exact+=err<=1e-6;near_count+=1
            errors.extend(np.linalg.norm(delta,axis=1))
            if f.get('uv_exact',False):
                assert err<=1e-6,'serialized recognizable wall panel was approximated'
                exact_panels+=1
            if f['kind']=='top':
                actual_top+=1;actual_top_errors+=err>1e-6
                outline=(0,1,2) if triangle else (0,1,3,2)
                for j,i in enumerate(outline):
                    k=outline[(j+1)%len(outline)];p,q=ps[i],ps[k]
                    auv,buv=uv[i],uv[k];suv,tuv=expected_uv[i],expected_uv[k]
                    if p>q:p,q=q,p;auv,buv=buv,auv;suv,tuv=tuv,suv
                    source_edges[(p,q,str(key))].append((auv,buv,suv,tuv))
    assert near_count==len(faces) and all(not v for v in source_polys.values())
    assert actual_top==2041 and actual_top_errors==0
    assert exact==proof['source_exact_faces']
    joins=0;join_errors=0
    for records in source_edges.values():
        if len(records)!=2:continue
        a,b=records
        # Only joins continuous in the source are claimed; a source chart seam
        # is recorded separately and never relabelled as a newly proven join.
        if np.allclose(a[2:],b[2:],atol=1e-6):
            joins+=1;join_errors+=not np.allclose(a[:2],b[:2],atol=1e-6)
    assert not join_errors
    print('CHECK PASS independently serialized source polygon/texture/UV binding',near_count,'faces; exact',exact,'top exact',actual_top,flush=True)
    if report.get('stone_polish'):
        assert exact_panels>0
        print('CHECK PASS recognizable wall-panel source UVs exact in serialized faces',exact_panels,flush=True)
    print('CHECK PASS source-continuous top edge joins',joins,'new UV discontinuities',join_errors,flush=True)
    points=np.array(sorted({p for p,q,key in source_edges}|{q for p,q,key in source_edges}),float)
    atomic=defaultdict(list);split_edges=0
    for (p,q,key),records in source_edges.items():
        p0,p1=np.array(p,float),np.array(q,float);direction=p1-p0;den=float(direction@direction)
        assert den>0
        relative=points-p0;fractions=relative@direction/den
        cross=np.cross(relative,direction)
        inside=(fractions>1e-9)&(fractions<1-1e-9)&(np.square(cross).sum(1)<1e-8)
        cuts=[(0.,p)]+sorted((float(t),tuple(int(x) for x in pt)) for t,pt in zip(fractions[inside],points[inside]))+[(1.,q)]
        split_edges+=len(cuts)>2
        for (a,pa),(b,pb) in zip(cuts,cuts[1:]):
            for auv,buv,suv,tuv in records:
                atomic[(pa,pb,key)].append((auv+(buv-auv)*a,auv+(buv-auv)*b,
                                            suv+(tuv-suv)*a,suv+(tuv-suv)*b))
    linear_joins=0;wrapped_joins=0;errors=0
    for records in atomic.values():
        if len(records)!=2:continue
        a,b=records;source_delta=np.array(a[2:])-np.array(b[2:])
        continuous=np.allclose(source_delta,0,atol=1e-6)
        wrapped=(np.allclose(source_delta,np.round(source_delta),atol=1e-6) and
                 np.allclose(source_delta[0],source_delta[1],atol=1e-6))
        if continuous or wrapped:
            linear_joins+=1;wrapped_joins+=not continuous
            errors+=not np.allclose(np.array(a[:2])-np.array(b[:2]),source_delta,atol=1e-6)
    assert errors==0
    print('CHECK PASS top T-junction/periodic source coordinate audit; split edges',split_edges,
          'continuous atomic joins',linear_joins,'including repeat boundaries',wrapped_joins,'new errors',errors,flush=True)
    print('LIMITATION UV continuity is measured; per-pixel colour quantization and user visual/gameplay acceptance remain distinct',flush=True)
    print('UV EXPERIMENT CHECK PASS',digest,'automated evidence only; remaining limitations above',flush=True)


if __name__=='__main__':main()

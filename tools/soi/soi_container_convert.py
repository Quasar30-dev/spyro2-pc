"""Use common C9 attack filtering for SoI's five charge-only type136 boxes.

Append records/independent parameters, keeping all original indices/types/data
pointers. Add a bounded Glimmer overlay extension for C9 on the PS1; PC's
existing common C9 consumer supplies the same normal-flame/charge distinction.
Original basket art is retained. No native executable is changed.
"""
import argparse
import json
from pathlib import Path
import struct

import soi_model_convert as fmt

BASE=0x8006d264


def overlay_extension(original):
    assert fmt.u32(original,0x80070540-BASE)==0x240200c8
    assert fmt.u32(original,0x80070544-BASE)==0x10620ceb
    assert fmt.u32(original,0x80070548-BASE)==0
    assert fmt.u32(original,0x8007054c-BASE)==0x0801f86b
    code=bytearray(original);code.extend(bytes((-len(code))%16));entry=BASE+len(code)
    j=lambda address:(2<<26)|((address>>2)&0x3ffffff)
    branch=lambda pc,rs,rt,dest:(4<<26)|(rs<<21)|(rt<<16)|(((dest-pc-4)//4)&65535)
    words=[0x240800c8,branch(entry+4,3,8,0x800738f4),0,
           0x8e880018,0x3c090016,0x01094024,
           branch(entry+24,8,0,entry+40),0,j(0x800738f4),0,
           0xae800018,j(0x8007e1ac),0]
    code.extend(struct.pack('<'+'I'*len(words),*words))
    patch={0x80070540:0x3062fffe,0x80070544:0x240e00c8,
           0x80070548:branch(0x80070548,2,14,entry),0x8007054c:0,0x80070550:j(0x8007e1ac)}
    for address,value in patch.items():fmt.put32(code,address-BASE,value)
    return bytes(code),dict(base=BASE,entry=entry,original_sha256=fmt.sha(original),sha256=fmt.sha(code),
         patches={hex(k):hex(v) for k,v in patch.items()},words=[hex(x) for x in words],
         gate='C8 unchanged; C9 accepts attack mask 0x160000; rejects normal flame 0x10000',
         continuation=hex(0x800738f4),reject=hex(0x8007e1ac))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('conversion',type=Path);ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();out=a.out.resolve();assert not out.exists()
    conversion=json.loads(a.conversion.read_text());old=Path(conversion['archive']).read_bytes()
    build=json.loads(a.conversion.with_name('mesa-ps1-report.json').read_text())
    ch=fmt.wad_index(old[:65536],len(old));g=bytearray(old[ch[3].offset:ch[3].offset+ch[3].size]);table=0x327c
    size,count=struct.unpack_from('<II',g,table);rec=lambda i:table+8+88*i
    selected=[x for x in build['container_records'] if x['source_type']==136];assert len(selected)==5
    pool,end=rec(count),table+size
    assert not any(g[pool:pool+88*5]) and end-rec(count+5)>=112*32
    psize=fmt.u32(g,end);insert=end+psize;assert struct.unpack_from('<II',g,insert)==(8,0)
    rel=insert+8;nr=fmt.u32(g,rel);refs=list(struct.unpack_from('<'+'I'*nr,g,rel+4))
    assert not any(g[rel+4+nr*4:])
    # The original Summer Forest common C9 wobble table is used byte for byte.
    retail=fmt.read_archive(fmt.ROOT/'data/disc',15);rch=fmt.wad_index(retail[:65536],len(retail))
    rg=retail[rch[3].offset:rch[3].offset+rch[3].size];at=0x2c
    for _ in range(8):at+=fmt.u32(rg,at)
    source=next(at+8+88*i for i in range(fmt.u32(rg,at+4)) if struct.unpack_from('<H',rg,at+8+88*i+54)[0]==0xc9)
    q=fmt.u32(rg,source);curve_at=fmt.u32(rg,q+24);curve=rg[curve_at:curve_at+64];assert len(curve)==64
    added=64+28*5;g[insert:insert]=curve+bytes(28*5);fmt.put32(g,end,psize+added)
    newrefs=[]
    for field in refs:
        shifted=field+added if field>=insert else field;value=fmt.u32(g,shifted)
        if value>=insert:fmt.put32(g,shifted,value+added)
        newrefs.append(shifted)
    changes=[]
    for j,e in enumerate(selected):
        old_index=e['index'];oldp=rec(old_index);newp=rec(count+j);param=insert+64+28*j
        assert struct.unpack_from('<H',g,oldp+54)[0]==0xc8 and g[oldp+72]==0
        g[newp:newp+88]=g[oldp:oldp+88];g[oldp+72]=0xfd
        struct.pack_into('<H',g,newp+54,0xc9);fmt.put32(g,newp,param);fmt.put32(g,param+24,insert)
        newrefs.extend((newp,param+24));changes.append(dict(source_index=e['source_index'],old_index=old_index,index=count+j,
              original_state=0,new_class=0xc9,position=e['position'],parameter=param,curve=insert))
        e.update(previous_index=old_index,index=count+j,target_class=0xc9,mechanism='Common C9 charge filter with basket bank; PS1 bounded overlay extension',
                 difference='Basket art retained; C9 also accepts powered attack bits absent in SoI')
    fmt.put32(g,table+4,count+5);rel+=added;fmt.put32(g,rel,len(newrefs))
    struct.pack_into('<'+'I'*len(newrefs),g,rel+4,*newrefs)
    # Independent copies avoid relocating one static bank/pose twice.
    entries=[];banks={}
    for i in range(1,63):
        b,e=struct.unpack_from('<II',old,64+4*i)
        if not e:break
        cls=struct.unpack_from('<H',old,0x140+2*i)[0];entries.append((i,cls,b,e));banks[cls]=old[b:e]
    assert 0xc9 not in banks and 0x133 not in banks
    model=bytearray(old[ch[2].offset:entries[0][2]]);head=bytearray(old[:ch[0].offset]);relative={}
    for i,cls,b,e in entries:
        relative[i]=len(model);model.extend(banks[cls])
    for cls,source_class in ((0xc9,0xc8),(0x133,0x132)):
        i=len(relative)+1;relative[i]=len(model);struct.pack_into('<H',head,0x140+2*i,cls);model.extend(banks[source_class])
    relative[len(relative)+1]=len(model)
    children=[old[ch[0].offset:ch[0].offset+ch[0].size],old[ch[1].offset:ch[1].offset+ch[1].size],bytes(model),bytes(g)]
    output=bytearray(head);offsets=[]
    for i,data in enumerate(children):
        output.extend(bytes(fmt.align(len(output))-len(output)));offsets.append(len(output))
        struct.pack_into('<II',output,i*8,len(output),fmt.align(len(data)));output.extend(data);output.extend(bytes(fmt.align(len(output))-len(output)))
    fmt.put32(output,64,fmt.u32(old,64)-ch[2].offset+offsets[2])
    for i,offset in relative.items():fmt.put32(output,64+4*i,offsets[2]+offset)
    fmt.put32(output,64+4*(max(relative)+1),0)
    overlay,extension=overlay_extension(fmt.read_archive(fmt.ROOT/'data/disc',16))
    out.mkdir(parents=True);archive=out/'slot17-content.bin';archive.write_bytes(output);(out/'slot16-overlay.bin').write_bytes(overlay)
    proof=dict(base_archive=str(Path(conversion['archive']).resolve()),base_archive_sha256=fmt.sha(old),
         archive=str(archive),archive_sha256=fmt.sha(output),overlay=str(out/'slot16-overlay.bin'),overlay_sha256=fmt.sha(overlay),
         extension=extension,containers=changes,wobble_source_slot=15,wobble_source_offset=curve_at,wobble_sha256=fmt.sha(curve),
         original_count=count,count=count+5,retained_dynamic_pool_bytes=end-rec(count+5),release_eligible=False,
         limits=['Original/native acceptance and source-136 rejection/charge tests required.',
                 'Basket art retained. No Rhynoc AI, critter or Bianca story integration completed.'])
    (out/'container-conversion.json').write_text(json.dumps(proof,indent=2)+'\n')
    build.update(archive=str(archive),archive_sha256=fmt.sha(output),release_eligible=False,container_conversion=str(out/'container-conversion.json'))
    build['objects']['count']=count+5
    build['archive_layout'].update(offsets=offsets,sizes=[fmt.align(len(x)) for x in children],total=len(output),
         growth=fmt.align(len(children[1]))-505856,model_directory_entries_moved=max(relative)+1)
    (out/'mesa-ps1-report.json').write_text(json.dumps(build,indent=1))
    print('ARCHIVE',archive,'sha256',fmt.sha(output));print('OVERLAY',out/'slot16-overlay.bin','sha256',fmt.sha(overlay))
    print('APPENDED SOURCE CONTAINERS',json.dumps(changes));print('SOURCE WOBBLE',curve_at,'sha256',fmt.sha(curve))
    print('LIMIT',json.dumps(proof['limits']))


if __name__=='__main__':main()

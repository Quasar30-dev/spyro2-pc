"""Recover RAM from explicitly inactive, already-parked Glimmer-only actors.

Keep their class IDs, indices, positions and parameters. Use original 0xfd
removed state, and a small valid retail static bank solely for loader safety.
No visible SoI character uses this bank. Never apply to active/source actors.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import soi_model_convert as fmt


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('conversion',type=Path)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();out=a.out.resolve();assert not out.exists()
    old_report=json.loads(a.conversion.read_text());base=Path(old_report['archive']).read_bytes()
    build=json.loads(a.conversion.with_name('mesa-ps1-report.json').read_text())
    ch=fmt.wad_index(base[:65536],len(base))
    entries=[]
    for i in range(1,63):
        begin,end=struct.unpack_from('<II',base,64+4*i)
        if not end:break
        cls=struct.unpack_from('<H',base,0x140+2*i)[0]
        entries.append((i,cls,begin,end))
    banks={cls:base[b:e] for _,cls,b,e in entries}
    stub=banks[0x132];assert struct.unpack_from('<i',stub)[0]<0
    unused={0x22a,0x242,0x22f}
    assert not unused&{int(k,16) for k in build['objects']['placed']}
    gameplay=bytearray(base[ch[3].offset:ch[3].offset+ch[3].size]);at=0x2c
    for _ in range(8):at+=fmt.u32(gameplay,at)
    changes=[]
    for i in range(fmt.u32(gameplay,at+4)):
        p=at+8+88*i;cls=struct.unpack_from('<H',gameplay,p+54)[0]
        if cls not in unused:continue
        assert struct.unpack_from('<3i',gameplay,p+12)==(2048,2048,512), 'only already-parked records'
        assert gameplay[p+72] in (0,0xfd)
        changes.append(dict(index=i,cls=cls,offset=p+72,previous=gameplay[p+72],state=0xfd))
        gameplay[p+72]=0xfd
    for cls in unused:
        assert sum(x['cls']==cls for x in changes)==build['objects']['parked'][hex(cls)]
    model=bytearray(base[ch[2].offset:entries[0][2]])
    header=bytearray(base[:ch[0].offset]);rel={}
    for index,cls,b,e in entries:
        rel[index]=len(model)
        model.extend(stub if cls in unused else banks[cls])
    rel[len(entries)+1]=len(model)
    output=bytearray(header)
    children=[base[ch[0].offset:ch[0].offset+ch[0].size],base[ch[1].offset:ch[1].offset+ch[1].size],bytes(model),bytes(gameplay)]
    offsets=[]
    for i,data in enumerate(children):
        output.extend(bytes(fmt.align(len(output))-len(output)));offsets.append(len(output))
        struct.pack_into('<II',output,i*8,len(output),fmt.align(len(data)))
        output.extend(data);output.extend(bytes(fmt.align(len(output))-len(output)))
    for index,offset in rel.items():fmt.put32(output,64+index*4,offsets[2]+offset)
    fmt.put32(output,64,fmt.u32(base,64)+offsets[2]-ch[2].offset)
    digest=fmt.sha(output);out.mkdir(parents=True)
    archive=out/'slot17-model-assets.bin';archive.write_bytes(output)
    conversion=dict(old_report)
    conversion.update(archive=str(archive),archive_sha256=digest,pre_budget_archive=str(Path(old_report['archive']).resolve()),
        pre_budget_archive_sha256=fmt.sha(base),inactive_glimmer_records=changes,
        inactive_glimmer_banks=[dict(cls=cls,old_sha256=fmt.sha(banks[cls]),old_bytes=len(banks[cls]),
                                    inert_static_bank_sha256=fmt.sha(stub),bytes=len(stub)) for cls in sorted(unused)])
    conversion['unchanged']=['terrain, material descriptors, collision, sky and source placements',
                             'all gameplay bytes except declared already-parked record state bytes']
    conversion['limits']=old_report['limits']+['Three Glimmer-only parked classes use original removed state and compact loader-only static banks; no visible character substitution.']
    for model_info in old_report['models']:
        cls=model_info['target_class']
        for suffix in ('source','s2'):(out/f'{cls:03x}-{suffix}.bank').write_bytes(a.conversion.with_name(f'{cls:03x}-{suffix}.bank').read_bytes())
    # Reserve masks are part of the parent integration ABI.
    (out/'model-vram-reservation.npz').write_bytes(a.conversion.with_name('model-vram-reservation.npz').read_bytes())
    (out/'model-conversion.json').write_text(json.dumps(conversion,indent=2)+'\n')
    build.update(archive=str(archive),archive_sha256=digest,release_eligible=False,model_conversion=str(out/'model-conversion.json'))
    build['inactive_glimmer_records']=changes
    build['archive_layout'].update(offsets=offsets,sizes=[fmt.align(len(x)) for x in children],total=len(output),
         growth=fmt.align(len(children[1]))-505856,model_directory_entries_moved=len(entries)+2)
    (out/'mesa-ps1-report.json').write_text(json.dumps(build,indent=1))
    print('BASE ARCHIVE',Path(old_report['archive']).resolve(),'sha256',fmt.sha(base))
    print('RAM-BUDGET CANDIDATE',archive,'sha256',digest)
    print('REMOVED STATE',json.dumps(changes))
    print('MODEL RAM SAVED',len(base)-len(output),'bytes; original source actor models and all terrain unchanged')
    print('LIMIT inactive Glimmer-only banks; no new actor AI/story or release claim')


if __name__=='__main__':main()

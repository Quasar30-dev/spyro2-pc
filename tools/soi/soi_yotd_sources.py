"""Bounded local YotD bank census for future animated character import.

No donor identity is invented from a class number. This does not install models
or prove their S2 animation-state/sound integration.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import mipsdis

ROOT=Path(__file__).resolve().parents[2]


def u32(b,o):return struct.unpack_from('<I',b,o)[0]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'data/soi/content-20261105/yotd-model-census.json')
    a=ap.parse_args();path=ROOT/'data-yotd/WAD.WAD';exe_path=ROOT/'data-yotd/SCUS_944.67'
    rows=[];errors=[]
    with path.open('rb') as f:
        head=f.read(2048)
        for slot in range(97,170,2):
            off,size=struct.unpack_from('<II',head,slot*8);f.seek(off);arc=f.read(size)
            assert len(arc)==size
            archive_sha=hashlib.sha256(arc).hexdigest()
            for i in range(1,63):
                begin,end=u32(arc,0xa0+i*4),u32(arc,0xa4+i*4)
                if not end:break
                cls=struct.unpack_from('<H',arc,0x1a0+i*2)[0]
                if not 0<begin<end<=len(arc):errors.append({'slot':slot,'class':cls,'error':'bank range'});continue
                bank=arc[begin:end];count=struct.unpack_from('<i',bank)[0]
                row={'slot':slot,'directory_index':i,'class':cls,'bank_begin':begin,'bytes':len(bank),
                     'archive_sha256':archive_sha,'bank_sha256':hashlib.sha256(bank).hexdigest(),'clip_count':count}
                if 0<count<=255:
                    clips=[]
                    for c in range(count):
                        desc=u32(bank,60+c*4)
                        assert desc+36<=len(bank),(slot,cls,c,desc)
                        frames=bank[desc]
                        assert frames and desc+36+frames*8<=len(bank),(slot,cls,c,'frame bounds')
                        clips.append({'clip':c,'descriptor':desc,'frames':frames,'colours':bank[desc+1],
                                      'shift':bank[desc+2],'near_vertices':bank[desc+4],'far_vertices':bank[desc+5]})
                    row['clips']=clips
                rows.append(row)
    base,s2=mipsdis.exe_image();s3=exe_path.read_bytes();load=u32(s3,0x18)
    shared={}
    for name,address,size in [('moby_codebook',0x80062674,500),('animation_blends',0x80062868,256)]:
        needle=s2[address-base:address-base+size];offset=s3.find(needle,0x800)
        shared[name]={'s2_address':hex(address),'bytes':size,'sha256':hashlib.sha256(needle).hexdigest(),
                      'exact_yotd_match_address':hex(load+offset-0x800) if offset>=0 else None}
    result={'source_wad':str(path),'source_wad_sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),
            'source_exe':str(exe_path),'source_exe_sha256':hashlib.sha256(s3).hexdigest(),
            'shared_original_tables':shared,'models':rows,'errors':errors,
            'known_appearance_references':[{'slot':99,'class':0xfd,'identity':'Sunny Villa small shield Rhynoc; docs/SPYRO3_FORMATS.md'},
                                          {'slot':99,'class':0xee,'identity':'Sunny Villa large club Rhynoc; docs/SPYRO3_FORMATS.md'}],
            'limits':['Bianca and best matching spear/robed donors still require original visual/code identification.',
                      'No texture/CLUT relocation, vertex-stream conversion, clip remapping, sound integration or engine import completed.']}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2))
    print('ORIGINAL WAD',path,'sha256',result['source_wad_sha256'])
    print('ORIGINAL EXE',exe_path,'sha256',result['source_exe_sha256'])
    print('MEASURED',len(rows),'banks; range errors',len(errors),'shared tables',json.dumps(shared))
    print('CENSUS',a.out.resolve(),'sha256',hashlib.sha256(a.out.read_bytes()).hexdigest())
    print('LIMIT',json.dumps(result['limits']))


if __name__=='__main__':main()

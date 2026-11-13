"""Original SoI background-container denominations and S2 release input.

0801fec0 passes type+7d (130..134) / type+78 (135..139) to 0801c474,
which masks with 3 and reads 081ab8f0. Original S2 80039360/8003939c
reads Moby+50 as the value before the variant-table lookup.
"""
import hashlib
import json
import struct

from gba import Rom,ROM_PATH,ROM_SHA256
import mesa_ps1 as mesa
import mipsdis


def sources():
    rom=Rom();digest=hashlib.sha256(rom.data).hexdigest();assert digest==ROM_SHA256
    table=rom.u32(0x0801c5dc);assert table==0x081ab8f0
    values=list(rom.bytes(table,4));assert values==[1,2,5,10]
    base,code=mipsdis.exe_image()
    for addr in (0x80039360,0x8003939c):
        assert struct.unpack_from('<I',code,addr-base)[0] in (0x92630050,0x92620050)
    level=json.loads((mesa.PROJECT/'data/soi/levels/06-market-mesa/level.json').read_text())
    records=[]
    for obj in level['objects']['objects']:
        if obj['family'] not in ('bg_container_a','bg_container_b'):continue
        typ=obj['type'];assert 130<=typ<=139
        variant=(typ+(0x7d if typ<135 else 0x78))&3
        records.append(dict(source_index=obj['index'],source_type=typ,source_position=obj['pos'],
                            value=values[variant],source_variant=variant))
    assert len(records)==15 and sum(e['value'] for e in records)==15
    return records,dict(rom=str(ROM_PATH),rom_sha256=digest,table=hex(table),values=values,
                        gba_release='0801fec0 -> 0801c474',s2_release='80039334; +50 reads 80039360/8003939c',
                        executable_sha256=hashlib.sha256(mipsdis.EXE.read_bytes()).hexdigest())


if __name__=='__main__':
    records,oracle=sources()
    print('ORIGINAL',json.dumps(oracle))
    print('SOURCE CONTAINERS',json.dumps(records))
    print('MEASURED original factory release values',len(records),'containers /',sum(e['value'] for e in records),'gems')

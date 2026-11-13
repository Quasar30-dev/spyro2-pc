"""Append loose gems without renumbering any retail Moby or moving its data.

8001c734 obtains the free pool from table +8+count*88 and the next section
from table+size. Consume 88-byte records from that reserve; append separate
24-byte gem parameters at the end of the parameter section and add relocation
entries. All original records and pointer targets retain their addresses.
"""
import struct
from pathlib import Path

TABLE=0x327c


def expand(g,needed):
    size,count=struct.unpack_from('<II',g,TABLE)
    rec=lambda i:TABLE+8+88*i
    ids=[i for i in range(count) if struct.unpack_from('<H',g,rec(i)+54)[0]==1]
    extra=needed-len(ids)
    assert 0<=extra and count+extra<=256
    pool=rec(count); end=TABLE+size
    assert not any(g[pool:pool+88*extra]),'would overwrite populated reserve'
    assert end-(pool+88*extra)>=112*32,'insufficient retained dynamic pool'
    parameter_size=struct.unpack_from('<I',g,end)[0]
    insert=end+parameter_size
    assert struct.unpack_from('<II',g,insert)==(8,0)
    rel=insert+8; nr=struct.unpack_from('<I',g,rel)[0]
    refs=list(struct.unpack_from('<'+'I'*nr,g,rel+4))
    assert not any(g[rel+4+nr*4:]),'non-padding follows relocation list'
    assert all(not pool<=p<end for p in refs)
    # No original parameter points into the free reserve being consumed.
    assert all(not pool<=struct.unpack_from('<I',g,p)[0]<pool+88*extra for p in refs)
    template=bytes(g[rec(ids[0]):rec(ids[0])+88])
    data=struct.unpack_from('<I',template)[0]
    params=bytes(g[data:data+24]); added=24*extra
    assert len(params)==24
    g[insert:insert]=params*extra
    struct.pack_into('<I',g,end,parameter_size+added)
    new_refs=[]
    for p in refs:
        q=p+added if p>=insert else p
        value=struct.unpack_from('<I',g,q)[0]
        if value>=insert: struct.pack_into('<I',g,q,value+added)
        new_refs.append(q)
    for j in range(extra):
        p=rec(count+j)
        g[p:p+88]=template
        struct.pack_into('<I',g,p,insert+24*j)
        new_refs.append(p)
    struct.pack_into('<I',g,TABLE+4,count+extra)
    rel+=added
    struct.pack_into('<I',g,rel,len(new_refs))
    struct.pack_into('<'+'I'*len(new_refs),g,rel+4,*new_refs)
    return dict(original_count=count,added_gems=extra,count=count+extra,
                original_pool_bytes=end-pool,retained_pool_bytes=end-rec(count+extra),
                original_indices_preserved=True,parameter_insert=insert,parameter_bytes_added=added,
                relocation_offset=rel,relocations=len(new_refs))


def tables():
    exe=(Path(__file__).resolve().parents[2]/'data/disc/SCUS_944.25').read_bytes()
    load=struct.unpack_from('<I',exe,0x18)[0]
    get=lambda addr,n:exe[0x800+addr-load:0x800+addr-load+n]
    return get(0x8006440c,28),get(0x80064428,5),get(0x80064430,5),get(0x80064438,5)

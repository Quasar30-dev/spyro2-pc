"""Bind the inactive-Moneybags design to original instruction bytes and data.

Static observations only. The complementary private runtime check proves that
the real loader/helpers retain the chosen state and cannot reach the handler.
"""
import argparse
import hashlib
import struct
from pathlib import Path
import mipsdis
from s2scene import read_archive
from spyrodisc import wad_index

ROOT=Path(__file__).resolve().parents[2]
REMOVED=0xfd
OVERLAY_BASE=0x8006d264


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive',type=Path)
    a=ap.parse_args()
    arc=a.archive.read_bytes(); digest=hashlib.sha256(arc).hexdigest()
    base,main=mipsdis.exe_image()
    overlay=read_archive(ROOT/'data/disc',16)
    print('ARTIFACT',a.archive,'sha256',digest)
    print('ORACLE',mipsdis.EXE,'sha256',hashlib.sha256(mipsdis.EXE.read_bytes()).hexdigest())
    print('ORACLE overlay slot16 base',hex(OVERLAY_BASE),'sha256',hashlib.sha256(overlay).hexdigest())
    def words(code,load,pinned):
        for address,word in pinned.items():
            assert struct.unpack_from('<I',code,address-load)[0]==word,hex(address)
    words(main,base,{0x80041384:0x2002fffd,0x80041388:0x03e00008,0x8004138c:0xa0820048,
                     0x80040ef0:0x80220048,0x80040ef8:0x04400038,
                     0x80040fdc:0x2003ffff,0x80040fe0:0x1462ffc2,
                     0x8001d034:0x90830048,0x8001d038:0x240200ff,
                     0x8001d2c0:0x90420048,0x8001d2c8:0x1456ff62,
                     0x80041c4c:0xac87001c})
    words(overlay,OVERLAY_BASE,{0x8007043c:0x92820048,0x80070444:0x2c420080,0x80070448:0x10403758,
                              0x8007ded4:0x8c427008,0x8007dedc:0x2842000a,0x8007deec:0x8442467e,
                              0x8007df00:0x0c008d97,0x8007df10:0x0c008dc4})
    ch=wad_index(arc[:65536],len(arc)); g=arc[ch[3].offset:ch[3].offset+ch[3].size]
    at=0x2c
    for _ in range(8): at+=struct.unpack_from('<I',g,at)[0]
    count=struct.unpack_from('<I',g,at+4)[0]
    records=[g[at+8+88*i:at+8+88*(i+1)] for i in range(count)]
    money=[(i,r) for i,r in enumerate(records) if struct.unpack_from('<H',r,54)[0]==0x220]
    assert len(money)==1 and money[0][1][0x48]==REMOVED
    assert all(r[0x48]!=0xff for r in records)
    print('MONEYBAGS',money[0][0],'state',hex(REMOVED),'record',money[0][1].hex())
    print('CONSUMER DATA SAFETY PASS',digest,
          '0xfd is original initial-record removal; negative activity states skip, only 0xff terminates;',
          'dispatcher skips >=0x80 before handler and its purchased-bridge animation calls')
    # Retain the actual original implementation for review of the complete
    # loader loop and helpers, rather than relying on a host-side state model.
    for start,end in ((0x8004136c,0x80041390),(0x80040eec,0x80040f08),(0x80040fdc,0x80040fe8),
                      (0x8001d028,0x8001d2d0),(0x80041438,0x80041538),(0x80041ab8,0x80041c50)):
        print('ORIGINAL MAIN RANGE',hex(start),hex(end))
        print('\n'.join(mipsdis.dis(main,base,start,end)))
    for start,end in ((0x8007043c,0x80070450),(0x8007ded0,0x8007df28)):
        print('ORIGINAL GLIMMER RANGE',hex(start),hex(end))
        print('\n'.join(mipsdis.dis(overlay,OVERLAY_BASE,start,end)))
    print('LIMIT static consumer evidence; loader preservation and saved-purchase fixture require exact-image runtime log')


if __name__=='__main__': main()

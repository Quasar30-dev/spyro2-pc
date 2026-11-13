"""Prepare hash-bound expanded-ROM replacements and final WAD offsets."""
import argparse
import json
import struct
from pathlib import Path
import mesa_extra_loader as loader
import s2scene


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive',type=Path,required=True);ap.add_argument('--overlay',type=Path,required=True)
    ap.add_argument('--summer',type=Path);ap.add_argument('--music-start',type=int);ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
    retail=(s2scene.PROJECT/'data/disc/WAD.WAD').read_bytes()
    old=[struct.unpack_from('<II',retail,i*8) for i in range(197)]
    overlay=a.overlay.read_bytes();padded_overlay=overlay+bytes((-len(overlay))%2048)
    parts={198:padded_overlay,199:a.archive.read_bytes()}
    if a.summer:parts[15]=a.summer.read_bytes()
    bootstrap,proof=loader.build([(2048,0)]*200,a.music_start);parts[1]=bootstrap
    for _ in range(2):
        toc=[];offset=2048
        for i in range(200):
            o,n=old[i] if i<len(old) else (0,0)
            data=parts.get(i,retail[o:o+n]);toc.append((offset,len(data)))
            offset=(offset+len(data)+2047)&~2047
        new,proof=loader.build(toc,a.music_start);assert len(new)==len(bootstrap)
        bootstrap=new;parts[1]=new
    replacements={}
    for i,data in parts.items():
        p=a.out/f'slot{i:03d}.bin';p.write_bytes(data)
        replacements[str(i)]=dict(path=str(p.resolve()),sha256=loader.sha(data))
    (a.out/'loader-report.json').write_text(json.dumps(proof,indent=2)+'\n')
    (a.out/'layout.json').write_text(json.dumps(dict(wad_toc=toc),indent=2)+'\n')
    (a.out/'manifest.json').write_text(json.dumps(dict(kind='Expanded Market Mesa private candidate',replacements=replacements,
        preserve_original_slots=[16,17],release_eligible=False,fixture=False,extra_scene=proof),indent=2)+'\n')
    print('EXPANSION PREPARED',proof['loader_sha256'],'physical198/199;',len(parts),'explicit replacements; original Glimmer16/17 retained')


if __name__=='__main__':main()

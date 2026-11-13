"""Identify the supplied prerelease discs from their actual files, preserving source bytes."""
from pathlib import Path
import sys
import hashlib,json,struct,zlib
from spyrodisc import Disc,exe_header,wad_index,write_json,FormatError

ROOT=Path(__file__).resolve().parents[1]
for build in (sys.argv[1:] or ['july-1999','august-1999']):
    out=ROOT/'data/builds'/build
    cues=list((out/'source-disc').glob('*.cue'))
    if len(cues)!=1:raise RuntimeError(f'Expected one source cue for {build}')
    # The CUE reader selects its one data track and preserves separate CDDA.
    source=cues[0]
    with Disc(source) as disc:
        files=disc.entries();report={'source':str(cues[0].relative_to(ROOT)),'sector_stride':disc.stride,'files':[],
            'executables':[],'archives':[]}
        for entry in files:
            info={'name':entry.path,'bytes':entry.size,'lba':entry.lba}
            report['files'].append(info)
            if entry.size>=2048 and not entry.path.endswith(('.STR','.XA')):
                prefix=disc.read(entry.lba,min(entry.size,2048))
                if prefix[:8]==b'PS-X EXE':
                    data=disc.read(entry.lba,entry.size);exe=exe_header(data)
                    exe.update(name=entry.path,crc32=f'{zlib.crc32(data):08x}',sha256=hashlib.sha256(data).hexdigest())
                    target=out/'disc'/entry.path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
                    payload=out/'exe'/('main.bin' if not report['executables'] else Path(entry.path).name+'.bin')
                    payload.parent.mkdir(exist_ok=True);payload.write_bytes(data[2048:2048+exe['load_size']])
                    report['executables'].append(exe)
            if entry.path.endswith('.WAD') and ('SPYRO' in entry.path or build!='winter-jampack-1999'):
                dest=out/'disc'/entry.path
                if not dest.exists() or dest.stat().st_size!=entry.size:disc.extract(entry,dest)
                with dest.open('rb') as f:header=f.read(0x10000)
                root=wad_index(header,entry.size)
                index=[{'id':w.index,'offset':w.offset,'size':w.size} for w in root]
                write_json(out/(Path(entry.path).stem.lower()+'-index.json'),index)
                scenes=[]
                with dest.open('rb') as f:
                    for w in root:
                        if not w.size:continue
                        f.seek(w.offset);block=f.read(min(w.size,0x10000))
                        try:
                            child=wad_index(block,w.size)
                            if len(child)==4:scenes.append({'slot':w.index,'bytes':w.size,'children':[c.size for c in child]})
                        except FormatError:pass
                report['archives'].append({'name':entry.path,'slots':len(root),'empty_slots':[w.index for w in root if not w.size],
                    'four_child_candidates':scenes})
            if entry.path=='SYSTEM.CNF':
                text=disc.read(entry.lba,entry.size)
                (out/'disc').mkdir(exist_ok=True);(out/'disc/SYSTEM.CNF').write_bytes(text)
                report['system_cnf']=text.decode('ascii','replace')
        write_json(out/'manifest.json',report)
        print(json.dumps({'build':build,**report},indent=2),flush=True)

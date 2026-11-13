"""Read-only research inventory of the US retail copy, not an unused-content detector.

Every offset is recorded against the user's WAD/executable. Absence from placed
objects or a string pointer scan does NOT establish that an asset is unused.
No retail bytes are written outside the selected local analysis directory.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib
from spyrodisc import u32, wad_index
from verify_secondary import Retail, frame_banks

TERMS = ['spyro 3', 'spyro3', 'spyro iii', 'year of the dragon', 'bianca',
         'sorceress', 'rhynoc', 'sheila', 'bentley', 'sgt', 'byrd', 'agent 9',
         'midnight mountain', 'sunrise spring', 'sunny villa', 'dragon egg',
         'forgotten worlds', 'evening lake', 'midday gardens', 'buzz',
         'aquifer', 'bastion', 'spring', 'unused', 'test', 'debug', 'cheat',
         'level 10', 'prototype', 'talisman', 'orb', 'powerup', 'power-up']
STRINGS = re.compile(rb'[\x20-\x7e]{4,}')

def sha(data): return hashlib.sha256(data).hexdigest()
def word(data, at): return struct.unpack_from('<H', data, at)[0]
def need(data, at, size):
    if at < 0 or size < 0 or at + size > len(data):
        raise ValueError(f'Extent {at:#x}+{size:#x} outside {len(data):#x}')

def sections(data, count=9):
    out, at = [], 0x2c
    for i in range(count):
        size = u32(data, at)
        if size < 4 or size % 4: raise ValueError('Invalid gameplay section')
        need(data, at, size)
        out.append({'index': i, 'offset': at, 'size': size})
        at += size
    return out

def speech_table(data, parts):
    part = parts[7]
    if (part['size'] - 4) % 8: raise ValueError('Invalid voice table')
    return [{'id': i, 'offset': at, 'channel': word(data, at),
             'sectors': word(data, at + 2), 'sector': u32(data, at + 4)}
            for i, at in enumerate(range(part['offset']+4, part['offset']+part['size'], 8))]

def dialogue_records(data, strings, voices, records):
    # Accept only command-prefixed, terminated dialogue records. Do not infer
    # NPC layouts: parameter references below are leads, not ownership proofs.
    pointers = defaultdict(list)
    for p in range(0, len(data)-3, 4): pointers[u32(data,p)].append(p)
    out=[]
    for start, refs in pointers.items():
        if start<0x2c or start>=len(data):continue
        n=data[start]
        if not 1<=n<=64 or start+n>=len(data):continue
        at=start+n;commands=[];p=start+1;good=True
        while p<at:
            c=data[p]
            # Resident 8003a0d8/8003a568 also interpret lower commands (e.g.
            # 03 71 awards an orb). Unknown commands have a two-byte stride;
            # retain them as evidence rather than dropping reward dialogue.
            if not 1 <= c <= 0x2d:good=False;break
            size=3 if c==0x23 else 5 if c==0x25 else 1 if c in (0x26,0x2a,0x2b) else 2
            if p+size>at:good=False;break
            commands.append({'opcode':f'{c:02x}','args':list(data[p+1:p+size])});p+=size
        if not good or p!=at:continue
        end=at
        while end<min(len(data),at+8192) and 32<=data[end]<127:end+=1
        if end==len(data) or data[end] not in (0,255) or end-at<10:continue
        text=data[at:end].decode('ascii')
        if sum(c.isalpha() for c in text)<4 or text.count(' ')<1:continue
        voice_ids=[c['args'][0]-1 for c in commands if c['opcode']=='28']
        if any(v!=254 and not 0<=v<len(voices) for v in voice_ids):continue
        out.append({'record_offset':start,'text_offset':at,'text':text,
                    'commands':commands,'aligned_pointer_references':refs,
                    'voice_ids':voice_ids,
                    'voices':[voices[v] for v in voice_ids if 0<=v<len(voices)]})
    return sorted(out,key=lambda line:line['record_offset'])

def raw_strings(data, scope, wad_offset=None, address=None):
    for match in STRINGS.finditer(data):
        text=match.group().decode('ascii')
        # Reject obvious geometry/texture patterns, retain names and sentences.
        letters=sum(c.isalpha() for c in text)
        if letters<4 or letters<len(text)*0.42 or len(set(text.lower()))<5:continue
        if len(text)>4096:continue
        item={'scope':scope,'offset':match.start(),'length':len(text),'text':text}
        if wad_offset is not None:item['wad_offset']=wad_offset+match.start()
        if address is not None:item['address']=f'{address+match.start():08x}'
        yield item

def term_hits(data, scope, base=0):
    out=[]
    folded=data.lower()
    for term in TERMS:
        for encoding in ('ascii','utf-16-le','utf-16-be'):
            needle=term.encode(encoding)
            at=folded.find(needle)
            while at>=0:
                context=data[max(0,at-60):at+len(needle)+80]
                out.append({'term':term,'encoding':encoding,'scope':scope,'offset':at,
                            'source_offset':base+at,
                            'context':''.join(chr(b) if 32<=b<127 else '.' for b in context)})
                at=folded.find(needle,at+len(needle))
    return out

def audit(source, output):
    output.mkdir(parents=True,exist_ok=True)
    retail=Retail(source)
    try:
        exe=retail.read('SCUS_944.25')
        if zlib.crc32(exe)!=0xcdc0917c:raise ValueError('Only verified US SCUS_944.25 supported')
        wad=retail.read('WAD.WAD')
        slots=wad_index(wad[:0x800],len(wad))
        read_slot=lambda s:wad[slots[s].offset:slots[s].offset+slots[s].size]
        names={}
        for i in range(29):
            at=u32(exe,0x800+0x549b4+i*4)-0x80010000+0x800
            names[15+i*2]=exe[at:exe.index(0,at)].decode('ascii')
        global_bank=read_slot(0);model=u32(global_bank,4)
        global_clips=[i for i in range(62) if u32(global_bank,model+60+i*4)!=0xffffffff]
        all_strings=list(raw_strings(exe[0x800:],'resident',address=0x80010000))
        hits=term_hits(exe,'SCUS_944.25')
        scenes=[];archives=[];speech=[];dialogue=[]
        for entry in slots:
            bank=read_slot(entry.index)
            archives.append({'slot':entry.index,'offset':entry.offset,'bytes':entry.size,
                             'sha256':sha(bank) if bank else None})
            if not bank:continue
            scope=f'WAD[{entry.index}]'
            strings=list(raw_strings(bank,scope,entry.offset))
            all_strings.extend(strings)
            hits.extend(term_hits(bank,scope,entry.offset))
            if entry.index<15 or entry.index>95 or entry.index%2==0:continue
            children=wad_index(bank[:0x800],len(bank))
            scene={'slot':entry.index,'name':names.get(entry.index,f'Scene {entry.index}'),
                   'children':[vars(c) for c in children],'models':[],'spyro_clips':[]}
            for clip, raw in frame_banks(global_bank,bank).items():
                scene['spyro_clips'].append({'id':clip,'frames':raw[0], 'vertices':raw[3],
                    'origin':'global' if clip in global_clips else 'level',
                    'descriptor_frames_sha256':sha(raw)})
            records=[]
            if entry.index!=95:
                child=children[3];data=bank[child.offset:child.offset+child.size]
                parts=sections(data);scene['sections']=parts
                table=parts[8];count=u32(data,table['offset']+4)
                need(data,table['offset']+8,count*88)
                for i in range(count):
                    at=table['offset']+8+i*88
                    records.append({'id':i,'offset':at,'class':f'{word(data,at+54):03x}',
                                    'parameters':u32(data,at),'state':struct.unpack_from('<b',data,at+72)[0],
                                    'initial_clip':data[at+60],'variant':data[at+73],
                                    'position':list(struct.unpack_from('<iii',data,at+12))})
                scene['objects']=records
                voices=speech_table(data,parts)
                for v in voices:speech.append(dict(slot=entry.index,name=scene['name'],**v))
                strings=list(raw_strings(data,f'WAD[{entry.index}].gameplay',entry.offset+child.offset))
                for line in dialogue_records(data,strings,voices,records):
                    dialogue.append(dict(slot=entry.index,name=scene['name'],
                        wad_offset=entry.offset+child.offset+line['record_offset'],**line))
            placed=Counter(r['class'] for r in records)
            for i in range(1,63):
                at,end=u32(bank,0x40+i*4),u32(bank,0x44+i*4)
                if not end:break
                need(bank,at,end-at)
                type_id=word(bank,0x140+i*2);raw=bank[at:end]
                count=struct.unpack_from('<i',raw)[0]
                info={'class':f'{type_id:03x}','offset':at,'bytes':end-at,
                      'sha256':sha(raw),'placed_instances':placed[f'{type_id:03x}'],
                      'animated':count>0,'clips':[]}
                if not count or abs(count)>255:raise ValueError('Invalid model count')
                if count>0:
                    for clip in range(count):
                        p=u32(raw,60+clip*4);need(raw,p,36)
                        frames=raw[p];need(raw,p+36,frames*8)
                        metadata=[u32(raw,p+40+f*8) for f in range(frames)]
                        sound=[(m>>16)&255 for m in metadata if ((m>>16)&255)<128]
                        info['clips'].append({'id':clip,'offset':p,'frames':frames,
                            'vertices':raw[p+4], 'vblanks':sum(((m>>9)&15)+1 for m in metadata),
                            'frame_sound_candidates':sound,
                            'descriptor_frames_sha256':sha(raw[p:p+36+frames*8])})
                else:info['static_meshes']=-count
                scene['models'].append(info)
            scenes.append(scene)
            print(f"Inventoried {entry.index}: {scene['name']}",flush=True)
        extra_files=[]
        # Separate bundled demo/padding data from Spyro content when a full disc
        # image is provided; extracted folders may intentionally omit these.
        file_names=sorted(retail.files) if retail.disc else [p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()]
        for name in file_names:
            if name in ('SCUS_944.25','WAD.WAD') or 'SPEECH.STR' in name:continue
            representation='logical-2048'
            if retail.disc and name.upper().endswith('.XA'):
                entry=retail.files[name]
                data=b''.join(retail.disc.sector(entry.lba+i,raw=True) for i in range((entry.size+2047)//2048))
                representation='raw-2352'
            else:data=retail.read(name)
            extra_files.append({'path':name,'size':len(data),'sha256':sha(data),'representation':representation})
            hits.extend(term_hits(data,name))
            all_strings.extend(raw_strings(data,name))
        summary={'schema':1,'profile':'US SCUS_944.25 / cdc0917c',
                 'executable_sha256':sha(exe),'wad_sha256':sha(wad),
                 'wad_slots':len(slots),'nonempty_slots':sum(bool(e.size) for e in slots),
                 'scenes':len(scenes),'object_records':sum(len(s.get('objects',[])) for s in scenes),
                 'model_banks':sum(len(s['models']) for s in scenes),
                 'moby_animation_banks':sum(len(m['clips']) for s in scenes for m in s['models']),
                 'spyro_clip_ids':sorted(set(c['id'] for s in scenes for c in s['spyro_clips'])),
                 'global_spyro_clip_ids':global_clips,'speech_records':len(speech),
                 'structured_dialogue_candidates':len(dialogue),'ascii_string_candidates':len(all_strings),
                 'terms':TERMS,'extra_files':extra_files,
                 'limitations':['Pointer absence and no placed instance do not prove unused.',
                     'Dialogue recognizer is conservative; compressed/encoded and voice-only lines need separate study.',
                     'Title slot 95 has its own object/cinematic format.',
                     'ASCII/UTF-16 name searches cannot rule out nameless sequel prototypes.']}
        for filename,value in [('summary.json',summary),('archive_inventory.json',archives),
                                ('scene_inventory.json',scenes),('speech_inventory.json',speech),
                                ('dialogue_candidates.json',dialogue),('term_hits.json',hits)]:
            (output/filename).write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        with (output/'strings.jsonl').open('w',encoding='utf-8') as f:
            for row in all_strings:f.write(json.dumps(row,ensure_ascii=False)+'\n')
        print(json.dumps(summary,indent=2))
    finally:retail.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game',type=Path)
    parser.add_argument('--output',type=Path,default=Path('data/analysis/cut-content'))
    args=parser.parse_args();audit(args.game,args.output)

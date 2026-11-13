"""Finite physical-input actor checks on a copied native workbench.

Only the S2SHOT spawn/controller input is supplied. No enemy hit mailbox,
runtime object position, health or save result is written by this harness.
"""
import argparse
import json
import math
from pathlib import Path
import shutil

import numpy as np
import mesa_pc_check as private
from mesa_container_check import floor_query,removed_objects


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path);ap.add_argument('proof',type=Path);ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--sources',default='0,9,141');ap.add_argument('--frames',type=int,default=240);ap.add_argument('--attack',choices=['flame','charge','none'],default='flame')
    ap.add_argument('--source-executable',type=Path,default=private.pc.PROJECT/'build/soi-cli-candidate/Release/spyro_workbench.exe')
    a=ap.parse_args();cue=a.cue.resolve();proof=json.loads(a.proof.read_text());work=a.work.resolve();assert not work.exists();work.mkdir(parents=True)
    source=a.source_executable.resolve()
    assert private.sha(source)=='9a798a70060a14452a73acf117d72cc420b3f13c80124d83de7eba41d3ea5c8c','Frozen executable identity mismatch'
    exe=work/source.name;shutil.copyfile(source,exe);private.pc.SCRATCH=work
    digest=private.sha(cue.with_suffix('.bin'));print('ARTIFACT BIN SHA256',digest,cue,flush=True);print('PRIVATE NATIVE SHA256',private.sha(exe),exe,flush=True)
    wanted={int(v) for v in a.sources.split(',')};actors=[e for e in proof['actors'] if e['kind'] in [1,2,3] and e['source_index'] in wanted]
    assert {e['source_index'] for e in actors}==wanted,('Unknown requested source',wanted-{e['source_index'] for e in actors})
    floor=floor_query(Path(proof['archive']).read_bytes());results=[]
    for e in actors:
        target=np.array(e['position']);candidates=[]
        for distance in [900,1200,1600]:
            for k in range(8):
                xy=target[:2]+distance*np.array([math.cos(k*math.pi/4),math.sin(k*math.pi/4)]);z=floor(*xy,target[2])
                if z is None:continue
                if any(floor(xy[0]+dx,xy[1]+dy,z) is None for dx,dy in [(256,0),(-256,0),(0,256),(0,-256)]):continue
                candidates.append(np.array([xy[0],z+64,xy[1]])/1024)
        assert candidates,('No supported source approach',e['source_index'])
        success=False
        for attempt,spawn in enumerate(candidates[:8]):
            tag=f'actor-{e["source_index"]}-{a.attack}-{attempt}';shot=work/(tag+'.s2shot');cap=work/(tag+'.bmp');save=work/(tag+'.s2save')
            yaw=math.atan2(target[0]/1024-spawn[0],target[1]/1024-spawn[2])
            acts=[] if a.attack=='none' else ([f'{t} {t+1} 0 0 1 0 0 0 0 0 0 0 0 0 0 0' for t in [12,36,72,108]] if a.attack=='flame' else ['12 52 1 0 0 0 0 0 0 0 0 0 0 0 0 0'])
            sp=' '.join(str(v) for v in spawn);shot.write_text('\n'.join(['S2SHOT 4',f'640 480 {a.frames}',sp+f' {yaw} 1 50 0','1',sp,str(len(acts)),*acts,'0'])+'\n')
            cmd=[str(exe),'--game',str(cue),'--slot','17','--play','--cinematic-shot',str(shot),'--smoke',str(a.frames),'--capture',str(cap),'--save-game',str(save),'--menu-profile',str(work/'profile.json')]
            r=private.hidden_run(cmd,90)
            report=json.loads(cap.with_suffix('.json').read_text()) if cap.with_suffix('.json').exists() else {}
            selected={k:v for k,v in report.items() if k.startswith('retail_script') or 'butterfl' in k or k.startswith('sparx_') or k in ['health','gem_total','frames','bouncing_gems','player_position','audio_effects_played','audio_samples','held_sounds']}
            removed=removed_objects(save) if save.exists() else set();named=(e['index'],e['cls']) in removed
            result=dict(source=e['source_index'],index=e['index'],attack=a.attack,attempt=attempt,spawn=spawn.tolist(),exit=r.returncode,named_removed=named,save_exists=save.exists(),report=selected,capture=str(cap),save=str(save))
            print('NATIVE ACTOR RESULT',digest,json.dumps(result),flush=True);results.append(result)
            if r.returncode==0 and report.get('retail_script_faults')==0 and (named or e['kind']==3 and report.get('sparx_butterflies_eaten',0)>0 or a.attack=='none' and report.get('retail_script_hits',0)>0):success=True;break
        if not success:print('UNRESOLVED NATIVE ACTOR',digest,e['source_index'],'finite supported approaches exhausted; retained all artifacts',flush=True)
    (work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    assert private.sha(cue.with_suffix('.bin'))==digest
    print('NATIVE PHYSICAL INPUT CHECK FINISHED',digest,'requested sources',sorted(wanted),'results',len(results),'inspect named removals/save/health; no manual acceptance',flush=True)
    assert not any(not any(r['source']==e['source_index'] and r['exit']==0 and r['report'].get('retail_script_faults')==0 and (r['named_removed'] or e['kind']==3 and r['report'].get('sparx_butterflies_eaten',0)>0 or a.attack=='none' and r['report'].get('retail_script_hits',0)>0) for r in results) for e in actors),'Unresolved requested actors'


if __name__=='__main__':main()

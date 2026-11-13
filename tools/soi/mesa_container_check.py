"""Finite native common-basket interaction captures on a private hidden copy.

Every source container is targeted. Fields/contents have separate static and
original-engine proofs; this checks that the existing donor interaction opens
and releases a gem. Nearby loose pickups can affect the total, so it is recorded
without falsely attributing the whole total to the container.
"""
import argparse
import hashlib
import json
import shutil
import math
import struct
from pathlib import Path

import numpy as np

import mesa_pc_check as private
import mesa_ps1 as mesa
import s2collision
from spyrodisc import wad_index


def floor_query(archive):
    ch=wad_index(archive[:65536],len(archive))
    scene=mesa.s2scene_build.split(archive[ch[1].offset:ch[1].offset+ch[1].size])
    collision=s2collision.decode(scene['collision'])
    triangles=np.array([s2collision.tri_points(collision.tri,i) for i in range(collision.count)],float)
    a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
    n=np.cross(b-a,c-a);keep=np.abs(n[:,2])>.85*np.linalg.norm(n,axis=1)
    triangles=triangles[keep];a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
    d=b[:,:2]-a[:,:2];e=c[:,:2]-a[:,:2];den=d[:,0]*e[:,1]-d[:,1]*e[:,0]
    def floor(x,y,target):
        q=np.array([x,y])-a[:,:2]
        s=(q[:,0]*e[:,1]-q[:,1]*e[:,0])/den;t=(d[:,0]*q[:,1]-d[:,1]*q[:,0])/den
        z=a[:,2]+s*(b[:,2]-a[:,2])+t*(c[:,2]-a[:,2])
        inside=(s>=-1e-8)&(t>=-1e-8)&(s+t<=1+1e-8)&(np.abs(z-target)<=512)
        return float(z[inside].max()) if inside.any() else None
    return floor


def removed_objects(path):
    lines=path.read_text().splitlines();head=lines[0].split();assert head[0]=='S2SAVE' and int(head[1])>=3
    payload=lines[1:];assert int(payload[2])==1
    state=payload[3].split();assert int(state[0])==17
    entities=int(payload[4]);at=5+entities;count=int(payload[at]);at+=1
    return {tuple(map(int,line.split())) for line in payload[at:at+count]}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path);ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--only-index',type=int)
    ap.add_argument('--approach-set',choices=('ground','jump-charge','source'),default='ground')
    ap.add_argument('--attack',choices=('flame','charge'))
    ap.add_argument('--expect-intact',action='store_true',help='isolated C9 flame rejection with positive heat evidence')
    ap.add_argument('--source-executable',type=Path,help='copy a specified existing native executable into the private fixture')
    a=ap.parse_args(); a.work=a.work.resolve(); assert not a.work.exists();a.work.mkdir(parents=True)
    build=json.loads(a.report.read_text()); assert len(build['container_records'])==15
    source=(a.source_executable or private.pc.PROJECT/'build-soi/spyro_workbench.exe').resolve()
    exe=a.work/source.name;shutil.copy2(source,exe)
    digest=private.sha(a.cue.with_suffix('.bin'))
    print('ARTIFACT',a.cue,'BIN sha256',digest,flush=True)
    print('PRIVATE EXE',exe,'sha256',private.sha(exe),flush=True)
    private.pc.SCRATCH=a.work
    archive=Path(build['archive']).read_bytes();assert hashlib.sha256(archive).hexdigest()==build['archive_sha256']
    floor=floor_query(archive)
    reports=[];failures=[]
    selected=[e for e in build['container_records'] if a.only_index is None or e['index']==a.only_index]
    assert selected
    for e in selected:
        attack=a.attack or ('charge' if e['source_type']>=135 else 'flame')
        approach=('jump-charge' if e['source_index']==29 else 'ground') if a.approach_set=='source' else a.approach_set
        if a.expect_intact:
            assert attack=='flame' and e.get('target_class')==0xc9
            assert all(np.linalg.norm(np.array(other['position'])-e['position'])>12288
                       for other in build['container_records'] if other.get('target_class')==0xc9 and other['index']!=e['index']), 'heat evidence needs isolated named C9'
        target=np.array(e['position'],float);candidates=[]
        distances=(1000,600,1400) if approach=='jump-charge' else (1400,1000)
        directions=16 if approach=='jump-charge' else 8
        for distance in distances:
            for angle in range(directions):
                theta=angle*2*math.pi/directions;x,y=target[:2]+distance*np.array([math.cos(theta),math.sin(theta)])
                z=floor(x,y,target[2])
                if z is None:continue
                samples=[floor(x+dx,y+dy,z) for dx,dy in ((320,0),(-320,0),(0,320),(0,-320))]
                if any(h is None or abs(h-z)>128 for h in samples):continue
                candidates.append(np.array([x,z+64,y])/1024.)
        if not candidates:
            failures.append(e['index']);reports.append(dict(index=e['index'],passed=False,reason='no supported floor footprint'))
            print('SOURCE CONTAINER FAILURE',digest,e['index'],'no supported floor footprint',flush=True)
            continue
        # At most 16 finite, meaningful approach fixtures; no idle polling.
        target_pc=target[[0,2,1]]/1024.;success=False
        for attempt,spawn in enumerate(candidates):
            capture=a.work/f"basket-{e['index']:03d}-{attempt:02d}.bmp";save=capture.with_suffix('.s2save')
            yaw=math.atan2(target_pc[0]-spawn[0],target_pc[2]-spawn[2])
            shot=capture.with_suffix('.s2shot')
            actions=['30 44 1 0 0 0 0 0 0 0 0 0 0 0 0 0'] if attack=='charge' else ['30 31 0 0 1 0 0 0 0 0 0 0 0 0 0 0','90 91 0 0 1 0 0 0 0 0 0 0 0 0 0 0']
            if a.expect_intact:actions+=['165 166 0 0 1 0 0 0 0 0 0 0 0 0 0 0']
            if approach=='jump-charge' and attack=='charge':
                actions=['28 40 0 1 0 0 0 0 0 0 0 0 0 0 0 0']+actions
            # Start the charge from the supported footprint, without walking
            # over an untested ledge toward the target before the charge begins.
            route=[spawn]
            text=['S2SHOT 4','960 720 180',' '.join(map(str,spawn))+f' {yaw} 1 50 0',str(len(route))]
            text+=[' '.join(map(str,p)) for p in route]+[str(len(actions))]+actions+['0']
            shot.write_text('\n'.join(text)+'\n')
            cmd=[str(exe),'--game',str(a.cue),'--slot','17','--play','--cinematic-shot',str(shot),
                 '--smoke','180','--capture',str(capture),'--save-game',str(save),'--menu-profile',str(a.work/'profile.json')]
            result=private.hidden_run(cmd,90)
            assert result.returncode==0,(e['index'],attempt,result.returncode)
            report=json.loads(capture.with_suffix('.json').read_text())
            facts={k:report.get(k) for k in ('broken_containers','released_container_gems','gem_total','retail_script_faults','frames','player_position','heated_vases')}
            assert facts['retail_script_faults']==0 and facts['frames']>=180
            removed=removed_objects(save);success=(e['index'],e.get('target_class',0xc8)) in removed
            target_removed=success
            if a.expect_intact:success=not target_removed and facts['heated_vases']>0
            print('SOURCE FOOTPRINT ATTEMPT',digest,e['index'],attempt,'fixture',approach,'spawn',spawn.tolist(),'target removed',target_removed,'heated C9',facts['heated_vases'],flush=True)
            if success:break
        if not success:
            failures.append(e['index']);reports.append(dict(index=e['index'],source_index=e['source_index'],passed=False,
                                                           reason='finite supported approaches did not open named target',last_report=facts))
            print('SOURCE CONTAINER FAILURE',digest,e['index'],'finite approach fixtures exhausted; continuing other source records',flush=True)
            continue
        if a.expect_intact:
            assert facts['heated_vases']>0
            facts['target_removed_in_private_save']=False
            facts['isolated_target_heated_by_flame_and_intact']=True
        else:
            assert facts['broken_containers']>=1 and facts['released_container_gems']>=1
            facts['target_removed_in_private_save']=True
        facts['passed']=True
        facts.update(index=e['index'],source_index=e['source_index'],source_value=e['value'],attack=attack)
        facts['approach_set']=approach
        reports.append(facts);print('CONTAINER INTERACTION',digest,json.dumps(facts),flush=True)
    assert private.sha(a.cue.with_suffix('.bin'))==digest
    (a.work/'results.json').write_text(json.dumps(reports,indent=2))
    if failures:
        print('PC SOURCE CONTAINER INTERACTIONS FAIL',digest,'tested',len(reports),'failed indices',failures,flush=True)
        raise SystemExit(1)
    print('PC SOURCE CONTAINER INTERACTIONS PASS',digest,len(reports),'flame rejection' if a.expect_intact else 'positive break/release',
          'native common basket donor; subset' if a.only_index is not None else 'all 15 source records;',
          'no manual acceptance; totals may include nearby loose gems',flush=True)


if __name__=='__main__':main()

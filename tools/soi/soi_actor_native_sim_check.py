"""Bind the finite native Game probe and results to exact content identities."""
import argparse
import json
from pathlib import Path
import subprocess

from soi_model_convert import sha


ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('proof',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--sounds',type=Path,required=True);a=ap.parse_args();out=a.out.resolve();assert not out.exists();out.mkdir(parents=True)
    j=json.loads(a.proof.read_text());archive=Path(j['archive']);overlay=Path(j['overlay'])
    assert sha(archive.read_bytes())==j['archive_sha256'] and sha(overlay.read_bytes())==j['overlay_sha256']
    exe=ROOT/'build/soi-content-sol-phase3/soi_actor_native_sim.exe';source=ROOT/'tools/soi/soi_actor_native_sim.cpp'
    command=[str(exe),str(archive),str(overlay),str(out),str(a.sounds.resolve())]
    receipt=dict(archive=str(archive),archive_sha256=j['archive_sha256'],overlay=str(overlay),overlay_sha256=j['overlay_sha256'],
        probe=str(exe),probe_sha256=sha(exe.read_bytes()),probe_source_sha256=sha(source.read_bytes()),
        library=str(ROOT/'build/Release/spyro_core.lib'),library_sha256=sha((ROOT/'build/Release/spyro_core.lib').read_bytes()),command=command,
        fixture='Finite Game/controller/save simulation linked to retained core library; starting health/approaches/Sparx origins and RAM bell-reset checks explicitly marked. No user app/manual acceptance.')
    print('ARTIFACT SLOT17 SHA256',j['archive_sha256'],archive,flush=True);print('ARTIFACT SLOT16 SHA256',j['overlay_sha256'],overlay,flush=True)
    print('NATIVE PROBE SHA256',receipt['probe_sha256'],'SOURCE',receipt['probe_source_sha256'],'LIBRARY',receipt['library_sha256'],flush=True)
    r=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=90)
    print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True)
    receipt['exit']=r.returncode;receipt['output']=r.stdout;receipt['error']=r.stderr
    save=out/'source-quest-complete.s2save'
    if save.exists():receipt['quest_save']=dict(path=str(save),sha256=sha(save.read_bytes()))
    (out/'native-simulation-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    assert r.returncode==0,'Native finite simulation failed; preserved output and receipt'
    assert r.stdout.count('NATIVE CRITTER index ')==6 and r.stdout.count('NATIVE ENEMY CHARGE index ')==3
    assert 'NATIVE SOURCE QUEST PHYSICAL JUMP/FLAME' in r.stdout and 'NATIVE CONTENT SIMULATION CHECK COMPLETE' in r.stdout
    print('HASH-BOUND NATIVE SIMULATION COMPLETE',j['archive_sha256'],j['overlay_sha256'],'receipt',out/'native-simulation-receipt.json',flush=True)


if __name__=='__main__':main()

"""Freeze a tested Phase3 content pair with exact, read log-line receipts."""
import argparse
import json
from pathlib import Path
import shutil
import stat

import soi_model_convert as fmt


ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/soi-content-sol-phase3'


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('proof',type=Path);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();out=a.out.resolve();assert not out.exists()
    j=json.loads(a.proof.read_text());archive=Path(j['archive']);overlay=Path(j['overlay'])
    assert fmt.sha(archive.read_bytes())==j['archive_sha256'] and fmt.sha(overlay.read_bytes())==j['overlay_sha256'] and overlay.stat().st_size%2048==0
    disc=json.loads((BUILD/'disc-actors16/disc-report.json').read_text());assert disc['archive_sha256']==j['archive_sha256'] and disc['overlay_sha256']==j['overlay_sha256']
    specifications=[
        ('static','actor-static16.log','PHASE3 STRUCTURAL CHECK COMPLETE'),
        ('vm','actor-vm16-bound.log','BOUNDED ACTOR ADAPTER CHECK COMPLETE'),
        ('native_all21','actor-native-all21-16.log','NATIVE PHYSICAL INPUT CHECK FINISHED'),
        ('native_damage','actor-native-damage16.log','NATIVE PHYSICAL INPUT CHECK FINISHED'),
        ('native_game_save_critter_bell_charge','actor-native-sim-final16.log','HASH-BOUND NATIVE SIMULATION COMPLETE'),
        ('original_all21','actor-original16-console.log','ORIGINAL ACTOR EXECUTION COMPLETE'),
        ('original_features','actor-original-features16.log','ORIGINAL FEATURE EXECUTION COMPLETE'),
        ('original_render','actor-original-render16.log','ORIGINAL ACTOR CANDIDATE RENDER COMPLETE'),
        ('original_descriptors_after_dma','actor-descriptor-final16.log','ORIGINAL POST1800 DESCRIPTORS EXACT'),
        ('disc','actor-disc16.log','CONTENT DISC READBACK PASS'),
        ('critter_lods','actor-model-2e3-abi14.log','ORIGINAL S2 VERTEX SLICE EQUALITY'),
        ('fan_lods','actor-model-2e5-abi14.log','ORIGINAL S2 VERTEX SLICE EQUALITY'),
        ('bell_lods','actor-model-2e6-abi14.log','ORIGINAL S2 VERTEX SLICE EQUALITY')]
    receipts=[]
    for kind,name,needle in specifications:
        path=BUILD/name;raw=path.read_bytes();lines=raw.decode(errors='replace').splitlines();matches=[dict(line=i,text=s) for i,s in enumerate(lines,1) if needle in s]
        assert matches,('Required finite gate not complete',kind,path)
        assert any(h in raw.decode(errors='replace') for h in [j['archive_sha256'],j['overlay_sha256'],disc['sha256']]),('Unbound gate',kind)
        receipts.append(dict(kind=kind,original_log=str(path),sha256=fmt.sha(raw),evidence=matches))
        for e in matches:print('READ',kind,str(path)+':'+str(e['line']),e['text'],flush=True)
    results=json.loads((BUILD/'native-all21-actors16/results.json').read_text());assert len(results)==21
    assert {e['source'] for e in results}==set(range(20))|{141}
    assert all(e['exit']==0 and e['named_removed'] and e['report']['retail_script_faults']==0 for e in results)
    # All preconditions pass before creating an immutable candidate directory.
    out.mkdir(parents=True);evidence=out/'evidence';evidence.mkdir()
    for r in receipts:
        source=Path(r['original_log']);target=evidence/source.name;shutil.copyfile(source,target);r['immutable_log']=str(target)
        assert fmt.sha(target.read_bytes())==r['sha256']
    copied={}
    for name,path in [('slot17-content.bin',archive),('slot16-overlay.bin',overlay),('model-vram-reservation.npz',Path(j['model_reservation']['path'])),
                      ('editable-source-assets.json',a.proof.parent/'editable-source-assets.json'),('source-quest-binding.json',a.proof.parent/'source-quest-binding.json'),
                      ('overlay-sector-padding.json',a.proof.parent/'overlay-sector-padding.json'),('native-capture-performance.json',a.proof.parent/'native-capture-performance.json'),
                      ('native-pending-script-gem.json',a.proof.parent.parent/'engine-blockers/native-pending-script-gem.json')]:
        target=out/name;shutil.copyfile(path,target);copied[name]=dict(path=str(target),bytes=target.stat().st_size,sha256=fmt.sha(target.read_bytes()))
    sim_receipt=BUILD/'native-sim-final16/native-simulation-receipt.json';shutil.copyfile(sim_receipt,evidence/sim_receipt.name)
    quest_save=BUILD/'native-sim-final16/source-quest-complete.s2save';shutil.copyfile(quest_save,evidence/quest_save.name)
    receipts_path=out/'test-receipts.json';receipts_path.write_text(json.dumps(dict(archive_sha256=j['archive_sha256'],overlay_sha256=j['overlay_sha256'],bin_sha256=disc['sha256'],tests=receipts),indent=2)+'\n')
    j.update(kind='Immutable Phase3 functional content candidate; parent composition/integration pending',archive=str(out/'slot17-content.bin'),overlay=str(out/'slot16-overlay.bin'),
        immutable=True,model_reservation=dict(path=str(out/'model-vram-reservation.npz'),sha256=j['model_reservation']['sha256'],halfwords=7328),
        test_receipts=dict(path=str(receipts_path),sha256=fmt.sha(receipts_path.read_bytes())),
        limits=['Authored source-informed PS1 behavior adapters; not instruction-identical GBA AI or a full source story reconstruction.',
                'Bianca is an expressly authored ambient entrance greeter, without invented dialogue, quest reward or cutscene.',
                'Source148 is the return portal. Bell completion opens its source-position visual; actual full/compact travel trigger binding remains parent/lead work.',
                'Native GameSave rejects an uncollected overlay-spawned gem with UINT32_MAX source_id; precise unowned-engine repro/proposal included.',
                'Development physical slot17 tested. Parent expanded scene14/ordinal29/slots198+199 native integration is outside this worker and remains unverified.',
                'Original CPU hit/health/approach fixtures and native spawn/health fixtures are explicitly identified. No manual gameplay, listening or app acceptance.',
                'Original CPU persistence-list registration is proven; native GameSave write/read/apply is proven. Original memory-card round-trip is not claimed.',
                'No parent stone/music composition or full/compact packaging; no release/launcher/global-ledger promotion.',
                'Source bell resampling/SPU quantization is lossy; source notes/wave/envelope retained, not bit-exact GBA mixer output.',
                'Native capture timing was recorded while private CPU probes ran; frame-budget acceptance requires isolated parent/user validation.'])
    (out/'actor-conversion.json').write_text(json.dumps(j,indent=2)+'\n')
    report=json.loads((a.proof.parent/'mesa-ps1-report.json').read_text());report.update(archive=j['archive'],archive_sha256=j['archive_sha256'],actor_conversion=str(out/'actor-conversion.json'),release_eligible=False)
    # These two values described the phase2 repack and must not masquerade
    # as sizes/counts for this final archive. Keep explicit current units.
    layout=report['archive_layout'];layout.pop('growth',None);layout.pop('model_directory_entries_moved',None)
    layout.update(growth_vs_content04_bytes=archive.stat().st_size-2011136,model_bank_count=46,model_offset_words=48)
    report['phase3']=dict(source_enemies=21,source_critters=3,attached_fans=11,source_bells=3,ambient_greeters=1,total_records=266,
        inactive_standins=14,enemy_reward_value=26,dynamic_pool_guard_bytes=j['retained_dynamic_pool_bytes'],source_proof=str(out/'actor-conversion.json'))
    (out/'mesa-ps1-report.json').write_text(json.dumps(report,indent=1)+'\n')
    for name in ['actor-conversion.json','mesa-ps1-report.json','test-receipts.json']:
        p=out/name;copied[name]=dict(path=str(p),bytes=p.stat().st_size,sha256=fmt.sha(p.read_bytes()))
    helpers={str(p.relative_to(ROOT)):fmt.sha(p.read_bytes()) for p in sorted((ROOT/'tools/soi').glob('soi_actor*.py'))}
    for pattern in ['soi_actor*.cpp','soi_critter*.py','soi_fan*.py','soi_quest*.py']:
        for p in sorted((ROOT/'tools/soi').glob(pattern)):helpers[str(p.relative_to(ROOT))]=fmt.sha(p.read_bytes())
    manifest=dict(immutable=True,release_eligible=False,files=copied,source_proof=str(a.proof.resolve()),archive_sha256=j['archive_sha256'],overlay_sha256=j['overlay_sha256'],
        model_reserved_halfwords=7328,dynamic_pool_guard_bytes=j['retained_dynamic_pool_bytes'],model_count=46,source_enemies=21,source_critters=3,records=266,
        private_disc=disc,helper_sha256=helpers,read_receipts=str(receipts_path),frozen_native_exe_sha256='9a798a70060a14452a73acf117d72cc420b3f13c80124d83de7eba41d3ea5c8c')
    (out/'candidate-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    for p in out.rglob('*'):
        if p.is_file():p.chmod(stat.S_IREAD)
    print('IMMUTABLE PHASE3 CONTENT CANDIDATE',out,'SLOT17',j['archive_sha256'],'SLOT16',j['overlay_sha256'],'RESERVATION',j['model_reservation']['sha256'],flush=True)


if __name__=='__main__':main()

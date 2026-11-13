"""Build a private PS1 review patcher while native expansion remains incomplete.

This does not call or weaken the production release gate, mark an archive release
eligible, install an executable, update a launcher, retire anything or write dist.
The GUI/backend roundtrip is independently useful evidence for lead review. Its
unfinished native mode is named in the GUI, output filename and feature manifest.
"""
import argparse
import json
import shutil
from pathlib import Path

import package_patcher as package


ROOT=Path(__file__).resolve().parents[2]
TASK=Path('D:/Theia-staging/spyro2-mod/work/soi-content-20261105')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--disc-report',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--encoded',type=Path,help='Existing private xdelta; its decoded full target is independently hash-checked again')
    a=ap.parse_args();work=a.work.resolve()
    assert work.is_relative_to((ROOT/'build').resolve()) or work.is_relative_to(TASK.resolve())
    assert not work.exists(),'new private review directory required'
    disc=json.loads(a.disc_report.read_text());image=Path(disc['image'])
    identity=package.sha(image);assert identity==disc['sha256'] and disc['fixture'] is False
    proof=json.loads(a.evidence.read_text());assert proof['target_sha256']==identity and proof['release_eligible'] is False
    roles={'renderer','portal','content','music_runtime','music_score','music_presence','expanded_static'}
    assert set(proof['logs'])==roles and proof['unfinished_features']
    copied=[]
    for role,row in proof['logs'].items():
        p=Path(row['path']);raw=p.read_bytes();assert package.sha(p)==row['sha256']
        lines=raw.decode(errors='replace').splitlines();assert row['markers']
        for marker in row['markers']:
            hits=[(i,s) for i,s in enumerate(lines,1) if marker+' '+identity in s]
            assert len(hits)==1,(role,marker)
            copied.append(dict(role=role,path=str(p),line=hits[0][0],text=hits[0][1],sha256=row['sha256']))
    assert package.sha(package.RETAIL_BIN)==package.RETAIL_ID['sha256']
    decoder=package.FRAMEWORK/'tools/xdelta3.exe';assert package.sha(decoder)==package.DECODER_SHA
    work.mkdir(parents=True);name='spyro2-soi-marketmesa-expanded-PS1-REVIEW-20261105'
    stage=work/name;stage.mkdir();patchfile=stage/'market-mesa-PS1-review.xdelta'
    package.say('PRIVATE REVIEW TARGET',identity,str(image),'release_eligible=false; PC expansion incomplete')
    if a.encoded:shutil.copyfile(a.encoded,patchfile)
    else:
        result=package.run_limited([str(decoder),'-e','-9','-A','-S','lzma','-s',str(package.RETAIL_BIN),str(image),str(patchfile)],900,capture_output=True,text=True)
        assert result.returncode==0,result.stderr
    target=dict(bytes=image.stat().st_size,sha256=identity)
    rt=package.roundtrip(decoder,patchfile,target,900)
    patch=dict(file=patchfile.name,bytes=patchfile.stat().st_size,sha256=package.sha(patchfile),format='VCDIFF xdelta3 3.2.0; original USA retail source')
    for source,name_ in [(package.BACKEND,package.SCRIPT),(package.GUI,package.EXE)]:shutil.copyfile(source,stage/name_)
    (stage/package.BAT).write_text(package.BAT_TEXT.format(script=package.SCRIPT),encoding='ascii',newline='\r\n')
    for filename in package.RUNTIME:shutil.copyfile(package.FRAMEWORK/filename,stage/filename)
    for folder in ('tools','licenses'):shutil.copytree(package.FRAMEWORK/folder,stage/folder)
    shutil.copyfile(package.FRAMEWORK/'THIRD-PARTY.md',stage/'THIRD-PARTY.md')
    shutil.copytree(package.GUI_SRC,stage/'source');shutil.copyfile(package.BACKEND,stage/'source'/package.SCRIPT)
    shutil.copyfile(stage/'source/imgui/LICENSE.txt',stage/'licenses/IMGUI-MIT.txt')
    v=stage/'verification';v.mkdir()
    shutil.copyfile(package.FRAMEWORK/'verification/xdelta-provenance.json',v/'xdelta-provenance.json')
    for role,row in proof['logs'].items():shutil.copyfile(row['path'],v/(role+'.log'))
    feature=dict(proof,kind='PRIVATE PS1 REVIEW; native expanded-level mode incomplete',citations=copied)
    (v/'feature-manifest.json').write_text(json.dumps(feature,indent=2)+'\n')
    shutil.copyfile(a.disc_report,v/'disc-report.json');(v/'backend-roundtrip.json').write_text(json.dumps(rt,indent=2)+'\n')
    gui=package.texts('PRIVATE PS1 REVIEW - PC expansion incomplete',round(target['bytes']/2**20),True)
    gui.update(window='Market Mesa - PRIVATE PS1 REVIEW - PC expansion incomplete',
        heading='MARKET MESA - PS1 REVIEW',tag='PRIVATE REVIEW | Native PC expansion incomplete',
        output_name='Spyro2-MarketMesa-PS1-REVIEW-PC-EXPANSION-INCOMPLETE.bin',
        summary='Adds Market Mesa through a dedicated Summer Forest portal and preserves original Glimmer. This private review build targets PS1; expanded PC gameplay is still being integrated.',
        play_instructions='PS1 review: open the new CUE in your PS1 setup. Native PC expanded-level gameplay remains incomplete; keep your working PC build.',
        step5='5. Open the new CUE in your PS1 setup. Start normally in Glimmer, then visit Summer Forest and enter the new Market Mesa portal. Expanded native PC gameplay is incomplete; retain your current working PC build.')
    manifest=dict(schema=1,name='Market Mesa - PRIVATE PS1 REVIEW - PC EXPANSION INCOMPLETE',
        game="Spyro 2: Ripto's Rage",edition='USA SCUS-94425 NTSC BIN/CUE',source=package.RETAIL_ID,target=target,patch=patch,
        decoder=dict(file='tools/xdelta3.exe',version='3.2.0',sha256=package.DECODER_SHA),gui=gui,
        release_eligible=False,unfinished_features=proof['unfinished_features'],features=proof.get('finished_features',{}))
    (stage/'patch.json').write_text(json.dumps(manifest,indent=2)+'\n')
    notes='\n'.join('- '+s for s in proof['unfinished_features'])
    (stage/'README.md').write_text(f'''# Market Mesa: private PS1 review patcher

This is a private review artifact. Native PC expanded-level gameplay remains incomplete.
The normal release packager remains gated; no installed game or launcher is replaced.

Use your unmodified USA SCUS-94425 BIN/CUE. The patcher creates a separate BIN/CUE,
checks the entire source and output hashes, and preserves original Glimmer and the
other original levels. Start normally, visit Summer Forest and enter the new portal.
Normal ability purchases and Return Home apply here. The separate Mesa-only ROM's
menu bypass, unlocked learned skills and travel restrictions do not apply to this patch.

Target SHA-256: `{identity}`; target bytes: {target['bytes']}.
Both the GUI and script must reproduce this exact target; receipts are retained outside
this ZIP in the private packaging workspace. No original game image is included.

Unfinished work and evidence limits:

{notes}

Exact original-engine automated logs, feature limits and source-bound citations are
in verification/. Manual gameplay and art acceptance belong to the user.
''',encoding='utf-8')
    sums=[f'{package.sha(p)} *{p.relative_to(stage).as_posix()}' for p in sorted(stage.rglob('*')) if p.is_file()]
    (stage/'SHA256SUMS.txt').write_text('\n'.join(sums)+'\n')
    archive=work/(name+'.zip');archive_sha=package.make_zip(stage,archive,(2026,11,5,0,0,0))
    e2e=package.end_to_end(archive,name,work,target,900,'PRIVATE PS1 REVIEW')
    receipt=dict(kind='Private packaging verification only; not release/gameplay approval',release_eligible=False,
        zip=str(archive),zip_sha256=archive_sha,target=target,patch=patch,roundtrip=rt,end_to_end=e2e,
        unfinished_features=proof['unfinished_features'],evidence_sha256=package.sha(a.evidence))
    rp=work/'review-package-verification.json';rp.write_text(json.dumps(receipt,indent=2)+'\n')
    package.say('PRIVATE REVIEW PATCHER REPRODUCTION PASS',identity,'GUI and script exact target; ZIP',archive_sha,'receipt',str(rp),'not promoted')


if __name__=='__main__':main()

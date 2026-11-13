"""Build reproducible shadow/jar snapshot packages from the installed main."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile,sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'build/character-shadows-20261105/packages'
READY=ROOT/'build/character-shadows-20261105/ready.json'
if '--quality-r2' in sys.argv:
    WORK=ROOT/'build/shadow-quality-20261105/packages'
    READY=ROOT/'build/shadow-quality-20261105/ready.json'

def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    WORK.mkdir(parents=True,exist_ok=True)
    ready=json.loads(READY.read_text());exe=Path(ready['main_exe']);h=ready['sha256']
    assert ready['ready'] and sha(exe)==h
    for record in ready['logs']:
        p=Path(record['path']);assert sha(p)==record['sha256']
        assert record['text'] in p.read_text().splitlines()[record['line']-1]
    packages=[]
    for base in ('spyro2-pc-20261105','spring-realm-20261105'):
        name=base+ready.get('package_suffix','-shadows');folder=ROOT/'dist'/name
        assert not folder.exists(),folder
        shutil.copytree(ROOT/'dist'/base,folder)
        shutil.copy2(exe,folder/'spyro_workbench.exe')
        old_info=json.loads((folder/'BUILD-INFO.json').read_text())
        readme=(folder/'README.txt').read_text().replace(old_info['exe_sha256'],h)
        start=readme.index('REFRESH 5 NOVEMBER 2026')
        end=readme.index('\nA native C++',start) if '\nA native C++' in readme[start:] else readme.index('\n\n',readme.index('Manual gameplay',start))
        refresh='''REFRESH 5 NOVEMBER 2026 - CHARACTER SHADOWS AND LIFE JARS
-------------------------------------------------------
Characters, including Spyro, now cast animated mesh silhouette shadows onto
opaque terrain. Wings, tail and the current pose affect the silhouette. These
are PC terrain-projected shadows, not pixel-identical PS1 radial shadows.

Life butterflies stay in their jars while the jars are intact. Breaking a jar
releases its butterfly through the original game script and awards one life
when it is eaten. The native food system no longer takes caged butterflies.

Automated checks on this executable: 226 character-shadow / life-jar checks,
including all 23 retail life jars kept intact for 1,908 VBlanks each, followed
by release and exactly one life award; 46,894 companion and 41 interaction
checks. Installed-main hidden captures show shadows and a normal charge
breaking a jar, restoring full health with zero script faults.

Preserved from the prior snapshot: dialogue-loop fixes, Zoe's corrected
sparkle/zap, Cloud Temples, the saved camera recentre delay (default 4 seconds),
PS1 FOV/HUD choices, and the existing launchers. The prior snapshot's broader
42-test / 20-challenge / 18-exit results are historical baseline evidence;
they are not claimed here as reruns on this new executable. Current evidence
and any later regression results are in VALIDATION.txt / BUILD-INFO.json.
Manual gameplay remains for the player.
'''
        if ready.get('shadow_quality'):
            refresh=refresh.replace('by release and exactly one life award; 46,894 companion and 41 interaction\nchecks.', 'by release and exactly one life award.').replace('and any later regression results are in VALIDATION.txt / BUILD-INFO.json.', 'and any later regression results are in VALIDATION.txt / BUILD-INFO.json.\nThe prior 46,894 companion and 41 interaction checks are baseline evidence;\nthis R2 build reruns the focused 226 renderer / jar checks.')
            refresh+='\nSHADOW QUALITY R2: 4096 x 4096 map (twice the resolution in each axis),\nwith bilinear depth-comparison filtering and a weighted edge filter. Coverage\nand the animated character silhouettes are unchanged.\n'
        (folder/'README.txt').write_text((readme[:start]+refresh+readme[end:]).replace(base+'-blender',name+'-blender'))
        validation=['Executable SHA-256: '+h,'Automated evidence for this exact installed main:']
        for r in ready['logs']:validation.append(f"{r['path']}:{r['line']}: {r['text']}")
        validation+=['','Limitations:',*ready['limitations'],'','Package extraction/hash and hidden render smoke results are retained in the source workspace under build/character-shadows-20261105/packages/.']
        (folder/'VALIDATION.txt').write_text('\n'.join(validation)+'\n')
        info=dict(date='2026-11-05',feature_update='character shadows and caged life butterflies',exe_sha256=h,validation=ready,files={})
        info['files']={p.relative_to(folder).as_posix():sha(p) for p in sorted(folder.rglob('*')) if p.is_file() and p.name not in ('BUILD-INFO.json','disc_path.txt')}
        extras=[]
        source=ROOT/'dist'/(base+'-blender')
        if source.exists():
            extra=folder.parent/(name+'-blender');assert not extra.exists();shutil.copytree(source,extra);extras.append(extra)
            info['supplementary_files']={p.relative_to(folder.parent).as_posix():sha(p) for p in sorted(extra.rglob('*')) if p.is_file()}
        (folder/'BUILD-INFO.json').write_text(json.dumps(info,indent=2))
        archive=folder.parent/(name+'.zip')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for tree in [folder,*extras]:
                for p in sorted(tree.rglob('*')):
                    if p.is_file() and p.name!='disc_path.txt':z.write(p,p.relative_to(folder.parent))
        digest=sha(archive);archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n')
        unpack=WORK/'unpacked'/name;assert not unpack.exists()
        with zipfile.ZipFile(archive) as z:
            assert all((unpack/m).resolve().is_relative_to(unpack.resolve()) for m in z.namelist())
            z.extractall(unpack)
        deployed=unpack/name;assert sha(deployed/'spyro_workbench.exe')==h
        for rel,expected in info['files'].items():assert sha(deployed/rel)==expected
        for rel,expected in info.get('supplementary_files',{}).items():assert sha(unpack/rel)==expected
        result=dict(path=str(archive),bytes=archive.stat().st_size,sha256=digest,exe_sha256=h,unpacked=str(deployed),members_verified=len(info['files'])+1+len(info.get('supplementary_files',{})))
        packages.append(result);print('PACKAGE HASH PASS',json.dumps(result),flush=True)
    (WORK/'packages.json').write_text(json.dumps(packages,indent=2))
    smoke(packages,h)

def smoke(packages,h):
    disc=ROOT/'Spyro 2 - Riptos Rage (USA)'/"Spyro 2 - Ripto's Rage! (USA).cue";assert disc.exists()
    for p in packages:
        game=Path(p['unpacked']);jobs=[('spring',['--custom-level',str(game/'levels/spring-realm/pc/spring-realm.s2world'),'--play'])]
        if game.name.startswith('spyro2'):jobs=[('summer-forest',['--slot','15','--play']),('glimmer',['--slot','17','--play']),('title',['--main-menu'])]
        for label,flags in jobs:
            capture=WORK/(game.name+'-'+label+'.bmp');log=capture.with_suffix('.log')
            command=[str(game/'spyro_workbench.exe'),'--game',str(disc),*flags,'--smoke','300','--headless','--menu-profile',str(WORK/(game.name+'-'+label+'-profile.json')),'--capture',str(capture)]
            with log.open('w') as f:
                f.write('EXE SHA256 '+h+'\nCOMMAND '+json.dumps(command)+'\n');f.flush()
                run=subprocess.run(command,cwd=game,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
                assert run.returncode==0,(label,run.returncode,log)
                report=json.loads(capture.with_suffix('.json').read_text());assert report['frames']==300 and report['hardware_renderer']
                if label=='title':assert report['main_menu'] and report['title_actors']>0
                else:
                    assert report['simulation_ticks']>=290 and report['spyro_vertices']>0
                    assert report['retail_script_faults']==0 and report['triangles']>1000 and report['audio_music_frames']>0
                    assert report['rendered_character_shadows']>0 and report['rendered_shadow_triangles']>0
                Image.open(capture).save(capture.with_suffix('.png'))
                f.write('PACKAGE SMOKE PASS '+label+'\n')
            print('PACKAGE SMOKE PASS',game.name,label,flush=True)
    (WORK/'summary.json').write_text(json.dumps(dict(passed=True,exe_sha256=h,packages=packages,smokes=4),indent=2))
    print('SHADOW SNAPSHOT PACKAGES PASS 2 packages / 4 hidden smokes',flush=True)

if __name__=='__main__':
    if '--smoke-only' in sys.argv:
        packages=json.loads((WORK/'packages.json').read_text())
        for p in packages:assert sha(p['path'])==p['sha256'] and sha(Path(p['unpacked'])/'spyro_workbench.exe')==p['exe_sha256']
        smoke(packages,packages[0]['exe_sha256'])
    else:main()

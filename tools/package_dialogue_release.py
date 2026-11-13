"""Refresh the two existing PC packages from a validated private main build.

No downloads, game-image modifications, or deletion. Existing packages remain
available. The Spring Realm source and previews are copied from its last ship.
"""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/dialogue-20261105'
DIST = ROOT / 'dist'
DATE = '20261105'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, text):
    path.write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))


def main():
    expected = json.loads((WORK / 'artifacts.json').read_text())['spyro_workbench.exe']
    exe = WORK / 'bin/spyro_workbench.exe'
    assert sha(exe) == expected == sha(ROOT / 'build/Release/spyro_workbench.exe')
    assert '100% tests passed, 0 tests failed out of 42' in (WORK / 'logs/ctest.log').read_text()
    assert 'desktop 38/38 PASS' in (WORK / 'logs/desktop-summary.log').read_text()
    for visit in ('first', 'revisit'):
        for answer in ('yes', 'no'):
            name = f'talk-{visit}-{answer}'
            assert f'{name} PASS' in (WORK / 'logs' / (name + '.log')).read_text()
    refresh = '''REFRESH 5 NOVEMBER 2026
-----------------------
Fixed the repeated talisman conversation that could hold Spyro after talking to
Twitchy in Glimmer, including on a revisit with the talisman already collected.
The reward can play on a revisit without adding a second talisman.

Preserved: Zoe's wand sparkle correction; Cloud Temples' freeze shots, whirlwind
and bells; the 4-second camera recentre default and saved 0 (never) / 1-10-second
options in Pause > Options and the workbench; PS1 FOV and PS1 HUD options.

Automated checks on this executable: 42 CTest tests, 20 orb challenges in both the
headless runner and main application, and all 18 main-application level exits.
The dialogue sweep opened 127 conversations per first/revisit and Yes/No run,
with zero locks. Idol Springs' Triangle checks opened five conversations on each
visit, with zero locks. Other candidate records did not open in the starting
state; quest-dependent branches are not all verified. Main-application challenge
and exit checks use recorded fixtures (including teleports / simulated end
conditions). Manual gameplay and visual inspection are left to the player.

'''
    validation = '''Automated evidence is retained in the source workspace under
build/dialogue-20261105/. Every job log records the exact main SHA-256.
logs/ctest.log:90: 100% tests passed, 0 tests failed out of 42
logs/desktop-summary.log:2: desktop 38/38 PASS (20 challenges, 18 exits)
logs/challenges-headless.log:84: 0 failed (20 challenges)
logs/talk-first-yes.log:392: 261 candidates, 127 talked, 0 locks
logs/talk-first-no.log:392: 261 candidates, 127 talked, 0 locks
logs/talk-revisit-yes.log:392: 261 candidates, 127 talked, 0 locks
logs/talk-revisit-no.log:392: 261 candidates, 127 talked, 0 locks
logs/idol-triangle-first.log:18: 9 candidates, 5 talked, 0 locks
logs/idol-triangle-revisit.log:18: 9 candidates, 5 talked, 0 locks

Failure retained: logs/exits-headless.log:244: exits-headless FAIL.
The legacy standalone probe does not satisfy several level-end prerequisites.
Its aggregate logs/headless-summary.log:2 reports 7/8 FAIL. This is not a passing
exit gate. The 18 main-application exits are the release evidence; their JSON
reports record simulated_end and other fixtures. No manual play or visual
validation is claimed for this release.
'''
    packages = []
    for old, name in [('spyro2-pc-20261104c', f'spyro2-pc-{DATE}'),
                      ('spring-realm-20261103', f'spring-realm-{DATE}')]:
        target = DIST / name
        if target.exists():
            raise RuntimeError(f'Refusing to overwrite existing {target}')
        shutil.copytree(DIST / old, target, ignore=shutil.ignore_patterns('disc_path.txt'))
        shutil.copy2(exe, target / exe.name)
        for runtime in (DIST / 'spyro2-pc-20261104c').glob('*.dll'):
            shutil.copy2(runtime, target / runtime.name)
        text = (target / 'README.txt').read_text(encoding='utf-8')
        previous_sha = '274aab5e4ecc853eee1e95e51f2d4736539500a2b277d580f3a5f2f1f228f067'
        text = text.replace(old, name).replace(previous_sha, expected)
        text = text.replace('(snapshot 20261104c)', '(snapshot 20261105)').replace('PC (20261103)', 'PC (20261105)')
        if old.startswith('spyro2-pc'):
            start = text.index('- KNOWN ISSUE: some NPCs,')
            end = text.index('- Orb challenges not working yet:', start)
            text = text[:start] + text[end:]
        else:
            text = text.replace('spring-realm-20261103-blender', f'spring-realm-{DATE}-blender')
            text = text.replace('  README.txt, THIRD-PARTY-NOTICES.txt',
                                '  msvcp140.dll, msvcp140_atomic_wait.dll, vcruntime140.dll, vcruntime140_1.dll\n'
                                '  README.txt, THIRD-PARTY-NOTICES.txt (app-local Microsoft VC++ runtime included)')
        first_break = text.index('\n\n') + 2
        text = text[:first_break] + refresh + text[first_break:]
        write(target / 'README.txt', text)
        write(target / 'VALIDATION.txt', validation + f'\nExecutable SHA-256: {expected}\n')
        files = {p.relative_to(target).as_posix(): sha(p) for p in sorted(target.rglob('*')) if p.is_file()}
        (target / 'BUILD-INFO.json').write_text(json.dumps({'date': DATE, 'exe_sha256': expected,
                                                         'files': files}, indent=2) + '\n')
        sources = [target]
        if old.startswith('spring'):
            blender = DIST / f'{name}-blender'
            shutil.copytree(DIST / f'{old}-blender', blender)
            readme = (blender / 'README.txt').read_text(encoding='utf-8').replace(f'{old}.zip', f'{name}.zip')
            write(blender / 'README.txt', readme)
            sources.append(blender)
        path = DIST / (name + '.zip')
        with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for source in sources:
                for item in sorted(source.rglob('*')):
                    if item.is_file():
                        archive.write(item, item.relative_to(DIST).as_posix())
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            assert hashlib.sha256(archive.read(f'{name}/spyro_workbench.exe')).hexdigest() == expected
            assert not any(Path(p).suffix.lower() in {'.iso', '.bin', '.cue', '.wad', '.str', '.raw', '.pdb'} for p in archive.namelist())
        digest = sha(path)
        path.with_name(path.name + '.sha256').write_text(f'{digest} *{path.name}\n', encoding='ascii')
        packages.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest, 'exe_sha256': expected})
        print(f'PACKAGE {name} SHA256 {digest} bytes {path.stat().st_size} CRC and exe identity PASS', flush=True)
    (WORK / 'packages.json').write_text(json.dumps(packages, indent=2) + '\n')


if __name__ == '__main__':
    main()

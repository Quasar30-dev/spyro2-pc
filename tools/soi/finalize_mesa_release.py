"""Write the verified test05 launcher, task documentation and audit handoff."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/'data/soi/ps1'
STAGE=Path('D:/Theia-staging/spyro2-mod')
PACKAGE=Path('C:/Users/Shadow/Documents/Theia/dist/spyro2-soi-marketmesa-test05-20261105.zip')


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def evidence(path,marker,identity=None):
    lines=path.read_text().splitlines()
    matches=[i+1 for i,line in enumerate(lines) if marker in line and (not identity or identity in line)]
    assert len(matches)==1,(path,marker,matches)
    return dict(path=str(path),line=matches[0],text=lines[matches[0]-1])


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('-o','--output',type=Path,required=True)
    a=ap.parse_args()
    disc=json.loads((DATA/'mesa-disc-report.json').read_text())
    image=Path(disc['image']); digest=sha(image); archive=Path(disc['archive'])
    assert digest==disc['sha256'] and sha(archive)==disc['archive_sha256']
    receipt=json.loads((STAGE/'patcher'/f'{PACKAGE.stem}-verify.json').read_text())
    package_sha=sha(PACKAGE)
    assert receipt['zip_sha256']==package_sha and receipt['target']['sha256']==digest
    assert all(receipt['end_to_end'][key]['sha256']==digest for key in ('gui','script'))
    runtime=Path((DATA/'runtime-test05-review-console.log').read_text().splitlines()[0][4:])
    ev={}
    for key,path,marker,identity in (
        ('static',DATA/'static-test05.log','STATIC CHECK PASS',None),
        ('disc',DATA/'disc-test05.log','DISC BUILD PASS',None),
        ('oracle',DATA/'oracle-test05.log','ORACLE STRUCTURAL CHECK PASS',None),
        ('consumer',DATA/'moneybags-oracle-test05.log','CONSUMER DATA SAFETY PASS',disc['archive_sha256']),
        ('loader',runtime,'MONEYBAGS LOADER PASS',digest),
        ('purchase',runtime,'MONEYBAGS PURCHASED FLAG PASS',digest),
        ('gems',runtime,'GEM RUNTIME PASS',digest),
        ('visibility',runtime,'VISIBILITY RUNTIME PASS',digest),
        ('far',runtime,'FAR RENDERER RUNTIME PASS',digest),
        ('gpu',runtime,'FAR GPU EMISSION PASS',digest),
        ('ticks',runtime,'FINITE TICKS PASS',digest),
        ('inert_final',runtime,'MONEYBAGS FINAL RECORD PASS',digest),
        ('runtime',runtime,'PRIVATE CHECK PASS',digest),
        ('pc',DATA/'pc-test05.log','PC DISC LOAD/CAPTURE PASS',digest),
        ('gui',DATA/'patcher-test05.log','END TO END GUI PASS',digest),
        ('script',DATA/'patcher-test05.log','END TO END SCRIPT PASS',digest),
        ('package',DATA/'patcher-test05.log','PATCHER PASS',package_sha),
        ('integrity',DATA/'final-integrity-test05.log','RELEASE INVARIANTS PASS',digest),
        ('test02',DATA/'baselines-test05.log','BASELINE RESTORE PASS','2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb'),
        ('test03',DATA/'baselines-test05.log','BASELINE RESTORE PASS','60680946e3be8fe3546c4173909ae8a31b54e9316935a0a3e105496d7e68308c')):
        ev[key]=evidence(path,marker,identity)
    for p,identity in ((DATA/'static-test05.log',disc['archive_sha256']),
                       (DATA/'oracle-test05.log',disc['archive_sha256'])):
        assert identity in p.read_text().splitlines()[0]
    assert digest in (DATA/'disc-test05.log').read_text()
    def cite(key):
        e=ev[key]; p=Path(e['path']).relative_to(ROOT).as_posix()
        return f'`{p}:{e["line"]}`'
    launcher=ROOT/'Play Market Mesa (Season of Ice).cmd'
    source=launcher.read_bytes()
    assert b'Spyro2-SoI-MarketMesa-test04.cue' in source
    launcher.write_bytes(source.replace(b'test04',b'test05').replace(
        b'0da66c9fe609476aa749bb925514607f12e702f548174b46a66bc2e270da0f71',digest.encode('ascii')))
    docs=ROOT/'docs/SEASON_OF_ICE_PORT.md'
    title='### 10.13 Market Mesa test05 release review (5 Nov 2026)'
    assert title not in docs.read_text()
    text=f'''

{title}

Current clean image: `{image.as_posix()}` and matching CUE. BIN SHA-256
`{digest}`; CUE `{sha(image.with_suffix('.cue'))}`.
Slot-17 archive `{archive.as_posix()}`, SHA-256 `{disc['archive_sha256']}`.
Patcher: `{PACKAGE.as_posix()}`, {PACKAGE.stat().st_size:,} bytes, SHA-256
`{package_sha}`. Launcher is pinned to test05 and retains the optional CUE argument.
Disc readback and untouched other WAD slots: {cite('disc')} (image/archive identities at lines 4/6).
Package GUI/script exact-output checks: {cite('gui')}, {cite('script')}; package identity: {cite('package')}.

**Distant rendering and visibility inherited from the reviewed test04 implementation.**
Far terrain contains exactly 5,246 selected near terrain faces, preserving their positions, winding and material B
colours; decor stays near-only. Removed spatial averaging and material-blind colour sharing. There are 7,364 near
faces, 142 regions, 62 materials/30 texture slots, and 10,320 collision triangles. Per-region far words peak at 254,
within the original 255-word limit. Full 32x32 texture descriptors and all 16 depth CLUT rows now follow the original
consumer layout. Static evidence: {cite('static')} (identity line 1; far equality line 17, limits line 13).
The original visibility consumer accepts the conservative all-visible tree at 266 tested region/cell positions
(static log line 15); the real loader enables distance 0x2c000 ({cite('visibility')}).
Retail comparison: {cite('oracle')} (candidate identity line 1); unchanged near polygons and no displaced far vertices
at line 5. Runtime original far-record selection and POLY_G4 emission: {cite('far')}, {cite('gpu')}.

**Moneybags safety correction (the only gameplay-byte change from final test04).**
Parking class 0x220 was unsafe: original Glimmer `8007ded0` checks timer `80067008 < 10` and purchase halfword
`8006467e != 0` before its state switch, calling terrain-animation group 1 at `8007df00/8007df10` even far from Spyro.
The seven animation sections remain empty, so distance is insufficient. Keep Moneybags at original index 9/type 0x220,
but set object +0x48 to **0xfd**, the original initial-table removed state written by `80041384/8004138c`.
`80040ef0/80040ef8` skips negative states and continues past 0xfd; only 0xff ends the table. Glimmer's dispatcher
`8007043c/80070444/80070448` also skips states >=0x80 before any class handler. Never put 0xff inside the table.
The loader loop and original helpers `80041438`, `80041524`, `8003ee3c`, `80041ab8` were inspected; 80041ab8 only
stores object +0x1c. Original byte signatures and disassembly: {cite('consumer')} (oracle hashes at lines 2/3).
Static enforcement of state 0xfd and no internal sentinel: `data/soi/ps1/static-test05.log:20`.
All 201 original indices/types and parameter pointers remain; 19 appended gems give 220 records with separate
24-byte parameters. All 110 SoI loose gems use their source denominations: 56x1 + 37x2 + 12x5 + 5x10 = 240
(static lines 19, 21-25; runtime {cite('gems')}).

The real loader was stopped at `8001d2d0`: Moneybags was still 0xfd, index 219 had been initialized, and the terminal
0xff followed record 220 ({cite('loader')}, full runtime records retained). The purchased flag was then set in private
RAM **before the first actor update**, with timer zero. A further 12 finite ticks reset that timer to zero each tick;
the breakpoint at `8007ded0` was never reached and the exact record stayed 0xfd ({cite('purchase')}).
The same private run completed 1,800 further logic ticks ({cite('ticks')}); the final record remained inactive
({cite('inert_final')}); complete run: {cite('runtime')}. This covers the original purchased-bridge trigger with a
RAM fixture, not every possible old-save quest combination or a complete saved-game playthrough.
An initial attempt checked volatile +0x4d after gameplay changed it and stopped on a harness assertion; its retained
`data/soi/ps1/runtime-test05-console.log` is not release evidence. The corrected check observes the actual loader boundary.

**Restored and protected baselines.** The earlier cleanup incorrectly removed explicitly preserved test02/test03.
test02 was rebuilt from local `work/slot17-mesa-test02.bin` (archive SHA-256
`b7150b290b88ff15a9efe555f12261bced9e77a64f6f872606d5ab7640e4df8b`); test03 was extracted from the retained local RAR.
Exact restored identities: {cite('test02')}, {cite('test03')}:

| Baseline | BIN SHA-256 | CUE SHA-256 |
|---|---|---|
| test02 | `2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb` | `478e39bf4639a6750953970c56759e621fccb9791fa9f0aad9f3f0fc983a1da4` |
| test03 WIP | `60680946e3be8fe3546c4173909ae8a31b54e9316935a0a3e105496d7e68308c` | `1074a4ebc2b4d475c027e204a7b1ca01ea499019aef52a60d591b6678f39aba5` |

The pinned CUEs use CRLF bytes; they were restored to the pinned hashes. Both recovery snapshot directories and
the test03 RAR remain. test01 stays absent. The old test02 patcher ZIP remains retired; it was not restored.
`retire_mesa.py` now requires explicit paths, rejects test02/test03 (including patchers), validates the newest
image/package receipt, and retains in-use groups using Win32 sharing guards. Only `safe_delete.py` performs deletion.
Superseded test04 BIN/CUE/ZIP cleanup is recorded in `data/soi/ps1/retirement-test05.log`.

**Release integrity and remaining limits.** Patcher verification reads slot 17 from the exact target BIN, regenerates
hash-bound static evidence, requires completed matching runtime checks, and embeds the original-consumer and PC logs.
No stale PASS text is accepted. It was packaged with `--no-retire` and both extracted GUI/script paths rebuilt the
exact final image before cleanup. Preservation/baseline/package invariants: {cite('integrity')}; its line 2 proves
test05 differs from final test04's archive by only byte 0x1e9de4 (0 -> 0xfd).
PC disc-load evidence is {cite('pc')}: four automatic cameras on a separate hidden desktop, 110 gems/220 objects,
10,320 collision triangles and zero script faults. It uses a task-private copy of `build-soi/spyro_workbench.exe`
SHA-256 `5055ed71f26bee91fd4d6b56c14ecc27d3a8596ef53e21b4146a9c1b855fbf8d`, not a rebuilt executable.
The current main executable, retail executable and v2g source mesh/world hashes are unchanged (integrity log lines 3-7).
No new character imports, downloads, commit, push, manual gameplay/UI testing, or operation of a user-owned running app.

PS1 LP terrain is inherently untextured vertex-colour POLY_G4, so a flat-looking distant surface is expected.
Decor remains absent at far range; separate background mesas are still omitted. CLUT layout is corrected, but the fade
formula is a **fit**, not exact recovered generation: 98.8995% of measured RGB5 channels match on rows 2..14 across
443 retail materials, maximum channel error 14 (`data/soi/ps1/oracle-test05.log:14`). 641 partial-rectangle faces still
fall back to whole tiles. Containers retain retail basket contents; 14 of 21 source enemy positions have retail
lizard stand-ins. Ice barrier/critters, Bianca, Grendor and new Rhynoc models remain unfinished. Manual visual and
gameplay acceptance belongs to the user; automated captures do not establish it.
'''
    original_docs=docs.read_bytes()
    docs_nl='\r\n' if b'\r\n' in original_docs else '\n'
    docs.write_bytes(original_docs+text.replace('\n',docs_nl).encode('utf-8'))
    changelog=ROOT/'CHANGELOG.md'; current=changelog.read_bytes()
    header='## 2026-11-05 - Market Mesa test05 release review (Codex, solo)'
    assert header.encode('utf-8') not in current
    entry=f'''{header}

- Fixed the purchased-bridge regression by retaining Moneybags at original index 9 with retail's inactive state
  0xfd. Original consumer evidence: {cite('consumer')}; real loader and targeted purchase fixture:
  {cite('loader')}, {cite('purchase')}; final inactive record after 1,800 ticks: {cite('inert_final')}.
- Retained exact far terrain (5,246 faces), enabled retail 0x2c000 visibility, and kept all 110 SoI loose gems/240 value.
  Static/archive evidence: {cite('static')}; clean disc readback: {cite('disc')}; private runtime: {cite('runtime')}.
- Restored clean test02 and test03 WIP to their exact BIN/CUE hashes ({cite('test02')}, {cite('test03')}).
  Cleanup requires explicit paths and protects those baselines; recovery snapshots and the user's RAR remain.
  test01 and the old test02 ZIP remain retired. Superseded test04 cleanup: `data/soi/ps1/retirement-test05.log`.
- Shipped `{PACKAGE.name}`, SHA-256 `{package_sha}`; target BIN `{digest}`.
  Extracted hidden-desktop GUI/script exact-output checks: {cite('gui')}, {cite('script')}; package {cite('package')}.
  Launcher pinned to test05. Exact unchanged source/executable and baseline evidence: {cite('integrity')}.
- Automated PC load/captures only: {cite('pc')}. No main executable/source edits or manual acceptance.
  Far props/background mesas remain absent; LP is untextured; CLUT fading is fitted, 641 faces retain whole-tile
  fallback, containers retain retail contents, and 14/21 enemy positions use retail stand-ins. Full limits in §10.13.

'''
    prefix='# Changelog\n\nThis folder is not a git repository; changes made by agents are recorded here.\n\n'
    change_nl='\r\n' if b'\r\n' in current else '\n'
    prefix_bytes=prefix.replace('\n',change_nl).encode('utf-8')
    assert current.startswith(prefix_bytes)
    changelog.write_bytes(prefix_bytes+entry.replace('\n',change_nl).encode('utf-8')+current[len(prefix_bytes):])
    changed=[f'tools/soi/{n}.py' for n in ('mesa_ps1','mesa_ps1_check','mesa_disc','ds_private_check','mesa_pc_check',
             'mesa_compare','package_patcher','retire_mesa','restore_mesa_baselines','mesa_moby_safety','mesa_release_check',
             'finalize_mesa_release')]+['Play Market Mesa (Season of Ice).cmd','docs/SEASON_OF_ICE_PORT.md','CHANGELOG.md']
    manifest=dict(image=str(image),bin_sha256=digest,cue_sha256=sha(image.with_suffix('.cue')),
                  archive=str(archive),archive_sha256=disc['archive_sha256'],package=str(PACKAGE),package_sha256=package_sha,
                  evidence=ev,followup_changed_files={p:sha(ROOT/p) for p in changed},manual_acceptance=False)
    (DATA/'test05-release.json').write_text(json.dumps(manifest,indent=1))
    table='\n'.join(f'- {key}: {cite(key)}' for key in ev)
    handoff=f'''Market Mesa test05 completed. Work was solo; no additional model/agent/CLI worker was invoked.

BIN: {image}\nSHA-256: {digest}\nCUE SHA-256: {sha(image.with_suffix('.cue'))}
Archive: {archive}\nSHA-256: {disc['archive_sha256']}
Patcher: {PACKAGE}\nSHA-256: {package_sha}

Moneybags keeps index 9/type 0x220 with original removed state 0xfd. The real loader retained it and completed all
220 records. The purchased-bridge flag was set before the first actor update; 12 forced early-timer ticks never
reached the unsafe handler. 1,800 further ticks completed with the record inactive. These are automated fixtures,
not a full old-save playthrough. The test05 archive changes exactly one byte from final test04; v2g and executables
remain unchanged. Current main SHA-256 is 2baa0e554edce23b10b1ed5445dfe9ca24b82968bae2a0dcb6a2e1a9c9938e8e;
the tested private PC source is 5055ed71f26bee91fd4d6b56c14ecc27d3a8596ef53e21b4146a9c1b855fbf8d.

Exact evidence (all target BIN/archive identities are in the cited logs):
{table}

Restored test02 BIN 2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb;
CUE 478e39bf4639a6750953970c56759e621fccb9791fa9f0aad9f3f0fc983a1da4.
Restored test03 BIN 60680946e3be8fe3546c4173909ae8a31b54e9316935a0a3e105496d7e68308c;
CUE 1074a4ebc2b4d475c027e204a7b1ca01ea499019aef52a60d591b6678f39aba5.
Both baselines, recovery snapshots and test03 RAR are protected/retained. test01 stays absent; test02 ZIP was not restored.
Retirement log: data/soi/ps1/retirement-test05.log (explicit test04 BIN/CUE/ZIP only, safe_delete sharing guards).

Remaining limits: inherent untextured LP look; far props and background mesas absent; fitted CLUT fade
(98.8995% measured channels exact, max error 14); 641 whole-tile fallbacks; retail container contents;
14/21 source enemy positions use retail lizards; no new characters/ice barrier/critters. Manual gameplay/visual
acceptance remains unperformed. Initial harness failure is retained in runtime-test05-console.log and is not
release evidence; corrected observation uses the actual loader-completion breakpoint.

Follow-up changed files:
'''+ '\n'.join('- '+p for p in changed)+'''

Carried implementation from the preceding worker also includes tools/soi/mesa_gems.py, mesa_visibility.py,
retail_visibility_check.py, mesa_oracle_check.py, and patcher/Patch-Spyro2.ps1. No new imports or src/main exe edits.
Generated evidence/reports: data/soi/ps1/*test05* and ds/logs/sol-test05-review-20261105-151120.log;
captures/metadata: ds/shots/sol-test05-*, ds/sol-test05-review-views.json; exact machine-readable index:
data/soi/ps1/test05-release.json. No commit (not a git repo), push, downloads or user-process inspection/control.
'''
    a.output.write_text(handoff,encoding='utf-8')
    (DATA/'final-review.txt').write_text(handoff,encoding='utf-8')
    print('HANDOFF',a.output,'sha256',sha(a.output))
    print('MANIFEST',DATA/'test05-release.json')


if __name__=='__main__': main()

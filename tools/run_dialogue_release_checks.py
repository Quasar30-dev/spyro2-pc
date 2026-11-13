"""Bounded release checks on private copies; never controls an existing game.

Build the main workbench and test targets first. Run with stage, ctest,
headless, or desktop. All results carry the executable's SHA-256. No downloads.
"""
import argparse
import concurrent.futures
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXITS = [17, 19, 21, 23, 25, 27, 35, 37, 39, 41, 45, 47, 49, 51, 59, 61, 65, 67]
FRAMES = {'colossus-hockey-goalie': 30000, 'colossus-hockey-1v1': 40000,
          'metropolis-saucers-2': 16000, 'mystic-marsh-thieves': 40000,
          'crystal-glacier-draclets': 24000, 'magma-cone-party-crashers': 40000}


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def run(out, name, command, timeout=1800, validate=None):
    log = out / 'logs' / (name + '.log')
    with log.open('w', encoding='utf-8') as stream:
        stream.write('COMMAND ' + json.dumps([str(c) for c in command]) + '\n')
        artifact = Path(command[0])
        if artifact.suffix.lower() == '.exe':
            stream.write(f'ARTIFACT {artifact} SHA256 {sha(artifact)}\n')
        stream.write(f'MAIN SHA256 {sha(out / "bin/spyro_workbench.exe")}\n')
        stream.flush()
        try:
            result = subprocess.run([str(c) for c in command], cwd=ROOT, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout)
            ok = result.returncode == 0
            stream.write(f'EXIT {result.returncode}\n')
        except subprocess.TimeoutExpired:
            ok = False
            stream.write('TIMEOUT\n')
        stream.flush()
        if ok and validate:
            try:
                ok = bool(validate())
            except Exception as error:
                ok = False
                stream.write(f'VALIDATION ERROR {error}\n')
        stream.write(f'{name} {"PASS" if ok else "FAIL"}\n')
    print(f'{name} {"PASS" if ok else "FAIL"}: {log}', flush=True)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['stage', 'ctest', 'headless', 'desktop'])
    parser.add_argument('--out', type=Path, default=ROOT / 'build/dialogue-20261105')
    parser.add_argument('--disc', type=Path, default=ROOT / 'data/disc')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--ctest', type=Path)
    parser.add_argument('--legacy-exit-probe', action='store_true',
                        help='Also run the old standalone exit diagnostic (does not satisfy every end prerequisite)')
    args = parser.parse_args()
    out = args.out.resolve()
    (out / 'logs').mkdir(parents=True, exist_ok=True)
    binary = out / 'bin'
    if args.mode == 'stage':
        binary.mkdir(parents=True, exist_ok=True)
        for path in (ROOT / 'build/Release').glob('*.exe'):
            shutil.copy2(path, binary / path.name)
        for path in (ROOT / 'dist/spyro2-pc-20261104c').glob('*.dll'):
            shutil.copy2(path, binary / path.name)
        hashes = {p.name: sha(p) for p in sorted(binary.glob('*')) if p.is_file()}
        assert hashes['spyro_workbench.exe'] == sha(ROOT / 'build/Release/spyro_workbench.exe')
        (out / 'artifacts.json').write_text(json.dumps(hashes, indent=2) + '\n')
        text = (ROOT / 'build/CTestTestfile.cmake').read_text()
        text = text.replace((ROOT / 'build/Release').as_posix(), binary.as_posix())
        # Preserve the original default working directory. Explicit CTest
        # properties (disc_python) are already retained in the copied file.
        for name in sorted(set(re.findall(r'add_test\(\[=\[(.*?)\]=\]', text))):
            if name != 'disc_python':
                text += f'\nset_tests_properties([=[{name}]=] PROPERTIES WORKING_DIRECTORY "{(ROOT / "build").as_posix()}")\n'
        (out / 'CTestTestfile.cmake').write_text(text)
        print('Private artifacts staged; main SHA256', hashes['spyro_workbench.exe'])
        return 0
    expected = json.loads((out / 'artifacts.json').read_text())
    assert all(sha(binary / name) == value for name, value in expected.items()), 'Private executable changed'
    if args.mode == 'ctest':
        assert args.ctest and args.ctest.is_file(), '--ctest must name the local CTest executable'
        return 0 if run(out, 'ctest', [args.ctest, '--test-dir', out, '-C', 'Release', '--output-on-failure',
                                     '--parallel', str(args.jobs)], timeout=1800) else 1
    challenges = binary / 'spyro_challenges.exe'
    if args.mode == 'headless':
        tasks = [('challenges-headless', [challenges, args.disc, 'all'])]
        if args.legacy_exit_probe:
            tasks.append(('exits-headless', [binary / 'spyro_level_exits.exe', args.disc, *map(str, EXITS)]))
        for revisit in (False, True):
            for no in (False, True):
                name = 'talk-' + ('revisit' if revisit else 'first') + ('-no' if no else '-yes')
                tasks.append((name, [challenges, args.disc, 'talk', 'all',
                                     *(['--revisit'] if revisit else []), *(['--no'] if no else [])]))
        for revisit in (False, True):
            tasks.append(('idol-triangle-' + ('revisit' if revisit else 'first'),
                          [challenges, args.disc, 'talk', '19', '--triangle', *(['--revisit'] if revisit else [])]))
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda task: run(out, *task), tasks))
    else:
        names = subprocess.check_output([str(challenges), str(args.disc), 'list'], cwd=ROOT, text=True)
        tasks = []
        for line in names.splitlines():
            name = line.split()[0]
            tasks.append(('challenge-' + name, ['--challenge-check', name, '--smoke', str(FRAMES.get(name, 12000))], 'challenge'))
        for slot in EXITS:
            tasks.append((f'exit-{slot}', ['--level-exit-check', str(slot), '--smoke', '10000' if slot == 51 else '6000'], 'level_exit'))
        def desktop(task):
            name, flags, key = task
            capture = out / (name + '.bmp')
            def validate():
                report = json.loads(capture.with_suffix('.json').read_text())
                result = report[key]
                return (result['finished'] and (result['awarded'] if key == 'challenge' else result['outro'])
                        and report.get('retail_script_faults', 0) == 0)
            return run(out, name, [binary / 'spyro_workbench.exe', '--game', args.disc, *flags, '--capture', capture], validate=validate)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(desktop, tasks))
    passed = sum(results)
    summary = out / 'logs' / (args.mode + '-summary.log')
    summary.write_text(f'MAIN SHA256 {expected["spyro_workbench.exe"]}\n{args.mode} {passed}/{len(results)} {"PASS" if all(results) else "FAIL"}\n')
    print(summary.read_text(), end='', flush=True)
    return 0 if all(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())

"""Unzip and smoke the 20261105 packages in private directories, with no UI input."""
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/dialogue-20261105'
DISC = ROOT / 'Spyro 2 - Riptos Rage (USA)' / "Spyro 2 - Ripto's Rage! (USA).cue"


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    assert DISC.is_file(), DISC
    packages = json.loads((WORK / 'packages.json').read_text())
    successes = 0
    for package in packages:
        archive_path = Path(package['path'])
        assert sha(archive_path) == package['sha256']
        folder = archive_path.stem
        destination = WORK / 'unzipped' / folder
        if destination.exists():
            raise RuntimeError(f'Refusing to overwrite {destination}')
        with zipfile.ZipFile(archive_path) as archive:
            for member in archive.namelist():
                assert (destination / member).resolve().is_relative_to(destination.resolve())
            archive.extractall(destination)
        game_dir = destination / folder
        exe = game_dir / 'spyro_workbench.exe'
        assert sha(exe) == package['exe_sha256']
        jobs = [('spring', ['--custom-level', str(game_dir / 'levels/spring-realm/pc/spring-realm.s2world'), '--play'])]
        if folder.startswith('spyro2'):
            jobs = [('summer-forest', ['--slot', '15', '--play']), ('glimmer', ['--slot', '17', '--play']),
                    ('idol-springs', ['--slot', '19', '--play']), ('title', ['--main-menu'])]
        for label, flags in jobs:
            name = folder + '-' + label
            capture = WORK / (name + '.bmp')
            command = [str(exe), '--game', str(DISC), *flags, '--smoke', '300', '--capture', str(capture)]
            with (WORK / 'logs' / (name + '.log')).open('w', encoding='utf-8') as log:
                log.write(f'PACKAGE {archive_path} SHA256 {package["sha256"]}\n')
                log.write(f'EXE {exe} SHA256 {sha(exe)}\n')
                log.write('COMMAND ' + json.dumps(command) + '\n')
                log.flush()
                result = subprocess.run(command, cwd=game_dir, stdout=log, stderr=subprocess.STDOUT, timeout=300)
                log.write(f'EXIT {result.returncode}\n')
                assert result.returncode == 0, name
                report = json.loads(capture.with_suffix('.json').read_text())
                assert report['frames'] == 300 and report['hardware_renderer'], name
                image = Image.open(capture).convert('RGB')
                assert max(ImageStat.Stat(image).stddev) > 5, 'Flat / empty output'
                image.save(capture.with_suffix('.png'))
                if label == 'title':
                    assert report['main_menu'] and report['title_actors'] > 0, name
                else:
                    assert report['simulation_ticks'] >= 290 and report['spyro_vertices'] > 0, name
                    assert report['retail_script_faults'] == 0 and report['triangles'] > 1000, name
                    assert report['audio_music_frames'] > 0, 'No decoded music'
                log.write(f'REPORT frames {report["frames"]} triangles {report.get("triangles", 0)} '
                          f'faults {report.get("retail_script_faults", 0)} music {report.get("audio_music_frames", 0)}\n')
                log.write(f'{name} PASS (automated render/report checks; no visual inspection)\n')
            successes += 1
            print(name, 'PASS', flush=True)
    (WORK / 'logs/package-smoke-summary.log').write_text(
        f'MAIN SHA256 {packages[0]["exe_sha256"]}\nUnzipped packages {successes}/5 PASS; automated only\n')


if __name__ == '__main__':
    main()

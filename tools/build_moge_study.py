"""Pack independent workbench inspection levels for each perspective reconstruction."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
WORLD=ROOT/'data/custom-levels/lanternfall'
VALIDATION=ROOT/'data/validation/lanternfall/moge'

def main():
    VALIDATION.mkdir(parents=True,exist_ok=True)
    template=(WORLD/'pc/exterior.s2world').read_text()
    for module in ['exterior-splats-front','exterior-splats-left','interior-splats-front']:
        dest=WORLD/'moge'/module/'known'
        data=json.loads((dest/'report.json').read_text())
        level=template.replace('Lanternfall Court - Gaussian test','MoGe geometry study - '+module)
        level=level.replace('SPLATS 1\n"exterior.s2gs"','SCENERY 1\n"mesh.s2scene"')
        (dest/'study.s2world').write_text(level)
    # Central reconstruction view and third-person view displaced from it. The
    # latter is deliberately allowed to reveal gaps instead of hiding them.
    for label,eye,target in [('source',(0,1.6,0),(0,1.6,8)),('offset',(3,2.5,-1),(0,1.6,8)),('gameplay',(0,2.6,-2),(0,1.2,5))]:
        spec='S2SHOT 1\n1024 1024 120\n0 .08 2 0 0 100 1\n0\n0\n1\n0 '+' '.join(map(str,eye+target))+'\n'
        (VALIDATION/(label+'.shot')).write_text(spec)
    # Sustained fixed camera baseline, headless unpaced draw/CPU cost.
    (VALIDATION/'benchmark.shot').write_text('S2SHOT 1\n1920 1080 600\n0 .08 2 0 0 65 1\n0\n0\n1\n0 0 2.6 -2 0 1.2 5\n')

if __name__=='__main__':main()

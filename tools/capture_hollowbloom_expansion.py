"""Author native-engine review cameras, without substituting Blender renders."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
folder=root/'data/validation/hollowbloom-expansion';folder.mkdir(parents=True,exist_ok=True)
shots=[
    ('valley',(0,.15,-38),(112,128,-109),(0,10,28),46),
    ('arrival',(0,.15,-38),(7,5,-48),(0,6,20),57),
    ('mooncrown',(0,16.15,90),(-8,23,81),(0,21,109),58),
    ('briarwatch',(-67,8.15,14),(-101,31,-7),(-66,13,28),57),
    ('village',(61,4.15,-11),(94,27,-43),(64,11,12),58),
    ('rooftops',(50,6.15,-18),(44,9,-25),(60,7,-18),58),
    ('aqueduct',(86,12.15,22),(103,22,7),(78,14,39),58),
]
for name,spawn,eye,target,fov in shots:
    text=['S2SHOT 1','1920 1080 45',' '.join(map(str,(*spawn,0,0,fov,1))),'0','0','1',' '.join(map(str,(0,*eye,*target)))]
    (folder/f'{name}.s2shot').write_text('\n'.join(text)+'\n')
print(f'Authored {len(shots)} native review cameras.')
# A longer, paced view measures the shipped renderer at the most complete
# overview, separately from the short photography runs and Blender exports.
timing=(folder/'valley.s2shot').read_text().replace('1920 1080 45','1920 1080 600')
(folder/'timing.s2shot').write_text(timing)

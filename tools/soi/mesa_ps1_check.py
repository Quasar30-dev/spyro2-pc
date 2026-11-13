"""Static checks of the Market Mesa slot-17 archive against the retail formats (run after mesa_ps1.py build).

Checks: archive layout (2048-aligned children, model directory inside child 2), scene child parts, every region
record (counts within the retail maxima, vertex / colour indices in range, far words <= 255, +12 contiguous from 1,
total near faces < 0x2000, neighbour indices valid, materials < count), descriptors inside VRAM, the collision
chunk decodes and re-encodes byte-exact, the gameplay child's sections and object table.
    python tools/soi/mesa_ps1_check.py [ARCHIVE]   -> prints CHECK lines and "STATIC CHECK PASS/FAIL"
"""
import struct
import sys
import hashlib
import json
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import s2collision  # noqa: E402
import s2regions  # noqa: E402
import s2scene_build  # noqa: E402
from spyrodisc import wad_index  # noqa: E402

ARC = Path(sys.argv[1] if len(sys.argv) > 1 else "D:/Theia-staging/spyro2-mod/work/slot17-mesa.bin")
fails = []


def check(name, ok, detail=""):
    print(f"CHECK {'PASS' if ok else 'FAIL'} {name} {detail}")
    if not ok:
        fails.append(name)


arc = ARC.read_bytes()
digest = hashlib.sha256(arc).hexdigest()
print('ARTIFACT', ARC.resolve(), 'sha256', digest)
ch = wad_index(arc[:0x10000], len(arc))
check("children", len(ch) == 4 and all(c.offset % 2048 == 0 for c in ch), [(c.offset, c.size) for c in ch])
lst = []
for o in range(64, 2048, 4):
    w = struct.unpack_from("<I", arc, o)[0]
    if w == 0:
        break
    lst.append(w)
check("model directory inside child 2", all(ch[2].offset <= w < ch[2].offset + ch[2].size for w in lst), len(lst))
scene = arc[ch[1].offset: ch[1].offset + ch[1].size]
sc = s2scene_build.split(scene)
nmat = struct.unpack_from("<I", sc["parts"][0], 4)[0]
check("materials", sc["parts"][0][:4] == struct.pack("<I", 8 + 48 * nmat) and nmat <= 128, nmat)
bad = 0
for m in range(nmat):
    for k in range(6):
        d = sc["parts"][0][8 + (m * 6 + k) * 8: 8 + (m * 6 + k + 1) * 8]
        page = d[6]
        depth = (page >> 7) & 1
        ppw = 2 if depth else 4
        x1 = (page & 15) * 64 + d[4] // ppw
        if d[4] < d[0] or d[5] < d[1] or (page & 15) * 64 < 512 or x1 > 1023:
            bad += 1
check("descriptors inside VRAM x 512..1023", bad == 0, bad)
# retail rule: every descriptor is a 32 x 32 texel cell (the renderers form the UV corners as word0 + 0x1f00 /
# 0x1f1f and word1 - 0x1f00: 80024f2c, 800253d4, 8002654c)
cells = sum(1 for m in range(nmat) for k in range(6)
            if (lambda d: d[4] - d[0] == 31 and d[5] - d[1] == 31)(sc["parts"][0][8 + (m * 6 + k) * 8: 16 + (m * 6 + k) * 8]))
check("every descriptor a 32 x 32 cell (retail renderer rule)", cells == nmat * 6, f"{cells} / {nmat * 6}")
# retail rule: a far CLUT is 16 rows fading to neutral (80024e28 adds a depth row to the CLUT)
import numpy as np  # noqa: E402
import mesa_ps1  # noqa: E402
vram = np.frombuffer(arc, "<u2", 512 * 512, struct.unpack_from("<I", arc, 0)[0]).reshape(512, 512)
fbad = 0
for m in range(nmat):
    clut = struct.unpack_from("<H", sc["parts"][0], 8 + m * 48 + 2)[0]
    y, x = (clut >> 6) & 511, (clut & 63) * 16 - 512
    if not np.array_equal(vram[y:y + 16, x:x + 16], mesa_ps1.far_clut_fade(vram[y, x:x + 16].copy())):
        fbad += 1
check("far CLUTs carry 16 generated fade rows (fitted design; not exact retail formula)", fbad == 0, f"{fbad} of {nmat} materials wrong")
reg = s2regions.decode(sc["parts"][1])
check("region chunk round trip", s2regions.encode(reg["moving"], reg["records"], reg["order"], reg["head_gap"]) == sc["parts"][1])
recs = reg["records"]
check("regions <= 255 (visibility bytes)", len(recs) <= 255, len(recs))
expect, nbad, ibad, maxes = 1, 0, 0, [0] * 7
total = 0
for r in recs:
    h = r.header
    lv, lc, lf, hv, hc, hf, lw = h[16], h[17], h[18], h[20], h[21], h[22], h[23]
    maxes = [max(a, b) for a, b in zip(maxes, (lv, lc, lf, hv, hc, hf, lw))]
    f12 = struct.unpack_from("<H", h, 12)[0]
    if f12 != expect:
        nbad += 1
    expect += hf
    total += hf
    if lw != lv + lc + 2 * lf or h[24:28] != b"\xff" * 4 or (lv == 0 and not struct.unpack_from("<H", h, 4)[0] & 0x2000):
        ibad += 1
    for k in range(hf):
        f = r.hp_faces[16 * k: 16 * k + 16]
        vs = [f[0], f[1], f[2], f[3]]
        cs = [f[5], f[6], f[7]] + ([f[4]] if f[0] != f[1] else [])
        if max(vs) >= hv or max(cs) >= hc or (f[12] & 0x7f) >= nmat:
            ibad += 1
    for k in range(lf):
        w0, w1 = struct.unpack_from("<II", r.lp_faces, 8 * k)
        if max((w0 >> s) & 0x7f for s in (24, 17, 10, 3)) >= lv or max((w1 >> s) & 0x7f for s in (25, 18, 11, 4)) >= lc:
            ibad += 1
check("+12 = 1 + earlier near faces", nbad == 0, nbad)
check("indices in range, far words, +0x18, 0x2000 on regions without a far mesh (retail rule)", ibad == 0, ibad)
check("near faces < 0x2000", total < 0x2000, total)
check("per-region maxima within retail (hp verts 170, cols 221, faces 145, far words 255)",
      maxes[3] <= 170 and maxes[4] <= 221 and maxes[5] <= 145 and maxes[6] <= 255, maxes)
c = s2collision.decode(sc["collision"])
check("collision round trip", s2collision.encode(c.tri, c.flags, c.flags2, c.cells, c.extra) == sc["collision"],
      f"{c.count} triangles, {len(c.cells)} cells")
from mesa_visibility import decode as visibility_decode
from retail_visibility_check import execute as retail_visibility
part = sc['parts'][2]
positions = set()
for r in recs:
    y,x,_,z = struct.unpack_from('<4H',r.header)
    positions.add((x>>7,y>>7,z>>7))
positions.update((x,y,z) for x in (0,63,64,127,255) for y in (0,63,64,127,255) for z in (0,63,64,127,255))
vb = 0
for pos in positions:
    v,ret,_ = retail_visibility(part,pos)
    vb += (v,ret) != visibility_decode(part,pos) or ret != 1 or v[:len(recs)] != bytes([255])*len(recs)
check('all-visible tree through original consumer, region centres and cell boundaries',not vb,
      f'{len(positions)} cells; {len(recs)} regions; loader distance 0x2c000 when part2 size > 8')
report_path = ARC.with_name('mesa-ps1-report.json')
if report_path.is_file():
    report=json.loads(report_path.read_text())
    check('build evidence bound to archive sha256',report.get('archive_sha256')==digest)
    mismatch=0; matched=0
    for ri,r in enumerate(recs):
        expected=[]
        for j in report['far_mesh']['near_face_indices'][ri]:
            f=r.hp_faces[j*16:j*16+16]
            inds=[3,2,1] if f[0]==f[1] else [3,2,0,1]
            vs=[r.hp_verts[f[k]*4:f[k]*4+4] for k in inds]
            cs=[r.hp_colors[(r.header[21]+f[k+4])*4:(r.header[21]+f[k+4]+1)*4] for k in inds]
            if len(vs)==3: vs.append(vs[-1]); cs.append(cs[-1])
            expected.append(tuple(zip(vs,cs)))
        got=[]
        for k in range(r.header[18]):
            w0,w1=struct.unpack_from('<II',r.lp_faces,k*8)
            vs=[r.lp_verts[((w0>>s)&127)*4:(((w0>>s)&127)+1)*4] for s in (24,17,10,3)]
            cs=[r.lp_colors[((w1>>s)&127)*4:(((w1>>s)&127)+1)*4] for s in (25,18,11,4)]
            got.append(tuple(zip(vs,cs)))
        mismatch+=Counter(expected)!=Counter(got); matched+=len(got)
    check('far terrain faces/positions/winding/B colours exactly equal selected near faces',not mismatch,
          f'{matched} faces; decor excluded; {mismatch} mismatching regions')
else:
    check('build evidence present for exact far-face coverage',False,str(report_path))
g = arc[ch[3].offset: ch[3].offset + ch[3].size]
at = 0x2C
zero = True
for k in range(7):
    n, cnt = struct.unpack_from("<II", g, at)
    zero &= cnt == 0
    at += n
check("seven animation sections empty", zero)
at += struct.unpack_from("<I", g, at)[0]
count = struct.unpack_from("<I", g, at + 4)[0]
types = [struct.unpack_from("<H", g, at + 8 + 88 * i + 54)[0] for i in range(count)]
import mesa_gems
import s2scene
original=s2scene.read_archive(mesa_ps1.DISC,17)
original_ch=wad_index(original[:0x10000],len(original))
og=original[original_ch[3].offset:original_ch[3].offset+original_ch[3].size]
old_count=struct.unpack_from('<I',og,at+4)[0]
old_types=[struct.unpack_from('<H',og,at+8+88*i+54)[0] for i in range(old_count)]
check('all original Moby indices/types preserved',types[:old_count]==old_types,
      f'{old_count} original / {count} total objects')
moneybags=[i for i,t in enumerate(types) if t==0x220]
check('Moneybags inert through retail removed state 0xfd; never mid-table sentinel 0xff',
      len(moneybags)==1 and g[at+8+88*moneybags[0]+0x48]==0xfd and
      all(g[at+8+88*i+0x48]!=0xff for i in range(count)),
      f'indices {moneybags}; original RemoveMoby 80041384/8c, activity 80040ef0/f8, dispatcher 8007043c/48')
size=struct.unpack_from('<I',g,at)[0]
data_end=at+size
data_end+=struct.unpack_from('<I',g,data_end)[0]
rel=data_end+struct.unpack_from('<I',g,data_end)[0]
nr=struct.unpack_from('<I',g,rel)[0]
refs=struct.unpack_from('<'+'I'*nr,g,rel+4)
ptrs=[struct.unpack_from('<I',g,at+8+88*i)[0] for i in range(count)]
check('all original object data pointers retain exact offsets',
      ptrs[:old_count]==[struct.unpack_from('<I',og,at+8+88*i)[0] for i in range(old_count)])
check('all object parameter pointers have valid relocation entries',
      all(at+8+88*i in refs and 0<=p<len(g) for i,p in enumerate(ptrs)),f'{nr} relocations')
variants,values,meshes,colours=mesa_gems.tables()
gem_bad=0; gem_values=Counter(); param_ptrs=[]
for i,t in enumerate(types):
    if t!=1: continue
    p=at+8+88*i; value=g[p+80]; variant=variants[value] if value<len(variants) else 255
    gem_bad+=(variant>=len(values) or (g[p+73],g[p+60],g[p+84])!=(variant,meshes[variant],colours[variant])
              or values[variant]!=value)
    gem_values[value]+=1; param_ptrs.append(ptrs[i])
source_level=json.loads((mesa_ps1.PROJECT/'data/soi/levels/06-market-mesa/level.json').read_text())
expected_values=Counter(e['gem_value'] for e in source_level['objects']['objects'] if e.get('gem_value'))
check('all 110 SoI loose gems / denominations match source and retail tables',
      not gem_bad and gem_values==expected_values,f'{dict(gem_values)}; value total {sum(v*n for v,n in gem_values.items())}')
check('loose gems have separate 24-byte parameter blocks',len(set(param_ptrs))==len(param_ptrs)
      and all(p+24<=data_end for p in param_ptrs),len(param_ptrs))
expected_records=report.get('gem_records',[]) if report_path.is_file() else []
check('every SoI gem position encoded at its recorded object index',len(expected_records)==110 and
      all(struct.unpack_from('<iii',g,at+8+88*e['index']+12)==tuple(e['position']) for e in expected_records))
if report_path.is_file() and 'container_records' in report:
    import mesa_containers
    source_containers,oracle=mesa_containers.sources()
    expected={e['source_index']:e for e in source_containers};actual=report['container_records']
    container_variant={}
    if 'container_conversion' in report:
        cp=json.loads(Path(report['container_conversion']).read_text())
        check('charge-only container conversion bound to exact archive',cp['archive_sha256']==digest)
        container_variant={e['index']:e for e in cp['containers']}
        check('charge-only containers append records with inactive originals and independent 28-byte parameters',
              len(container_variant)==5 and all(e['index']>=old_count and types[e['old_index']]==0xc8
                  and g[at+8+88*e['old_index']+72]==0xfd and types[e['index']]==0xc9
                  and ptrs[e['index']]==e['parameter'] and ptrs[e['index']]+28<=data_end
                  and e['parameter']+24 in refs for e in container_variant.values()))
    check('all 15 SoI background container placements and source-held denominations',
          len(actual)==len(expected) and {e['source_index'] for e in actual}==set(expected)
          and all(types[e['index']]==(0xc9 if e['index'] in container_variant else 0xc8)
                  and (e['index'] not in container_variant or e['source_type']==136)
                  and g[at+8+88*e['index']+80]==expected[e['source_index']]['value']==e['value']
                  and struct.unpack_from('<iii',g,at+8+88*e['index']+12)==tuple(e['position']) for e in actual),
          '10 type131 + 5 type136; source 0801fec0/081ab8f0; original S2 +50 release input; value total 15')
print("STATIC CHECK", "PASS" if not fails else f"FAIL {fails}")
raise SystemExit(bool(fails))

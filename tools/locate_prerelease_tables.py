"""Find relocated resident tables with unique anchors; retain changed bytes.

Research evidence only. Unmatched/ambiguous candidates need manual RE before
being installed in a runtime profile.
"""
from pathlib import Path
import json,struct
ROOT=Path(__file__).resolve().parents[1]
base=0x8000f800
retail=(ROOT/'data/disc/SCUS_944.25').read_bytes()
tables={
 'loops':(0x80060334,248),'transitions':(0x8006042c,3844),'secondary':(0x80061330,64),'secondary_clips':(0x80066d54,8),
 'allowed':(0x80061370,248),'codebook':(0x80062674,500),'blends':(0x80062868,256),
 'sine':(0x80061bd8,640),'cosine':(0x80061c58,512),'atan':(0x80061e5c,256),'sqrt':(0x80066478,384),
 'camera_follow':(0x80061560,60),'camera_swim':(0x800615d8,20),'camera_states':(0x80061468,248),
 'camera_conversation':(0x8006163c,60),'camera_spring':(0x8006172c,20),'camera_damping':(0x800617b8,20),
 'camera_minvel':(0x80061858,20),'camera_limit':(0x80061844,20),
 'hud_enter':(0x80063430,24),'hud_exit':(0x80063448,24),'hud_sizes':(0x800634b8,48),'hud_sprites':(0x8006351c,220),
 'font':(0x800636a4,256),'escapes':(0x800637a4,46),'font_colors':(0x800637d4,56),
 'dialogue_enter':(0x800633f4,36),'dialogue_exit':(0x80063418,24),
 'gem_variants':(0x8006440c,28),'gem_values':(0x80064428,5),'gem_meshes':(0x80064430,5),'gem_colors':(0x80064438,5),'gem_shine':(0x80064440,160),
 'purchase_prices':(0x80064670,80),'env_radii':(0x800625ac,129),'env_angles':(0x80062630,65),
 'sparx_glow':(0x8006430c,256),'sparx_colors':(0x80061adc,16),'sparx_health':(0x80061a8c,4),
 'sparx_radius':(0x80061a90,4),'sparx_height':(0x80061a98,4),
 'fire_shapes':(0x80062ad0,704),'fire_shapes2':(0x80062d90,896),'fire_dimensions':(0x80062a70,96),
 'fire_growth':(0x80062968,240),'fire_order':(0x80063130,192),'fire_colors':(0x80063110,32),
 'scene_ids':(0x80064994,29),'scene_names':(0x800649b4,116),'scene_homes':(0x80064b84,29),
 'music_ids':(0x80060140,41),'music_clips':(0x80064de4,256),
 'cinematic_ids':(0x80064914,41),'pause_sprites':(0x8006387c,156),'page_angles':(0x80063870,12)
}
report={}
for name,path in [('july-1999','SPYRO2.EXE'),('august-1999','SPYRO2.EXE'),('winter-jampack-1999','SPYRO2/DEMO2.EXE')]:
 exe=(ROOT/'data/builds'/name/'disc'/path).read_bytes();found={}
 target_base=struct.unpack_from('<I',exe,24)[0]-0x800
 for key,(address,size) in tables.items():
  pattern=retail[address-base:address-base+size];candidates=set();exact=exe.find(pattern)
  if exact>=0 and exe.find(pattern,exact+1)<0:candidates.add(exact)
  else:
   for off in range(0,max(1,size-7),4):
    sample=pattern[off:off+min(16,size-off)]
    if len(set(sample))<3:continue
    at=exe.find(sample)
    if at>=0 and exe.find(sample,at+1)<0:candidates.add(at-off)
  ranking=sorted([(sum(x==y for x,y in zip(pattern,exe[p:p+size]))/size,p) for p in candidates if p>=2048 and p+size<=len(exe)],reverse=True)
  found[key]={'retail':hex(address),'bytes':size,'candidates':[(hex(p+target_base),round(score,3)) for score,p in ranking[:3]]}
 report[name]=found
(ROOT/'data/analysis/prerelease-20260930/resident-tables.json').write_text(json.dumps(report,indent=2))
for key in tables:
 print(key,hex(tables[key][0]),*(str(report[b][key]['candidates'][:2]) for b in report))

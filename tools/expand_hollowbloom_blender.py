"""Castle expansion, executed in the inspected Blender scene through MCP.

The compact source scene is retained in memory and in revisions/compact-v1.
New geometry uses the same retail tiles, units, corner colours and export tags.
"""
import bpy, ast, math, json, random
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/hollowbloom-springs')
ASSETS=ROOT/'assets'
base=next((s for s in bpy.data.scenes if s.get('level_id')=='hollowbloom-springs' and s.get('revision',1)<2 and 'PS1' not in s.name),None)
if base is None:
    # Reopening the expanded master must not expand the expansion again.
    with bpy.data.libraries.load(str(ROOT/'revisions/compact-v1/Hollowbloom Springs.blend'),link=False) as (src,dst):
        dst.scenes=['Hollowbloom Springs']
    base=dst.scenes[0]
assert base.get('level_id')=='hollowbloom-springs'
scene=bpy.data.scenes.new('Hollowbloom Springs - Castle Expansion')
bpy.context.window.scene=scene
for k,v in base.items():scene[k]=v
scene['revision']=2
scene['design']='A sprawling haunted spring valley: orchard, two castles, village rooftops, cloisters and broken aqueducts.'
scene.world=base.world.copy()
collections={}
for c in base.collection.children:
    key=c.name.split('.')[0];nc=bpy.data.collections.new(key+' / Expanded');scene.collection.children.link(nc);collections[key]=nc
    for old in c.objects:
        o=old.copy();nc.objects.link(o)
        if o.get('kind')=='route':o['kind']='legacy_route'
        if o.name.startswith('Distant valley ridge'):o['s2_export']='reference';o.hide_render=True
for key in ['11_West_Castle','12_North_Castle','13_Cloisters','14_Village','15_Aqueduct','16_Platforming','17_New_Terrain','18_Dense_Gardens','19_Expansion_Views']:
    c=bpy.data.collections.new(key);scene.collection.children.link(c);collections[key]=c
materials={m.name.split('.')[0]:m for m in bpy.data.materials if 's2_material' in m}
GRASS=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==42)
ROAD=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==7)
STONE=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==65)
PLASTER=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==54)
BARK=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==3)
BRONZE=next(m for m in materials.values() if m['source_slot']==33 and m['source_tile']==28)
WATER=next(m for m in materials.values() if m['source_slot']==59 and m['source_tile']==1)
LEAF=next(m for m in materials.values() if m['source_slot']==59 and m['source_tile']==48)
PETAL=next(m for m in materials.values() if m.name.startswith('Petal'))
GLOW=next(m for m in materials.values() if m.name.startswith('Lantern'))
material_specs=[{'name':m.name,'slot':m['source_slot'],'tile':m['source_tile']} for m in materials.values()]
source=ROOT.parents[2]/'tools/build_hollowbloom_blender.py'
tree=ast.parse(source.read_text())
# Reuse the reviewed authoring helpers, without rerunning the compact builder.
for node in tree.body:
    if isinstance(node,ast.FunctionDef) and node.name in {'material','mesh','box','tube','ico','arch','actor','marker','area','camera'}:
        exec(compile(ast.Module(body=[node],type_ignores=[]),str(source),'exec'),globals())
rng=random.Random(23092602)
ROOF=material('Expansion / verdigris shingles',33,16)
WINDOW=material('Expansion / retail lancet window',33,0)
DOOR=material('Expansion / retail oak door',33,50)
PAVING=material('Expansion / courtyard diamond paving',33,45)
FACADE=material('Expansion / bordered plaster',33,38)
prototypes={}
placements=[]
for o in sorted((o for o in scene.objects if o.get('s2_export')=='moby'),key=lambda o:o['stable_id']):
    prototypes.setdefault(o['class_id'],(o.data,o['source_record']))
    placements.append({'id':o['stable_id'],'type':o['class_id'],'position':list(o.location)})

main=[(0,-38,0),(-15,-31,1),(-25,-17,2),(-28,2,3),(-42,6,5),(-49,1,6),(-62,1,7),(-67,5,8),(-67,10,8),(-68,24,8),(-67,42,8),(-56,50,10),(-37,53,11),(-18,49,9),(0,43,8),(18,49,9),(39,52,10),(59,46,10),(72,42,10),(72,20,10),(72,8,6),(65,0,4),(61,-11,4),(60,-8.9,4),(47,-8.9,4),(42,-14,3),(44,-23,3),(36,-29,2),(25,-31,1.3),(23,-19,1.5),(28,0,2.5),(22,17,4),(7,27,5),(0,27,5),(0,32,5),(0,43,8),(0,60,11),(0,76,16),(0,94,16),(0,105,16)]
# Complete the incline before it meets the transverse cloister pavement.
main.insert(main.index((0,32,5))+1,(0,39,8))
roads=[main[3:main.index((23,-19,1.5))+1],main[main.index((0,32,5)):]]
rooms=[('Briarwatch Castle',(-83,10,-51,42),8),('Mooncrown Castle',(-22,76,22,118),16),('Aqueduct Court',(55,20,82,43),10),('Weeping Village',(48,-30,78,0),4),('Cloister Garden',(-13,39,13,51),8)]
def nearest(x,y,paths=roads):
    best=(1e9,0,0,0)
    for path in paths:
        for a,b in zip(path,path[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
            qx,qy=a[0]+t*dx,a[1]+t*dy;d=math.hypot(x-qx,y-qy)
            if d<best[0]:best=(d,a[2]+t*(b[2]-a[2]),qx,qy)
    return best
def ground(x,y):
    d,z,_,_=nearest(x,y)
    if d<3.5:return z
    for _,(x0,y0,x1,y1),h in rooms:
        if x0<=x<=x1 and y0<=y<=y1:return h
    return z+max(0,min(1,(d-4)*.25))*.24*math.sin(x*.29)*math.cos(y*.32)
def land(x,y):
    if -37<x<37 and y<38:return False
    return nearest(x,y)[0]<7 or any(x0-2<=x<=x1+2 and y0-2<=y<=y1+2 for _,(x0,y0,x1,y1),_ in rooms)

# A closed, contiguous terrain shell. Broad route corridors connect distinct
# rooms; the lake is now the centre of a built-up valley rather than the map.
step=2
cells={(x,y) for x in range(-96,96,step) for y in range(-40,130,step) if land(x+1,y+1)}
boundary={}
for x,y in cells:
    for dx,dy,a,b in [(0,-2,(x,y),(x+2,y)),(2,0,(x+2,y),(x+2,y+2)),(0,2,(x+2,y+2),(x,y+2)),(-2,0,(x,y+2),(x,y))]:
        if (x+dx,y+dy) not in cells:boundary.setdefault(a,set()).add(b);boundary.setdefault(b,set()).add(a)
remap={p:tuple(Vector(p)*.5+sum((Vector(q) for q in ns),Vector((0,0)))/len(ns)*.5) for p,ns in boundary.items()}
def corner(x,y):
    x,y=remap.get((x,y),(x,y));return (x,y,ground(x,y))
chunks={};banks={}
for x,y in sorted(cells):
    v,f=chunks.setdefault((x//16,y//16),([],[]));n=len(v)
    v.extend([corner(x,y),corner(x+2,y),corner(x+2,y+2),corner(x,y+2)]);f.append((n,n+1,n+2,n+3))
    for dx,dy,a,b in [(0,-2,(x,y),(x+2,y)),(2,0,(x+2,y),(x+2,y+2)),(0,2,(x+2,y+2),(x,y+2)),(-2,0,(x,y+2),(x,y))]:
        if (x+dx,y+dy) in cells:continue
        aa,bb=corner(*a),corner(*b);v,f=banks.setdefault((x//16,y//16),([],[]))
        levels=max(1,math.ceil((max(aa[2],bb[2])+4)/2.5))
        for k in range(levels):
            n=len(v);t0=k/levels;t1=(k+1)/levels
            v.extend([(aa[0],aa[1],-4+(aa[2]+4)*t0),(bb[0],bb[1],-4+(bb[2]+4)*t0),(bb[0],bb[1],-4+(bb[2]+4)*t1),(aa[0],aa[1],-4+(aa[2]+4)*t1)])
            f.append((n,n+1,n+2,n+3))
for k,(v,f) in chunks.items():mesh(f'Expansion terrain {k}',v,f,GRASS,(.66,.78,.68),'17_New_Terrain',True)
for k,(v,f) in banks.items():mesh(f'Expansion cliff {k}',v,f,STONE,(.46,.54,.58),'17_New_Terrain',True)

def ribbon(name,path,width=4.8,mat=ROAD,collision=True,collection='17_New_Terrain',offset=0):
    # Shared mitered endpoints avoid overlapping paving and triangular holes
    # where paths turn. Narrow village alleys retain their building clearance.
    directions=[];widths=[];sides=[]
    for a,b in zip(path,path[1:]):
        a,b=Vector(a),Vector(b);d=b-a;d.z=0;directions.append(d.normalized())
        widths.append(2.6 if name.startswith('Royal causeway') and a.x>45 and b.x>45 and a.y<0 and b.y<0 else width)
    for i in range(len(path)):
        before=directions[max(0,i-1)];after=directions[min(i,len(directions)-1)]
        n=Vector((-after.y,after.x,0));m=(Vector((-before.y,before.x,0))+n).normalized()
        half=min(widths[max(0,i-1)],widths[min(i,len(widths)-1)])/2
        sides.append(m*min(half*1.5,half/max(.35,m.dot(n))))
    for s,(a,b) in enumerate(zip(path,path[1:])):
        a,b=Vector(a),Vector(b);d=b-a
        n=max(1,math.ceil(d.length/2))
        for j in range(n):
            p=a.lerp(b,j/n)+Vector((0,0,offset));q=a.lerp(b,(j+1)/n)+Vector((0,0,offset))
            ps=sides[s].lerp(sides[s+1],j/n);qs=sides[s].lerp(sides[s+1],(j+1)/n)
            mesh(f'{name} {s}.{j}',list(map(tuple,[p-ps,q-qs,q+qs,p+ps])),[(0,1,2,3)],mat,(.83,.82,.78),collection,collision)
            if collision:
                for sign in (-1,1):
                    pa=p+ps*sign;qa=q+qs*sign;down=Vector((0,0,.42))
                    mesh(f'{name} stone fascia',list(map(tuple,[pa-down,qa-down,qa,pa])),[(0,1,2,3)] if sign==1 else [(3,2,1,0)],STONE,(.60,.66,.65),collection,False)
        if collection=='16_Platforming':
            for j in range(1,max(2,math.ceil(d.length/6))):
                p=a.lerp(b,j/max(2,math.ceil(d.length/6)))
                tube(name+' bridge support',[(p.x,p.y,-3.8),(p.x,p.y,p.z-.42)],[.65,.55],STONE,(.55,.62,.63),collection,6,True)
for i,path in enumerate(roads):ribbon('Royal causeway '+str(i),path,4.8,offset=.025)

# Texture subdivision is independent of collision: large masonry walls keep
# small retail texels without making every decorative seam a collision edge.
occupied=[]
def block(name,p,size,mat=STONE,color=(.74,.78,.76),collection='05_Ruins',collision=True,pc=False,tile=3):
    x,y,z=p;hx,hy,hz=[v/2 for v in size];verts=[];faces=[]
    corners=[[(x-hx,y-hy,z-hz),(x+hx,y-hy,z-hz),(x+hx,y-hy,z+hz),(x-hx,y-hy,z+hz)],[(x+hx,y+hy,z-hz),(x-hx,y+hy,z-hz),(x-hx,y+hy,z+hz),(x+hx,y+hy,z+hz)],[(x-hx,y+hy,z-hz),(x-hx,y-hy,z-hz),(x-hx,y-hy,z+hz),(x-hx,y+hy,z+hz)],[(x+hx,y-hy,z-hz),(x+hx,y+hy,z-hz),(x+hx,y+hy,z+hz),(x+hx,y-hy,z+hz)],[(x-hx,y-hy,z+hz),(x+hx,y-hy,z+hz),(x+hx,y+hy,z+hz),(x-hx,y+hy,z+hz)]]
    for quad in corners:
        a,b,c,d=map(Vector,quad);nx=max(1,math.ceil((b-a).length/tile));ny=max(1,math.ceil((d-a).length/tile))
        for i in range(nx):
            for j in range(ny):
                n=len(verts);verts.extend(tuple(a+(b-a)*u+(d-a)*v) for u,v in [(i/nx,j/ny),((i+1)/nx,j/ny),((i+1)/nx,(j+1)/ny),(i/nx,(j+1)/ny)]);faces.append((n,n+1,n+2,n+3))
    o=mesh(name,verts,faces,mat,color,collection,False,pc)
    if collision:
        occupied.append((x-hx,y-hy,x+hx,y+hy,z-hz,z+hz))
        c=box('COL '+name,p,size,mat,color,'09_Collision',True);c['s2_export']='collision';c.hide_render=True
    return o
def tower(name,x,y,z,r=2.8,h=10,collection='11_West_Castle',roof=True):
    occupied.append((x-r-.25,y-r-.25,x+r+.25,y+r+.25,z-.5,z+h))
    tube(name+' foundation',[(x,y,z-.5),(x,y,z+.45)],[r+.5,r+.5],STONE,(.62,.69,.69),collection,10,True)
    rings=max(1,math.ceil(h/2.8))
    tube(name+' shaft',[(x,y,z+h*i/rings) for i in range(rings+1)],[r*(1-.12*i/rings) for i in range(rings+1)],PLASTER,(.78,.80,.77),collection,10,True)
    for zz in (z+1,z+h-1.0,z+h):tube(name+' gold belt',[(x,y,zz),(x,y,zz+.24)],[r+.15,r+.15],BRONZE,(.70,.63,.46),collection,10)
    for angle in (0,math.pi/2,math.pi,3*math.pi/2):
        d=Vector((math.sin(angle),-math.cos(angle),0));s=Vector((math.cos(angle),math.sin(angle),0));p=Vector((x,y,z+h*.55))+d*(r*.97)
        mesh(name+' lancet',[tuple(p-s*.45),tuple(p+s*.45),tuple(p+s*.45+Vector((0,0,2.1))),tuple(p-s*.45+Vector((0,0,2.1)))],[(0,1,2,3)],WINDOW,(.90,.89,.84),collection,False)
    if roof:
        tube(name+' shingle cap',[(x,y,z+h+.1),(x,y,z+h+4.6)],[r+1.05,.03],ROOF,(.55,.71,.62),collection,10,True)
        ico(name+' finial',(x,y,z+h+4.7),.26,BRONZE,(.91,.75,.44),collection)
    else:
        for i in range(10):
            a=i*math.tau/10;box(name+' crown tooth',(x+math.cos(a)*r,y+math.sin(a)*r,z+h+.6),(.6,.6,1.1),STONE,(.77,.78,.74),collection,True)
def wall(name,a,b,z,h=5,collection='11_West_Castle'):
    a,b=Vector(a),Vector(b);d=b-a;mid=(a+b)/2
    size=(abs(d.x)+1.0,abs(d.y)+1.0,h)
    block(name,(mid.x,mid.y,z+h/2),size,STONE,(.72,.76,.75),collection)
    block(name+' cornice',(mid.x,mid.y,z+h-.1),(size[0]+.25,size[1]+.25,.35),BRONZE,(.62,.60,.46),collection,False,tile=2)
    n=math.ceil(d.length/1.8)
    for i in range(n+1):
        p=a.lerp(b,i/n);box(name+' crenel',(p.x,p.y,z+h+.45),(1.05,1.05,.95),STONE,(.78,.79,.76),collection,True)
def castle(name,rect,z,collection,height=5,tower_height=11,east_gate=None,west_gate=None):
    x0,y0,x1,y1=rect;cx=(x0+x1)/2
    for y,side in [(y0,'south'),(y1,'north')]:
        wall(name+' '+side+' W',(x0,y),(cx-4.3,y),z,height,collection)
        wall(name+' '+side+' E',(cx+4.3,y),(x1,y),z,height,collection)
        arch(name+' '+side+' gate',(cx,y,z),7.3,8.2,1.2,collection=collection)
    if west_gate:
        wall(name+' west S',(x0,y0),(x0,west_gate-3.5),z,height,collection)
        wall(name+' west N',(x0,west_gate+3.5),(x0,y1),z,height,collection)
        arch(name+' broken west gate',(x0,west_gate,z),5.8,7.4,1.2,collection=collection,angle=math.pi/2)
    else:wall(name+' west',(x0,y0),(x0,y1),z,height,collection)
    if east_gate:
        wall(name+' east S',(x1,y0),(x1,east_gate-3.5),z,height,collection)
        wall(name+' east N',(x1,east_gate+3.5),(x1,y1),z,height,collection)
        arch(name+' side gate',(x1,east_gate,z),5.8,7.4,1.2,collection=collection,angle=math.pi/2)
    else:wall(name+' east',(x1,y0),(x1,y1),z,height,collection)
    for i,(x,y) in enumerate([(x0,y0),(x1,y0),(x0,y1),(x1,y1)]):tower(name+' tower '+str(i+1),x,y,z,2.7,tower_height+(i%2)*1.5,collection)
    # Narrow walkable paving ribbons avoid layering a flat courtyard over
    # sloped approach terrain or baking collisions into decoration.
    ribbon(name+' courtyard',[(cx,y0+.8,z+.04),(cx,y1-.8,z+.04)],6,PAVING,False,collection)
block('Orchard bridge turning landing',(25,-31,1.02),(5.2,5.2,.56),PAVING,(.76,.79,.74),'17_New_Terrain')
tube('Orchard bridge turning pier',[(25,-31,-3.8),(25,-31,.74)],[1.5,1.2],STONE,(.54,.63,.64),'17_New_Terrain',8,True)
castle('Briarwatch',(-83,10,-51,42),8,'11_West_Castle',west_gate=24)
castle('Mooncrown',(-22,76,22,118),16,'12_North_Castle',6,15,88)
tower('Mooncrown high keep W',-11,108,16,3.1,20,'12_North_Castle')
tower('Mooncrown high keep E',11,108,16,3.1,20,'12_North_Castle')
arch('Mooncrown throne porch',(0,108,16),11,13,1.2,collection='12_North_Castle')
block('High keep balcony',(0,112,25),(15,3,.7),PLASTER,(.81,.79,.75),'12_North_Castle',True)
for x in (-7,7):tower('Throne pinnacle',x,114,25,.8,5,'12_North_Castle')

# A second entrance silhouette is immediately visible from the original spawn.
for x in (-7,7):tower('Arrival watchtower',x,-43,0,1.8,6.5,'05_Ruins')
for x in (-11,11):block('Arrival garden wall',(x,-39,1.2),(3.5,1.0,2.4),STONE,(.65,.73,.69),'05_Ruins',True)

def cottage(name,x,y,z,w=4.6,d=5,h=3.2,collection='14_Village',flat=False):
    block(name+' house',(x,y,z+h/2),(w,d,h),FACADE,(.80,.76,.75),collection)
    if flat:
        block(name+' roof',(x,y,z+h+.1),(w+.15,d+.15,.2),PAVING,(.76,.79,.74),collection)
    else:
        v=[(x-w*.62,y-d*.62,z+h),(x+w*.62,y-d*.62,z+h),(x,y-d*.62,z+h+2.4),(x-w*.62,y+d*.62,z+h),(x+w*.62,y+d*.62,z+h),(x,y+d*.62,z+h+2.4)]
        mesh(name+' gabled roof',v,[(0,3,5,2),(1,2,5,4),(0,2,1),(3,4,5)],ROOF,(.55,.67,.63),collection,True)
        block(name+' chimney',(x+w*.23,y+.8,z+h+2),(.6,.7,2),STONE,(.61,.65,.63),collection,False,True)
    mesh(name+' door',[(x-.52,y-d/2-.015,z),(x+.52,y-d/2-.015,z),(x+.52,y-d/2-.015,z+1.6),(x-.52,y-d/2-.015,z+1.6)],[(0,1,2,3)],DOOR,(.86,.78,.68),collection)
    for dx in (-w*.32,w*.32):
        mesh(name+' window',[(x+dx-.35,y-d/2-.02,z+1.5),(x+dx+.35,y-d/2-.02,z+1.5),(x+dx+.35,y-d/2-.02,z+2.6),(x+dx-.35,y-d/2-.02,z+2.6)],[(0,1,2,3)],WINDOW,(.86,.86,.78),collection)
for i,(x,y,w,d) in enumerate([(53,-5,4,5),(73,-6,5,5),(54,-25,5,4),(72,-25,5,5),(76,-15,3.8,5),(50,-12,3.2,3.2),(66,-27,4.2,3.2)]):cottage('Weeping Village '+str(i+1),x,y,4,w,d)
for i,(x,y,z) in enumerate([(-78,18,8),(-56,19,8),(-56,34,8),(-16,88,16),(15,97,16)]):cottage('Castle lodge '+str(i+1),x,y,z,4.2,5,3.5,'11_West_Castle' if x<-40 else '12_North_Castle')
for x in (-13,13):cottage('Mooncrown great hall',x,108,16,8.2,13,7.5,'12_North_Castle')
tower('Village chapel',77,-29,4,2.3,9,'14_Village')
for y in (24,34):arch('Ruined spring chapel',(62,y,10),6.2,7.5,.8,collection='15_Aqueduct')
for x in (58.6,65.4):
    block('Spring chapel broken nave',(x,29,14),(.7,10,.6),PLASTER,(.73,.77,.72),'15_Aqueduct')
    for y in (24,34):tower('Chapel corner',x,y,10,.65,7.5,'15_Aqueduct')

# The central cloister is an open colonnade, not a solid wall across a road.
for x in (-12,12):
    for y in (40,46,52):
        arch('Cloister arcade',(x,y,8),4.8,6.2,.65,collection='13_Cloisters',angle=math.pi/2)
    block('Cloister entablature',(x,46,14.3),(.7,17,.5),BRONZE,(.68,.67,.52),'13_Cloisters',False)
for x in (-7,7):
    tube('Cloister basin',[(x,44,8.1),(x,44,8.8)],[1.2,1.6],PLASTER,(.77,.80,.75),'13_Cloisters',8,True)
    ico('Cloister pearl',(x,44,9),.32,GLOW,(.57,.83,.67),'08_PC_Detail',pc=True)
for i in range(5):arch('Grand aqueduct',(60+i*4.7,44,2.3),4.2,7.0,.8,collection='15_Aqueduct')
ribbon('Aqueduct parapet',[(58,44,9.96),(82,44,9.96)],2.4,ROAD,True,'15_Aqueduct')
for x in (58,82):tower('Aqueduct sentinel',x,44,10,1.2,4,'15_Aqueduct',False)

# Four measured jump courses. The main route never requires an upgrade or
# cheat; these branches use normal jump/held-jump and reward rooftop exploration.
courses=[]
def course(name,centres,width,approach,exit_path=None,roof=False):
    approach=list(approach)
    end=Vector(approach[-1]);heading=end-Vector(approach[-2]);heading.z=0;heading.normalize()
    half_depth=2 if roof else width/2
    extent=min(width/2/max(abs(heading.x),.00001),half_depth/max(abs(heading.y),.00001))
    lip=end-heading*(extent+.5);lip.z=end.z
    approach.insert(-1,tuple(lip))
    ribbon(name+' approach',approach,2.5,ROAD,True,'16_Platforming',offset=.025)
    points=[{'position':list(p),'jump':False} for p in approach]
    for i,p in enumerate(centres):
        x,y,z=p
        if roof:cottage(name+' roof '+str(i),x,y,4,width,4,z-4-.2,'16_Platforming',True)
        else:
            block(name+' landing '+str(i),(x,y,z-.25),(width,width,.5),PAVING,(.81,.80,.73),'16_Platforming')
            tube(name+' broken pier',[(x,y,-3.8),(x,y,z-.5)],[.75,.62],STONE,(.58,.67,.66),'16_Platforming',6,True)
            block(name+' gold lip '+str(i),(x,y,z-.45),(width+.14,width+.14,.16),BRONZE,(.71,.64,.43),'16_Platforming',False)
        actor(1,(x,y,z+.18),gem=5 if i==len(centres)-1 else 2,label=name+' reward '+str(i))
        if i:
            before=Vector(centres[i-1]);towards=(Vector(p)-before);towards.z=0;towards.normalize()
            launch=before+towards*(width/2-.4)
            points.append({'position':list(launch),'jump':False})
            points.append({'position':list(p),'jump':True})
    if exit_path:
        exit_path=list(exit_path);start=Vector(exit_path[0]);heading=Vector(exit_path[1])-start;heading.z=0;heading.normalize()
        lip=start+heading*(width/2+.5);lip.z=start.z;exit_path.insert(1,tuple(lip))
        ribbon(name+' return',exit_path,2.5,ROAD,True,'16_Platforming',offset=.025);points.extend({'position':list(p),'jump':False} for p in exit_path[1:])
    courses.append({'name':name,'waypoints':points,'gaps':len(centres)-1})
course('Briarwatch wall jumps',[(-88,24+i*3.9,11.5+i*.18) for i in range(6)],3.0,[(-76,24,8),(-88,24,11.5)])
course('Village roof run',[(50+i*4.2,-18,6+i*.16) for i in range(6)],3.2,[(47,-28,4),(50,-18,6)],roof=True)
course('Broken aqueduct',[(86,22+i*3.9,12+i*.18) for i in range(7)],3.0,[(78,22,10),(86,22,12)],[(86,45.4,13.08),(62,50,10)])
course('Moonstep ascent',[(27,60+i*3.9,12.3+i*.2) for i in range(6)],3.0,[(18,49,9),(27,60,12.3)],[(27,79.5,13.3),(28,88,16),(22,88,16),(16,88,16)])

# Density comes from coherent planted courtyards and streets. Reserve the
# actual movement/camera corridor before placing any solid scenery.
def clear_of_route(x,y,clearance=4.0):return nearest(x,y,[main])[0]>clearance
def tree_at(x,y,z,index):
    h=rng.uniform(4.6,6.7);lean=rng.uniform(-.65,.65)
    tube('Spring plum '+str(index),[(x,y,z),(x+.12,y,z+2),(x+lean,y+.2,z+h)],[.32,.25,.04],BARK,(.43,.47,.47),'18_Dense_Gardens',6)
    c=tube('COL spring trunk',[(x,y,z),(x+.12,y,z+2.8)],[.31,.20],STONE,(.4,.4,.4),'09_Collision',5,True);c['s2_export']='collision';c.hide_render=True
    for j in range(3):
        a=j*math.tau/3+index;tip=(x+math.cos(a)*1.25,y+math.sin(a)*1.25,z+h-.5)
        tube('Spring fork',[(x+lean*.5,y,z+h*.62),tip],[.16,.025],BARK,(.47,.48,.45),'18_Dense_Gardens',5)
        ico('Rose ash blossom',tip,1.32,PETAL,(.78,.57+.04*(j%2),.72),'18_Dense_Gardens',(1.25,1,.65),j==2)
    if index%3==0:ico('Fallen petal bed',(x+.2,y,z+.05),1.0,PETAL,(.59,.42,.53),'08_PC_Detail',(1.2,1,.04),True)
tree_sites=[]
for _ in range(1500):
    x=rng.uniform(-92,88);y=rng.uniform(-30,122)
    if not land(x,y) or not clear_of_route(x,y,4.4):continue
    if any(x0-3<x<x1+3 and y0-3<y<y1+3 for _,(x0,y0,x1,y1),_ in rooms):continue
    if any(math.hypot(x-a,y-b)<4.0 for a,b in tree_sites):continue
    if x>82 or (23<x<31 and y>53):continue
    tree_sites.append((x,y));tree_at(x,y,ground(x,y),len(tree_sites))
    if len(tree_sites)>=78:break
for i,(x,y,z) in enumerate([(-77,35,8),(-57,26,8),(-17,99,16),(16,81,16),(9,116,16),(57,-10,4),(68,-4,4),(-8,48,8),(8,40,8)]):
    if clear_of_route(x,y,3.3):tree_at(x,y,z,100+i)
for path in roads:
    for a,b in zip(path,path[1:]):
        a,b=Vector(a),Vector(b);d=b-a;side=Vector((-d.y,d.x,0)).normalized()
        for i in range(1,max(1,int(d.length/7))):
            p=a.lerp(b,i/max(1,int(d.length/7)))+side*3.8
            if not land(p.x,p.y):continue
            p.z=ground(p.x,p.y)
            if any(x0-1<p.x<x1+1 and y0-1<p.y<y1+1 for _,(x0,y0,x1,y1),_ in rooms):continue
            tube('Wayfinder lantern',[p,p+Vector((0,0,1.8))],[.09,.07],BRONZE,(.44,.52,.46),'18_Dense_Gardens',5)
            box('Wayfinder lantern cap',p+Vector((0,0,2.0)),(.46,.46,.55),BRONZE,(.54,.54,.43),'18_Dense_Gardens',False)
            ico('Wayfinder warm pane',p+Vector((0,-.22,2.0)),.18,GLOW,(.98,.64,.30),'08_PC_Detail',(.85,.15,1.2),True)
            for j in range(4):
                q=p+side*(.7+j*.25);q.z=ground(q.x,q.y)
                ico('Crocus bed',q+Vector((0,0,.2)),.24,PETAL,(.78,.61,.77),'08_PC_Detail',(.6,.6,1.4),True)
for x,y,z in [(-75,28,8),(-59,37,8),(-12,95,16),(13,90,16),(58,-3,4),(76,-11,4)]:
    for side in (-1,1):block('Garden seat leg',(x+side*.7,y,z+.25),(.3,.55,.5),STONE,(.62,.67,.66),'18_Dense_Gardens',True)
    block('Garden seat',(x,y,z+.55),(2,.65,.22),PLASTER,(.74,.76,.72),'18_Dense_Gardens',True)
for i in range(12):
    x=-45+(i%4)*2.2;y=49+(i//4)*2.4
    if clear_of_route(x,y,3.4):block('Memorial stele',(x,y,ground(x,y)+.6),(.7,.3,1.2),PLASTER,(.67,.73,.72),'18_Dense_Gardens',True)

# Distant valley ridges move outside the new playable footprint.
for ring in range(2):
    verts=[];n=72;r=180+ring*35
    for i in range(n):
        a=i*math.tau/n;h=22+18*math.sin(a*4+.6*ring)**2+9*math.sin(a*11)**2
        verts.extend([(math.cos(a)*r,35+math.sin(a)*r,-4),(math.cos(a)*(r+8),35+math.sin(a)*(r+8),h)])
    mesh('Expanded valley ridge',verts,[(2*i,2*((i+1)%n),2*((i+1)%n)+1,2*i+1) for i in range(n)],PETAL,(.16+.02*ring,.20+.02*ring,.26+.02*ring),'08_PC_Detail',False,ring==1)
for o in scene.objects:
    if o.name.startswith(('The Stillwater Mirror','Lake bed')):o.scale.x=o.scale.y=1.7

# Place original retail actors on the actual collision surface, after the
# architecture is complete. Preview colour is not exported as a retail model.
bpy.context.view_layer.update()
verts=[];faces=[]
for o in scene.objects:
    if o.type!='MESH' or not o.get('collision') or o.get('s2_export') not in ('geometry','collision'):continue
    start=len(verts);verts.extend(o.matrix_world@v.co for v in o.data.vertices)
    faces.extend(tuple(start+i for i in p.vertices) for p in o.data.polygons)
floor_bvh=BVHTree.FromPolygons(verts,faces)
def floor_at(x,y,z):
    if any(x0-.4<x<x1+.4 and y0-.4<y<y1+.4 and lo-.1<z+.35<hi-.05 for x0,y0,x1,y1,lo,hi in occupied):return None
    p,n,_,_=floor_bvh.ray_cast(Vector((x,y,z+.7)),Vector((0,0,-1)),2)
    if p is None or n.z<.8 or abs(p.z-z)>.6:return None
    return p.z
new_actors=[]
for path in roads:
    for a,b in zip(path,path[1:]):
        n=max(1,math.ceil(math.dist(a,b)/2.8))
        for i in range(1,n):
            p=Vector(a).lerp(Vector(b),i/n);z=floor_at(p.x,p.y,p.z)
            if z is not None:actor(1,(p.x,p.y,z+.18),gem=1 if i%5 else 2)
for i,(x,y,z) in enumerate([(-64,20,8),(-72,33,8),(-56,14,8),(-55,40,8),(-39,55,11),(-16,49,9),(16,49,9),(40,53,10),(63,39,10),(78,29,10),(73,13,7.6),(65,-7,4),(55,-21,4),(41,-24,2.8),(-12,84,16),(12,83,16),(-7,101,16),(8,102,16)]):
    h=floor_at(x,y,z)
    if h is not None:actor(555 if i%3==0 else 496,(x,y,h),yaw=math.pi*(i%2),label='Castle guardian '+str(i+1))
for i,(x,y,z) in enumerate([(-72,15,8),(-63,17,8),(-76,37,8),(-57,39,8),(-49,49,10.5),(-36,55,10.9),(-12,43,8),(12,43,8),(36,53,9.9),(58,40,10),(77,37,10),(78,23,10),(67,-5,4),(60,-23,4),(74,-21,4),(46,-24,3.5),(-14,82,16),(14,82,16),(-16,104,16),(16,105,16),(-4,114,16),(4,114,16)]):
    h=floor_at(x,y,z)
    if h is not None:actor(200,(x,y,h),gem=5,label='Courtyard basket '+str(i+1))
# Relocate the original exit to the final castle; no source-level portal remains.
exit_portal=next(o for o in scene.objects if o.get('class_id')==426)
exit_portal.location=(0,112,16);exit_portal.name='Return Home / Mooncrown sanctum'
for i,p in enumerate(main):marker(f'Expansion route {i:02}',p,'route',index=i,minimum_clearance=2.3)
for name,rect,z in rooms:marker(name,((rect[0]+rect[2])/2,(rect[1]+rect[3])/2,z),'region')
for item in courses:marker(item['name'],item['waypoints'][0]['position'],'platform_course',jumps=item['gaps'])

scene['route_length']=sum(math.dist(a,b) for a,b in zip(main,main[1:]))
scene['platform_courses']=len(courses);scene['authored_jumps']=sum(c['gaps'] for c in courses)
scene['authoring_profile']='Expanded PC master with separate simplified collisions and removable detail; PS1 reduction remains a visibility/packing study.'
scene.camera=camera('Expansion 01 / complete valley',(150,-180,165),(0,37,6),45)
camera('Expansion 02 / Mooncrown approach',(-26,56,23),(0,94,29),28)
camera('Expansion 03 / Briarwatch battlements',(-102,-1,31),(-65,29,12),32)
camera('Expansion 04 / village and aqueduct',(98,-47,26),(65,20,10),32)
camera('Expansion 05 / master plan',(0,35,235),(0,35,0),ortho=245)
area('Expanded moonlight',(-65,50,85),34000,(.68,.79,1),95,(0,55,10))
area('Expanded spring dusk',(65,-30,65),22000,(1,.67,.66),75,(0,35,9))
area('Mooncrown green lantern light',(0,102,39),2300,(.70,.91,.73),25,(0,96,16))
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1920;scene.render.resolution_y=1080;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
collections['09_Collision'].hide_render=True
bpy.context.view_layer.update()
manifest={'schema':2,'id':'hollowbloom-springs','revision':'castle-expansion','route':main,'route_length':scene['route_length'],'regions':[{'name':name,'rectangle':r,'height':z} for name,r,z in rooms],'courses':courses,'added_trees':len(tree_sites),'new_castles':2,'retail_instances':len(placements),'coordinates':'Blender Z-up; 1 unit = 1024 retail units','original_preserved':'revisions/compact-v1','ps1_status':'PC master and geometry reduction only; no WAD, visibility, overlay or hardware certification'}
(ROOT/'source'/'expansion-design.json').write_text(json.dumps(manifest,indent=2))
lines=['S2COURSES 1',str(len(courses))]
for c in courses:
    lines.extend([json.dumps(c['name']),str(len(c['waypoints']))])
    for w in c['waypoints']:
        x,y,z=w['position'];lines.append(f'{x} {z} {y} {int(w["jump"])}')
(ROOT/'source'/'platforming.s2courses').write_text('\n'.join(lines)+'\n')
for im in bpy.data.images:
    if im.source=='FILE' and im.filepath:im.pack()
bpy.data.libraries.write(str(ROOT/'Hollowbloom Springs - Expanded.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
result={'scene':scene.name,'objects':len(scene.objects),'retail_instances':len(placements),'new_trees':len(tree_sites),'main_route_length':scene['route_length'],'courses':len(courses),'jumps':scene['authored_jumps'],'file':str(ROOT/'Hollowbloom Springs - Expanded.blend')}

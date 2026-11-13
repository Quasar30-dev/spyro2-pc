"""Executed through Blender MCP. Authored level, retail-derived preview assets.

Blender axes: X/right, Y/forward, Z/up. Runtime: X/right, Y/up, Z/forward.
One Blender unit is 1,024 retail units, matching the PC engine's conversion.
"""
import bpy, math, json, random
from pathlib import Path
from mathutils import Vector

ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/hollowbloom-springs')
ASSETS=ROOT/'assets'
rng=random.Random(23092026)
scene=bpy.data.scenes.new('Hollowbloom Springs')
bpy.context.window.scene=scene
scene.unit_settings.system='NONE'
scene['level_id']='hollowbloom-springs'
scene['retail_units_per_blender_unit']=1024
scene['design']='An abandoned spring orchard surrounding a drowned bell conservatory.'
scene['ps1_status']='Reduction profile only; no retail WAD packing or hardware certification.'
collections={}
for name in ['00_Gameplay','01_Terrain','02_Causeway','03_Conservatory','04_Orchard','05_Ruins','06_Water','07_Retail_Mobys','08_PC_Detail','09_Collision','10_Views']:
    c=bpy.data.collections.new(name);scene.collection.children.link(c);collections[name]=c
materials={}; material_specs=[]
def material(name,slot=None,tile=0,emission=0.24,alpha=1):
    m=bpy.data.materials.new(name);m.use_nodes=True
    m['source_slot']=slot if slot is not None else -1;m['source_tile']=tile;m['runtime_alpha']=alpha
    m['s2_material']=len(material_specs);material_specs.append({'name':name,'slot':slot or 0,'tile':tile,'alpha':alpha})
    n=m.node_tree.nodes;n.clear();links=m.node_tree.links
    out=n.new('ShaderNodeOutputMaterial');bs=n.new('ShaderNodeBsdfPrincipled');bs.inputs['Roughness'].default_value=.95
    col=n.new('ShaderNodeVertexColor');col.layer_name='Color'
    if slot:
        atlas=bpy.data.images.load(str(ASSETS/f'terrain-{slot}.png'),check_existing=True)
        # Individual tiles make the authoring UVs portable and prevent atlas
        # sampling from leaking into adjacent PS1 texture pages.
        im=bpy.data.images.new(f'Retail tile {slot}-{tile}',width=64,height=64,alpha=True)
        pixels=list(atlas.pixels);aw,ah=atlas.size;tx=(tile%16)*64;ty=ah-(tile//16+1)*64
        im.pixels=[c for y in range(64) for c in pixels[((ty+y)*aw+tx)*4:((ty+y)*aw+tx+64)*4]]
        im.pack()
        tex=n.new('ShaderNodeTexImage');tex.image=im;tex.interpolation='Closest'
        coord=n.new('ShaderNodeUVMap');coord.uv_map='UVMap';links.new(coord.outputs['UV'],tex.inputs['Vector'])
        mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
        links.new(tex.outputs['Color'],mix.inputs[1]);links.new(col.outputs['Color'],mix.inputs[2]);color=mix.outputs[0]
    else:color=col.outputs['Color']
    links.new(color,bs.inputs['Base Color']);links.new(color,bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=emission
    bs.inputs['Alpha'].default_value=alpha
    if alpha<1:m.surface_render_method='DITHERED'
    links.new(bs.outputs[0],out.inputs[0]);materials[name]=m;return m
material('Grass / Autumn Plains 42',33,42)
material('Cobble / Autumn Plains 7',33,7)
material('Moss masonry / Autumn Plains 65',33,65)
material('Pale plaster / Autumn Plains 54',33,54)
material('Bark / Autumn Plains 3',33,3)
material('Bronze ornament / Autumn Plains 28',33,28)
material('Pool / Mystic Marsh 1',59,1,.38,.42)
material('Leaf / Mystic Marsh 48',59,48)
material('Petal / vertex colour',emission=.32)
material('Lantern / vertex colour',emission=2.2)
GRASS,ROAD,STONE,PLASTER,BARK,BRONZE,WATER,LEAF,PETAL,GLOW=list(materials.values())

def mesh(name,verts,faces,mat,color=(1,1,1),collection='05_Ruins',collision=False,pc=False,uvs=None):
    data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces);data.update();data.validate()
    o=bpy.data.objects.new(name,data);collections[collection].objects.link(o);data.materials.append(mat)
    o['s2_export']='geometry';o['collision']=collision;o['pc_only']=pc
    center=sum((Vector(v) for v in verts),Vector())/max(1,len(verts));o['sector']=f'{math.floor(center.x/16)},{math.floor(center.y/16)}'
    o['ps1_priority']=2 if pc else 0
    uv=data.uv_layers.new(name='UVMap');colors=data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    slot=mat['source_slot'];tile=mat['source_tile'];height=64
    if slot>=0:height=json.loads((ASSETS/f'terrain-{slot}.json').read_text())['height']
    for p in data.polygons:
        shade=.86+.14*max(0,p.normal.z)
        for j,li in enumerate(p.loop_indices):
            xy=uvs[p.index][j] if uvs else [(0,0),(1,0),(1,1),(0,1)][j%4]
            if slot>=0:xy=((.5+xy[0]*63)/64,(.5+xy[1]*63)/64)
            uv.data[li].uv=xy;colors.data[li].color=(*[min(1,c*shade) for c in color],1)
    data.update()
    return o
def box(name,center,size,mat,color,collection='05_Ruins',collision=True,pc=False):
    x,y,z=center;a,b,c=[v/2 for v in size]
    verts=[(x+i*a,y+j*b,z+k*c) for k in (-1,1) for j in (-1,1) for i in (-1,1)]
    return mesh(name,verts,[(0,2,3,1),(4,5,7,6),(0,1,5,4),(1,3,7,5),(3,2,6,7),(2,0,4,6)],mat,color,collection,collision,pc)
def tube(name,points,radii,mat,color,collection='04_Orchard',sides=6,collision=False,pc=False):
    verts=[]
    for k,p in enumerate(points):
        tangent=Vector(points[min(k+1,len(points)-1)])-Vector(points[max(0,k-1)])
        tangent.normalize();u=tangent.cross(Vector((0,0,1)))
        if u.length<.01:u=Vector((1,0,0))
        u.normalize();v=tangent.cross(u).normalized()
        verts.extend(tuple(Vector(p)+radii[k]*(u*math.cos(i*math.tau/sides)+v*math.sin(i*math.tau/sides))) for i in range(sides))
    faces=[tuple(reversed(range(sides)))]
    for k in range(len(points)-1):
        for i in range(sides):faces.append((k*sides+i,k*sides+(i+1)%sides,(k+1)*sides+(i+1)%sides,(k+1)*sides+i))
    faces.append(tuple(range((len(points)-1)*sides,len(points)*sides)))
    return mesh(name,verts,faces,mat,color,collection,collision,pc)
def ico(name,p,r,mat,color,collection='04_Orchard',scale=(1,1,1),pc=False):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=r,location=p)
    o=bpy.context.object;o.name=name
    verts=[tuple(o.matrix_world@Vector((v.co.x*scale[0],v.co.y*scale[1],v.co.z*scale[2]))) for v in o.data.vertices]
    faces=[tuple(f.vertices) for f in o.data.polygons]
    # This temporary operator-created primitive belongs to this script only.
    bpy.data.objects.remove(o,do_unlink=True)
    return mesh(name,verts,faces,mat,color,collection,False,pc)

# Clockwise route; the arrival basin is always clear and nearly level.
route=[(0,-38,0),(-15,-31,1),(-25,-17,2),(-28,2,3),(-21,18,4),(-7,27,5),(7,27,5),(22,17,4),(28,0,2.5),(23,-19,1.5),(13,-32,.5),(0,-38,0)]
rooms=[(0,-38,0),(-25,-17,2),(-28,2,3),(0,27,5),(22,17,4),(23,-19,1.5)]
def nearest(x,y):
    best=(1e9,0,0,0)
    for a,b in zip(route,route[1:]):
        dx=b[0]-a[0];dy=b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
        qx=a[0]+t*dx;qy=a[1]+t*dy;d=math.hypot(x-qx,y-qy)
        if d<best[0]:best=(d,a[2]+t*(b[2]-a[2]),qx,qy)
    return best
def ground(x,y):
    d,z,_,_=nearest(x,y)
    for a,b,h in rooms:
        w=max(0,min(1,(7-math.hypot(x-a,y-b))/3))
        z=z*(1-w)+h*w
    return z+max(0,min(.7,(d-3)*.16))*(.45*math.sin(x*.37)+.25*math.cos(y*.43))
def land(x,y):return nearest(x,y)[0]<5.7 or any(math.hypot(x-a,y-b)<9 for a,b,_ in rooms)

# Chunked, closed terrain shell, with a clear 4.6-unit road corridor.
sectors={};step=2
cells={(x,y) for x in range(-40,42,step) for y in range(-48,42,step) if land(x+1,y+1)}
for x,y in sorted(cells):
    key=(x//16,y//16);v,f=sectors.setdefault(key,([],[]));n=len(v)
    v.extend([(x,y,ground(x,y)),(x+step,y,ground(x+step,y)),(x+step,y+step,ground(x+step,y+step)),(x,y+step,ground(x,y+step))]);f.append((n,n+1,n+2,n+3))
for key,(v,f) in sectors.items():mesh(f'Terrain {key[0]:+03d} {key[1]:+03d}',v,f,GRASS,(.63,.77,.68),'01_Terrain',True)
for x,y in sorted(cells):
    for dx,dy,a,b in [(0,-step,(x,y),(x+step,y)),(step,0,(x+step,y),(x+step,y+step)),(0,step,(x+step,y+step),(x,y+step)),(-step,0,(x,y+step),(x,y))]:
        if (x+dx,y+dy) in cells:continue
        aa=(*a,ground(*a));bb=(*b,ground(*b))
        mesh('Moss bank', [aa,bb,(b[0],b[1],-4),(a[0],a[1],-4)],[(0,3,2,1)],STONE,(.43,.52,.56),'01_Terrain',True,uvs=[[(0,1),(0,0),(1,0),(1,1)]])
route_sides=[]
for i,p in enumerate(route[:-1]):
    previous=Vector(p)-Vector(route[(i-1)%(len(route)-1)]);following=Vector(route[(i+1)%(len(route)-1)])-Vector(p)
    previous.z=following.z=0;previous.normalize();following.normalize()
    normal=Vector((-following.y,following.x,0));miter=(Vector((-previous.y,previous.x,0))+normal).normalized()
    route_sides.append(miter*min(2.8,1.85/max(.35,miter.dot(normal))))
route_sides.append(route_sides[0])
for seg,(a,b) in enumerate(zip(route,route[1:])):
    n=max(2,math.ceil(math.dist(a,b)/2))
    for k in range(n):
        p=Vector(a).lerp(Vector(b),k/n);q=Vector(a).lerp(Vector(b),(k+1)/n)
        left=route_sides[seg].lerp(route_sides[seg+1],k/n);right=route_sides[seg].lerp(route_sides[seg+1],(k+1)/n)
        points=[p-left,q-right,q+right,p+left];verts=[(v.x,v.y,ground(v.x,v.y)+.045) for v in points]
        mesh(f'Causeway {seg:02d}.{k:02d}',verts,[(0,1,2,3)],ROAD,(.83,.83,.78),'02_Causeway',False)

# Dark translucent water lies below the entire raised garden, with submerged floor.
mesh('The Stillwater Mirror',[(-150,-160,-1.4),(150,-160,-1.4),(150,140,-1.4),(-150,140,-1.4)],[(0,1,2,3)],WATER,(.16,.40,.43),'06_Water',False,uvs=[[(0,0),(1,0),(1,1),(0,1)]])
box('Lake bed',(0,-4,-4.4),(300,300,.8),STONE,(.17,.25,.27),'01_Terrain',True)

# The central landmark is a roofless bell conservatory, traversable underneath.
def arch(name,center,width,height,depth,mat=PLASTER,collection='03_Conservatory',angle=0):
    cx,cy,cz=center;half=width/2;thick=.45;verts=[];faces=[];segments=10;spring=height-width*.58
    for i in range(segments+1):
        t=math.pi-i*math.pi/segments
        # Raised crown produces the slightly pointed fairy-tale silhouette.
        z=spring+math.sin(t)*width*.58
        for side in (-1,1):
            for outer in (0,1):
                x=math.cos(t)*(half+outer*thick);zz=z+outer*thick
                xx=x*math.cos(angle)-side*depth/2*math.sin(angle);yy=x*math.sin(angle)+side*depth/2*math.cos(angle)
                verts.append((cx+xx,cy+yy,cz+zz))
    for i in range(segments):
        a=i*4;b=(i+1)*4
        faces.extend([(a,b,b+1,a+1),(a+2,a+3,b+3,b+2),(a,a+2,b+2,b),(a+1,b+1,b+3,a+3)])
    mesh(name+' crown',verts,faces,mat,(.86,.84,.79),collection,True)
    for sign in (-1,1):
        x=sign*(half+thick/2)
        box(name+' pier',(cx+x*math.cos(angle),cy+x*math.sin(angle),cz+spring/2),(.65,.75,spring),STONE,(.68,.76,.75),collection,True)
        box(name+' plinth',(cx+x*math.cos(angle),cy+x*math.sin(angle),cz+.18),(1.05,1.0,.36),STONE,(.60,.68,.67),collection,True)
arch('Conservatory south',(0,23,5),8.4,8.8,.7)
arch('Conservatory north',(0,31,5),8.4,8.8,.7)
for x in (-4.4,4.4):
    for y in (23,31):
        box('Buttress',(x,y,8.2),(.85,1.1,6.4),STONE,(.64,.71,.71),'03_Conservatory',True)
        box('Worn gold capital',(x,y,11.5),(1.15,1.3,.3),BRONZE,(.69,.61,.46),'03_Conservatory',False)
    box('Broken clerestory',(x,27,11.7),(.7,9,.7),PLASTER,(.77,.76,.75),'03_Conservatory',True)
tube('Bell suspension',[(0,27,14),(0,27,11.8)],[.12,.12],BRONZE,(.53,.55,.41),'03_Conservatory',8)
tube('The Silent Bell',[(0,27,11.4),(0,27,11.7),(0,27,12.9)],[1.5,1.15,.5],BRONZE,(.55,.65,.53),'03_Conservatory',12)
ico('Bell clapper',(0,27,11.1),.22,BRONZE,(.62,.55,.37),'03_Conservatory')
for x in (-4.4,4.4):
    for y in (23,31):
        tube('Crown spire',[(x,y,11.6),(x,y,14.7)],[.62,.02],STONE,(.64,.68,.65),'03_Conservatory',5)

# Arrival arch frames the landmark beyond the lake.
arch('Arrival gate',(0,-42,0),5.6,5,.65,collection='05_Ruins')
for pos in [(-27,-17),(-31,5),(24,-20),(23,15)]:
    x,y=pos;z=ground(x,y)
    for k in range(3):
        wx,wy=x+5.6,y+(k-1)*1.8;dist,_,qx,qy=nearest(wx,wy)
        if dist<4.3:
            wx=qx+(wx-qx)*4.3/max(.01,dist);wy=qy+(wy-qy)*4.3/max(.01,dist)
        box('Orchard ruined wall',(wx,wy,ground(wx,wy)+.65+k*.1),(1.1,1.6,1.3+k*.2),STONE,(.60,.66,.61),collision=True)
for x,y in [(-21,-13),(-29,7),(29,11),(18,-19)]:
    z=ground(x,y);tube('Empty reflecting font',[(x,y,z+.1),(x,y,z+.6),(x,y,z+.9)],[.65,.42,1.0],STONE,(.71,.75,.70),sides=8,collision=True)

# Crooked trunks and clustered blossom crowns; clear both path and camera corridor.
tree_sites=[]
candidate_trees=[(-32,-24),(-30,-13),(-34,-2),(-35,10),(-23,24),(-16,30),(-8,34),(9,34),(18,25),(29,21),(35,9),(34,-4),(31,-23),(18,-31),(-9,-41),(-14,-28),(-21,4),(21,-5),(16,-23),(-17,-19),(15,19)]
for cx,cy,cz in rooms:
    for i in range(10):
        a=i*math.tau/10;candidate_trees.append((cx+math.cos(a)*7.2,cy+math.sin(a)*7.2))
for x,y in candidate_trees:
    if any(math.hypot(x-a,y-b)<3.2 for a,b,_ in tree_sites):continue
    if not land(x,y) or nearest(x,y)[0]<4.6:continue
    z=ground(x,y);tree_sites.append((x,y,z));h=rng.uniform(4.7,6.5);lean=rng.uniform(-.9,.9)
    trunk=[(x,y,z),(x+.2,y,z+1.8),(x+lean,y+.3,z+h*.65),(x+lean*.8,y-.15,z+h)]
    tube('Crooked plum trunk',trunk,[.44,.32,.24,.08],BARK,(.48,.53,.52),collision=False)
    # Collision is deliberately simpler than the visible branching silhouette.
    o=tube('COL trunk',trunk[:3],[.43,.34,.22],STONE,(.3,.3,.3),'09_Collision',6,True);o.hide_render=True;o.display_type='WIRE';o['s2_export']='collision'
    for branch in range(3):
        a=branch*math.tau/3+rng.random();tip=(x+math.cos(a)*1.65,y+math.sin(a)*1.65,z+h-rng.uniform(.3,1.5))
        tube('Reaching bough',[trunk[2],(tip[0]*.4+x*.6,tip[1]*.4+y*.6,z+h*.8),tip],[.2,.13,.03],BARK,(.49,.51,.50))
        for k in range(3):
            p=(tip[0]+rng.uniform(-.6,.6),tip[1]+rng.uniform(-.6,.6),tip[2]+rng.uniform(-.2,.8))
            tint=rng.choice([(.83,.69,.76),(.91,.81,.84),(.73,.65,.79),(.77,.84,.78)])
            ico('Pale plum blossom',p,rng.uniform(1.0,1.55),PETAL,tint,scale=(1.25,1,.62),pc=k==2)

# Drowned orchard silhouettes, deliberately outside the playable landing/path.
for x,y in [(-12,-2),(12,7),(-9,12),(4,-15)]:
    tube('Drowned willow',[(x,y,-3.8),(x-.2,y,-.2),(x+.4,y,2.4),(x+1,y-.4,4.6)],[.48,.37,.24,.03],BARK,(.29,.40,.42),'04_Orchard',6)
    for sign in (-1,1):tube('Willow hand',[(x+.3,y,2.2),(x+sign*1.8,y+.6,3),(x+sign*2.8,y+.7,2.6)],[.22,.13,.015],BARK,(.31,.41,.44))

# Lanterns lead the loop. Pale tulips and reeds live outside the walking strip.
for i,(a,b) in enumerate(zip(route,route[1:])):
    for t in (.23,.73):
        p=Vector(a).lerp(Vector(b),t);d=Vector(b)-Vector(a);side=Vector((-d.y,d.x,0)).normalized();p+=side*3.5;p.z=ground(p.x,p.y)
        tube('Garden lantern post',[p,p+Vector((0,0,1.7))],[.1,.07],BRONZE,(.41,.48,.43),'05_Ruins',5)
        box('Lantern housing',p+Vector((0,0,1.85)),(.42,.42,.6),BRONZE,(.52,.55,.42),collision=False)
        box('Honey light',p+Vector((0,-.225,1.9)),(.25,.025,.34),GLOW,(1,.63,.25),'08_PC_Detail',False,True)
        for j in range(3):
            q=p+side*(.55+j*.32)+Vector((rng.uniform(-.2,.2),rng.uniform(-.7,.7),0));q.z=ground(q.x,q.y)
            tube('Spring stalk',[q,q+Vector((0,0,.55))],[.035,.018],LEAF,(.44,.65,.49),'08_PC_Detail',4,False,True)
            ico('Ivory tulip',q+Vector((0,0,.58)),.16,PETAL,(.93,.86,.72),'08_PC_Detail',(.6,.6,1.4),True)
for i in range(90):
    a=rng.random()*math.tau;r=rng.uniform(9,17);x=math.cos(a)*r;y=math.sin(a)*r+2
    if land(x,y):continue
    p=(x,y,-1.35)
    tube('Marsh reed',[p,(x+.18,y,-.3-rng.random()*.4)],[.06,.012],LEAF,(.33,.50,.46),'08_PC_Detail',4,False,True)

# A secluded island breaks up the main circuit: a low moonwell under a
# huge flowering tree, reached by a narrow, gently descending stone bridge.
island_center=(-3,1,1.0);edge=[]
for i in range(18):
    a=i*math.tau/18;r=6.6+math.sin(a*3)*.5;edge.append((-3+math.cos(a)*r,1+math.sin(a)*r,1))
mesh('Moonwell island crown',[island_center]+edge,[(0,i+1,(i+1)%18+1) for i in range(18)],GRASS,(.64,.72,.69),'01_Terrain',True)
for i,a in enumerate(edge):
    b=edge[(i+1)%len(edge)];mesh('Moonwell island bank',[a,b,(b[0],b[1],-4),(a[0],a[1],-4)],[(0,3,2,1)],STONE,(.46,.54,.57),'01_Terrain',True,uvs=[[(0,1),(0,0),(1,0),(1,1)]])
optional_route=[(-23,15,ground(-23,15)+.07),(-15,10,2.6),(-8,5,1.5),(-3,1,1.08)]
for seg,(a,b) in enumerate(zip(optional_route,optional_route[1:])):
    d=Vector(b)-Vector(a);side=Vector((-d.y,d.x,0)).normalized()*1.7;n=math.ceil(d.length/2)
    for i in range(n):
        p=Vector(a).lerp(Vector(b),i/n);q=Vector(a).lerp(Vector(b),(i+1)/n)
        mesh('Moonwell footbridge',list(map(tuple,[p-side,q-side,q+side,p+side])),[(0,1,2,3)],ROAD,(.76,.79,.75),'02_Causeway',True)
        if i%2==0:
            box('Bridge support',(p.x,p.y,(p.z-3)/2),(1.2,1.2,p.z+3),STONE,(.42,.55,.56),collision=True)
            for sign in (-1,1):
                loc=p+side*sign;box('Broken bridge parapet',(loc.x,loc.y,loc.z+.35),(.35,.6,.7),STONE,(.73,.76,.72),collision=True)
for i in range(12):
    a=i*math.tau/12;x=-3+math.cos(a)*1.8;y=1+math.sin(a)*1.8
    box('Moonwell rim',(x,y,1.25),(.7,.7,.5),PLASTER,(.70,.83,.78),collision=True)
ico('Stillwater pearl',(-3,1,1.4),.68,GLOW,(.39,.71,.65),'08_PC_Detail',pc=True)
tube('Old flowering willow',[(-.5,3,1),(-.8,3,3.5),(-1.8,3,6),(-.5,3,9)],[.8,.6,.4,.09],BARK,(.39,.45,.47),collision=False)
for i in range(6):
    a=i*math.tau/6;p=(-.7+math.cos(a)*3.5,3+math.sin(a)*3,7.8+math.sin(a)*.5)
    tube('Old willow arm',[(-1.8,3,5.8),(-.8+math.cos(a)*2,3+math.sin(a)*1.5,7.5),p],[.32,.2,.04],BARK,(.40,.47,.46))
    for j in range(3):ico('Old willow bloom',(p[0]+rng.uniform(-1,1),p[1]+rng.uniform(-1,1),p[2]+rng.uniform(-.3,.9)),1.7,PETAL,(.78,.68+.04*j,.77),'04_Orchard',(1.2,1,.6),pc=j==2)
# A broken rotunda at the western bank and sunken gate add secondary landmarks.
for i in range(6):
    a=i*math.tau/6;x=-31+math.cos(a)*3;y=2+math.sin(a)*3;z=ground(x,y)
    if nearest(x,y)[0]<2.8:continue
    tube('Rotunda column',[(x,y,z),(x,y,z+3.9)],[.32,.24],PLASTER,(.74,.80,.77),'05_Ruins',8,True)
    box('Rotunda capital',(x,y,z+4),(1,1,.35),BRONZE,(.58,.66,.51),collision=False)
arch('Drowned gate',(11,0,-2.4),4.0,6.0,.6,collection='05_Ruins',angle=.3)
# Layered low-poly ridges give the spring valley a distant, closed silhouette.
for ring in range(2):
    v=[];count=64;r=105+ring*29
    for i in range(count):
        a=i*math.tau/count;h=9+7*math.sin(a*5+.8*ring)**2+5*math.sin(a*13)**2
        v.extend([(math.cos(a)*r,math.sin(a)*r-3,-4),(math.cos(a)*(r+3),math.sin(a)*(r+3)-3,h)])
    mesh('Distant valley ridge',v,[(2*i,2*((i+1)%count),2*((i+1)%count)+1,2*i+1) for i in range(count)],PETAL,(.13+.025*ring,.21+.025*ring,.25+.025*ring),'08_PC_Detail',False,ring==1)

# Broad shallow banks give a swimmer several ways back out of the lake.
for a,b,width in [((0,-30,-1.8),(0,-35,ground(0,-35)+.06),3.7),((-17,1,-1.8),(-22,1,ground(-22,1)+.06),3.5),((17,-4,-1.8),(23,-4,ground(23,-4)+.06),3.5)]:
    d=Vector(b)-Vector(a);side=Vector((-d.y,d.x,0)).normalized()*width/2
    mesh('Shallow return bank',list(map(tuple,[Vector(a)-side,Vector(b)-side,Vector(b)+side,Vector(a)+side])),[(0,1,2,3)],STONE,(.53,.63,.62),'01_Terrain',True)

# Smooth the grid perimeter coherently, sharing the same displaced corner on
# both the walking surface and banks. No detached collision shell is created.
boundary={}
for o in collections['01_Terrain'].objects:
    if not o.name.startswith('Moss bank'):continue
    a,b=[tuple(v.co) for v in o.data.vertices[:2]]
    boundary.setdefault(a[:2],set()).add(b[:2]);boundary.setdefault(b[:2],set()).add(a[:2])
remap={p:tuple((Vector(p)*.45+sum((Vector(q) for q in neighbors),Vector((0,0)))/len(neighbors)*.55)) for p,neighbors in boundary.items()}
for o in collections['01_Terrain'].objects:
    if not (o.name.startswith('Terrain ') or o.name.startswith('Moss bank')):continue
    for v in o.data.vertices:
        p=tuple(v.co[:2])
        if p in remap:v.co.x,v.co.y=remap[p]
    o.data.update()

# Import the exact decoded retail meshes. Instances share mesh data.
retail=json.loads((ASSETS/'retail-meshes.json').read_text());prototypes={}
def actor_mesh(key,data,texture_name):
    verts=[(v[0],v[2],v[1]) for v in data['vertices']]
    # Swapping Y/Z changes handedness; reverse each face and its attributes.
    faces=[tuple(reversed(f['indices'])) for f in data['faces']]
    md=bpy.data.meshes.new(key);md.from_pydata(verts,[],faces);md.update();uv=md.uv_layers.new(name='UVMap');col=md.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for tex in (True,False):
        mat=bpy.data.materials.new(key+(' textured' if tex else ' colour'));mat.use_nodes=True
        n=mat.node_tree.nodes;links=mat.node_tree.links;bs=n.get('Principled BSDF');bs.inputs['Roughness'].default_value=.9
        attr=n.new('ShaderNodeVertexColor');attr.layer_name='Color';color=attr.outputs['Color']
        if tex:
            im=n.new('ShaderNodeTexImage');im.image=bpy.data.images.load(str(ASSETS/(texture_name+'.png')),check_existing=True);im.interpolation='Closest'
            mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;links.new(im.outputs['Color'],mix.inputs[1]);links.new(color,mix.inputs[2]);color=mix.outputs[0]
        links.new(color,bs.inputs['Base Color']);links.new(color,bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=.4;md.materials.append(mat)
    for face,p in zip(data['faces'],md.polygons):
        p.material_index=0 if face['textured'] else 1
        for j,li in enumerate(p.loop_indices):
            k=len(p.loop_indices)-1-j;u,v=face['uv'][k];uv.data[li].uv=(u,1-v);col.data[li].color=(*[min(1,c) for c in face['colors'][k]],1)
    md.update()
    return md
for data in retail['models']:prototypes[data['type']]=(actor_mesh(f'Retail {data["type"]:03X}',data,'objects-17'),data['record'])
placements=[]
def actor(type,p,yaw=0,gem=1,label=None):
    md,record=prototypes[type];o=bpy.data.objects.new(label or f'Moby {type:03X} / {len(placements):03}',md);collections['07_Retail_Mobys'].objects.link(o)
    o.location=p;o.rotation_euler.z=-yaw
    if type==1:
        # Retail gems use a special view-dependent lighting routine. Give the
        # static Blender preview its denomination colour; the game uses that
        # original shader and model variant when loading the instance.
        o.data=md.copy();tint=(.92,.12,.20) if gem==1 else (.14,.83,.40)
        for poly in o.data.polygons:
            light=.6+.4*max(0,poly.normal.z)
            for li in poly.loop_indices:o.data.color_attributes['Color'].data[li].color=(*[c*light for c in tint],1)
    o['s2_export']='moby';o['source_slot']=17;o['source_record']=record;o['class_id']=type;o['gem_value']=gem;o['stable_id']=len(placements)
    placements.append({'id':len(placements),'type':type,'source_slot':17,'source_record':record,'position':list(p),'yaw':yaw,'gem_value':gem,'name':o.name})
    return o
for a,b in zip(route,route[1:]):
    n=math.ceil(math.dist(a,b)/2.4)
    for j in range(1,n):
        p=Vector(a).lerp(Vector(b),j/n);p.z=ground(p.x,p.y)+.18;actor(1,p,gem=1)
for x,y in [(-22,-19),(-29,-12),(-31,0),(-22,20),(-7,29),(7,29),(25,16),(30,1),(26,-18),(18,-31)]:
    if nearest(x,y)[0]<2.5: x+=3
    actor(200,(x,y,ground(x,y)),gem=5)
for i,(x,y) in enumerate([(-25,-15),(-30,5),(-16,22),(16,22),(29,-2),(24,-22)]):
    actor(555 if i%2 else 496,(x,y,ground(x,y)),yaw=math.pi*(i%2),label='Orchard guardian '+str(i+1))
actor(426,(0,33,5),0,label='Retail Return Home portal')
for a,b in zip(optional_route,optional_route[1:]):
    n=math.ceil(math.dist(a,b)/2)
    for i in range(1,n):p=Vector(a).lerp(Vector(b),i/n);p.z+=.18;actor(1,p,gem=2)
actor(200,(-6,0,1),gem=5);actor(200,(-3,-3,1),gem=5)
ref=bpy.data.objects.new('Spyro / original-scale reference',actor_mesh('Spyro retail idle',retail['spyro'],'spyro'));collections['00_Gameplay'].objects.link(ref);ref.location=(0,-38,.34765625);ref['s2_export']='reference'

def marker(name,p,kind,**props):
    o=bpy.data.objects.new(name,None);collections['00_Gameplay'].objects.link(o);o.location=p;o.empty_display_type='ARROWS';o.empty_display_size=.6;o['s2_export']='marker';o['kind']=kind
    for k,v in props.items():o[k]=v
    return o
marker('Arrival / safe landing',(0,-38,.15),'spawn',yaw=0.0,clear_radius=3.5)
for i,p in enumerate(route):marker(f'Route {i:02}',p,'route',index=i,minimum_clearance=2.3)
for name,p in [('Arrival Court',(0,-38,0)),('Pale Orchard',(-25,-17,2)),('Stillwater Bank',(-28,2,3)),('Bell Conservatory',(0,27,5)),('Wilted Nursery',(22,17,4)),('Memorial Walk',(23,-19,1.5))]:marker(name,p,'region')
marker('Optional objective / gather orchard gems',(-25,-17,3),'objective',objective='Collect the loop and side-alcove gems',implemented='Native gem and basket interactions')

# Dusk lighting is a presentation rig, excluded from the runtime package.
world=bpy.data.worlds.new('Dusky violet air');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.052,.074,.12,1);world.node_tree.nodes['Background'].inputs[1].default_value=.45;scene.world=world
def area(name,p,energy,color,size,target):
    data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.color=color;data.shape='DISK';data.size=size
    o=bpy.data.objects.new(name,data);collections['10_Views'].objects.link(o);o.location=p;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Moon through cloud',(-28,-2,55),10500,(.69,.8,1),55,(0,0,0))
area('Last spring light',(25,-45,35),7000,(1,.65,.63),45,(0,0,0))
area('Conservatory glow',(0,23,16),1600,(.70,1,.79),12,(0,22,5))
def camera(name,p,target,lens=40,ortho=None):
    data=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,data);collections['10_Views'].objects.link(o);o.location=p;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();data.lens=lens;data.clip_end=500
    if ortho:data.type='ORTHO';data.ortho_scale=ortho
    return o
hero=camera('01 / Spring at the silent bell',(66,-86,69),(0,-3,3),42)
camera('02 / Arrival at player scale',(2,-42,2.3),(-8,-24,2.5),28)
camera('03 / Pale orchard',(-33,-25,6.3),(-17,0,6),30)
camera('04 / Level plan',(0,-3,100),(0,-3,0),ortho=106)
scene.camera=hero;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
scene.view_settings.view_transform='AgX';scene.render.filepath=str(ROOT/'hollowbloom-overview.png')
scene['route_length']=sum(math.dist(a,b) for a,b in zip(route,route[1:]));scene['authoring_profile']='PC master; PS1 preview excludes 08_PC_Detail and tagged canopy clumps.'
collections['09_Collision'].hide_render=True
scene['ps1_triangle_target']=16000;scene['ps1_visible_triangle_target']=5000
scene['ps1_texture_policy']='64x64 retail tiles; nearest filtering; per-vertex colour; palette packing deferred.'
bpy.context.view_layer.update()
for image in bpy.data.images:
    if image.source=='FILE' and image.filepath: image.pack()
manifest={'schema':1,'id':'hollowbloom-springs','title':'Hollowbloom Springs','coordinates':'Blender Z-up; 1 unit = 1024 retail units','route':route,'optional_route':optional_route,'spawn':[0,-38,.15],'materials':material_specs,'objects':placements,'retail_profile':'SCUS_944.25 CDC0917C','ps1':{'status':'Planning profile, not a packed retail archive','sector_size':16,'quantization':1/64,'target_triangles':16000,'target_visible_triangles':5000,'exclude_collection':'08_PC_Detail'}}
(ROOT/'source'/'design.json').write_text(json.dumps(manifest,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Hollowbloom Springs.blend'),check_existing=False,compress=True)
result={'file':str(ROOT/'Hollowbloom Springs.blend'),'scene':scene.name,'objects':len(scene.objects),'retail_instances':len(placements),'trees':len(tree_sites),'route_length':scene['route_length'],'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in scene.objects if o.type=='MESH')}

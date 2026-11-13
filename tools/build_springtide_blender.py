"""Springtide Castle, authored through Blender MCP; one unit = 1024 retail units.

Every room, water passage and route is new. Retail assets are preview imports;
the portable world contains references resolved against the player's own disc.
"""
import bpy, json, math, random
from pathlib import Path
from mathutils import Vector

ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/springtide-castle')
ASSETS=ROOT/'assets'
for d in ('source','pc','ps1-study','previews'): (ROOT/d).mkdir(exist_ok=True)
rng=random.Random(23926)
scene=bpy.data.scenes.new('Springtide Castle - PC Master')
bpy.context.window.scene=scene
scene.unit_settings.system='NONE'
scene['level_id']='springtide-castle'
scene['retail_units_per_blender_unit']=1024
scene['design']='A leafy castle homeworld: inhabited interiors above a submerged spring crypt.'
scene['ps1_status']='Reduction study; archive packing, visibility and hardware validation pending.'
cols={}
for n in ('01 Landscape','02 Great Hall','03 Library','04 Banquet Hall','05 Throne Room','06 Stair Tower','07 Gardens','08 Spring Crypt','09 Retail Trees','10 Retail Actors','11 Gameplay','12 Collision','13 Views'):
    c=bpy.data.collections.new(n);scene.collection.children.link(c);cols[n]=c
section='01 Landscape'
mats={}
atlas=bpy.data.images.load(str(ASSETS/'terrain-15.png'),check_existing=True)
atlas_pixels=list(atlas.pixels);aw,ah=atlas.size

def material(tile,name,water=False):
    m=bpy.data.materials.new(name);m.use_nodes=True;m['source_slot']=15 if tile>=0 else -1
    source_tile={3:36,27:36,53:55,67:68}.get(tile,tile)
    m['source_tile']=max(0,source_tile);m['water']=water;m['s2_material']=len(mats)
    nodes=m.node_tree.nodes;nodes.clear();links=m.node_tree.links
    out=nodes.new('ShaderNodeOutputMaterial');bs=nodes.new('ShaderNodeBsdfPrincipled')
    bs.inputs['Roughness'].default_value=.9
    vc=nodes.new('ShaderNodeVertexColor');vc.layer_name='Color';color=vc.outputs['Color']
    if tile>=0:
        im=bpy.data.images.new('Summer Forest / '+str(tile),width=64,height=64,alpha=True)
        tx=source_tile%16*64;ty=ah-(source_tile//16+1)*64
        im.pixels=[c for y in range(64) for c in atlas_pixels[((ty+y)*aw+tx)*4:((ty+y)*aw+tx+64)*4]];im.pack()
        tex=nodes.new('ShaderNodeTexImage');tex.image=im;tex.interpolation='Closest'
        mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
        links.new(tex.outputs['Color'],mix.inputs[1]);links.new(color,mix.inputs[2]);color=mix.outputs[0]
    links.new(color,bs.inputs['Base Color']);links.new(color,bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=.45
    if water:
        bs.inputs['Alpha'].default_value=.35;m.surface_render_method='DITHERED'
    links.new(bs.outputs[0],out.inputs[0]);mats[tile]=m
    return m

for tile,name in [(59,'Spring grass'),(41,'Mossy ground'),(43,'Bank rock'),(50,'Grass bank edge'),(3,'Mossy limestone'),(27,'Castle limestone'),(29,'Warm limestone'),(5,'Carved scroll frieze'),(14,'Jewel arch'),(17,'Gold cornice'),(22,'Arch voussoirs'),(32,'Recessed stone'),(51,'Blue glass recess'),(53,'Slate roof'),(60,'Gold'),(67,'Courtyard paving'),(68,'Hall paving'),(70,'Oak'),(72,'Fluted column'),(75,'Gothic arch'),(77,'Panel border'),(0,'Tree root'),(47,'Tree bark'),(48,'Tree ivy'),(49,'Summer Forest canopy'),(-1,'Vertex colour')]:material(tile,name)
material(87,'Retail additive water',True)

def mesh(name,verts,faces,tile,color=(1,1,1),collision=False,pc=False,uvs=None):
    md=bpy.data.meshes.new(name);md.from_pydata(verts,[],faces);md.update()
    o=bpy.data.objects.new(name,md);cols[section].objects.link(o);md.materials.append(mats[tile])
    o['s2_export']='geometry';o['collision']=collision;o['pc_only']=pc
    o['sector']=str(math.floor(sum(v[0] for v in verts)/len(verts)/16))+','+str(math.floor(sum(v[1] for v in verts)/len(verts)/16))
    uv=md.uv_layers.new(name='UVMap');cl=md.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for p in md.polygons:
        shade=.78+.22*max(0,p.normal.z)
        axes=[i for i in range(3) if i!=max(range(3),key=lambda i:abs(p.normal[i]))]
        projected=[(md.vertices[i].co[axes[0]],md.vertices[i].co[axes[1]]) for i in p.vertices]
        lows=[min(v[i] for v in projected) for i in range(2)];highs=[max(v[i] for v in projected) for i in range(2)]
        for j,li in enumerate(p.loop_indices):
            a,b=uvs[p.index][j] if uvs else [(0,0),(1,0),(1,1),(0,1)][j%4]
            if not uvs and len(p.vertices)>4:
                a,b=[(projected[j][i]-lows[i])/max(.001,highs[i]-lows[i]) for i in range(2)]
            if tile==40:b=.03+.4*b # Bare lower rock, excluding the source tile's grassy corner.
            uv.data[li].uv=((.5+a*63)/64,(.5+b*63)/64)
            cl.data[li].color=(*[min(1,max(0,c*shade)) for c in color],1)
    return o

def box(name,p,size,tile=27,color=(1,1,1),collision=True,pc=False):
    x,y,z=p;a,b,c=[s/2 for s in size]
    v=[(i*a,j*b,k*c) for k in (-1,1) for j in (-1,1) for i in (-1,1)]
    o=mesh(name,v,[(0,2,3,1),(4,5,7,6),(0,1,5,4),(1,3,7,5),(3,2,6,7),(2,0,4,6)],tile,color,collision,pc)
    o.location=p;o['sector']=str(math.floor(x/16))+','+str(math.floor(y/16));return o

def plane(name,x0,x1,y0,y1,z,tile=59,color=(1,1,1),collision=True,step=2,holes=()):
    xs=sorted(set([x0,x1]+[x0+(x1-x0)*i/max(1,math.ceil((x1-x0)/step)) for i in range(1,math.ceil((x1-x0)/step))]+[x for h in holes for x in h[:2] if x0<x<x1]))
    ys=sorted(set([y0,y1]+[y0+(y1-y0)*i/max(1,math.ceil((y1-y0)/step)) for i in range(1,math.ceil((y1-y0)/step))]+[y for h in holes for y in h[2:] if y0<y<y1]))
    v=[];f=[]
    for a,b in zip(xs,xs[1:]):
        for c,d in zip(ys,ys[1:]):
            if any(h[0]<(a+b)/2<h[1] and h[2]<(c+d)/2<h[3] for h in holes):continue
            i=len(v);v.extend([(a,c,z),(b,c,z),(b,d,z),(a,d,z)]);f.append((i,i+1,i+2,i+3))
    return mesh(name,v,f,tile,color,collision)

def cylinder(name,p,r,h,tile=27,color=(1,1,1),n=12,collision=True,r2=None,caps=True):
    x,y,z=p;r2=r if r2 is None else r2
    v=[(x+radius*math.cos(i*2*math.pi/n),y+radius*math.sin(i*2*math.pi/n),z+dz) for radius,dz in ((r,0),(r2,h)) for i in range(n)]
    f=([tuple(reversed(range(n))),tuple(range(n,n*2))] if caps else [])+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,f,tile,color,collision)

def column(name,x,y,z,h,r=.42):
    cylinder(name+' plinth',(x,y,z),r*1.5,.22,17,n=8)
    cylinder(name+' shaft',(x,y,z+.22),r,h-.52,72,n=12)
    cylinder(name+' capital',(x,y,z+h-.3),r*1.65,.3,17,n=8)

def wall(name,a,b,z,h,holes=(),tile=27,thick=.55):
    ax,ay=a;bx,by=b;d=Vector((bx-ax,by-ay));length=d.length;d.normalize();n=Vector((-d.y,d.x))
    us=sorted(set([0,length]+[i*length/max(1,math.ceil(length/2.5)) for i in range(1,math.ceil(length/2.5))]+[q for v in holes for q in v[:2]]))
    zs=sorted(set([z,z+h]+[z+i*h/max(1,math.ceil(h/2.5)) for i in range(1,math.ceil(h/2.5))]+[q for v in holes for q in v[2:]]))
    v=[];f=[]
    for u,w in zip(us,us[1:]):
        for lo,hi in zip(zs,zs[1:]):
            if any(q[0]<(u+w)/2<q[1] and q[2]<(lo+hi)/2<q[3] for q in holes):continue
            if not (0<=(u+w)/2<=length and z<=(lo+hi)/2<=z+h):continue
            k=len(v)
            for zz in (lo,hi):
                for side in (-1,1):
                    for s in (u,w):v.append((ax+d.x*s+n.x*side*thick/2,ay+d.y*s+n.y*side*thick/2,zz))
            def opening(s,t):return s<0 or s>length or t<z or t>z+h or any(q[0]<s<q[1] and q[2]<t<q[3] for q in holes)
            faces=[(0,1,5,4),(3,2,6,7)]
            if opening((u+w)/2,lo-.001):faces.append((0,2,3,1))
            if opening((u+w)/2,hi+.001):faces.append((4,5,7,6))
            if opening(u-.001,(lo+hi)/2):faces.append((2,0,4,6))
            if opening(w+.001,(lo+hi)/2):faces.append((1,3,7,5))
            f.extend(tuple(k+i for i in face) for face in faces)
    return mesh(name,v,f,tile,collision=True)

def arch(name,p,width,height,depth=.55,angle=0,tile=22):
    # Open passage with semicircular crown, not a texture painted on a wall.
    r=width/2;spring=height-r;v=[];f=[];steps=12
    for i in range(steps+1):
        t=math.pi*i/steps
        for rr in (r,r+.38):
            for y in (-depth/2,depth/2):v.append((math.cos(t)*rr,y,spring+math.sin(t)*rr))
    for i in range(steps):
        k=i*4;f.extend([(k,k+4,k+6,k+2),(k+1,k+3,k+7,k+5),(k,k+1,k+5,k+4),(k+2,k+6,k+7,k+3)])
    o=mesh(name+' curved crown',v,f,tile,collision=True);o.location=p;o.rotation_euler.z=angle
    for sign in (-1,1):
        x=sign*(r+.19);pos=(p[0]+math.cos(angle)*x,p[1]+math.sin(angle)*x,p[2]+spring/2)
        o=box(name+' jamb',pos,(.38,depth,spring),72);o.rotation_euler.z=angle

def ramp(name,a,b,width,tile=67):
    a,b=Vector(a),Vector(b);d=b-a;side=Vector((-d.y,d.x,0)).normalized()*width/2
    # Edge meets its destination height before a collision lip.
    n=max(1,math.ceil(d.length/3));v=[];f=[]
    for j in range(n):
        p=a.lerp(b,j/n);q=a.lerp(b,(j+1)/n);k=len(v)
        v.extend([p-side,q-side,q+side,p+side]);f.append((k,k+1,k+2,k+3))
    o=mesh(name,v,f,tile,collision=True)
    return o

def stair_flight(name,a,b,width,tile=68):
    av,bv=Vector(a),Vector(b);d=Vector((bv.x-av.x,bv.y-av.y,0)).normalized()
    padding=width*.5/max(abs(d.x),abs(d.y))
    aa=av+d*padding;bb=bv-d*padding
    if (bb-aa).dot(d)<.3:return ramp(name,av,bv,width,tile)
    return ramp(name,aa,bb,width,tile)

def path(name,points,width=3.2,tile=67):
    # Mitered join vertices avoid cracks and overlapping translucent surfaces.
    v=[]
    for i,p in enumerate(points):
        p=Vector(p);before=Vector(points[max(0,i-1)]);after=Vector(points[min(len(points)-1,i+1)])
        d0=(p-before) if i else (after-p);d1=(after-p) if i<len(points)-1 else d0
        n0=Vector((-d0.y,d0.x,0)).normalized();n1=Vector((-d1.y,d1.x,0)).normalized();n=(n0+n1).normalized()
        off=n*min(width,max(width/2,width/2/max(.3,n.dot(n0))))
        v.extend([p-off,p+off])
    tiled=[];faces=[]
    for i in range(len(points)-1):
        a,b,c,d=[v[k] for k in (2*i,2*i+2,2*i+3,2*i+1)]
        count=max(1,math.ceil((Vector(points[i+1])-Vector(points[i])).length/3))
        for j in range(count):
            k=len(tiled);t0=j/count;t1=(j+1)/count
            tiled.extend([a.lerp(b,t0),a.lerp(b,t1),d.lerp(c,t1),d.lerp(c,t0)]);faces.append((k,k+1,k+2,k+3))
    return mesh(name,tiled,faces,tile,collision=False)

def roof(name,x0,x1,y0,y1,z,peak):
    # Terraced battlements replace the rejected long campus-like pitched roofs.
    holes=[(-22,-12,27,36)] if name.startswith('Tower') else []
    plane(name+' walkable roof',x0,x1,y0,y1,z,53,holes=holes,step=3)
    for a,b in [((x0,y0),(x1,y0)),((x1,y0),(x1,y1)),((x1,y1),(x0,y1)),((x0,y1),(x0,y0))]:
        d=Vector(b)-Vector(a);n=max(1,int(d.length/2));direction=d.normalized();mid=(Vector(a)+Vector(b))/2
        o=box(name+' corbel band',(mid.x,mid.y,z-.25),(d.length+.5,.85,.5),17,collision=False);o.rotation_euler.z=math.atan2(d.y,d.x)
        for i in range(n+1):
            p=Vector(a)+d*i/n
            if name.startswith('Great hall') and p.x<x0+.1 and p.y>23:continue
            if name.startswith('Tower') and p.x>x1-.1 and 25<p.y<30:continue
            o=box(name+' battlement',(p.x,p.y,z+.6),(1.05,.7,1.2),27);o.rotation_euler.z=math.atan2(d.y,d.x)

def bench(name,x,y,z,angle=0):
    parts=[box(name+' seat',(0,0,.55),(2,.65,.2),70),box(name+' back',(0,.3,1),(2,.16,.8),70)]
    for xx in (-.7,.7):parts.append(box(name+' leg',(xx,0,.25),(.22,.5,.5),17))
    for o in parts:
        for vert in o.data.vertices:
            a,b,c=vert.co+o.location;vert.co=(x+math.cos(angle)*a-math.sin(angle)*b,y+math.sin(angle)*a+math.cos(angle)*b,z+c)
        o.location=(0,0,0)

pool=(-42,-22,-27,-13);grotto=(11,23,36,48);well=(-3,3,9,15)
ground_holes=[pool,grotto,well,(14,18,48,51)]
plane('Continuous homeworld ground',-52,52,-43,58,0,59,holes=ground_holes,step=3)
# The enlarged exterior joins this central plateau continuously.
outer=[(-52,-43),(-27,-47),(5,-46),(34,-43),(51,-32),(55,-6),(53,29),(46,52),(24,60),(-12,61),(-40,53),(-55,25),(-57,-10)]
for i,(a,b) in enumerate([]):
    innera=(max(-52,min(52,a[0])),max(-43,min(58,a[1])))
    innerb=(max(-52,min(52,b[0])),max(-43,min(58,b[1])))
    if math.dist(innera,innerb)>.01:mesh('Irregular grassy edge '+str(i),[(*innera,0),(*a,-.2),(*b,-.2),(*innerb,0)],[(0,1,2,3)],59,collision=True)
    mesh('Outer limestone escarpment '+str(i),[(*a,-.2),(*a,-8),(*b,-8),(*b,-.2)],[(0,1,2,3)],43,collision=True)
    # Low perimeter ridges read as a sheltered garden boundary.
    wall('Outer parapet '+str(i),a,b,-.1,1.25,tile=3,thick=.8)

section='02 Great Hall'
plane('Great hall patterned floor',-10,10,0,28,2,68,holes=[well])
plane('Underfloor well opening',-10,10,0,28,1.85,27,holes=[well],collision=False)
wall('Hall west wall',(-10,0),(-10,28),0,10,[(11.5,16.5,2,6),(20,24.5,6,9)])
wall('Hall east wall',(10,0),(10,28),0,10,[(11.5,16.5,2,6)])
wall('Entrance facade',(-10,0),(10,0),0,10,[(7,13,2,7.7)])
wall('Rear hall wall',(-10,28),(10,28),0,10,[(7,13,2,7.7)])
arch('Main entrance',(0,-.05,2),6,5.7,.8)
arch('Throne vestibule',(0,28,2),6,5.7,.7)
for x in (-10,10):arch('Side hall arcade',(x,14,2),5,4,.7,math.pi/2)
for x in (-7,7):
    for y in (5,17,25):column('Hall colonnade',x,y,2,7.7,.5)
    plane('Gallery walk',x-1.35,x+1.35,3.5,27,6,68)
plane('Rear gallery bridge',-8.35,8.35,24.7,27.5,6,68)
# Barrel vault, with repeated cross-ribs and a separate slate exterior.
v=[];f=[]
for yy in (0,28):
    for i in range(17):
        t=i*math.pi/16;v.append((10*math.cos(t),yy,10+3*math.sin(t)))
for i in range(16):f.append((i,i+1,18+i,17+i))
mesh('Visible coffered barrel vault',v,f,32,(.95,.94,.82),True)
roof('Great hall slate roof',-10.7,10.7,-.6,28.6,13.2,17)
for yy in (1,9,18,27):
    for i in range(12):
        t=(i+.5)*math.pi/12
        o=box('Vault gold rib',(9.75*math.cos(t),yy,10+2.96*math.sin(t)),(2.65,.16,.14),17,collision=False);o.rotation_euler.y=math.atan2(2.96*math.cos(t),-9.75*math.sin(t))
for x in (-3.2,3.2):box('Well side balustrade',(x,12,2.55),(.25,6.8,1.1),17)
for y in (8.8,15.2):
    for x in (-2,-.7,.7,2):column('Well spindle',x,y,2,1.15,.09)
    box('Well handrail',(0,y,3.15),(6.4,.18,.15),17)
for x in (-5.4,5.4):
    for y0,y1 in ((10,23),(3.5,5.5)):
        box('Gallery rail',(x,(y0+y1)/2,7.05),(.18,y1-y0,.16),17)
        for y in range(math.ceil(y0),math.floor(y1)+1):column('Gallery baluster',x,y,6,.98,.08)
ramp('Broad entrance stair collision',(0,-9,0),(0,-.5,2),6)
plane('Entrance top landing',-3,3,-.5,0,2,67)
for i in range(12):
    yy=-9+i*8.5/12;zz=i*2/12
    box('Entrance visible step',(0,yy+.22,zz-.1),(6,.45,.18),27,collision=False)
for x in (-6,6):
    for y in (2,27):cylinder('Entrance urn',(x,y,2),.5,1.1,17,n=8,r2=.7)

section='03 Library'
plane('Library marble floor',-28,-12,5,25,2,68)
wall('Library west',(-28,5),(-28,25),0,9,[(12.5,17.5,6,9)])
wall('Library east',(-12,5),(-12,25),0,9,[(6.5,11.5,2,6),(15,19.5,6,9)])
wall('Library front',(-28,5),(-12,5),0,9)
wall('Library rear',(-28,25),(-12,25),0,9,[(5.5,10.5,2,6),(12,15.8,6,9)])
plane('Library connector',-12,-10,11.5,16.5,2,68)
plane('Library upper connector',-12,-8.3,20,24.5,6,68)
plane('Library mezzanine',-27.7,-12.3,20,24.7,6,68)
plane('Library ceiling',-28,-12,5,25,9,5,collision=True,step=4)
roof('Library roof',-28.7,-11.4,4.3,25.6,9.4,12.6)
arch('Library east entrance',(-12,14,2),5,4,.6,math.pi/2)
arch('Library north stairs',(-20,25,2),5,4,.6)
arch('Library upper west door',(-28,20,6),5,3,.6,math.pi/2)

def bookcase(name,x,y,z,angle=0):
    pieces=[]
    for xx in (-1.4,1.4):pieces.append(box(name+' upright',(xx,0,1.7),(.18,.65,3.4),70))
    pieces.append(box(name+' back',(0,.26,1.7),(2.8,.15,3.4),70))
    for zz in (0,1,2,3,3.4):pieces.append(box(name+' shelf',(0,0,zz),(3,.75,.14),17,collision=False))
    for level in range(3):
        for j in range(10):
            h=rng.uniform(.5,.8);co=rng.choice([(.3,.48,.45),(.5,.24,.18),(.3,.25,.5),(.75,.53,.21)])
            pieces.append(box(name+' bound volume',(-1.2+j*.25,-.15,level+.13+h/2),(.19,.44,h),-1,co,False,True))
    for o in pieces:
        for vert in o.data.vertices:
            a,b,c=vert.co+o.location;vert.co=(x+math.cos(angle)*a-math.sin(angle)*b,y+math.sin(angle)*a+math.cos(angle)*b,z+c)
        o.location=(0,0,0)
for yy in (7.5,18):bookcase('West library shelves',-27,yy,2,-math.pi/2)
for xx in (-24,-19,-14):bookcase('South library shelves',xx,6,2,math.pi)
for xx in (-25,-20):bookcase('Upper library shelves',xx,24,6)
box('Reading table top',(-21,13,3),(3.5,4.4,.2),70)
for x in (-22.3,-19.7):
    for y in (11.4,14.6):box('Reading table carved leg',(x,y,2.48),(.28,.28,.95),72)
bench('Reader seat',-23.5,13,2,math.pi/2);bench('Reader seat',-18.5,13,2,-math.pi/2)
for x in (-22,-21,-20):box('Open manuscript',(x,13,3.16),(.64,.85,.06),-1,(.92,.83,.58),False,True)
cylinder('Library globe pedestal',(-25,21,2),.5,1.25,17)
cylinder('Faceted celestial globe',(-25,21,3.25),.8,1.2,-1,(.16,.35,.49),12,True,r2=.25)

section='04 Banquet Hall'
plane('Banquet patterned floor',12,28,5,25,2,67)
plane('Banquet connector',10,12,11.5,16.5,2,68)
for x in (12,28):wall('Banquet side',(x,5),(x,25),0,8.5,[(6.5,11.5,2,6)])
wall('Banquet front',(12,5),(28,5),0,8.5)
wall('Banquet rear',(12,25),(28,25),0,8.5)
plane('Banquet ceiling',12,28,5,25,8.5,5,step=4)
roof('Banquet roof',11.4,28.6,4.4,25.6,9,12)
for x in (12,28):arch('Banquet doorway',(x,14,2),5,4,.65,math.pi/2)
box('Banquet oak tabletop',(20,14,3.05),(3.5,8.5,.24),70)
for x in (18.8,21.2):
    for y in (10.6,17.4):column('Table leg',x,y,2,.95,.16)
for x in (17,23):
    for y in (11,14,17):bench('Banquet chair',x,y,2,math.pi/2 if x<20 else -math.pi/2)
for y in (11,14,17):
    cylinder('Gold serving platter',(20,y,3.2),.48,.07,60,collision=False)
    for x in (18.8,21.2):cylinder('Candle holder',(x,y,3.17),.12,.55,60,n=8,collision=False)
box('Hearth back',(26,6,3.5),(3,.5,3),53)
arch('Hearth surround',(26,6,2),2.5,2.8,.6)
for x in (14,26):box('Banquet banner',(x,24.6,6),(.95,.06,3),-1,(.22,.38,.55),False)

section='05 Throne Room'
plane('Throne vestibule floor',-3,3,28,30,2,68)
plane('Throne room floor',-10,10,30,47,2,68)
wall('Throne south',(-10,30),(10,30),0,12,[(7,13,2,7.7)])
wall('Throne north',(-10,47),(10,47),0,12,[(7,13,2,7.7),(7.5,12.5,9,11.5)])
wall('Throne west',(-10,30),(-10,47),0,12)
wall('Throne east',(10,30),(10,47),0,12,[(1.5,6.5,2,6.5)])
plane('Throne ceiling',-10,10,30,47,12,5,step=4)
roof('Throne roof',-10.7,10.7,29.5,47.7,12.4,17.4)
for y in (30,47):arch('Throne arch',(0,y,2),6,5.7,.7)
arch('Garden bridge door',(10,34,2),5,4.5,.6,math.pi/2)
box('Raised throne dais',(0,43,2.38),(12,5,.75),17)
ramp('Dais approach',(0,37,2),(0,40.5,2.75),5,68)
for x in (-3,3):
    box('Throne seat',(x,44,3.35),(1.65,1.55,.5),60)
    box('Throne embroidered back',(x,44.65,4.5),(1.65,.3,2.8),-1,(.31,.2,.43))
    for xx in (x-.94,x+.94):column('Throne finial',xx,44.6,2.75,3,.13)
for x in (-7,7):
    for y in (33,43):column('Throne pillars',x,y,2,9.7,.6)
box('Northern stained glass',(0,47,10.2),(4.9,.05,2.3),-1,(.25,.59,.65),False)
for x in (-1.6,0,1.6):box('Window tracery',(x,46.92,10.2),(.12,.12,2.4),60,collision=False)
plane('Raised grotto bridge',10,30,32,36,2,67)
for y in (32,36):
    box('Bridge parapet',(20,y,2.55),(20,.35,1.1),3)
    for x in (12,18,24,29):column('Bridge pier',x,y,-.2,2.2,.38)
ramp('North castle steps',(0,47,2),(0,55,0),6)

section='06 Stair Tower'
plane('Tower ground floor',-29,-12,25,37,2,68)
wall('Tower west',(-29,25),(-29,37),0,11)
wall('Tower north',(-29,37),(-12,37),0,11)
wall('Tower east',(-12,25),(-12,37),0,11,[(0,2,6,9)])
roof('Tower steep roof',-29.6,-11.4,24.4,37.6,11.4,18.5)
stairs=[(-25.5,26,2),(-25.5,34,3.35),(-20,34,4.7),(-20,28,6),(-14,26,6),(-14,22,6),(-7.5,22,6)]
for i,(a,b) in enumerate(zip(stairs,stairs[1:])):stair_flight('Broad tower stair flight '+str(i),a,b,3,68)
for p in stairs:plane('Stair turning landing',p[0]-1.5,p[0]+1.5,p[1]-1.5,p[1]+1.5,p[2],68)
plane('Upper stair library door',-21.5,-18.5,24,28,6,68)
# A four-sided articulated tower cap reads from the court, with inset panels.
for x in (-29,-12):
    for y in (26,36):column('Stair tower buttress',x,y,0,11,.65)
for x in range(-28,-12,3):box('Tower crown merlon',(x,37,11.4),(1.2,.7,1.3),17)

# Exterior detailing is distributed along the architecture, not scattered in routes.
for colname,rect,top in [('02 Great Hall',(-10,10,0,28),10),('03 Library',(-28,-12,5,25),9),('04 Banquet Hall',(12,28,5,25),8.5),('05 Throne Room',(-10,10,30,47),12)]:
    section=colname;x0,x1,y0,y1=rect
    for x in (x0,x1):
        box('Carved eave frieze',(x,(y0+y1)/2,top-.25),(.72,y1-y0,.65),5,collision=False)
        for y in (y0+.4,y1-.4):column('Corner buttress',x,y,0,top,.65)
    for y in (y0,y1):box('Gold roof cornice',((x0+x1)/2,y,top+.05),(x1-x0+.8,.8,.3),17,collision=False)
    for x in (x0+3,x1-3):
        box('Recessed facade window',(x,y0-.31,6.4),(1.8,.08,2.4),51,collision=False)
        arch('Window carving',(x,y0-.37,5.2),1.8,2.4,.16,tile=14)

# The remaining gardens, submerged route and gameplay placement are a separate
# authoring stage, but run in this same new Blender scene.
exec(compile((Path(__file__).with_name('springtide_gardens.py')).read_text(encoding='utf-8'),'springtide_gardens.py','exec'))

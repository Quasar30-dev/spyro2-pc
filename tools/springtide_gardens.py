"""Second stage of build_springtide_blender.py, using its explicit scene/helpers."""
section='07 Gardens'
# Three garden rooms with distinct circulation, linked to both castle entrances.
path('Arrival avenue',[(0,-39,.025),(0,-25,.025),(0,-12,.025),(0,-9,.025)],5)
path('Pond walk',[(-12,-30,.025),(-29,-32,.025),(-46,-30,.025),(-46,-10,.025),(-44,5,.025),(-33,29,.025)],3.4)
path('Court loop',[(-12,-30,.025),(-14,-12,.025),(0,-12,.025),(17,-12,.025),(39,-10,.025)],3.4)
path('Northern woodland walk',[(-33,29,.025),(-34,44,.025),(-19,53,.025),(0,55,.025),(25,55,.025),(43,42,.025)],3.4)
path('West castle walk',[(-31,29,.025),(-31,-1,.025),(-15,-12,.025)],3.4)
path('East return path',[(40,-10,.025),(42,-27,.025),(22,-34,.025),(0,-38,.025)],3.4)
plane('East garden terrace',29,49,4,34,2,67,step=3)
wall('Terrace foundation east',(49,4),(49,34),0,2,tile=3)
wall('Terrace foundation south',(29,4),(49,4),0,2,[(8,14,0,2)],tile=3)
wall('Terrace foundation west',(29,4),(29,32),0,2,tile=3)
plane('Banquet terrace threshold',28,29,11.5,16.5,2,67)
ramp('Garden ascent',(40,-10,0),(40,4,2),6)
for x,y in [(-15,-21),(15,-21)]:
    cylinder('Court planting bed',(x,y,.04),4,.22,3,n=12)
    cylinder('Court green parterre',(x,y,.27),3.65,.22,59,n=12)
    bench('Court bench',x,y-5,0,math.pi)
for x,y in [(31,7),(46,7),(31,29),(46,29)]:
    box('Terrace herb bed',(x,y,2.25),(2.7,2.7,.5),3)
    plane('Terrace herbs',x-1.2,x+1.2,y-1.2,y+1.2,2.52,59,collision=False)
    cylinder('Stone garden vase',(x,y,2.52),.28,.65,17,n=8,r2=.47)
# Fountain bowl: radial annulus leaves a visible shallow basin.
for x,y,z,r in [(39,19,2,2.8),(9,-30,0,1.9)]:
    for i in range(16):
        a=i*math.tau/16;b=(i+1)*math.tau/16
        v=[(x+rr*math.cos(t),y+rr*math.sin(t),z+zz) for zz in (.12,.75) for rr in (r-.3,r) for t in (a,b)]
        mesh('Fountain carved rim',v,[(4,6,7,5),(0,1,5,4),(2,6,7,3)],17,collision=True)
    cylinder('Fountain shallow basin',(x,y,z+.05),r-.3,.09,51,n=16,collision=True)
    cylinder('Fountain centre pillar',(x,y,z+.14),.32,1.5,72,n=8)
    cylinder('Fountain upper bowl',(x,y,z+1.45),.7,.22,60,n=12,r2=.9)
    cylinder('Fountain crown',(x,y,z+1.67),.14,.55,60,n=8,r2=.01)
    # Water is a separate physical surface, using the decoded additive tile.
    plane('Fountain water',x-r+.35,x+r-.35,y-r+.35,y+r-.35,z+.2,87,(.55,.85,1),False,step=1)
for x,y,ang in [(-48,8,math.pi/2),(-47,32,math.pi/2),(43,31,0),(35,7,math.pi),(27,55,0)]:bench('Garden seat',x,y,2 if y in (7,31) else 0,ang)

# Broad stepping ledges are attached to garden buttresses. This is a short
# optional climbing route to the library, with a continuous walking alternative.
course=[(-37,0,2),(-37,3.8,2.65),(-34,6.6,3.3),(-37,9.4,3.95),(-34,12.2,4.6),(-37,15,5.25),(-34,17.8,5.9)]
for i,(x,y,z) in enumerate(course):
    box('Courtyard climb masonry '+str(i),(x,y,(z-.16)/2),(3.1,3.1,z-.16),3)
    box('Courtyard climb carved cap '+str(i),(x,y,z-.08),(3.3,3.3,.16),17)
    for dx in (-1.3,1.3):box('Ledge pilaster',(x+dx,y-1.57,z/2),(.16,.12,max(.1,z-.2)),72,collision=False)
ramp('Climb approach ramp',(-37,-10,0),(-37,-2.1,2),3.3)
plane('Climb takeoff landing',-38.65,-35.35,-2.1,-1.6,2,67)
ramp('Library climbing-route bridge',(-34,17.8,5.9),(-28,20,6),3.1)
plane('Library upper threshold',-28,-24,18.5,21.5,6,68)
hall_course=[(-7,8,6),(-3.7,8,6.35),(-.4,8,6.65),(2.9,8,6.35),(7,8,6)]
section='02 Great Hall'
for i,(x,y,z) in enumerate(hall_course[1:-1]):
    column('Hall challenge pillar '+str(i),x,y,2,z-2-.5,.55)
    cylinder('Hall challenge capital '+str(i),(x,y,z-.15),1.18,.15,17,n=8)

# A continuous underwater route beneath the castle foundations. Only boundary
# edges receive walls; internal grid joins remain open to swimming.
section='08 Spring Crypt'
tunnel=[(-32,-20),(-20,-20),(-20,-5),(-4,-5),(-4,12),(4,12),(4,32),(16,32),(16,42)]
def distance_segment(p,a,b):
    p,a,b=Vector(p),Vector(a),Vector(b);d=b-a;t=max(0,min(1,(p-a).dot(d)/d.length_squared));return (p-a-d*t).length
cells=set()
for x in range(-43,25):
    for y in range(-28,52):
        p=(x+.5,y+.5)
        in_pool=pool[0]<p[0]<pool[1] and pool[2]<p[1]<pool[3]
        in_grotto=grotto[0]<p[0]<grotto[1] and grotto[2]<p[1]<grotto[3]
        in_exit=14<p[0]<18 and 48<p[1]<51
        if in_pool or in_grotto or in_exit or min(distance_segment(p,a,b) for a,b in zip(tunnel,tunnel[1:]))<2.5:cells.add((x,y))
vfloor=[];ffloor=[];vwater=[];fwater=[];vroof=[];froof=[];vwall=[];fwall=[]
for x,y in sorted(cells):
    p=(x+.5,y+.5);open_top=any(h[0]<p[0]<h[1] and h[2]<p[1]<h[3] for h in ground_holes)
    i=len(vfloor);vfloor.extend([(x,y,-4.5),(x+1,y,-4.5),(x+1,y+1,-4.5),(x,y+1,-4.5)]);ffloor.append((i,i+1,i+2,i+3))
    i=len(vwater);vwater.extend([(x,y,-.3),(x+1,y,-.3),(x+1,y+1,-.3),(x,y+1,-.3)]);fwater.append((i,i+1,i+2,i+3))
    if not open_top:
        i=len(vroof);vroof.extend([(x,y,-.65),(x+1,y,-.65),(x+1,y+1,-.65),(x,y+1,-.65)]);froof.append((i+3,i+2,i+1,i))
    for d,a,b in [((-1,0),(x,y+1),(x,y)),((1,0),(x+1,y),(x+1,y+1)),((0,-1),(x,y),(x+1,y)),((0,1),(x+1,y+1),(x,y+1))]:
        if (x+d[0],y+d[1]) in cells:continue
        top=0 if open_top else -.65
        i=len(vwall);vwall.extend([(*a,-4.5),(*b,-4.5),(*b,top),(*a,top)]);fwall.append((i,i+1,i+2,i+3))
mesh('Submerged limestone floor',vfloor,ffloor,3,(.73,.9,1),True)
mesh('Connected spring water',vwater,fwater,87,(.58,.85,1),False)
mesh('Crypt ceiling',vroof,froof,32,(.58,.77,.87),True)
mesh('Crypt boundary walls',vwall,fwall,3,(.72,.87,1),True)
ramp('Grotto resurfacing ramp',(16,42,-4.5),(16,50.5,0),3.9,3)
plane('Grotto dry landing',14,18,50.5,52,0,67)
# The entrance pond also has a walkable shallow lip for safe entry and exit.
ramp('Pond shallows',(-38,-14,0),(-38,-23,-4.5),4,3)
for x,y,angle in [(-24,-20,math.pi/2),(-20,-11,0),(-12,-5,math.pi/2),(-4,2,0),(4,20,0),(4,28,0),(11,32,math.pi/2)]:
    arch('Submerged crypt rib',(x,y,-4.5),4.6,3.7,.25,angle,tile=75)
    for sign in (-1,1):
        xx=x+math.cos(angle)*sign*2.12;yy=y+math.sin(angle)*sign*2.12
        cylinder('Crypt votive pedestal',(xx,yy,-4.5),.23,.7,17,n=8)
        cylinder('Crypt blue inset',(xx,yy,-3.8),.16,.25,-1,(.2,.7,.8),n=8,collision=False)
# Well shaft: the only light opening directly into the centre of the castle.
for x in (-3,3):wall('Well shaft',(x,9),(x,15),-.65,2.65,tile=3,thick=.2)
for y in (9,15):wall('Well shaft',(-3,y),(3,y),-.65,2.65,tile=3,thick=.2)

# Exact Summer Forest terrain trees, with the same source-region origin as the
# native loader. Geometry payload stays in the user's mounted retail copy.
section='09 Retail Trees'
reference=json.loads((ASSETS/'reference-15.json').read_text())
exec(compile(Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/tools/springtide_tree_caps.py').read_text(encoding='utf-8'),'springtide_tree_caps.py','exec'))
tree_protos={}
for region in (21,32,107):
    rows=[v for v in reference['vertices'] if v[9]==region]
    origin=((min(v[0] for v in rows)+max(v[0] for v in rows))/2,min(v[1] for v in rows),(min(v[2] for v in rows)+max(v[2] for v in rows))/2)
    rows=close_tree_canopy(rows)
    verts=[(v[0]-origin[0],v[2]-origin[2],v[1]-origin[1]) for v in rows]
    md=bpy.data.meshes.new('Summer Forest tree region '+str(region));md.from_pydata(verts,[],[(i+2,i+1,i) for i in range(0,len(verts),3)]);md.update()
    used=sorted(set(v[8] for v in rows));uv=md.uv_layers.new(name='UVMap');cl=md.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for t in used:md.materials.append(mats[t])
    for p in md.polygons:
        row=rows[p.vertices[0]];p.material_index=used.index(row[8])
        for li in p.loop_indices:
            row=rows[md.loops[li].vertex_index];tile=row[8]
            uv.data[li].uv=(max(0,min(1,(row[6]*1024-tile%16*64)/64)),max(0,min(1,1-(row[7]*384-tile//16*64)/64)))
            cl.data[li].color=(*[min(1,max(0,c)) for c in row[3:6]],1)
    base=[Vector(v) for v,row in zip(verts,rows) if row[8] in (0,47,48) and v[2]<.5]
    trunk=sum(base,Vector())/len(base) if base else Vector()
    tree_protos[region]=(md,trunk)
tree_sites=[(-19,-23,21,.85), (19,-22,32,.85),(-48,-34,107,1),(-27,-36,21,.85),(-48,-9,32,.8),(-46,20,21,.9),(-45,40,32,.9),(-28,48,21,.85),(-15,55,107,1),(10,55,32,.8),(33,49,21,.85),(47,40,107,1),(48,-1,32,.85),(48,-30,21,.8),(27,-37,107,1),(-34,35,107,.9)]
trees=[]
for i,(x,y,region,scale) in enumerate(tree_sites):
    # Regions 21/32 are interlocking fragments of the joined forest canopy.
    # Use the standalone retail tree, retaining the authored height variation.
    scale*=({21:10.625,32:11.78125,107:7.21875}[region]/7.21875)
    region=107
    md,trunk=tree_protos[region];o=bpy.data.objects.new('Summer Forest tree '+str(i),md);cols[section].objects.link(o)
    yaw=(i%5)*.61;o.location=(x,y,0);o.rotation_euler.z=yaw;o.scale=(scale,)*3
    o['s2_export']='retail_prefab';o['source_slot']=15;o['source_region']=region
    trees.append({'source_region':region,'position':[x,y,0],'yaw':-yaw,'scale':scale})
    p=Vector((x,y,0))+Vector((math.cos(yaw)*trunk.x-math.sin(yaw)*trunk.y,math.sin(yaw)*trunk.x+math.cos(yaw)*trunk.y,0))*scale
    collider=cylinder('Tree trunk collision',p,.32*scale,2.8*scale,47,n=8)
    collider['s2_export']='collision';collider.hide_render=True

# Textured, original-scale actor previews share the decoded retail meshes.
section='10 Retail Actors'
retail=json.loads((ASSETS/'retail-meshes.json').read_text());prototypes={}
def actor_mesh(key,data,texture):
    verts=[(v[0],v[2],v[1]) for v in data['vertices']];faces=[tuple(reversed(f['indices'])) for f in data['faces']]
    md=bpy.data.meshes.new(key);md.from_pydata(verts,[],faces);md.update();uv=md.uv_layers.new(name='UVMap');cl=md.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
    for textured in (True,False):
        m=bpy.data.materials.new(key+(' texture' if textured else ' colour'));m.use_nodes=True;n=m.node_tree.nodes;l=m.node_tree.links;bs=n.get('Principled BSDF')
        vc=n.new('ShaderNodeVertexColor');vc.layer_name='Color';color=vc.outputs['Color']
        if textured:
            tx=n.new('ShaderNodeTexImage');tx.image=bpy.data.images.load(str(ASSETS/(texture+'.png')),check_existing=True);tx.image.pack();tx.interpolation='Closest'
            mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;l.new(tx.outputs['Color'],mix.inputs[1]);l.new(color,mix.inputs[2]);color=mix.outputs[0]
        l.new(color,bs.inputs['Base Color']);l.new(color,bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=.5;md.materials.append(m)
    for face,p in zip(data['faces'],md.polygons):
        p.material_index=0 if face['textured'] else 1
        for j,li in enumerate(p.loop_indices):
            k=len(p.loop_indices)-1-j;u,v=face['uv'][k];uv.data[li].uv=(u,1-v);cl.data[li].color=(*[min(1,max(0,c)) for c in face['colors'][k]],1)
    return md
for data in retail['models']:
    if data['type'] in (1,0xc8,0xc9,0x12c,0x110,0x77,0x1bc,0x16d,0x71,0xd1):prototypes[data['type']]=(actor_mesh('Retail '+hex(data['type']),data,'objects-15'),data['record'])
actors=[];npcs=[];objectives=[];portals=[]
def actor(typ,p,label,gem=1,yaw=0):
    md,record=prototypes[typ];o=bpy.data.objects.new(label,md);cols[section].objects.link(o);o.location=p;o.rotation_euler.z=-yaw
    o['s2_export']='moby';o['class_id']=typ;o['source_record']=record;o['stable_id']=len(actors);o['gem_value']=gem
    actors.append({'id':len(actors),'type':typ,'record':record,'position':list(p),'yaw':yaw,'gem':gem,'name':label})
    return len(actors)-1
def npc(typ,p,name,initial,complete):
    i=actor(typ,p,name);npcs.append({'actor':i,'name':name,'initial':initial,'complete':complete});return i
hunter=npc(0x1bc,(-33,-8,0),'Hunter','Those old garden ledges lead to the library gallery. Jump all the way up and the orb is yours!','Nice climbing, Spyro! The library gallery also connects to the castle stairs.')
zoe=npc(0x110,(-4,4,1+2),'Zoe','From the upper gallery, hop across the three capitals in the great hall. There is an orb on the far side.','You made it across! You can reach the gallery again using the stairs beyond the library.')
elora=npc(0x16d,(-26,-29,0),'Elora','This spring flows beneath the castle. Dive from the pond, follow the gems through the crypt, and find the northern grotto.','You found the spring crypt! The northern grotto has a ramp back to the gardens.')
money=npc(0x77,(25,8,2),'Moneybags','Welcome to Springtide Castle! The portals lead to familiar worlds. You may admire the furnishings, but please keep your flame away from my books.','The halls, gardens and spring are yours to explore.')
for bit,npcid,p,title,points,wet in [
    (0,hunter,(-24,22,6.6),'Courtyard Climb',course+[(-24,22,6)],False),
    (1,zoe,(7,8,6.6),'Across the Great Hall',hall_course,False),
    (2,elora,(16,42,-3.75),'The Hidden Spring',[(x,y,-3.5) for x,y in tunnel],True)]:
    reward=actor(0x71,p,'Orb / '+title)
    objectives.append({'bit':bit,'npc':npcid,'reward':reward,'underwater':wet,'title':title,'checkpoints':points})

# An intentional gem trail draws the player through each space. No containers
# or trees are placed in the clear middle of doorways and narrow passages.
route=[(0,-38,0),(0,-29,0),(-12,-30,0),(-29,-32,0),(-46,-30,0),(-46,-10,0),(-44,5,0),(-33,29,0),(-31,29,0),(-31,-1,0),(-15,-12,0),(0,-12,0),(0,-9,0),(0,-.5,2),(0,5,2),(5,6,2),(5,18,2),(0,18,2),(-6,14,2),(-14,14,2),(-15,20,2),(-24,20,2),(-24,8,2),(-15,8,2),(-14,14,2),(-6,14,2),(0,22,2),(0,28,2),(0,32,2),(0,37,2),(5,34,2),(10,34,2),(30,34,2),(39,30,2),(43,20,2),(33,14,2),(26,14,2),(25,6.8,2),(15,6.8,2),(15,14,2),(8,14,2),(5,5,2),(0,0,2),(0,-9,0),(17,-12,0),(39,-10,0),(40,4,2)]
gem_positions=set()
for a,b in zip(route,route[1:]):
    n=math.ceil(math.dist(a,b)/3.1)
    for j in range(1,n):
        p=Vector(a).lerp(Vector(b),j/n);key=(round(p.x),round(p.y),round(p.z))
        if key in gem_positions:continue
        gem_positions.add(key);actor(1,p+Vector((0,0,.18)),'Route gem',gem=1 if len(gem_positions)%3 else 2)
for i,p in enumerate(course):actor(1,Vector(p)+Vector((0,0,.2)),'Climbing gem',gem=2)
for a,b in zip(tunnel,tunnel[1:]):
    for i in range(1,math.ceil(math.dist(a,b)/2.8)):
        p=Vector(a).lerp(Vector(b),i/math.ceil(math.dist(a,b)/2.8));actor(1,(p.x,p.y,-3.8),'Submerged gem',gem=2)
for i,(x,y,z) in enumerate([(-7,-39,0),(7,-37,0),(-20,-32,0),(-45,-17,0),(-48,5,0),(-41,26,0),(-29,41,0),(-16,52,0),(5,54,0),(28,52,0),(46,38,0),(46,-18,0),(29,-29,0),(-16,18,2),(-26,16,2),(-14,23,6),(5,26,6),(-7,37,2),(7,39,2),(25,22,2),(33,24,2),(44,9,2)]):
    actor(0xc8 if i%2==0 else 0xc9,(x,y,z),'Garden basket' if i%2==0 else 'Castle vase',gem=5)
for p in [(-19,-36,.25),(-47,0,.25),(-40,31,.25),(-22,46,.25),(5,53,.25),(34,45,.25),(42,-18,.25),(23,-33,.25)]:actor(0x12c,p,'Garden sheep')
actor(0xd1,(-15,35,2),'Stair tower life jar')

# Each opening uses a decoded retail portal picture, crossing plane and target.
section='07 Gardens'
portal_sites=[(0,(-12,-36,0),0,'Glimmer'),(1,(-44,-2,0),math.pi/2,'Idol Springs'),(2,(-27,12,2),math.pi/2,'Colossus'),(3,(20,24,2),0,'Hurricos'),(4,(22,53,0),0,'Aquaria Towers'),(5,(47,18,2),math.pi/2,'Sunny Beach'),(6,(8.8,25.5,6),math.pi/2,'Ocean Speedway')]
for ref,p,yaw,name in portal_sites:
    # The model's right vector is +X. Yaw is the engine's Y-up convention.
    arch('Portal / '+name,p,2.9,3.3,.5,-yaw,14)
    portals.append({'reference':ref,'position':p,'yaw':yaw,'scale':1.2,'name':name})
    if ref in (0,1,4):
        d=Vector((math.sin(yaw),math.cos(yaw),0));c=Vector(p)+d*.9
        o=box('Portal garden backing '+name,c+Vector((0,0,2.2)),(4.6,.65,4.4),3);o.rotation_euler.z=-yaw
        for sign in (-1,1):
            right=Vector((math.cos(yaw),-math.sin(yaw),0));q=Vector(p)+right*sign*2
            column('Portal finial',q.x,q.y,p[2],4.7,.26)

exec(compile(Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/tools/springtide_castle_expansion.py').read_text(encoding='utf-8'),'springtide_castle_expansion.py','exec'))
exec(compile(Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/tools/springtide_gameplay.py').read_text(encoding='utf-8'),'springtide_gameplay.py','exec'))
section='11 Gameplay'
spy=bpy.data.objects.new('Spyro / true-scale reference',actor_mesh('Spyro original idle',retail['spyro'],'spyro'));cols[section].objects.link(spy);spy.location=(0,-38,.34765625);spy['s2_export']='reference'
def marker(name,p,kind,**props):
    o=bpy.data.objects.new(name,None);cols[section].objects.link(o);o.location=p;o.empty_display_type='ARROWS';o.empty_display_size=.6;o['s2_export']='marker';o['kind']=kind
    for k,v in props.items():o[k]=v
    return o
marker('Arrival / clear landing',(*expanded_spawn[:2],expanded_spawn[2]+.05),'spawn',yaw=0.0)
for i,p in enumerate(route):marker('Walking tour '+str(i),p,'route',index=i)
for goal in objectives:
    for i,p in enumerate(goal['checkpoints']):marker(goal['title']+' checkpoint '+str(i),p,'objective',objective=goal['bit'],index=i)
for p in portals:marker('Portal target / '+p['name'],p['position'],'portal',reference=p['reference'],yaw=p['yaw'],scale=p['scale'])

section='13 Views'
world=bpy.data.worlds.new('Springtide sky');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.3,.45,.6,1);world.node_tree.nodes['Background'].inputs[1].default_value=.5;scene.world=world
sun=bpy.data.lights.new('Late spring sun','SUN');sun.energy=2.1;sun.angle=.15;ob=bpy.data.objects.new('Late spring sun',sun);cols[section].objects.link(ob);ob.rotation_euler=(.45,-.5,-.4)
for p,power,size in [((0,14,9),1700,8),((-20,15,8),850,6),((20,15,7.5),900,7),((0,39,11),1600,8),((-20,-20,-1),200,6),((4,22,-1),150,5)]:
    d=bpy.data.lights.new('Interior presentation light','AREA');d.energy=power;d.shape='DISK';d.size=size;o=bpy.data.objects.new(d.name,d);cols[section].objects.link(o);o.location=p
def camera(name,p,target,lens=30,ortho=None):
    d=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,d);cols[section].objects.link(o);o.location=p;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();d.lens=lens;d.clip_end=500
    if ortho:d.type='ORTHO';d.ortho_scale=ortho
    return o
hero=camera('01 Castle and gardens',(160,-199,143),(0,-4,3),45)
camera('02 Arrival at Spyro height',(9,-72,1),(0,5,12),23)
camera('03 Great hall interior',(1,2,4.8),(0,21,7),20)
camera('04 Library interior',(-14,8,4.3),(-25,20,5),23)
camera('05 Northern bridge',(29,40,6),(0,32,6),27)
camera('06 Underwater crypt',(-20,-18,-2.8),(-20,-7,-2.8),24)
camera('07 Homeworld plan',(0,-5,230),(0,-5,0),ortho=263)
camera('08 Courtyard climb',(-44,-4,5),(-30,15,6),26)
scene.camera=hero;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='Standard';scene.view_settings.look='Medium High Contrast' if 'Medium High Contrast' in [i.identifier for i in scene.view_settings.bl_rna.properties['look'].enum_items] else 'None'
scene.render.filepath=str(ROOT/'previews'/'castle-overview.png')
exec(compile(Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/tools/polish_springtide_roofs.py').read_text(encoding='utf-8'),'polish_springtide_roofs.py','exec'))
bpy.context.view_layer.update()
manifest={'schema':2,'id':'springtide-castle','title':'Springtide Castle','source_bank':15,'coordinates':'Blender Z-up, one unit = 1024 retail units','spawn':[0,-38,.05],'route':route,'stairs':stairs,'platform_courses':[{'name':'Courtyard Climb','points':course+[(-28,20,6),(-24,22,6)]},{'name':'Across the Great Hall','points':hall_course}],'swim_route':[(x,y,-3.5) for x,y in tunnel],'actors':actors,'npcs':npcs,'objectives':objectives,'portals':portals,'trees':trees,'retail_reference':{'summer_forest_environment_triangles':14553,'summer_forest_footprint':[140.22,114.5]},'ps1':{'status':'Reduction study only; not an archive or hardware build','grid':1/64,'sector_size':16,'retail_tree_references':True,'pc_only':'Detailed books and small furniture accents'}}
manifest['spawn']=expanded_spawn;manifest['extra_routes']=extra_routes;manifest['upper_stairs']=upper_stairs;manifest['lake_swim']=lake_swim
manifest['platform_courses'].append({'name':'Woodland Steps','points':wood_ledges})
manifest['footprint']=[240,232]
manifest['mechanisms']=mechanisms;manifest['trials']=trials
(ROOT/'source'/'design.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
bpy.data.libraries.write(str(ROOT/'Springtide Castle.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
result={'scene':scene.name,'blend':str(ROOT/'Springtide Castle.blend'),'objects':len(scene.objects),'actors':len(actors),'trees':len(trees),'portals':len(portals),'objectives':len(objectives),'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in scene.objects if o.type=='MESH')}

"""Authored homeworld interactions and thin, chamfered stone platform routes."""
section='07 Gardens'
mechanisms=[];trials=[]
def slab(name,p,width=3.5,depth=3.1,thickness=.28):
    x,y,z=p;a,b=width/2,depth/2;cut=.36
    outline=[(-a+cut,-b),(a-cut,-b),(a,-b+cut),(a,b-cut),(a-cut,b),(-a+cut,b),(-a,b-cut),(-a,-b+cut)]
    verts=[(x+xx,y+yy,z+zz) for zz in (-thickness,0) for xx,yy in outline]
    faces=[tuple(reversed(range(8))),tuple(range(8,16))]+[(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    return mesh(name,verts,faces,27,(.94,.96,1),True)
def mechanism(kind,name,p,extent,axis,travel,period,objects,objective=7):
    i=len(mechanisms)
    for o in objects:o['gameplay_group']=i
    mechanisms.append({'id':i,'kind':kind,'objective':objective,'name':name,'center':list(p),'extent':extent,'axis':axis,'travel':travel,'period':period})

# Replace only this scene's rejected block geometry, retaining its baseline
# course locations and all unrelated assets. Hidden draft meshes stay editable.
for o in list(scene.objects):
    if o.type=='MESH' and any(s in o.name for s in ['Courtyard climb masonry','Courtyard climb carved cap','Ledge pilaster','Hall challenge capital','Woodland ruin ledge','Woodland carved cap']):
        o['s2_export']='reference';o.hide_render=True;o.hide_set(True)
for i,(x,y,z) in enumerate(course):
    o=slab('Garden stepping slab '+str(i),(x,y,z),3.6,3.3)
    # Narrow carved supports leave the platform's slab silhouette readable.
    if i%2==0:column('Garden slab support',x,y,0,max(.2,z-.3),.26)
    elif i==3:mechanism(3,'Settling garden slab',(x,y,z-.14),(1.8,1.65,.14),(0,0,1),-.22,60,[o])
    else:mechanism(1,'Drifting garden slab',(x,y,z-.14),(1.8,1.65,.14),(0,1,0),.35,420,[o])
lift=slab('Pressure lift slab',(-40.7,-3,.02),3.6,3.6)
mechanism(2,'Garden pressure lift',(-40.7,-3,-.12),(1.8,1.8,.14),(0,0,1),1.98,120,[lift])
# Small corner posts make the lift's travel shaft legible without enclosing it.
for x in (-42.8,-38.6):
    for y in (-5.1,):column('Lift guide post',x,y,0,2.4,.12)
for i,(x,y,z) in enumerate(hall_course[1:-1]):
    o=slab('Great hall suspended slab '+str(i),(x,y,z),2.65,2.65,.24)
    if i==1:mechanism(1,'Hall crossing slab',(x,y,z-.12),(1.325,1.325,.12),(0,1,0),.42,480,[o])
for i,(x,y,z) in enumerate(wood_ledges):
    o=slab('Woodland slate slab '+str(i),(x,y,z),3.75,3.4)
    column('Woodland slab stem',x,y,land(x,y),max(.2,z-land(x,y)-.3),.32)
    if i in (1,3):mechanism(3,'Woodland settling slab',(x,y,z-.14),(1.875,1.7,.14),(0,0,1),-.18,72,[o])

# New optional puzzle: three watchfires respond to Spyro's directed flame.
watch_npc=npc(0x1bc,(-9,-75,land(-9,-75)),'Hunter','Light the three watchfires in this outer courtyard with your flame. The watchtower orb appears once all three are burning.','All three watchfires are alight! The courtyard is ready for the spring festival.')
watch_sites=[(-22,-62),(18,-54),(-8,-69)]
for i,(x,y) in enumerate(watch_sites):
    z=land(x,y)
    column('Watchfire plinth',x,y,z,.8,.42)
    bowl=cylinder('Watchfire brazier',(x,y,z+.8),.6,.3,60,n=12,r2=.8)
    flame=cylinder('Watchfire sculpted flame',(x,y,z+1.1),.35,.85,-1,(.8,.48,.15),n=7,r2=.015)
    mechanism(4,'Watchfire '+str(i+1),(x,y,z+1.05),(.8,.7,.8),(0,0,1),0,1,[bowl,flame],5)
watch_orb=actor(0x71,(-6,-63,land(-6,-63)+.6),'Orb / Light the Watchfires')
objectives.append({'bit':5,'npc':watch_npc,'reward':watch_orb,'underwater':False,'title':'Light the Watchfires','checkpoints':[(-6,-63,land(-6,-63))]})

# New optional timed route, started deliberately at its marked floor plate.
courier_start=(-20,-57,land(-20,-57))
courier_npc=npc(0x110,(-24,-58,land(-24,-58)+1),'Zoe','Stand on the gold courier plate and press E to start. Reach the gold markers in order, then claim the orb by the thrones before time runs out.','A perfect castle delivery! You can still explore the halls at your own pace.')
start_plate=slab('Courier start plate',Vector(courier_start)+Vector((0,0,.025)),2.8,2.8,.08)
start_plate.data.materials[0]=mats[60]
courier_points=[courier_start,(0,-34,0),(0,-9,0),(5,6,2),(-15,14,2),(0,22,2),(0,37,2),(0,41,2.75)]
for i,(x,y,z) in enumerate(courier_points[1:]):
    cylinder('Courier gold marker '+str(i),(x,y,z+.025),.58,.035,60,n=12,collision=False)
courier_orb=actor(0x71,(0,43,3.35),'Orb / Castle Courier')
objectives.append({'bit':6,'npc':courier_npc,'reward':courier_orb,'underwater':False,'title':'Castle Courier','checkpoints':courier_points})
trials.append({'objective':6,'limit':6000,'start':courier_start,'name':'CASTLE COURIER'})
for n in npcs:
    if n['actor']==hunter:n['initial']='The thin stone ledges lead to the library gallery. Some drift or settle under your weight. The pressure slab near the first ledge rises when you stand on it.'

# Fix the walking tour around furniture, tower footprints and the fountain.
new_route=[]
for p in route:
    if p==(-31,29,0):new_route.extend([(-41,30,0),(-46,-1,0),(-46,-12,0),(-39,-12,0)])
    elif p==(-31,-1,0):new_route.append((-31,-11,0))
    elif p==(-15,-12,0):new_route.extend([(-15,-8,0),(-5,-8,0),(-5,-12,0)])
    elif p==(-24,20,2):new_route.append((-25,20,2))
    elif p==(-24,8,2):new_route.append((-25,8,2))
    elif p==(39,30,2):new_route.extend([(39,30,2),(43,30,2)])
    elif p==(17,-12,0):new_route.extend([(5,-9,0),(15,-8,0),(17,-12,0)])
    else:new_route.append(p)
route=new_route
trail=[]
for a,b in zip(route,route[1:]):
    n=max(1,math.ceil(math.dist(a,b)/3))
    for j in range(1,n):trail.append(Vector(a).lerp(Vector(b),j/n)+Vector((0,0,.18)))
old_gems=[o for o in scene.objects if o.get('s2_export')=='moby' and o.name.startswith('Route gem')]
for i,o in enumerate(old_gems):
    if trail:o.location=trail[min(len(trail)-1,int(i*len(trail)/len(old_gems)))];actors[o['stable_id']]['position']=list(o.location)
for o in scene.objects:
    if 'Library globe pedestal' in o.name or 'Faceted celestial globe' in o.name:
        o.location+=Vector((-1,1,0))
# Apply the retail water tint and broad UV scale; avoid a tiled white grid.
for o in scene.objects:
    if o.type!='MESH' or not any(m.get('water') for m in o.data.materials):continue
    for c in o.data.color_attributes['Color'].data:c.color=(.28125,.390625,.6796875,1)

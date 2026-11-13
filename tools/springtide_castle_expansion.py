"""Castle revision: fortified silhouette, linked lower bailey and landscape.

Runs inside the new scene only. The rejected small draft is retained separately.
"""
section='01 Landscape'
scene['revision']=2
material(40,'Unbanded cliff rock')
def land(x,y):
    distance=max(0,abs(x)-52,-43-y,y-58)
    base=-6*min(1,distance/22)
    # Smooth hills frame the paths, rather than another rectangular flat plane.
    west=9*math.exp(-((x+91)/22)**2-((y-37)/35)**2)
    north=12*math.exp(-((x+5)/53)**2-((y-105)/17)**2)
    east=7*math.exp(-((x-109)/18)**2-((y+51)/40)**2)
    return base+(west+north+east)*min(1,distance/22)
lake=(63,101,1,45)
xs=sorted(set(list(range(-120,121,4))+[-52,52,63,101]));ys=sorted(set(list(range(-122,111,4))+[-43,58,1,45,110]))
v=[];f=[];co=[]
for a,b in zip(xs,xs[1:]):
    for c,d in zip(ys,ys[1:]):
        x,y=(a+b)/2,(c+d)/2
        if -52<x<52 and -43<y<58:continue
        if lake[0]<x<lake[1] and lake[2]<y<lake[3]:continue
        # Rounded world outline with irregular rocky shoulders.
        if (x/125)**4+((y+5)/122)**4>1.08:continue
        i=len(v);v.extend([(a,c,land(a,c)),(b,c,land(b,c)),(b,d,land(b,d)),(a,d,land(a,d))]);f.append((i,i+1,i+2,i+3))
o=mesh('Rolling outer homeworld',v,f,59,collision=True)
for poly in o.data.polygons:
    for li in poly.loop_indices:
        p=o.data.vertices[o.data.loops[li].vertex_index].co
        shade=.8+.17*(.5+.5*math.sin(p.x*.105)*math.cos(p.y*.073))
        o.data.color_attributes['Color'].data[li].color=(shade*.95,shade,shade*.84,1)
# Use the actual outer boundary, with an independent grass rim and bare rock
# below it. No grass-topped wall tile is repeated down the side of the world.
from collections import Counter
edges=Counter()
for face in f:
    for a,b in zip(face,face[1:]+face[:1]):edges[tuple(sorted((tuple(v[a]),tuple(v[b]))))]+=1
cliff_v=[];cliff_f=[]
for (a,b),count in edges.items():
    if count!=1:continue
    mid=(Vector(a)+Vector(b))/2
    central=(abs(abs(mid.x)-52)<.01 and -43.01<mid.y<58.01) or (abs(mid.y+43)<.01 or abs(mid.y-58)<.01) and abs(mid.x)<52.01
    lake_edge=(abs(mid.x-63)<.01 or abs(mid.x-101)<.01) and .99<mid.y<45.01 or (abs(mid.y-1)<.01 or abs(mid.y-45)<.01) and 62.99<mid.x<101.01
    if central or lake_edge:continue
    for lo,hi in ((0,1),(1,2),(2,3),(3,4)):
        def edgepoint(p,t):
            outward=Vector((p[0],p[1]+5,0)).normalized()
            q=Vector(p)+outward*(math.sin(t*.8)*1.6);q.z-=t*4.5;return q
        k=len(cliff_v);cliff_v.extend([edgepoint(a,lo),edgepoint(b,lo),edgepoint(b,hi),edgepoint(a,hi)]);cliff_f.append((k,k+1,k+2,k+3))
mesh('Shaped outer cliff faces',cliff_v,cliff_f,40,(.77,.79,.83),True)

def road(name,xy,width=4):
    pts=[]
    for a,b in zip(xy,xy[1:]):
        n=max(1,math.ceil(math.dist(a,b)/2))
        for j in range(n):
            x=a[0]+(b[0]-a[0])*j/n;y=a[1]+(b[1]-a[1])*j/n;pts.append((x,y,land(x,y)+.035))
    x,y=xy[-1];pts.append((x,y,land(x,y)+.035));path(name,pts,width,67)
    return [(p[0],p[1],p[2]-.035) for p in pts]
approach=road('Winding royal approach',[(0,-114),(-16,-103),(-8,-94),(0,-89),(0,-79),(18,-62),(12,-48),(0,-38)],5)
west_route=road('Woodland valley route',[(-16,-103),(-45,-99),(-72,-84),(-92,-61),(-91,-30),(-85,-8),(-79,21),(-68,42),(-43,51)],4)
north_route=road('North ridge route',[(-43,51),(-36,76),(-15,91),(14,87),(35,75),(45,55)],4)
east_route=road('Lake shore route',[(12,-48),(0,-57),(0,-90),(14,-96),(36,-91),(48,-87),(66,-82),(76,-61),(76,-43),(88,-22),(104,0),(105,27),(105,53),(87,68),(60,67),(45,55)],4)
road('West postern approach',[(-91,-30),(-72,-23),(-61,-15),(-46,-15)],4)
road('East postern approach',[(88,-22),(71,-19),(57,-15),(42,-15)],4)
road('Bailey cross path',[(-48,-52),(-31,-59),(-9,-62),(18,-62),(38,-53),(49,-37)],3.5)

def tower(name,x,y,z,r,h,spire=True):
    section_before=globals()['section'];globals()['section']='02 Great Hall'
    cylinder(name+' spreading foundation',(x,y,z),r+.6,.65,3,n=16,r2=r+.25)
    bands=math.ceil(h/2.5)
    for j in range(bands):cylinder(name+' stone drum',(x,y,z+.65+j*(h-.65)/bands),r,(h-.65)/bands,27,n=16,caps=False)
    for zz in (z+h*.45,z+h-.65):cylinder(name+' carved string course',(x,y,zz),r+.12,.28,17,n=16)
    cylinder(name+' machicolation collar',(x,y,z+h-.35),r+.45,.65,5,n=16,r2=r+.55)
    # Dark inset lancets and their raised pale jambs are on the curved surface.
    for row in range(2):
        for j in range(8):
            t=j*math.tau/8+.14;xx=x+(r+.015)*math.cos(t);yy=y+(r+.015)*math.sin(t)
            o=box(name+' lancet',(xx,yy,z+h*(.32+row*.34)),(.66,.045,1.85),51,(.38,.46,.57),False);o.rotation_euler.z=t+math.pi/2
    if spire:
        cylinder(name+' roof skirt',(x,y,z+h+.3),r+.8,.35,60,n=16)
        cylinder(name+' pointed roof',(x,y,z+h+.65),r+.75,r*2.25,-1,(.26,.32,.49),n=16,r2=.09)
        cylinder(name+' gold finial',(x,y,z+h+r*2.25+.6),.13,1.2,60,n=8,r2=.03)
    else:
        for j in range(12):
            t=j*math.tau/12;o=box(name+' crenel',(x+(r+.1)*math.cos(t),y+(r+.1)*math.sin(t),z+h+.85),(1.15,.75,1.25),27);o.rotation_euler.z=t+math.pi/2
    globals()['section']=section_before

# The inner castle is an enclosed fortified complex, with three posterns.
section='02 Great Hall'
wall('Inner south curtain west',(-51,-15),(-5,-15),0,8,[(2.5,7.5,0,4.5)],tile=27,thick=1.3)
wall('Inner south curtain east',(5,-15),(51,-15),0,8,[(32.5,40.5,0,5)],tile=27,thick=1.3)
wall('West inner curtain',(-51,-15),(-51,66),-2,10,[(59,73,-2,6)],tile=27,thick=1.4)
wall('East inner curtain',(51,-15),(51,66),-2,10,[(65,76,-2,6)],tile=27,thick=1.4)
wall('North inner curtain',(-51,66),(51,66),-2,10,[(42,59,-2,6),(8,18,-2,6),(85,97,-2,6)],tile=27,thick=1.4)
for x0,x1,y in [(-51,-5,-15),(5,51,-15),(-51,-9,66),(8,51,66)]:
    plane('Curtain wall walk',x0,x1,y-.8,y+.8,8,68)
    for x in range(math.ceil(x0),math.floor(x1),2):box('Curtain merlon',(x,y,8.65),(1,.85,1.3),27)
for x in (-51,51):
    plane('Side curtain wall walk',x-.8,x+.8,-15,66,8,68)
    for y in range(-14,67,2):box('Side curtain merlon',(x,y,8.65),(.85,1,1.3),27)
arch('Great gatehouse',(0,-15,0),10,9.5,2.5)
arch('West garden postern',(-46,-15,0),5,4.5,1.6)
arch('East garden postern',(41.5,-15,0),8,5,1.6)
for x,y,h,r,spire in [(-9,-15,15,4,True),(9,-15,15,4,True),(-51,-15,12,3.4,False),(51,-15,12,3.4,False),(-51,65,15,3.5,True),(51,65,15,3.5,True),(-10.5,0,23,2.6,True),(10.5,0,23,2.6,True),(-10.5,47,26,3.1,True),(10.5,47,26,3.1,True),(-28,5,15,2.5,False),(28,5,15,2.5,False)]:tower('Castle tower',x,y,0,r,h,spire)

# A tall, occupiable keep rises above the hall. Its terrace is reached from the
# existing gallery via the stair tower, with a roof hatch instead of a sealed lid.
keep_floor=13.2
plane('Upper keep floor',-7,7,17,28,keep_floor,68)
wall('Upper keep west',(-7,17),(-7,28),keep_floor,10.8,[(3,7,keep_floor,keep_floor+4.5)],thick=.8)
wall('Upper keep east',(7,17),(7,28),keep_floor,10.8,[(3.5,6.5,keep_floor+3,keep_floor+7)],thick=.8)
wall('Upper keep south',(-7,17),(7,17),keep_floor,10.8,[(2,4,keep_floor+3,keep_floor+7),(10,12,keep_floor+3,keep_floor+7)],thick=.8)
wall('Upper keep north',(-7,28),(7,28),keep_floor,10.8,[(5,9,keep_floor+3,keep_floor+7)],thick=.8)
roof('High keep',-7.5,7.5,16.5,28.5,24.1,24.1)
arch('Keep upper doorway',(-7,22,keep_floor),4,4.5,.8,math.pi/2)
for x,y in [(-7.5,16.5),(7.5,16.5),(-7.5,28.5),(7.5,28.5)]:tower('Keep crown turret',x,y,23.8,1.1,2.6,True)
for x in (-4,4):
    bookcase('Keep chart shelves',x,27,keep_floor)
column('Keep astrolabe plinth',0,23,keep_floor,1.2,.6)
cylinder('Keep star table',(0,23,keep_floor+1.2),1.4,.2,60,n=12)
for x in (-5,5):bench('Keep reading bench',x,19,keep_floor,math.pi)
upper_stairs=[(-14,26,6),(-14,33.5,9.25),(-20,33.5,11.4),(-12,26,13.2),(-8.3,26,13.2),(-8.3,22,13.2),(-5,22,13.2)]
for i,(a,b) in enumerate(zip(upper_stairs,upper_stairs[1:])):stair_flight('Keep ascent '+str(i),a,b,2.5,68)
for p in upper_stairs:plane('Keep stair landing',p[0]-1.25,p[0]+1.25,p[1]-1.25,p[1]+1.25,p[2],68)

section='07 Gardens'
def curtain(name,a,b):
    av,bv=Vector(a),Vector(b);n=math.ceil((bv-av).length/3.5);d=(bv-av).normalized()
    for i in range(n):
        p=av.lerp(bv,i/n);q=av.lerp(bv,(i+1)/n);z=min(land(*p),land(*q));h=7.5
        wall(name,p,q,z,h,tile=27,thick=1.1)
        c=(p+q)/2;o=box(name+' crenel',(c.x,c.y,z+h+.6),(1.5,1.2,1.2),27);o.rotation_euler.z=math.atan2(d.y,d.x)
for a,b in [((-6,-86),(-52,-78)),((-52,-78),(-66,-48)),((-66,-48),(-51,-15)),((6,-86),(53,-77)),((53,-77),(66,-47)),((66,-47),(51,-15))]:curtain('Outer bailey curtain',a,b)
for x,y in [(-9,-85),(9,-85),(-52,-78),(53,-77),(-65,-47),(65,-47)]:tower('Outer bailey tower',x,y,land(x,y),3.4,11,False)
arch('Outer gate',(0,-85,land(0,-85)),11,7.5,2)
# Guard chambers and roofed gate-side niches, with walk-through door openings.
for x,y,ang in [(-39,-62,0),(40,-61,0),(-59,-38,math.pi/2)]:
    z=land(x,y);plane('Guard court pad',x-5,x+5,y-5,y+5,z,67)
    wall('Guard chamber rear',(x-4,y+4),(x+4,y+4),z,5.8)
    wall('Guard chamber west',(x-4,y-4),(x-4,y+4),z,5.8)
    wall('Guard chamber east',(x+4,y-4),(x+4,y+4),z,5.8)
    wall('Guard chamber entrance',(x-4,y-4),(x+4,y-4),z,5.8,[(2.3,5.7,z,z+3.5)])
    arch('Guard chamber arch',(x,y-4,z),3.4,3.5,.7)
    roof('Guard tower roof',x-4.4,x+4.4,y-4.4,y+4.4,z+6.1,z+6.1)
    bench('Guard chamber bench',x,y+2,z)
    for xx in (x-2.5,x+2.5):actor(0xc9,(xx,y+1,z),'Guardroom vase',gem=5)

# Lake, ruined island sanctuary and an actual submerged circuit. The lake has
# shallow entrance/exit ramps, a crossing bridge and underwater columns.
section='08 Spring Crypt'
plane('Outer lake floor',63,101,1,45,-11,40,(.75,.87,.96),True,step=3)
plane('Outer lake water',63,101,1,45,-6.6,87,(.28125,.390625,.6796875),False,step=3)
for a,b in [((63,1),(101,1)),((101,1),(101,45)),((101,45),(63,45)),((63,45),(63,1))]:
    n=math.ceil(math.dist(a,b)/3)
    for j in range(n):
        p=Vector(a).lerp(Vector(b),j/n);q=Vector(a).lerp(Vector(b),(j+1)/n)
        mesh('Lake shaped bank',[(*p,-11),(*q,-11),(*q,land(*q)),(*p,land(*p))],[(0,1,2,3)],40,collision=True)
ramp('Lake south diving beach',(72,0,land(72,0)),(72,15,-11),6,3)
ramp('Lake north resurfacing beach',(92,33,-11),(92,46,land(92,46)),6,3)
island=(82,25)
cylinder('Lake sanctuary foundation',(*island,-11),6,9,3,n=12)
cylinder('Lake sanctuary platform',(*island,-2),6.4,.25,67,n=12)
for i in range(8):
    t=i*math.tau/8
    if i==4:continue
    x=82+4.8*math.cos(t);y=25+4.8*math.sin(t);column('Sanctuary broken arcade',x,y,-1.75,4.5 if i%2 else 3.2,.45)
    if i%2:cylinder('Sanctuary crown fragment',(x,y,2.75),.9,.35,17,n=8)
ramp('Lake sanctuary bridge',(58,25,land(58,25)),(76,25,-1.75),3.2,67)
for x in (62,68,74):
    column('Sanctuary bridge pier',x,25,-11,9.3,.7)
for x,y in [(69,8),(94,12),(69,37),(95,40)]:
    column('Drowned temple pillar',x,y,-11,4,.7)
    box('Drowned temple slab',(x,y,-6.8),(3,3,.4),17)
lake_swim=[(72,10,-8),(83,8,-8),(95,16,-8),(94,31,-8),(89,40,-8)]
lake_npc=npc(0x16d,(60,-1,land(60,-1)),'Elora','There are more ruins beneath this lake. Follow the blue gems around the island to uncover another orb.','The lake sanctuary is yours to explore. The north beach leads back to the castle gardens.')
reward=actor(0x71,(89,40,-7.65),'Orb / Drowned Sanctuary')
objectives.append({'bit':3,'npc':lake_npc,'reward':reward,'underwater':True,'title':'Drowned Sanctuary','checkpoints':lake_swim})
for a,b in zip(lake_swim,lake_swim[1:]):
    for j in range(1,5):actor(1,Vector(a).lerp(Vector(b),j/5),'Lake gem',gem=5)

# Woodland terraces and a ruined aqueduct produce a second long, elevated loop.
section='07 Gardens'
wood_ledges=[(-94,-4,land(-94,-4)+1),(-96,0,land(-94,-4)+1.65),(-93,3.6,land(-94,-4)+2.3),(-96,7.2,land(-94,-4)+2.95),(-93,10.8,land(-94,-4)+3.6)]
for i,(x,y,z) in enumerate(wood_ledges):
    base=land(x,y);cylinder('Woodland ruin ledge',(x,y,base-.15),1.6,max(.3,z-base+.15),3,n=8)
    cylinder('Woodland carved cap',(x,y,z-.12),1.8,.12,17,n=8)
    actor(1,(x,y,z+.2),'Woodland climb gem',gem=5)
ramp('Woodland challenge approach',(-94,-11,land(-94,-11)),(-94,-6,wood_ledges[0][2]),3.3)
plane('Woodland challenge landing',-95.65,-92.35,-6,-5.5,wood_ledges[0][2],67)
wood_npc=npc(0x1bc,(-87,-6,land(-87,-6)),'Hunter','These ruined ledges climb into the trees. Keep your jumps steady, and look for the orb at the last pillar.','That is the woodland route conquered! The valley path carries on to the north ridge.')
reward=actor(0x71,Vector(wood_ledges[-1])+Vector((0,0,.6)),'Orb / Woodland Steps')
objectives.append({'bit':4,'npc':wood_npc,'reward':reward,'underwater':False,'title':'Woodland Steps','checkpoints':wood_ledges})
for x in range(-85,-52,6):
    z=land(x,59);column('Old aqueduct pier',x,59,z,6,.7)
    if x<-55:arch('Aqueduct arch',(x+3,59,z),4.4,5.8,.8)
for x,y in [(-101,-50),(-67,-83),(84,-39),(110,56),(-35,87),(20,96)]:
    z=land(x,y);cylinder('Wayside stone terrace',(x,y,z),3.2,.3,67,n=12)
    bench('Wayside bench',x,y+1.5,z+.3)
    for xx in (x-2,x+2):actor(0xc8,(xx,y,z+.3),'Outer-world basket',gem=5)

# More exact retail trees form groves, while every main route retains a clear
# three-unit corridor. No blossoms or substitute procedural canopies are used.
section='09 Retail Trees'
extra_tree_sites=[(-31,-108),(-53,-104),(-76,-92),(-101,-77),(-108,-54),(-107,-25),(-102,1),(-108,26),(-98,43),(-78,62),(-62,76),(-43,88),(-24,102),(2,102),(27,102),(46,92),(66,86),(85,78),(106,71),(114,42),(111,12),(112,-20),(99,-48),(81,-65),(59,-92),(35,-107),(15,-118),(-23,-75),(-29,-39),(31,-42),(35,-77),(-79,-46),(-76,9),(-77,28),(63,-2),(69,53)]
for i,(x,y) in enumerate(extra_tree_sites):
    region=107;md,trunk=tree_protos[region];scale=(.9+(i%3)*.1)*(1.47,1.63,1)[i%3];yaw=(i%7)*.57;z=land(x,y)
    o=bpy.data.objects.new('Summer Forest outer grove '+str(i),md);cols[section].objects.link(o);o.location=(x,y,z);o.rotation_euler.z=yaw;o.scale=(scale,)*3
    o['s2_export']='retail_prefab';o['source_slot']=15;o['source_region']=region
    trees.append({'source_region':region,'position':[x,y,z],'yaw':-yaw,'scale':scale})
    p=Vector((x,y,z))+Vector((math.cos(yaw)*trunk.x-math.sin(yaw)*trunk.y,math.sin(yaw)*trunk.x+math.cos(yaw)*trunk.y,0))*scale
    ob=cylinder('Outer tree trunk collision',p,.32*scale,2.8*scale,47,n=8);ob['s2_export']='collision';ob.hide_render=True

section='10 Retail Actors'
expanded_spawn=approach[0]
# Place rewards along every long route, and sheep at sheltered resting points.
for walkway in (approach,west_route,north_route,east_route):
    for j,p0 in enumerate(walkway[1::2]):
        p=Vector(p0);p.z=land(p.x,p.y)+.18
        actor(1,p,'Outer trail gem',gem=2 if j%3 else 5)
for x,y in [(-30,-91),(-87,-41),(-72,32),(-26,79),(29,79),(107,61),(78,-51),(25,-86)]:actor(0x12c,(x,y,land(x,y)+.25),'Outer meadow sheep')
# Keep the original interior tour, preceded by the outer approach. Optional
# circuits are exported separately so the main tour does not become a marathon.
route=approach+route[1:]
route=[(43,13,2) if p==(43,20,2) else p for p in route]
extra_routes={'woodland':west_route,'north_ridge':north_route,'lake_shore':east_route,'keep_stairs':upper_stairs}

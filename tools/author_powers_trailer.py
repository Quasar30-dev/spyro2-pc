"""Reproducible retail-asset trailer shots; no simulation or artwork substitutes."""
from pathlib import Path
import json, math, struct, functools, sys
from PIL import Image, ImageDraw, ImageFilter
from spyrodisc import wad_index

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/spyro-powers-trailer'
for folder in ('source','gameplay','titles','review','edit'):(OUT/folder).mkdir(parents=True,exist_ok=True)

def action(begin,end=None,**kw):
    fields=('charge','jump','flame','headbash','melee','carry','throw','special','interact','steer','mx','mz','left','right')
    return [begin,end or begin+1]+[kw.get(k,0) for k in fields]

def shot(name,seconds,spawn,yaw=0,cameras=(),route=(),actions=(),mode=2,slot=17,flags=(),hud=0,fov=52):
    frames=round(seconds*60)+5
    lines=['S2SHOT 4',f'1920 1080 {frames}',' '.join(map(str,(*spawn,yaw,hud,fov,mode))),str(len(route))]
    lines+=[' '.join(map(str,p)) for p in route]
    lines+=[str(len(actions))]+[' '.join(map(str,p)) for p in actions]
    lines+=[str(len(cameras))]+[' '.join(map(str,(round(t*60),*e,*p))) for t,e,p in cameras]
    path=OUT/'source'/f'{name}.s2shot';path.write_text('\n'.join(lines)+'\n')
    args=['--game','data/disc','--headless','--cinematic-shot',path.relative_to(ROOT).as_posix(),'--slot',str(slot),'--capture-music-volume','0',*flags]
    shots.append(dict(name=name,args=args,seconds=seconds))

def clone(name,path,slot,flags=()):
    tokens=(ROOT/path).read_text().split();version=int(tokens[1]);frames=int(tokens[4]);at=12
    count=int(tokens[at]);at+=1;route=[]
    for _ in range(count):route.append(list(map(float,tokens[at:at+3])));at+=3
    count=int(tokens[at]);at+=1;actions=[]
    length={1:5,2:6,3:14,4:16}[version]
    for _ in range(count):
        a=list(map(float,tokens[at:at+length]));at+=length
        padded=a+[0]*(16-length)
        actions.append([int(v) if k not in (12,13) else v for k,v in enumerate(padded)])
    count=int(tokens[at]);at+=1;cameras=[]
    for _ in range(count):
        c=list(map(float,tokens[at:at+7]));at+=7;cameras.append((c[0]/60,c[1:4],c[4:7]))
    assert at==len(tokens)
    shot(name,(frames-5)/60,list(map(float,tokens[5:8])),float(tokens[8]),cameras,route,actions,int(tokens[11]),slot,flags,int(tokens[9]),float(tokens[10]))

def author_shots():
    global shots
    shots=[]
    p=(66.3033,9.25,33.0037)
    shot('glimmer-vista',7,p,cameras=[(0,(78,24,55),(64,11,37)),(7,(75,20,51),(64,11,37))],mode=1,fov=58)
    clone('forest-vista','output/backrealms-trailer/source/04-forest-night.s2shot',15)
    clone('shores-run','output/backrealms-trailer/source/03-return.s2shot',69)
    shot('regular-run',7,(56.9151,10.0659,37.0217),.756,cameras=[(0,(-2.8,1.8,-3.6),(0,.6,1)),(7,(-2.6,1.6,-3.3),(0,.6,1))],route=[(56.9151,10.0659,37.0217),(64,9.25,34),(72,9.25,35)],fov=54)
    shot('charge-jump',5,p,math.pi/2,cameras=[(0,(-2.8,1.1,-2.8),(.5,.65,1)),(5,(-3,1.2,-2.9),(.5,.65,1))],route=[p,(80,9.25,33)],actions=[action(5,140,charge=1),action(52,100,jump=1)])
    for tier,label in [(1,'yellow-charge'),(2,'red-charge')]:
        shot(label,6,p,math.pi/2,cameras=[(0,(-2.8,1.3,-2.2),(.8,.7,0)),(6,(-3,1.1,-1.6),(.8,.7,0))],route=[p,(82,9.25,33)],actions=[action(8,300,charge=1),action(64,68,jump=1)],flags=['--upgrade-tier',str(tier),'--supercharge','main-flap'],fov=58)
    shot('dodge-roll',5,p,0,cameras=[(0,(1.2,1.6,-3),(0,.35,0)),(5,(1.1,1.3,-2.8),(0,.35,0))],actions=[action(30,left=1),action(120,right=1),action(205,left=1)],flags=['--upgrade-tier','2'])
    shot('melee',6,p,math.pi,cameras=[(0,(1.5,1.3,-2.8),(0,.4,0)),(6,(1.5,1.2,-2.5),(0,.4,0))],actions=[action(t,melee=1) for t in (30,46,73,155,171,198)],flags=['--upgrade-tier','2'],fov=48)
    for label,flags in [('glide-boost',['--upgrade-tier','2']),('superfly',['--upgrade-tier','2','--superfly'])]:
        shot(label,7,(115,50,77),-1.55,slot=15,cameras=[(0,(2.4,1.3,-2.4),(0,.45,0)),(7,(2,1,-2.2),(0,.45,0))],actions=[action(t,jump=1) for t in (1,32,90,160)]+[action(2,390,steer=1,mx=-.1,mz=.02)],flags=flags,fov=56)
    for label,tier in [('headbash-crater',1),('headbash-lava',2)]:
        clone(label,'data/validation/headbash-upgrade/lava-demo.s2shot',17,['--upgrade-tier',str(tier)])
    shot('homing-flame',7,(56.9151,10.0659,37.0217),.756,cameras=[(0,(-2.3,1.2,-2.8),(1,.65,2)),(7,(-2.4,1.3,-2.8),(1,.65,2))],actions=[action(t,flame=1) for t in (20,65,110,155,200,245,290)],flags=['--upgrade-tier','2','--superflame'],fov=55)
    clone('wall-charge','data/analysis/destruction-20261002/wall-charge.s2shot',17,['--terrain-destruction','--supercharge','main-flap'])
    clone('wall-flame','data/analysis/destruction-20261002/wall-superflame.s2shot',17,['--terrain-destruction','--superflame'])
    clone('green-bomb','data/analysis/destruction-20261002/wall-bomb.s2shot',17,['--terrain-destruction','--green-firebomb'])
    clone('building-breach','data/analysis/destruction-20261002/building-bomb-traverse.s2shot',15,['--terrain-destruction','--green-firebomb','--destruction-strength','3'])
    clone('boss','output/backrealms-trailer/source/18-ripto.s2shot',15)
    # The boss clone uses the actual Crownworks arena, with ordinary Spyro.
    shots[-1]['args']+=['--custom-level','data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world']
    for label,ability,frames in [('carry','carry-inspect',245),('drop-smash','carry',245)]:
        shots.append(dict(name=label,seconds=(frames-5)/60,args=['--game','data/disc','--slot','19','--headless','--frames',str(frames),'--capture-fullscreen','--upgrade-tier','2','--ability-replay',ability,'--interaction-record','62','--camera-distance','3.2','--camera-height','1.6','--camera-orbit','-35','--capture-music-volume','0']))
    shots.append(dict(name='underwater',seconds=10,args=['--game','data/disc','--slot','15','--headless','--frames','605','--capture-fullscreen','--swim-replay','--camera-distance','4','--camera-height','1.2','--camera-orbit','35','--capture-music-volume','0']))
    shots.append(dict(name='superflame-target',seconds=5,args=['--game','data/disc','--slot','17','--headless','--frames','305','--capture-fullscreen','--upgrade-tier','2','--superflame','--interaction-record','8','--camera-distance','3.1','--camera-height','1.2','--camera-orbit','35','--capture-music-volume','0']))
    shots.append(dict(name='regular-flame',seconds=5,args=['--game','data/disc','--slot','17','--headless','--frames','305','--capture-fullscreen','--interaction-record','8','--camera-distance','3.1','--camera-height','1.2','--camera-orbit','35','--capture-music-volume','0']))
    outdoor=(67.8,9.03,94.1)
    shot('glimmer-vista-ground',7,outdoor,cameras=[(0,(85,13,110),(72,10,96)),(7,(81,12.5,112),(72,10,96))],mode=1,fov=50)
    route=[outdoor,(78,9.03,92),(92,9.03,97),(87,9.03,116),(75,9.03,118)]
    follow=['--camera-distance','3.6','--camera-height','.9','--camera-orbit','-18']
    shot('regular-run-clear',8,outdoor,1.4,mode=0,route=[outdoor,(80,9.03,98),(88,9.03,112)],flags=follow,actions=[action(175,180,jump=1)],fov=52)
    shot('charge-jump-clear',5,outdoor,1.4,mode=0,route=route,flags=follow,actions=[action(5,260,charge=1),action(90,130,jump=1)],fov=55)
    for tier,label in [(1,'yellow-charge-wide'),(2,'red-charge-wide')]:
        shot(label,7,outdoor,1.4,mode=0,route=route,flags=follow+['--upgrade-tier',str(tier),'--supercharge','main-flap'],actions=[action(8,390,charge=1),action(140,145,jump=1)],fov=58)
    clear_route=[outdoor,(80,9.03,98),(90,9.03,110),(82,9.03,116),(71,9.03,112)]
    close_follow=['--camera-distance','2.6','--camera-height','.25','--camera-orbit','-12']
    for tier,label in [(1,'yellow-charge-path'),(2,'red-charge-path')]:
        shot(label,7,outdoor,1.27,mode=0,route=clear_route,flags=close_follow+['--upgrade-tier',str(tier),'--supercharge','main-flap'],actions=[action(8,390,charge=1)],fov=52)
    shot('charge-jump-path',5,outdoor,1.27,mode=0,route=clear_route,flags=close_follow,actions=[action(5,260,charge=1),action(58,100,jump=1)],fov=53)
    # Capture replay-only abilities at native 1080p with a clean presentation.
    for old,label in [('carry','carry-close'),('drop-smash','drop-close'),('underwater','underwater-clean'),('superflame-target','superflame-close'),('regular-flame','regular-flame-clean')]:
        original=next(s for s in shots if s['name']==old);args=list(original['args'])+['--capture-size','1920','1080','--hide-hud']
        if old in ('carry','drop-smash'):
            for flag,value in [('--camera-distance','2.6'),('--camera-height','.5'),('--camera-orbit','120')]:args[args.index(flag)+1]=value
        shots.append(dict(name=label,args=args,seconds=original['seconds']))
    for old,label in [('yellow-charge-path','yellow-charge-final'),('red-charge-path','red-charge-final'),('drop-close','drop-final')]:
        original=next(s for s in shots if s['name']==old)
        shots.append(dict(name=label,args=list(original['args']),seconds=original['seconds']))
    shot('portal-gold',5,(127.12,36.54,17.52),math.pi,slot=15,mode=1,fov=48,
        cameras=[(0,(128.5,38.2,18.5),(127.5,37.7,12.5)),(5,(127.8,38,17.5),(127.5,37.7,12.5))])
    shot('gem-gold',4,(71.4,9.08,98.15),0,mode=2,fov=43,
        cameras=[(0,(1.5,1.05,-2.0),(0,.4,.4)),(4,(1.4,1,-1.9),(0,.4,.4))],
        route=[(71.4,9.08,98.15),(71.4,9.08,100.5)])
    for old,label in [('yellow-charge-final','yellow-charge-wind'),('red-charge-final','red-charge-wind'),('regular-run-clear','regular-run-wind'),('charge-jump-path','charge-jump-wind')]:
        original=next(s for s in shots if s['name']==old)
        shots.append(dict(name=label,args=list(original['args']),seconds=original['seconds']))
    # The actual permanent Glimmer elevator, with enough approach time for
    # its individually spawned sprites to rise before Spyro enters the lift.
    shot('whirlwind-retail',7,(95.45,8.8,100.8),0,mode=1,fov=50,
        cameras=[(0,(93.0,11.0,99.5),(96.45,10.65,104.72)),(7,(93.6,11.3,99.6),(96.45,10.9,104.72))],
        actions=[action(150,260,steer=1,mx=.24,mz=.98)])
    shot('whirlwind-close',7,(94.5,8.82,104.5),math.pi/2,mode=1,fov=50,
        cameras=[(0,(90.7,10.8,100.2),(96.45,10.95,104.72)),(7,(90.9,11.0,100.0),(96.45,10.95,104.72))],
        actions=[action(95,220,steer=1,mx=1,mz=.11)])
    original=next(s for s in shots if s['name']=='underwater-clean');args=list(original['args'])
    for flag,value in [('--camera-distance','2.3'),('--camera-height','.2'),('--camera-orbit','-25')]:args[args.index(flag)+1]=value
    args[args.index('--frames')+1]='305';shots.append(dict(name='underwater-close',args=args,seconds=5))
    for name,source in [('crownworks-tease','17-arena'),('cyborg-ripto-tease','18-ripto')]:
        clone(name,f'output/backrealms-trailer/source/{source}.s2shot',15,['--custom-level','data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world','--backrealms','--night','--hide-hud'])
    shot('cyborg-front',4,(0,.06,-8),0,slot=15,mode=1,fov=44,
        cameras=[(0,(3.4,1.5,-4.8),(0,.85,0)),(4,(3.0,1.35,-4.5),(0,.85,0))],
        flags=['--custom-level','data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world','--backrealms','--night','--hide-hud'])
    shot('cyborg-optic',4,(0,.06,-8),0,slot=15,mode=1,fov=44,
        cameras=[(0,(-3.4,1.5,-4.8),(0,.85,0)),(4,(-3.0,1.35,-4.5),(0,.85,0))],
        flags=['--custom-level','data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world','--backrealms','--night','--hide-hud'])
    # Replacement takes use the engine's unmodified gameplay camera. No
    # capture orbit, distance or height overrides are attached to these.
    def pov(name,seconds,spawn,yaw=0,slot=17,route=(),actions=(),flags=()):
        shot(name,seconds,spawn,yaw,slot=slot,mode=0,route=route,actions=actions,flags=[*flags,'--require-visible-footing'],fov=58.7634)
    for i,(spawn,yaw) in enumerate([(outdoor,1.27),((74.0,9.03,99.0),.2),(p,math.pi/2)]):
        pov(f'flame-intro-{i+1}-pov-v2',2.5,spawn,yaw,actions=[action(30,flame=1)])
    pov('run-pov-v2',6,outdoor,1.27,route=clear_route)
    pov('jump-pov-v2',5,outdoor,1.27,route=clear_route,actions=[action(5,260,charge=1),action(58,100,jump=1)])
    for tier,label in [(1,'yellow'),(2,'red')]:
        pov(f'{label}-pov-v2',6,outdoor,1.27,route=clear_route,actions=[action(8,340,charge=1)],flags=['--upgrade-tier',str(tier),'--supercharge','main-flap'])
    pov('roll-pov-v2',5,outdoor,1.27,actions=[action(30,left=1),action(120,right=1),action(205,left=1)],flags=['--upgrade-tier','2'])
    pov('melee-pov-v2',5,p,math.pi/2,actions=[action(t,melee=1) for t in (30,46,73,155,171,198)],flags=['--upgrade-tier','2'])
    pov('homing-pov-v2',5,(56.9151,10.0659,37.0217),.756,actions=[action(t,flame=1) for t in (20,65,110,155,200,245)],flags=['--upgrade-tier','2','--superflame'])
    # Jump and boost over the intact Glimmer courtyard, looking towards its
    # cliff enclosure rather than flying above the edge of Summer Forest.
    for label,flags in [('glide',['--upgrade-tier','2']),('fly',['--upgrade-tier','2','--superfly'])]:
        pov(f'{label}-pov-v2',5,(74,9.03,97),0,actions=[action(t,jump=1) for t in (5,30,90,150)]+[action(5,245,steer=1,mx=.03,mz=.20)],flags=flags)
    crown=['--custom-level','data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world','--backrealms','--night','--hide-hud']
    for phase,label in [('foot','crownworks'),('mecha-gulp','mechs')]:
        pov(f'{label}-pov-v2',6,(0,.001,-8),0,slot=15,flags=crown+['--capture-boss-stage',phase],
            actions=[action(5,70,steer=1,mx=.7,mz=.1),action(75,150,steer=1,mx=-.7,mz=.1),action(155,245,steer=1,mx=.7,mz=.05),action(25,flame=1),action(110,flame=1),action(210,flame=1)])
    pov('flame-intro-1-pov-v3',2.5,(72,9.001,100),.6,actions=[action(30,flame=1)])
    pov('glide-pov-v3',5,(83,9.001,97),0,actions=[action(10,35,jump=1),action(38,jump=1),action(62,jump=1),action(105,jump=1),action(10,245,steer=1,mx=-.18,mz=0)],flags=['--upgrade-tier','2'])
    pov('fly-pov-v3',5,(83,9.001,97),0,actions=[action(10,35,jump=1),action(38,jump=1),action(60,115,jump=1),action(10,245,steer=1,mx=-.18,mz=0)],flags=['--upgrade-tier','2','--superfly'])
    for phase,label in [('foot','crownworks'),('mecha-gulp','mechs')]:
        pov(f'{label}-pov-v3',5,(0,.001,-10.5),0,slot=15,flags=crown+['--capture-boss-stage',phase],
            actions=[action(20,flame=1),action(20,120,steer=1,mx=.06,mz=.17),action(130,flame=1)])
    for power,label in [('--green-firebomb','door-bomb'),('--superflame','door-flame')]:
        shots.append(dict(name=f'{label}-pov-v2',seconds=5,args=['--game','data/disc','--slot','65','--headless','--frames','305','--capture-fullscreen','--capture-size','1920','1080','--hide-hud','--interaction-record','52',power,'--capture-music-volume','0']))
    # Autumn Plains' castle corridor separates two authored, supported rooms.
    # The new opening can be crossed: it is not a dent in an exterior cliff.
    for power,label in [('--green-firebomb','bomb'),('--superflame','flame')]:
        pov(f'hall-breach-{label}-pov-v2',6,(74.4,31.001,137.5),math.pi/2,slot=33,
            flags=['--terrain-destruction','--destruction-strength','3',power],
            actions=[action(40,flame=1),action(150,200,jump=1),action(145,265,steer=1,mx=.5,mz=0)])
    pov('hall-charge-pov-v2',5,(74.4,31.001,137.5),math.pi/2,slot=33,
        flags=['--terrain-destruction','--supercharge','main-flap'],actions=[action(35,100,charge=1),action(35,100,steer=1,mx=0,mz=0)])
    # Native collision/visible-mesh probing verifies this Autumn Plains
    # castle wall opens into a supported room and Spyro can cross the breach.
    castle=(87.6927,31.01,178.544)
    pov('castle-breach-pov-v3',6,castle,0,slot=33,
        flags=['--terrain-destruction','--green-firebomb'],
        actions=[action(40,flame=1),action(150,180,jump=1),action(150,270,steer=1,mx=0,mz=.5)])
    pov('castle-flame-pov-v3',5,castle,0,slot=33,
        flags=['--terrain-destruction','--destruction-strength','3','--superflame'],actions=[action(40,flame=1)])
    pov('castle-charge-pov-v3',5,castle,0,slot=33,
        flags=['--terrain-destruction','--supercharge','main-flap'],actions=[action(35,95,charge=1)])
    for old,name in [('castle-breach-pov-v3','castle-breach-pov-v4'),('castle-flame-pov-v3','castle-flame-pov-v4'),('castle-charge-pov-v3','castle-charge-pov-v4')]:
        original=next(s for s in shots if s['name']==old)
        shots.append(dict(name=name,args=list(original['args']),seconds=original['seconds']))
    # Metropolis factory wall: the probe checks visible support at spawn and
    # throughout traversal, rejecting collision-only ledges over open sky.
    tokens=(OUT/'source/visible-breach-63/building-bomb.s2shot').read_text().split()
    factory=tuple(map(float,tokens[5:8]));factory_yaw=float(tokens[8])
    dx,dz=math.sin(factory_yaw)*.5,math.cos(factory_yaw)*.5
    for power,label in [('--green-firebomb','breach'),('--superflame','flame')]:
        pov(f'factory-{label}-pov-v4',6,factory,factory_yaw,slot=63,
            flags=['--terrain-destruction',power],actions=[action(40,flame=1),action(150,180,jump=1),action(150,270,steer=1,mx=dx,mz=dz)])
    pov('factory-charge-pov-v4',5,factory,factory_yaw,slot=63,flags=['--terrain-destruction','--supercharge','main-flap'],actions=[action(35,95,charge=1)])
    for old,name in [('crownworks-pov-v3','crownworks-pov-v4'),('mechs-pov-v3','mechs-pov-v4'),('glide-pov-v3','glide-pov-v4'),('fly-pov-v3','fly-pov-v4'),('roll-pov-v2','roll-pov-v4')]:
        original=next(s for s in shots if s['name']==old)
        shots.append(dict(name=name,args=list(original['args']),seconds=original['seconds']))
    for slot,label in [(33,'annex'),(57,'winter'),(61,'temple')]:
        tokens=(OUT/f'source/visible-breach-{slot}/building-bomb.s2shot').read_text().split()
        spawn=tuple(map(float,tokens[5:8]));yaw=float(tokens[8])
        pov(f'{label}-breach-pov-v4',6,spawn,yaw,slot=slot,flags=['--terrain-destruction','--green-firebomb'],
            actions=[action(40,flame=1),action(150,180,jump=1),action(150,270,steer=1,mx=math.sin(yaw)*.5,mz=math.cos(yaw)*.5)])
        if slot==61:
            pov('temple-flame-pov-v4',5,spawn,yaw,slot=slot,flags=['--terrain-destruction','--superflame'],actions=[action(40,flame=1)])
            pov('temple-charge-pov-v4',5,spawn,yaw,slot=slot,flags=['--terrain-destruction','--supercharge','main-flap'],actions=[action(35,95,charge=1)])
    for x,label in [(130.5,'left'),(132.5,'near'),(137,'right')]:
        pov(f'temple-{label}-breach-pov-v5',6,(x,21.01,76.8),math.pi,slot=61,flags=['--terrain-destruction','--green-firebomb'],
            actions=[action(40,flame=1),action(150,180,jump=1),action(150,270,steer=1,mx=0,mz=-.5)])
    pov('crownworks-pov-v5',7,(0,.001,-10.5),0,slot=15,flags=crown+['--capture-boss-stage','foot'],
        actions=[action(20,flame=1),action(20,120,steer=1,mx=.06,mz=.17),action(130,flame=1),action(220,310,steer=1,mx=.25,mz=.03),action(290,flame=1)])
    pov('farm-breach-pov-v6',6,(36.9316,9.73438,67.2197),-math.pi/2,slot=65,
        flags=['--terrain-destruction','--green-firebomb'],actions=[action(60,flame=1),action(195,290,steer=1,mx=-.5,mz=0)])
    # Moving normal-flame cuts, each with a real target in a different level.
    # The actors and baskets are unmodified retail records.
    for name,slot,spawn,yaw,mx,mz in [
        ('intro-glimmer-lizard-v6',17,(58.4502,10.0,35.6504),0,0,1),
        ('intro-idol-basket-v6',19,(112.8096,12.5,84.6201),0,0,1),
        ('intro-hurricos-grinder-v6',23,(33.1797,13.959,38.7197),math.pi/2,1,0)]:
        pov(name,2.5,spawn,yaw,slot=slot,
            actions=[action(5,80,steer=1,mx=mx,mz=mz),action(30,flame=1)])
    # This lawn has room for both full rolls; the old spawn intersected a pole.
    pov('roll-clear-pov-v6',5,(78,9.001,104),0,
        actions=[action(30,left=1),action(120,right=1),action(205,left=1)],flags=['--upgrade-tier','2'])
    # Show aimed powered attacks against the two live bosses, rather than
    # scripted ordinary flame taps into empty space in the foot phase.
    pov('crownworks-cyborg-pov-v6',3,(0,.001,-10.5),0,slot=15,
        flags=crown+['--capture-boss-stage','foot'],actions=[action(5,100,steer=1,mx=.1,mz=.12)])
    pov('crownworks-superflame-gulp-v6',7,(0,.001,-6),0,slot=15,
        flags=crown+['--capture-boss-stage','mecha-gulp','--superflame','--upgrade-tier','2'],
        actions=[action(5,150,steer=1,mx=0,mz=.25)]+[action(t,flame=1) for t in (110,155,200,250,310)])
    pov('crownworks-greenbomb-crush-v6',6,(5,.001,-3),0,slot=15,
        flags=crown+['--capture-boss-stage','mecha-gulp','--green-firebomb'],
        actions=[action(5,120,steer=1,mx=0,mz=.15)]+[action(t,flame=1) for t in (35,95,155,220,285)])
    pov('roll-clear-pov-v7',5,(72,9.001,100),0,
        actions=[action(30,left=1),action(120,right=1),action(205,left=1)],flags=['--upgrade-tier','2'])
    pov('intro-hurricos-grinder-v7',2.5,(39.1797,14.001,38.7197),-math.pi/2,slot=23,
        actions=[action(5,65,steer=1,mx=-1,mz=0),action(30,flame=1)])
    pov('intro-colossus-basket-v8',2.5,(30.280273,9.28125,63.389648),0,slot=21,
        actions=[action(5,55,steer=1,mx=0,mz=1),action(30,flame=1)])
    # Fire during the approach, leaving time to see the flame fan travel
    # before the nearby target clips its native trajectory at impact.
    for name,slot,spawn,end in [
        ('intro-glimmer-lizard-v9',17,(58.4502,10.0,35.6504),80),
        ('intro-idol-basket-v9',19,(112.8096,12.5,84.6201),80),
        ('intro-colossus-basket-v9',21,(30.280273,9.28125,63.389648),55)]:
        pov(name,2.5,spawn,0,slot=slot,actions=[action(5,end,steer=1,mx=0,mz=1),action(15,flame=1)])
    (OUT/'source/shots.json').write_text(json.dumps(shots,indent=2))
    print('Authored',len(shots),'native shots',flush=True)

class RetailArt:
    def __init__(self):
        self.exe=(ROOT/'data/disc/SCUS_944.25').read_bytes()
        with (ROOT/'data/disc/WAD.WAD').open('rb') as f:
            entries=wad_index(f.read(0x10000),f.seek(0,2));e=entries[17];f.seek(e.offset);archive=f.read(e.size)
        child=wad_index(archive,len(archive))[0];self.vram=archive[child.offset:child.offset+0x80000]
        types=[0]*10+[2]+[1]*11+[5]*4+[6,7,4,2,2,9]+[10]*4+[11]*4+[12]*2+[16,17,17,13,13,14,14,15,15]+[20]*4
        self.resident=[]
        for i,t in enumerate(types):
            u,v,b,c=self.exe[self.off(0x8006351c)+i*4:self.off(0x8006351c)+i*4+4]
            w=self.exe[self.off(0x800634b8)+t];h=self.exe[self.off(0x800634d0)+t]
            self.resident.append(self.texture(0x38,u,v,0x4020+(b&3)+c*64,w,h,half=bool(b&128)))
        self.book=Image.new('RGBA',(512,240))
        for address,count,ox in [(0x80063bf8,32,0),(0x80063c78,32,256),(0x80063cf8,40,0),(0x80063d98,40,256)]:
            for j in range(count):
                x,y,s,t=self.exe[self.off(address)+j*4:self.off(address)+j*4+4]
                self.book.alpha_composite(self.resident[s],(x+ox,y))
        self.book.save(OUT/'source/retail-open-book.png')
    @staticmethod
    def off(a):return a-0x8000f800
    def word(self,x,y):return struct.unpack_from('<H',self.vram,(y*512+x-512)*2)[0]
    def texture(self,page,u,v,clut,w,h,tint=0x808080,half=False):
        im=Image.new('RGBA',(w,h));depth=(page>>7)&3;per=2 if depth else 4;bits=8 if depth else 4
        for y in range(h):
            for x in range(w):
                xx=(u+x)&255;yy=(v+y)&255;p=self.word((page&15)*64+xx//per,((page>>4)&1)*256+yy)
                c=self.word((clut&63)*16+(p>>((xx%per)*bits)&((1<<bits)-1)),clut>>6)
                rgb=[min(255,((c>>(k*5)&31)*255//31)*(tint>>(k*8)&255)//128) for k in range(3)]
                im.putpixel((x,y),(*rgb,128 if half and c&0x8000 else 255 if c else 0))
        return im
    @functools.lru_cache(maxsize=1024)
    def glyph(self,c,color):
        d=struct.unpack_from('<H',self.exe,self.off(0x800636a4)+ord(c)*2)[0];index=d&255
        if index==255:
            # Retail lacks these two ASCII entries. The requested closing
            # wink extends its pixels/colours from existing punctuation/C.
            if c==';':
                colon=self.glyph(':',color)[0];comma=self.glyph(',',color)[0]
                im=Image.new('RGBA',(12,9));im.alpha_composite(colon.crop((0,0,12,5)),(0,0));im.alpha_composite(comma.crop((0,5,12,9)),(0,5))
                return im,8,0
            if c==')':
                curve=self.glyph('C',color)[0].transpose(Image.Transpose.FLIP_LEFT_RIGHT).crop((5,0,12,9))
                im=Image.new('RGBA',(12,9));im.alpha_composite(curve,(0,0));return im,8,0
            return None,10,0
        tint=struct.unpack_from('<I',self.exe,self.off(0x800637d4)+color*4)[0]
        im=self.texture(0x618,(index&31)*12,index//32*9,0x4922,12,9,tint)
        dy=(d>>12)&15;dy=dy-16 if dy>=8 else dy
        return im,(d>>8)&15,dy
    def text(self,text,color=1,scale=5,aspect=True):
        ps=[self.glyph(c,color) for c in text];im=Image.new('RGBA',(sum(a for g,a,d in ps)+12,24));x=0
        for g,a,d in ps:
            if g is not None:im.alpha_composite(g,(x,d+8))
            x+=a
        bbox=im.getbbox();im=im.crop(bbox) if bbox else im
        return im.resize((round(im.width*scale*(.625 if aspect else 1)),round(im.height*scale)),Image.Resampling.NEAREST)

if __name__=='__main__':
    author_shots();art=RetailArt();print('Decoded original guidebook and retail glyphs')

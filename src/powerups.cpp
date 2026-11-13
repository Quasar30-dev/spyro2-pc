#include "spyro/powerups.hpp"
#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include <bit>

namespace spyro {
namespace {
constexpr float unit=1/retail::units_per_world;
bool speedway(unsigned slot){return slot==29 || slot==43 || slot==53 || slot==63;}
Vec3 forward(float yaw,float pitch=0){return retail::rotate_model({0,0,1},yaw,pitch,0);}
ChargeSmokeAssets texture(std::span<const std::uint8_t> archive,unsigned left,unsigned right) {
    ChargeSmokeAssets result;const auto u=left&255,v=left>>8&255,page=right>>16,clut=left>>16;
    if((right&255)<u || (right>>8&255)<v || (page>>7&3)>1)throw FormatError("Unsupported power-up texture");
    result.width=(right&255)-u+1;result.height=(right>>8&255)-v+1;result.blend_mode=page>>5&3;
    const auto at=read_u32(archive,0);if(read_u32(archive,4)<0x80000)throw FormatError("Missing power-up VRAM");
    const auto word=[&](unsigned x,unsigned y){if(x<512 || x>=1024 || y>=512)throw FormatError("Power-up texture outside VRAM");return read_u16(archive,at+(y*512+x-512)*2);};
    const unsigned depth=page>>7&3,per_word=depth?2:4,bits=depth?8:4;
    for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x) {
        const auto packed=word((page&15)*64+(u+x)/per_word,(page>>4&1)*256+v+y);
        const auto color=word((clut&63)*16+(packed>>((u+x)%per_word*bits)&(depth?255:15)),clut>>6&511);
        for(unsigned c=0;c<3;++c){const auto n=color>>(5*c)&31;result.texture_rgba.push_back(static_cast<std::uint8_t>((n<<3)|(n>>2)));}
        result.texture_rgba.push_back(color?255:0);
    }
    return result;
}
}
PowerupAssets decode_powerup_assets(std::span<const std::uint8_t> archive) {
    PowerupAssets result;
    const auto gameplay=read_u32(archive,24);
    if(read_u32(archive,gameplay+0x14)==8 && !read_u32(archive,gameplay+0x18))return result;
    result.wing_trail=texture(archive,read_u32(archive,gameplay+0x24),read_u32(archive,gameplay+0x28));
    const auto data=archive.subspan(read_u32(archive,8),read_u32(archive,12));
    const auto skip=[&](unsigned at){const auto n=read_u32(data,at);if(n<4 || n%4 || at>data.size() || n>data.size()-at)throw FormatError("Invalid effect section");return at+n;};
    unsigned at=0;for(unsigned i=0;i<6;++i)at=skip(at);at=skip(at+12);at=skip(at);if(!compact_demo_layout(archive))at=skip(at);
    const auto end=skip(at),count=read_u32(data,at+4);at+=8;
    if(count>256)throw FormatError("Invalid effect material count");
    for(unsigned i=0;i<count;++i) {
        const auto type=read_u16(data,at),bytes=read_u16(data,at+2);
        if(bytes<8 || bytes%8 || at+4>end || bytes>end-at-4)throw FormatError("Invalid effect material record");
        if(type==0x3a)result.green_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        if(type==0x0d)result.fire_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        if(type==0x27)result.bubble_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        if(type==6)result.whirlwind_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        if(type==0x0c)result.sparkle_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        if(type==4)result.gate_particle=texture(archive,read_u32(data,at+4),read_u32(data,at+8));
        at+=bytes+4;
    }
    if(result.green_particle.texture_rgba.empty())result.green_particle=decode_charge_smoke(archive);
    if(result.fire_particle.texture_rgba.empty())result.fire_particle=decode_charge_smoke(archive);
    if(result.sparkle_particle.texture_rgba.empty())result.sparkle_particle=result.green_particle;
    if(result.gate_particle.texture_rgba.empty())result.gate_particle=result.sparkle_particle;
    return result;
}
unsigned attack_strength(std::uint32_t flags) {
    if(flags&0x400000)return 3;
    if(flags&0x940000)return 2;
    return flags?1:0;
}
bool power_target_accepts(const RetailObject& object,std::uint32_t flags) {
    if(!object.power_target || object.collected || object.state<0)return false;
    // Unported enemy scripts accept powered attacks through this common
    // combat path. Ordinary attacks retain each implemented enemy's rules.
    const unsigned minimum=object.power_target->scenery?object.power_target->minimum_strength:2;
    return attack_strength(flags)>=minimum;
}
void Powerups::reset(bool retain_permanent) {
    const bool keep=retain_permanent && permanent_fire;*this=Powerups{};permanent_fire=keep;
}
bool Powerups::fly_enabled(const Game& g)const{return g.has_animation(33) && g.has_animation(34) && g.has_animation(35) && (g.cheats.superfly || flight.remaining || speedway(g.level.wad_slot) || (g.level.universe && g.level.universe->address.space));}
bool Powerups::flame_enabled(const Game& g)const{return g.has_object_model(0x1e3) && (g.cheats.superflame || permanent_fire || fire.remaining);}
bool Powerups::freeze_enabled(const Game& g)const{return ice.remaining && g.has_object_model(0x270);}
bool Powerups::charge_enabled(const Game& g)const{return g.has_animation(61) && (g.cheats.supercharge || charge || g.ripto.supercharge());}
void Powerups::end_flight(){flying=false;takeoff_=false;lift_=roll_velocity_=0;lift_phase_=0;flight_clip_=33;}
bool PowerCheckpoint::valid()const {
    return flight.remaining<=flight.duration && fire.remaining<=fire.duration && flight.duration<=3600000 && fire.duration<=3600000 &&
        clip>=33 && clip<=35 && lift_phase<=2 && std::isfinite(speed) && speed>=2048 && speed<=19200 &&
        std::isfinite(roll_velocity) && std::abs(roll_velocity)<=1000000 && std::isfinite(lift) && lift>=0 && lift<=6144 &&
        invincibility.remaining<=invincibility.duration && invincibility.duration<=3600000 && ice.remaining<=ice.duration && ice.duration<=3600000;
}
PowerCheckpoint Powerups::checkpoint()const {return {flight,fire,charge,flying,takeoff_,flight_clip_,lift_phase_,flight_speed_,roll_velocity_,lift_,invincibility,ice};}
void Powerups::restore_checkpoint(const PowerCheckpoint& saved) {
    if(!saved.valid())throw FormatError("Invalid saved flight controller");
    flight=saved.flight;fire=saved.fire;charge=saved.charge;flying=saved.flying;takeoff_=saved.takeoff;
    flight_clip_=saved.clip;lift_phase_=saved.lift_phase;flight_speed_=saved.speed;roll_velocity_=saved.roll_velocity;lift_=saved.lift;
    invincibility=saved.invincibility;ice=saved.ice;
}
void Powerups::begin_tick(Game& g) {
    if(g.dialogue.active())return;
    if(speedway(g.level.wad_slot) && g.clock.ticks()<2)takeoff_=true;
    if(g.level.universe && g.level.universe->address.space && !flying)takeoff_=true;
    if(flight.remaining)--flight.remaining;
    if(invincibility.remaining)--invincibility.remaining;
    if(ice.remaining)--ice.remaining;
    const unsigned power_time=ice.remaining?ice.remaining:invincibility.remaining?invincibility.remaining:flight.remaining?flight.remaining:fire.remaining;
    const unsigned seconds=power_time/30;
    if(power_time && power_time%30==0 && (seconds<8 || (seconds<16 && (seconds&1))))g.sound_events.push_back({SoundScope::Global,0x1d,0,{}});
    // A shot that has begun can finish on the last power-up VBlank.
    if(fire.remaining && (fire.remaining>1 || winding_==PowerShot::None))--fire.remaining;
    if(flying && (!fly_enabled(g) || g.player.swim.mode!=WaterMode::Dry || g.player.hurt_ticks))end_flight();
    if(takeoff_ && fly_enabled(g) && g.player.swim.mode==WaterMode::Dry) {
        flying=true;takeoff_=false;flight_speed_=5952;g.player.velocity.y=0;
        g.player.grounded=false;g.player.airborne_ticks=4;g.player.position.y+=0.12f;
    }
}
void Powerups::gates_tick(Game& g) {
    const auto origin=g.player.position+retail::origin_above_feet;
    const auto* models=g.object_models();
    if(models)spirits_tick(g,*models);
    // 80075600 opens the gate on the spirits that have reached it (80066efc).
    const unsigned released=g.spirits(),airborne=spirits_in_flight(),arrived=released>airborne?released-airborne:0;
    for(auto& object:g.retail_objects)if(object.powerup_gate && !object.collected) {
        auto& gate=*object.powerup_gate;
        gate.unlocked=arrived>=gate.requirement;
        if(gate.kind==10)gate.unlocked=gate.unlocked && g.player.gems>=10000 && g.player.orbs>=64;
        object.state=gate.unlocked?1:0;
        if(gate.cooldown){--gate.cooldown;continue;}
        if(!gate.unlocked || g.dialogue.active())continue;
        const auto center=object.position-Vec3{0,gate.origin_offset,0};
        const auto delta=origin-center,previous=g.player.previous_position+retail::origin_above_feet-center;
        const auto direction=forward(retail::native_heading(object.rotation[2]*16));
        const float side=dot(delta,direction),before=dot(previous,direction);
        const float x=std::abs(delta.x),z=std::abs(delta.z);
        // 80075f6c / 8007652c: cross the gate plane inside 0x380 units;
        // vertical reach is measured about record +0x38's origin correction.
        if((side<0)==(before<0) || std::max(x,z)+std::min(x,z)*0.375f>=896*unit || std::abs(delta.y)>=1664*unit)continue;
        if(gate.kind==0) {invincibility={gate.duration,gate.duration};gate.cooldown=8;}
        else if(gate.kind==1) {charge=true;gate.cooldown=60;}
        else if(gate.kind==2) {
            flight={gate.duration,gate.duration};takeoff_=!g.player.grounded;
            // Metropolis shares its flight duration with superflame.
            if(g.level.wad_slot==67)fire=flight;
            gate.cooldown=8;
        } else if(gate.kind==3) {fire={gate.duration,gate.duration};gate.cooldown=8;}
        else if(gate.kind==5) {
            // Overlay 38 8006f8d0: state 1B launches at +58, or 0x1900
            // Q6 when that parameter is zero. This field is not a timer.
            end_flight();g.player.velocity.y=(gate.duration?gate.duration:0x1900)*MovementTuning::velocity_unit;
            g.player.grounded=false;g.player.airborne_ticks=4;g.player.air={};
            g.player.jump={};g.player.jump.kind=JumpState::Kind::Normal;g.player.jump.ticks=13;
            g.player.jump.released=true;gate.cooldown=30;
        }
        else if(gate.kind==7) {ice={gate.duration,gate.duration};gate.cooldown=8;}
        else if(gate.kind==10) {permanent_fire=true;gate.cooldown=8;}
        else continue;
        // 80075dcc: eight updates of lightning, then the icon's sound 0 (the
        // gate's own sound 0 is its ambient loop).
        gate.zap=8;for(auto& word:gate.bolt_bends)word=static_cast<std::uint16_t>(random_.next());
        ++gates_activated;g.sound_events.push_back({SoundScope::Object,0,object.held_gem,object.position});
    }
    if(!models)return;
    holds_.clear();bolts_.clear();
    const bool update=(g.clock.ticks()&1)==0;
    for(auto& object:g.retail_objects)if(object.powerup_gate && !object.collected && object.powerup_gate->unlocked)open_gate_tick(g,*models,object,update);
    // Particle 47 (8007fd1c): each update the streak moves by its own random-
    // walking step and both ends fade; it lasts 32 updates.
    for(auto& c:crackles_) {
        c.previous_head=c.head;c.previous_tail=c.tail;++c.age;
        if(update){const auto walk=[&]{return static_cast<int>(random_.next()&2)-1;};c.step=c.step+retail::IVec3{walk(),walk(),walk()};}
        const auto half=retail::world_position({c.step.x*2.0f,c.step.y*2.0f,c.step.z*2.0f});c.head+=half;c.tail+=half;
    }
    std::erase_if(crackles_,[](const auto& c){return c.age>=64;});
    // The gate's counter: on its first update a locked gate creates one
    // class-104 digit (+49 = 3) for a requirement under ten, otherwise tens (1)
    // and units (2), showing the requirement. 104 then sits 0x520 above the
    // gate facing the camera eye, the pair 1/28 unit either side, with a
    // 64-frame yaw sway (overlay 16 class-104 case); it is removed once the
    // gate leaves state 0.
    const auto digits=models->models.find(0x104);
    if(digits==models->models.end() || digits->second.animated || digits->second.meshes.size()<10)return;
    const auto eye=retail::from_world(g.camera.sample(1).eye);
    const int sway=models->sine[(g.clock.ticks()&63)*4]>>7;
    for(const auto& object:g.retail_objects)if(object.powerup_gate && !object.collected && !object.powerup_gate->unlocked &&
        arrived<object.powerup_gate->requirement) {
        const auto requirement=object.powerup_gate->requirement;const auto base=retail::from_world(object.position);
        const std::array<std::pair<unsigned,unsigned>,2> places{std::pair{requirement<10?3u:1u,requirement<10?requirement:requirement/10%10},
            std::pair{2u,requirement%10}};
        for(unsigned n=0;n<(requirement<10?1u:2u);++n) {
            const auto [place,value]=places[n];
            auto toward=models->math.angle(eye.x-base.x,eye.y-base.y);
            const float yaw=static_cast<float>((sway+static_cast<int>(toward)+128)&255);
            if(place==2)toward=(toward+128)&255;
            const retail::IVec3 offset=place==3?retail::IVec3{0,0,0x520}:retail::IVec3{models->sine[toward]/0x1c,-models->cosine[toward]/0x1c,0x520};
            PowerVisual v;v.kind=PowerVisualKind::GateDigit;v.lifetime=2;auto& o=v.object;
            o.type=0x104;o.model=static_cast<std::uint8_t>(value);o.material_color=2;o.draw_flags=8;o.runtime_spawned=true;o.held_gem=255;
            o.position=o.previous_position=retail::to_world(base+offset);o.rotation=o.previous_rotation={0,0,yaw};
            v.scale=v.previous_scale=1;v.previous_material=o.material_color;visuals_.push_back(std::move(v));
        }
    }
}
namespace {
// 8001be9c: the absolute difference of two byte angles.
int angle_gap(unsigned a,unsigned b){const int d=static_cast<int>((a-b)&255);return d>128?256-d:d;}
retail::IVec3 scaled(retail::IVec3 v,int weight){return {v.x*weight,v.y*weight,v.z*weight};}
retail::IVec3 shifted(retail::IVec3 v,int bits){return {v.x>>bits,v.y>>bits,v.z>>bits};}
// 80066e80 / 800647ac / 800647bc / 800647d4: 8003ad84's first steps on a new
// route segment blend the step toward the waypoint with the previous one.
constexpr std::array<std::pair<int,int>,2> ease_short{{{341,683},{683,341}}};
constexpr std::array<std::pair<int,int>,4> ease_five{{{205,819},{409,615},{615,409},{819,205}}};
constexpr std::array<std::pair<int,int>,6> ease_seven{{{146,878},{293,731},{439,585},{585,439},{731,293},{878,146}}};
constexpr std::array<std::pair<int,int>,8> ease_nine{{{114,910},{228,796},{342,682},{455,569},{569,455},{682,342},{796,228},{910,114}}};
// 800644f0: the first four offsets of a particle-0C burst (colour 800644d0[3]).
constexpr std::array<retail::IVec3,4> sparkle_offsets{{{0,0,0},{80,0,80},{-40,69,80},{-40,-69,80}}};
}
void Powerups::open_gate_tick(Game& g,const ObjectModels& models,RetailObject& object,bool update) {
    // Overlay 16, gate state 1 (80075a40..80075d70).
    auto& gate=*object.powerup_gate;
    const auto basis=object_basis(object);
    const auto local=[&](int x,int y,int z){return object.position+basis*retail::model_position({float(x),float(y),float(z)});};
    const auto origin=g.player.position+retail::origin_above_feet;
    const auto delta=retail::from_world(origin)-retail::from_world(object.position);
    // 8001bf6c: max(|dx|,|dy|) + 3/8 min (horizontal).
    const int ax=std::abs(delta.x),ay=std::abs(delta.y),distance=std::max(ax,ay)+std::min(ax,ay)*3/8;
    // 80050b74 / 80050648(gate, 0, 4): the gate's sound 0 restarts whenever it ends.
    holds_.push_back({object.id,{SoundScope::Object,0,object.type,object.position}});
    if(gate.zap) {
        if(update){--gate.zap;if(distance>0x600)gate.zap=0;}
        const auto at=retail::from_world(origin)-retail::IVec3{0,0,0x40};
        if(update)for(unsigned n=0;n<4 && crackles_.size()<512;++n) {
            // 80080fc4: a streak ±128 about Spyro, its two ends one step apart.
            const auto r=[&]{return (static_cast<int>(random_.next()&0xfff)-2048)<<4>>12;};
            const retail::IVec3 step{r(),r(),r()};
            // Positions are quarter units: the head lies step*16 away, the tail step*4 on.
            const auto head=retail::to_world(at+retail::IVec3{step.x*16,step.y*16,step.z*16}),tail=head+retail::world_position({step.x*4.0f,step.y*4.0f,step.z*4.0f});
            crackles_.push_back({head,head,tail,tail,step,0});
        }
        // Two four-segment bolts from the pillar tips (0, ±0x400, 0x6cc) to
        // Spyro. 8004b194 re-rolls each interior bend every other frame,
        // averaging the old bend at half strength with a new one.
        if(gate.zap)for(unsigned side=0;side<2;++side) {
            GateBolt bolt;bolt.from=local(0,side?-0x400:0x400,0x6cc);bolt.to=retail::to_world(at);
            const bool reroll=g.clock.ticks()&1;
            for(unsigned k=0;k<3;++k) {
                auto& word=gate.bolt_bends[side*3+k];
                const auto field=[](unsigned w,unsigned low,unsigned bits){const int v=static_cast<int>((w>>low)&((1u<<bits)-1));return v>=(1<<(bits-1))?v-(1<<bits):v;};
                retail::IVec3 bend{field(word,7,8),field(word,2,8),field(word,0,5)*8};
                if(reroll) {
                    const unsigned fresh=random_.next();
                    bend={bend.x/2+field(fresh,8,7),bend.y/2+field(fresh,3,7),bend.z/2+field(fresh,0,5)*4};
                    word=static_cast<std::uint16_t>(fresh);
                }
                bolt.bends[k]=bend;
            }
            bolts_.push_back(bolt);
        }
        if(!gate.zap)++gate_zaps;
    }
    // While drawn and within 0xA000, one particle 04 every fourth update
    // (every second within 0x5000), on the line between the pillars.
    const auto view=g.camera.sample(1);
    const auto frames=g.clock.ticks()/2;
    if(update && distance<0xa000 && dot(object.position-view.eye,view.forward)>0 && ((frames&3)==0 || ((frames&1)==0 && distance<0x5000)) && particles_.size()<2048) {
        const auto at=local(0,0x352-static_cast<int>(random_.next()%0x6a4),0);
        PowerParticle p{at,at,{0,((random_.next()&7)+3)*4*unit*30,0},{1,1,1},8*4*unit,0,600,PowerParticleKind::GateGlow,
            (random_.next()&255)*16*retail::angle_to_radians};
        p.ceiling=object.position.y+0x640*unit;p.grow=(random_.next()&3)+4;p.spin=((random_.next()&3)+2)*8*retail::angle_to_radians;
        particles_.push_back(p);
    }
}
unsigned Powerups::spirits_in_flight()const {
    unsigned total=0;for(const auto& s:spirits_)total+=s.value;return total;
}
void Powerups::spirits_tick(Game& g,const ObjectModels& models) {
    // Spirits already counted when the level (or a respawn) began never fly.
    const auto authored=std::min(g.level.objects.size(),g.retail_objects.size());
    if(spirit_seen_.size()!=authored) {
        spirit_seen_.assign(authored,0);spirits_.clear();
        for(std::size_t i=0;i<authored;++i)spirit_seen_[i]=spirit_released(g.retail_objects[i]);
    }
    for(std::size_t i=0;i<authored;++i)if(!spirit_seen_[i] && spirit_released(g.retail_objects[i])) {
        spirit_seen_[i]=1;launch_spirit(g,models,g.retail_objects[i]);
    }
    // Class 68 runs with the 30 Hz object scripts (elapsed 2) and is drawn
    // between its last two positions.
    const bool update=(g.clock.ticks()&1)==0;
    for(auto it=spirits_.begin();it!=spirits_.end();) {
        if(it->gate>=g.retail_objects.size() || !g.retail_objects[it->gate].powerup_gate){it=spirits_.erase(it);continue;}
        if(!update){it->body.transform_phase=1;++it;continue;}
        if(advance_spirit(g,models,*it))++it;else it=spirits_.erase(it);
    }
    const auto bank=models.models.find(0x68);if(bank==models.models.end())return;
    for(const auto& s:spirits_) {
        PowerVisual v;v.kind=PowerVisualKind::Spirit;v.lifetime=2;v.object=s.body;
        v.scale=v.previous_scale=bank->second.animated?object_model_scale(v.object):1;v.previous_material=v.object.material_color;
        visuals_.push_back(std::move(v));
    }
}
void Powerups::launch_spirit(Game& g,const ObjectModels& models,const RetailObject& source) {
    // 80037608: the first gate within 0x2800 in clear sight takes the spirit
    // directly. Otherwise the closest waypoint of any gate route wins (moving
    // on when the next waypoint lies the other way), or a route-less gate.
    const auto from=retail::from_world(source.position);
    int best=INT_MAX,route=-1,waypoint=-1;unsigned chosen=UINT_MAX;
    for(unsigned i=0;i<g.retail_objects.size();++i) {
        const auto& gate=g.retail_objects[i];if(!gate.powerup_gate || gate.collected)continue;
        const auto& routes=gate.powerup_gate->spirit_routes;
        const int distance=models.math.length(retail::from_world(gate.position)-from);
        const auto sight=gate.position-source.position;const float reach=length(sight);
        if(distance<0x2800 && (reach<1e-4f || !g.level.raycast(source.position,sight/reach,reach))){chosen=i;route=waypoint=-1;break;}
        if(routes.empty()){if(distance<best){best=distance;chosen=i;route=waypoint=-1;}continue;}
        for(int k=0;k<static_cast<int>(routes.size());++k) {
            const auto& points=routes[k].points;int nearest=0,closest=0xffffff;
            for(int n=0;n<static_cast<int>(points.size());++n) {
                const int d=models.math.length(retail::from_world(points[n])-from);
                if(d<closest){closest=d;nearest=n;}
            }
            if(closest<0x4000 && nearest+1<static_cast<int>(points.size())) {
                const auto here=retail::from_world(points[nearest])-from,next=retail::from_world(points[nearest+1])-from;
                if(angle_gap(models.math.angle(here.x,here.y),models.math.angle(next.x,next.y))>0x3f)++nearest;
            }
            if(closest<best){best=closest;chosen=i;route=k;waypoint=nearest;}
        }
    }
    if(chosen==UINT_MAX)return;
    SpiritFlight s;s.gate=chosen;s.route=route;s.waypoint=waypoint;s.value=source.record[83]&127;
    s.origin=from;s.angle=random_.next()&255;s.radius=0x8c;
    s.at={from.x+(models.cosine[s.angle]*s.radius>>12),from.y+(models.sine[s.angle]*s.radius>>12),from.z};
    auto& body=s.body;body.type=0x68;body.runtime_spawned=true;body.held_gem=255;
    body.position=body.previous_position=retail::to_world(s.at);
    if(const auto bank=models.models.find(0x68);bank!=models.models.end() && bank->second.animated)
        body.animation=body.previous_animation=start_object_animation(bank->second,0);
    s.trail.push_front(body.position);
    // State 0 plays the class's sound 0 (80050648) and starts the spiral.
    g.sound_events.push_back({SoundScope::Object,0,0x68,body.position});
    spirits_.push_back(std::move(s));++spirits_launched;
}
bool Powerups::advance_spirit(Game& g,const ObjectModels& models,SpiritFlight& s) {
    constexpr int elapsed=2;
    auto& body=s.body;begin_object_transform(body,2);
    if(const auto bank=models.models.find(0x68);bank!=models.models.end() && bank->second.animated) {
        body.previous_animation=body.animation;tick_object_animation(bank->second,body.animation);
    }
    const auto& routes=g.retail_objects[s.gate].powerup_gate->spirit_routes;
    const auto gate=retail::from_world(g.retail_objects[s.gate].position);
    if(s.route>=static_cast<int>(routes.size()) || (s.route>=0 && s.waypoint>=static_cast<int>(routes[s.route].points.size())))s.route=-1;
    if(s.state==1) {
        // Rise 20, turn 10 and widen 8 per elapsed VBlank about the defeat point.
        s.at.z+=20*elapsed;s.angle=(s.angle+10*elapsed)&255;s.radius+=8*elapsed;
        s.at.x=s.origin.x+(models.cosine[s.angle]*s.radius>>12);s.at.y=s.origin.y+(models.sine[s.angle]*s.radius>>12);
        s.counter+=elapsed;
        if(s.counter>0x34) {
            // Leave the spiral once its tangent faces the target (or after 0x58).
            const auto target=s.route<0?gate:retail::from_world(routes[s.route].points[s.waypoint]);
            const auto toward=models.math.angle(target.x-s.at.x,target.y-s.at.y);
            if(angle_gap(s.angle,(toward+192)&255)<elapsed*8 || s.counter>0x58){s.state=s.route<0?3:2;s.retarget=s.route<0?1:2;}
        }
    } else if(s.state==2) {
        const auto& route=routes[s.route];const auto target=retail::from_world(route.points[s.waypoint]);
        if(s.retarget) {
            // A segment lasts (distance + 150) / 300 updates, at least two.
            const auto delta=target-s.at;
            s.frames=static_cast<unsigned>(std::max(2,(models.math.length(delta)+150)/300));s.counter=0;
            if(s.retarget==2)s.velocity=delta/static_cast<int>(s.frames);
            s.retarget=0;
        }
        // 8003ad84: close 1/(frames-counter+1) of the gap each update, eased
        // from the previous step at first; at the end snap and keep that step.
        retail::IVec3 step;
        if(s.counter<s.frames) {
            step=(target-s.at)/static_cast<int>(s.frames-s.counter+1);
            const auto ease=[&](const auto& table){
                if(s.counter<table.size())step=shifted(scaled(step,table[s.counter].first)+scaled(s.velocity,table[s.counter].second),10);};
            if(s.frames<5)ease(ease_short);else if(s.frames<7)ease(ease_five);else if(s.frames<9)ease(ease_seven);else ease(ease_nine);
            ++s.counter;
        } else {step=target-s.at;s.velocity=step;s.counter=0;}
        s.at=s.at+step;
        if(!s.counter) {
            s.retarget=1;
            if(++s.waypoint>=static_cast<int>(route.points.size())) {
                // Link -1 ends at the gate; a looping chain is cut short.
                if(route.link==-1 || ++s.hops>64)s.state=4;
                else {s.route=route.link&255;s.waypoint=route.link>>8;}
            }
        }
    } else if(s.state==3) {
        if(s.retarget) {
            // Straight to 0x520 above the gate in (distance + 150) / 300 updates.
            const auto delta=gate+retail::IVec3{0,0,0x520}-s.at;
            s.frames=static_cast<unsigned>(std::max(1,(models.math.length(delta)+150)/300));s.counter=0;
            s.velocity=delta/static_cast<int>(s.frames);s.retarget=0;
        }
        s.at=s.at+s.velocity;
        if(++s.counter>=s.frames)s.state=4;
    }
    body.position=retail::to_world(s.at);
    if(s.state==4) {
        // Arrival: three type-0C bursts of four, then the gate's count
        // (80066efc) rises and the moby is removed.
        const auto basis=object_basis(body);
        for(unsigned burst=0;burst<3;++burst)for(const auto offset:sparkle_offsets)if(particles_.size()<2048) {
            const auto at=body.position+basis*retail::model_position({float(offset.x),float(offset.y),float(offset.z)});
            const auto jitter=[&]{return static_cast<int>(random_.next()&7)-4;};
            const retail::IVec3 v{offset.x/4+jitter()*4,offset.y/4+jitter()*4,offset.z/4+jitter()*4};
            particles_.push_back({at,at,retail::world_position({float(v.x),float(v.y),float(v.z)})*30,{192/128.0f,160/128.0f,96/128.0f},
                (32+(random_.next()&7))*4*unit,0,48,PowerParticleKind::Sparkle,(random_.next()&255)*16*retail::angle_to_radians});
        }
        ++spirits_arrived;return false;
    }
    // Facing the camera (80067ecc - 0x80) and spinning about its X axis.
    const auto forward=g.camera.sample(1).forward;
    const auto yaw=models.math.angle(static_cast<int>(forward.x*4096),static_cast<int>(forward.z*4096));
    body.rotation[2]=static_cast<float>((yaw+128)&255);body.rotation[0]=static_cast<float>((static_cast<unsigned>(body.rotation[0])+5)&255);
    s.trail.push_front(body.position);if(s.trail.size()>6)s.trail.pop_back();
    return true;
}
void Powerups::fly_move(Game& g,const InputState& input) {
    auto& p=g.player;
    const float change=input.move_x*(127.0f/128)*20;
    p.yaw=std::remainder(p.yaw+change*retail::angle_to_radians,2*std::numbers::pi_v<float>);
    float pitch=p.pitch/retail::angle_to_radians;
    pitch=std::clamp(pitch-input.move_z*(127.0f/128)*20,-800.0f,800.0f);
    // Resident table 80060180: descent / level / ascent, Q6 per VBlank.
    const float target=pitch<0?5952+pitch*(5952-11968)/800:5952+pitch*(2560-5952)/800;
    flight_speed_=std::clamp(flight_speed_+(target-flight_speed_)/64,2048.0f,19200.0f);
    p.pitch=pitch*retail::angle_to_radians;
    float roll=p.roll/retail::angle_to_radians;
    roll_velocity_+=-change*std::cos(p.pitch)*32-roll_velocity_/4-roll;
    roll=std::clamp(roll+roll_velocity_/64,-768.0f,768.0f);p.roll=roll*retail::angle_to_radians;
    if(input.jump_pressed && !lift_phase_)lift_phase_=1;
    if(lift_phase_==1){lift_=std::min(6144.0f,lift_+512);if(lift_==6144)lift_phase_=2;}
    else if(lift_phase_==2){lift_=std::max(0.0f,lift_-256);if(lift_==0)lift_phase_=0;}
    p.velocity=forward(p.yaw,p.pitch)*(flight_speed_*MovementTuning::velocity_unit);
    p.velocity.y+=lift_*MovementTuning::velocity_unit;
    if(g.level.universe && g.level.universe->address.space)p.velocity=p.velocity*(input.charge?3.8f:2.4f);
    p.velocity=p.velocity*(g.upgrades.flight_multiplier()*g.abilities.carry_speed()*g.elements.movement(g,MoveStat::Flight));
    g.abilities.assist_flight(g,input);
    if(flight_clip_==34 && pitch<=255)flight_clip_=33;
    if(flight_clip_==35 && pitch>=-255)flight_clip_=33;
    if(pitch>=365)flight_clip_=34;else if(pitch<=-365)flight_clip_=35;
    if(lift_phase_)flight_clip_=34;
}
bool Powerups::fire_tick(Game& g,const InputState& input,bool allowed,bool suppressed) {
    cooldown_=std::min(cooldown_+1,1000u);
    if(suppressed){winding_=PowerShot::None;windup_=0;return false;}
    if(winding_!=PowerShot::None) {
        if(++windup_>=4){const auto kind=winding_;winding_=PowerShot::None;shoot(g,kind);cooldown_=0;}
        return false;
    }
    if(!input.flame_pressed || !allowed)return false;
    // 80030714: the green bomb (8006a1d4, class 2C2), then the freeze shot
    // (8006a1cc, class 270), then the underwater (1F8) or superflame (1E3) shot.
    auto kind=(g.cheats.green_firebomb || g.ripto.green_bomb())?PowerShot::GreenBomb:freeze_enabled(g)?PowerShot::Freeze:
        flame_enabled(g)?PowerShot::Superflame:PowerShot::None;
    if(g.player.swim.mode==WaterMode::Underwater && kind==PowerShot::Superflame)kind=PowerShot::Underwater;
    if(kind==PowerShot::None || cooldown_<(kind==PowerShot::GreenBomb?31u:22u))return false;
    winding_=kind;windup_=0;return true;
}
void Powerups::shoot(Game& g,PowerShot kind) {
    const bool bomb=kind==PowerShot::GreenBomb;
    const auto direction=forward(g.player.yaw,g.player.pitch);
    if(kind==PowerShot::Freeze) {
        // Overlay 60 8007fa6c (Spyro's shot, not first person): at the head
        // joint (80032a90), flying along the head's facing; sound 0 of 270.
        auto position=g.elements.mouth();
        if(length(position-g.player.position)>3)position=g.player.position+retail::origin_above_feet;
        spawn_shot(g,kind,position,direction);++shots_fired;++freeze_shots;
        g.sound_events.push_back({SoundScope::Global,3,0,{}});
        g.sound_events.push_back({SoundScope::Object,0,g.effect_model_type(0x270u),position});
        return;
    }
    const auto position=g.player.position+retail::origin_above_feet+direction*((bomb?600:500)*unit)+Vec3{0,bomb?200*unit:0,0};
    spawn_shot(g,kind,position,forward(g.player.yaw,g.player.pitch+(bomb?std::numbers::pi_v<float>/8:0)));
    ++shots_fired;g.sound_events.push_back({SoundScope::Global,3,0,{}});
    if(!bomb)g.sound_events.push_back({SoundScope::Object,0,g.effect_model_type(kind==PowerShot::Underwater?0x1f8u:0x1e3u),position});
}
void Powerups::spawn_shot(Game& g,PowerShot kind,Vec3 position,Vec3 direction,bool fragment) {
    if(projectiles_.size()>=64)return;
    RetailObject object;object.type=static_cast<std::uint16_t>(g.effect_model_type(kind==PowerShot::GreenBomb?0x2c2:kind==PowerShot::Underwater?0x1f8:kind==PowerShot::Freeze?0x270:0x1e3));
    object.size=fragment?64:32;object.draw_flags=kind==PowerShot::GreenBomb?0x50:0x40;object.persistence=255;
    if(kind==PowerShot::Freeze)object.size=127;   // 8007fa88: +4F = 7F
    object.held_gem=255;object.runtime_spawned=true;
    const bool bomb=kind==PowerShot::GreenBomb;
    object.position=position;
    object.previous_position=object.position;
    object.rotation={0,std::asin(std::clamp(direction.y,-1.0f,1.0f))/retail::angle_to_radians/16,
        retail::original_heading(std::atan2(direction.x,direction.z))/16};
    object.previous_rotation=object.rotation;
    // Recycle only our retired projectile slots; saved pickups/Sparx keep IDs.
    auto index=g.retail_objects.size();
    for(std::size_t i=0;i<g.retail_objects.size();++i) {
        const auto& old=g.retail_objects[i];
        if(!old.runtime_spawned || !old.collected || (old.type!=g.effect_model_type(0x2c2) && old.type!=g.effect_model_type(0x1e3) && old.type!=g.effect_model_type(0x1f8) &&
            old.type!=g.effect_model_type(0x270)))continue;
        if(std::none_of(projectiles_.begin(),projectiles_.end(),[&](const auto& p){return p.object==i;})){index=i;break;}
    }
    if(index==g.retail_objects.size()) {
        unsigned id=0;for(const auto& old:g.retail_objects)id=std::max(id,old.id+1);
        object.id=id;g.retail_objects.push_back(object);
    } else {object.id=g.retail_objects[index].id;g.retail_objects[index]=object;}
    // Speeds per script tick (30 a second): green bomb 300, superflame 440,
    // the freeze shot 340 (8007d7c0, its flight velocity each tick).
    projectiles_.push_back({static_cast<unsigned>(index),0,0,kind,
        direction*(fragment?8.5f:bomb?300*30*unit:kind==PowerShot::Freeze?340*30*unit:440*30*unit),false});
    projectiles_.back().upgrade=g.upgrades.enabled?g.upgrades.flame:0;projectiles_.back().fragment=fragment;
    if(fragment)++split_flames;
}
void Powerups::burst(Vec3 position,bool green,unsigned count,float radius) {
    for(unsigned i=0;i<count && particles_.size()<768;++i) {
        const float yaw=(random_.next()&4095)*retail::angle_to_radians;
        const float pitch=(static_cast<int>(random_.next()&2047)-1024)*retail::angle_to_radians;
        const auto v=forward(yaw,pitch)*(1.5f+(random_.next()&63)*0.045f);
        particles_.push_back({position,position,v,green?Vec3{0.3f,1,0.025f}:Vec3{1,0.5f,0.06f},radius,0,24+(random_.next()&15)});
    }
}
void Powerups::emit_visual(Game& g,const ObjectModels& models,PowerVisualKind kind,Vec3 position,std::array<float,3> rotation) {
    if(visuals_.size()>=512)return;
    PowerVisual effect;effect.kind=kind;auto& object=effect.object;
    object.type=kind==PowerVisualKind::FireCloud?0x1e2:kind==PowerVisualKind::FireSpoke?0x21d:
        kind==PowerVisualKind::FireTrail?0x1e3:kind==PowerVisualKind::WaterTrail?0x1f8:
        kind==PowerVisualKind::WaterBurst?0x1e1:kind==PowerVisualKind::WaterCloud?0x211:0x146;
    object.type=static_cast<std::uint16_t>(models.effect_type(object.type));
    const auto bank=models.models.find(object.type);if(bank==models.models.end())return;
    object.position=object.previous_position=position;object.draw_flags=0x40;object.runtime_spawned=true;
    const bool trail=kind==PowerVisualKind::FireTrail || kind==PowerVisualKind::WaterTrail;
    object.animation=start_object_animation(bank->second,trail?1:0);
    object.previous_animation=object.animation;object.rotation=rotation;
    effect.lifetime=trail?48:kind==PowerVisualKind::FireSpoke || kind==PowerVisualKind::WaterBurst?24:16;
    object.size=kind==PowerVisualKind::GreenBurst || trail || kind==PowerVisualKind::WaterBurst?127:
        kind==PowerVisualKind::FireCloud?70:kind==PowerVisualKind::FireSpoke?100:kind==PowerVisualKind::WaterCloud?80:96;
    object.material_color=kind==PowerVisualKind::FireCloud?0x3f004080:kind==PowerVisualKind::FireSpoke?0x30000000:
        kind==PowerVisualKind::FireTrail?0x3f003048:0x3b008080;
    if(kind==PowerVisualKind::WaterTrail)object.material_color=0x3f303030;
    if(kind==PowerVisualKind::WaterBurst)object.material_color=0x30000000;
    if(kind==PowerVisualKind::WaterCloud)object.material_color=0x3f808040;
    if(kind==PowerVisualKind::GreenBurst || kind==PowerVisualKind::FireBurst) {
        // 800881b0: the original shallow dome faces the camera at birth.
        const auto to_camera=retail::from_world(g.camera.sample(1).eye-position);
        object.rotation={0,float(models.math.angle(models.math.length(to_camera,false),to_camera.z))-64,
            float(models.math.angle(to_camera.x,to_camera.y))};
    } else if(kind==PowerVisualKind::FireCloud || kind==PowerVisualKind::WaterCloud || kind==PowerVisualKind::WaterBurst) {
        object.rotation={0,float(random_.next()&255),float(random_.next()&255)};
    }
    object.previous_rotation=object.rotation;effect.scale=effect.previous_scale=object_model_scale(object);
    effect.previous_material=object.material_color;visuals_.push_back(std::move(effect));++visuals_spawned;
}
void Powerups::emit_sparkle(Vec3 position,Vec3 velocity,Vec3 color,unsigned lifetime) {
    if(particles_.size()<2048)particles_.push_back({position,position,velocity,color,(32+(random_.next()&7))*4*unit,0,lifetime,
        PowerParticleKind::Sparkle,(random_.next()&255)*16*retail::angle_to_radians});
}
void Powerups::emit_particles(Vec3 position,bool green,unsigned count,unsigned sparks) {
    // Overlay 70 8007ba88 / 8007436c: radial particles 3A / 0D start four
    // velocity steps from the blast centre. 46 is a two-colour spark streak.
    for(unsigned i=0;i<count && particles_.size()<2048;++i) {
        const auto velocity=forward((random_.next()&255)*16*retail::angle_to_radians,
            (random_.next()&255)*16*retail::angle_to_radians)*((green?60+(random_.next()%41):48+(random_.next()&31))*unit);
        const auto p=position+velocity*4;
        particles_.push_back({p,p,velocity*30,green?Vec3{1,255.0f/128,0}:Vec3{255.0f/128,255.0f/128,0},
            96*unit,0,30,green?PowerParticleKind::Green:PowerParticleKind::Fire,(random_.next()&255)*16*retail::angle_to_radians});
    }
    for(unsigned i=0;i<sparks && sparks_.size()<512;++i) {
        const auto direction=forward((random_.next()&255)*16*retail::angle_to_radians,
            (random_.next()&255)*16*retail::angle_to_radians);
        const auto tip=position+direction*(128*unit);
        sparks_.push_back({position,position,tip,tip,0,(32-(random_.next()&15))*2});
    }
}
void Powerups::explosion_tick(Game& g,const ObjectModels& models,PowerProjectile& projectile) {
    const bool green=projectile.kind==PowerShot::GreenBomb;
    const auto& object=g.retail_objects[projectile.object];const auto age=projectile.blast_age;
    if(green) {
        const auto half=std::min(age,60-age);
        const float horizontal=(800+1600*(half/60.0f))*unit,vertical=(400+800*(half/60.0f))*unit;
        g.interactions.attack_sphere(g.retail_objects,models,object.position,horizontal+400*unit,green_bomb_attack);
        // Cloud Temples' golden dog doors (1E) take no ordinary contact; the
        // blast's heavy-door bit reaches them directly, and RetailScripts
        // then opens them the way their wizard (1F) does.
        if(g.level.wad_slot==61)for(auto& door:g.retail_objects)
            if(door.type==0x1e && door.state>=0 && !door.collected && length(door.position-object.position)<horizontal+2.0f)door.attack_flags|=green_bomb_attack;
        if(age%2==0) {
            if(age<=49)emit_particles(object.position,true,14,4);
            const auto spread=[&](float extent){const auto n=random_.next();return (n&1?1.0f:-1.0f)*(n%static_cast<unsigned>(extent/unit+1))*unit;};
            for(unsigned i=0;i<2;++i)emit_visual(g,models,PowerVisualKind::GreenBurst,
                object.position+Vec3{spread(horizontal),spread(vertical),spread(horizontal)});
        }
    } else if(projectile.kind==PowerShot::Superflame) {
        if(projectile.fragment) {
            if(!age){++flame_explosions;burst(object.position,false,6,0.07f);}
            g.interactions.attack_sphere(g.retail_objects,models,object.position,0.5f,superflame_attack);return;
        }
        // 8007419c..80074654: one dome and five radial spokes, then five
        // growing 1E2 clouds over a twenty-VBlank damaging explosion.
        if(!age) {
            ++flame_explosions;emit_visual(g,models,PowerVisualKind::FireBurst,object.position);
            const auto to_camera=retail::from_world(object.position-g.camera.sample(1).eye);
            const auto yaw=models.math.angle(to_camera.x,to_camera.y);
            for(unsigned i=0;i<5;++i)emit_visual(g,models,PowerVisualKind::FireSpoke,object.position,
                {0,float(int(i*51+(random_.next()&30))-15),float(yaw+64+(random_.next()&31))});
        }
        if(age%2==0 && age<=14)emit_particles(object.position,false,4,6);
        if(age%4==0)emit_visual(g,models,PowerVisualKind::FireCloud,object.position);
        const float radius=(1300+600*(std::min(age,20-age)/20.0f))*unit;
        g.interactions.attack_sphere(g.retail_objects,models,object.position,radius,superflame_attack);
        // The explosion's own moby query (8007e620: radius + 1000, attack
        // 970000) reaches script mobys too.
        g.overlay_scripts.attack_query(object.position,radius,superflame_attack);
    } else if(projectile.kind==PowerShot::Underwater) {
        // Summer Forest 8007a3a0..8007a5b8: a forty-VBlank blue explosion,
        // with 1E1/211 expanding meshes and particle 27 bubbles, not fire smoke.
        if(!age)++water_explosions;
        if(age%2==0) {
            if(age<=30) {
                emit_visual(g,models,PowerVisualKind::WaterBurst,object.position);
                const auto spread=[&](int extent){return (int(random_.next()%(extent*2+1))-extent)*unit;};
                for(unsigned i=0;i<4 && particles_.size()<2048;++i) {
                    const auto p=object.position+Vec3{spread(100),spread(100),spread(100)};
                    const auto velocity=Vec3{spread(150),(int(random_.next()%131)-50)*unit,spread(150)}*30;
                    particles_.push_back({p,p,velocity,{1,1,1},(10+random_.next()%15)*4*unit,0,
                        40+random_.next()%31,PowerParticleKind::Bubble});
                }
            }
            if(projectile.effect_wait<=2) {
                emit_visual(g,models,PowerVisualKind::WaterCloud,object.position);
                projectile.effect_wait=2+random_.next()%3;
            } else projectile.effect_wait-=2;
        }
        g.interactions.attack_sphere(g.retail_objects,models,object.position,2000*unit,superflame_attack);
    } else if(projectile.kind==PowerShot::Freeze) {
        // Overlay 60 8007d8f8 / 8007db04: four 2B8 ice chunks and forty type-5
        // particles at the burst, then ten updates (20 VBlanks) of a moby query
        // with radius 1000 and attack 01000000.
        if(!age) {
            ++freeze_explosions;
            for(unsigned i=0;i<24;++i) {
                const float yaw=(random_.next()&4095)*retail::angle_to_radians,pitch=(static_cast<int>(random_.next()&2047)-1024)*retail::angle_to_radians;
                emit_sparkle(object.position,forward(yaw,pitch)*(0.6f+(random_.next()&63)*0.02f),{0.75f,0.95f,1.25f},24+(random_.next()&15));
            }
        }
        freeze_contacts(g,models,object.position,1000*unit);
        g.overlay_scripts.attack_query(object.position,1000*unit,freeze_attack);
    }
}
void Powerups::freeze(Game& g,const ObjectModels* models,RetailObject& object) {
    if(!object.frozen_ticks && models) {
        g.sound_events.push_back({SoundScope::Object,0,object.type,object.position});
        if(object.type==0x1ee)if(const auto bank=models->models.find(object.type);bank!=models->models.end() && bank->second.animations.size()>4)
            object.animation=object.previous_animation=start_object_animation(bank->second,4);
        ++objects_frozen;
    }
    object.frozen_ticks=300;object.attack_flags=0;
}
// Native susceptible objects the freeze reaches. Script-owned mobys (Cloud
// Temples' trolls run their own case) take the bit through the moby query.
void Powerups::freeze_contacts(Game& g,const ObjectModels& models,Vec3 center,float radius) {
    for(auto& object:g.retail_objects) {
        if(object.boss_body || object.retail_script || object.collected || object.state<0)continue;
        if(!object.enemy && !object.power_target && !(g.level.wad_slot==61 && object.type==0x1ee))continue;
        if(container_contact(object,models,center,radius))freeze(g,&models,object);
    }
}
void Powerups::tick(Game& g,const ObjectModels* models,const SpyroModel* spyro_model) {
    ++phase_;
    for(auto& object:g.retail_objects) {
        if(object.frozen_ticks && !--object.frozen_ticks && models && object.type==0x1ee) {
            if(const auto bank=models->models.find(object.type);bank!=models->models.end())object.animation=object.previous_animation=start_object_animation(bank->second,0);
        }
        // The freeze shot's bit (01000000) on a native susceptible object.
        if(!object.boss_body && !object.retail_script && (object.attack_flags&freeze_attack) && (object.enemy || object.power_target || (g.level.wad_slot==61 && object.type==0x1ee)))
            freeze(g,models,object);
        if(object.frozen_ticks && !(object.attack_flags&(charge_attack|superflame_attack|green_bomb_attack)))object.attack_flags=0;
    }
    std::erase_if(visuals_,[](const auto& v){return v.kind==PowerVisualKind::GateIcon || v.kind==PowerVisualKind::IceShell ||
        v.kind==PowerVisualKind::GateDigit || v.kind==PowerVisualKind::Spirit;});
    for(auto& p:particles_) {
        p.previous=p.position;p.position+=p.velocity*(1.0f/60);
        // 800896d0 updates the quarter-unit vertical velocity every second
        // original script step; interpolate positions at native 60 Hz.
        if(p.kind==PowerParticleKind::Puff)p.velocity.y+=0.03f;
        else if(p.kind==PowerParticleKind::Sparkle){}   // 0C only drifts; its size and colour follow its age
        else if(p.kind==PowerParticleKind::GateGlow) {
            // 04 (overlay 16, 8007f24c): grows 4 per update for its first
            // updates, turns and rises; above the gate's ceiling it shrinks 4
            // per update and is removed below size 9 (radius 4 units per size).
            if(p.age<p.grow*2)p.radius+=8*unit;
            p.angle+=p.spin;
            if(p.position.y>p.ceiling){p.radius-=8*unit;if(p.radius<36*unit)p.age=p.lifetime;}
        }
        else if(p.kind==PowerParticleKind::Bubble) {
            const auto angle=((p.lifetime-p.age)&31)*8*16*retail::angle_to_radians;
            p.position+=Vec3{std::cos(angle),0,std::sin(angle)}*(16*unit);
            if(p.age%2==1) {
                const auto damp=[](float v){const float step=120*unit;return v>0?std::max(0.0f,v-step):std::min(0.0f,v+step);};
                p.velocity.x=damp(p.velocity.x);p.velocity.z=damp(p.velocity.z);
                p.velocity.y=std::min(24*120*unit,p.velocity.y+120*unit);
            }
        } else if(p.age%4==3)p.velocity.y+=120*unit;
        ++p.age;
    }
    std::erase_if(particles_,[](const auto& p){return p.age>=p.lifetime;});
    for(auto& p:sparks_) {
        p.previous=p.position;p.previous_tip=p.tip;
        const auto velocity=(p.tip-p.position)*0.25f;p.position+=velocity;p.tip+=velocity;
        if(phase_%2==0) {
            p.tip.x+=(int(random_.next()&2)-1)*4*unit;p.tip.z+=(int(random_.next()&2)-1)*4*unit;
            if(p.age&2)p.tip.y-=4*unit;
        }
        ++p.age;
    }
    std::erase_if(sparks_,[](const auto& p){return p.age>=p.lifetime;});
    if(flying && spyro_model) {
        const auto pose=g.animation.sample(1);const auto origin=g.player.position+retail::origin_above_feet;
        for(unsigned wing=0;wing<2;++wing) {
            const auto tip=spyro_model->wing_tips[wing];
            if(tip>=pose.vertices.size())throw FormatError("Superfly wing attachment outside Spyro pose");
            wings_[wing].push_front(origin+retail::rotate_model(pose.vertices[tip],g.player.yaw,g.player.pitch,g.player.roll));
            const unsigned count=flight_clip_==34?6:12;
            while(wings_[wing].size()>count)wings_[wing].pop_back();
            if(g.level.universe && g.level.universe->address.space) {
                // Extra space-flight speed must not stretch the short retail
                // wing streak through the follow camera's near plane.
                auto& trail=wings_[wing];float span=0;std::size_t keep=1;
                for(;keep<trail.size();++keep) {
                    span+=length(trail[keep]-trail[keep-1]);
                    if(span>(flight_clip_==34?0.6f:1.2f))break;
                }
                while(trail.size()>std::max(std::size_t(2),keep))trail.pop_back();
            }
        }
    } else for(auto& wing:wings_)if(!wing.empty())wing.pop_back();
    if(g.upgrades.enabled && g.upgrades.charge && (g.player.motion==Motion::Supercharge || g.player.motion==Motion::SuperchargeJump)) {
        const auto direction=forward(g.player.yaw,0),side=Vec3{direction.z,0,-direction.x};
        for(const float sign:{-1.0f,1.0f})if(particles_.size()<2048) {
            const auto position=g.player.position-direction*0.45f+side*(sign*0.18f)+Vec3{0,0.12f,0};
            const auto color=g.upgrades.charge==1?Vec3{1.8f,1.3f,0.02f}:Vec3{1.8f,0.08f,0.015f};
            particles_.push_back({position,position,direction*-1.5f+Vec3{0,1.2f,0},color,0.12f+g.upgrades.charge*0.035f,0,20,PowerParticleKind::ChargeFlame});++upgrade_flames;
        }
    }
    if(!models)return;
    for(auto& effect:visuals_) {
        auto& object=effect.object;effect.previous_scale=effect.scale;effect.previous_material=object.material_color;
        object.previous_animation=object.animation;tick_object_animation(models->models.at(object.type),object.animation);
        ++effect.age;const auto remaining=effect.lifetime-std::min(effect.age,effect.lifetime);
        unsigned r=0,gc=0,b=0,mode=0x3f;
        switch(effect.kind) {
        case PowerVisualKind::GateIcon:case PowerVisualKind::IceShell:case PowerVisualKind::GateDigit:case PowerVisualKind::Spirit:break;
        case PowerVisualKind::GreenBurst:case PowerVisualKind::FireBurst: {
            const bool green=effect.kind==PowerVisualKind::GreenBurst;
            object.size=static_cast<std::uint8_t>(remaining*(green?4:2)+(green?52:48));
            r=remaining>=8?128:remaining*16;gc=remaining>=8?(remaining-8)*16:0;
            if(green)std::swap(r,gc);mode=0x3b;break;
        }
        case PowerVisualKind::FireCloud:object.size=static_cast<std::uint8_t>(std::max(14,70-int(effect.age)*3));r=remaining*8;gc=remaining*4;break;
        case PowerVisualKind::FireSpoke:object.size=static_cast<std::uint8_t>(std::max(24,100-int(effect.age)*6));mode=0x30+std::max(0,16-int(remaining));break;
        case PowerVisualKind::FireTrail:object.size=static_cast<std::uint8_t>(std::clamp(16384/(1024-int(remaining)*20),18,127));r=remaining*4;gc=remaining*2;break;
        case PowerVisualKind::WaterTrail:object.size=static_cast<std::uint8_t>(std::clamp(16384/(1024-int(remaining)*20),18,127));r=gc=b=remaining;break;
        case PowerVisualKind::WaterBurst:object.size=static_cast<std::uint8_t>(std::max(24,127-int(effect.age)*6));mode=0x30+std::max(0,16-int(remaining));break;
        case PowerVisualKind::WaterCloud:object.size=static_cast<std::uint8_t>(std::max(14,80-int(effect.age)*4));r=remaining*4;gc=b=remaining*8;break;
        }
        object.material_color=(mode<<24)|(b<<16)|(gc<<8)|r;effect.scale=object_model_scale(object);
    }
    std::erase_if(visuals_,[](const auto& p){return p.age>=p.lifetime;});
    std::vector<std::pair<Vec3,Vec3>> splits;
    for(auto& projectile:projectiles_) {
        auto& object=g.retail_objects[projectile.object];const bool bomb=projectile.kind==PowerShot::GreenBomb,ice_shot=projectile.kind==PowerShot::Freeze;
        const auto flags=bomb?green_bomb_attack:ice_shot?freeze_attack:superflame_attack;
        if(projectile.exploding) {
            ++projectile.blast_age;
            if(projectile.blast_age<(bomb?60u:projectile.kind==PowerShot::Underwater?40u:20u))explosion_tick(g,*models,projectile);
            continue;
        }
        if(projectile.age==0) {
            const auto bank=models->models.find(object.type);
            if(bank==models->models.end()){object.collected=true;projectile.exploding=true;projectile.blast_age=60;continue;}
            object.animation=start_object_animation(bank->second);object.previous_animation=object.animation;
        }
        begin_object_transform(object,1);++projectile.age;
        if(g.upgrades.enabled && projectile.upgrade && projectile.kind==PowerShot::Superflame) {
            Vec3 goal{};float nearest=18;bool found=false;
            for(const auto& target:g.retail_objects) {
                if((!target.enemy && (!target.power_target || target.power_target->scenery)) || target.collected || target.state<0 || (target.enemy && target.enemy->defeated))continue;
                const auto delta=target.position+Vec3{0,0.4f,0}-object.position;const auto distance=length(delta);
                if(distance>=nearest || distance<0.01f || dot(normalized(delta),normalized(projectile.velocity))<0.15f)continue;
                if(g.level.raycast(object.position,delta,std::max(0.0f,distance-0.25f)))continue;
                nearest=distance;goal=delta;found=true;
            }
            if(found) {
                const float speed=length(projectile.velocity);
                projectile.velocity=normalized(lerp(normalized(projectile.velocity),normalized(goal),0.07f))*speed;++homing_ticks;
                const auto heading=normalized(projectile.velocity);
                object.rotation={0,std::asin(std::clamp(heading.y,-1.0f,1.0f))/retail::angle_to_radians/16,retail::original_heading(std::atan2(heading.x,heading.z))/16};
            }
        }
        if(projectile.kind==PowerShot::Underwater) {
            // 8007a108: forward 340, with a 50-unit spiral around the shot axis.
            const auto angle=(projectile.age-1)*7*16*retail::angle_to_radians;
            projectile.velocity=object_rotation({-50*std::cos(angle),50*std::sin(angle),340},object)*(30*unit);
        }
        const auto step=projectile.velocity*(1.0f/60),from=object.position;
        if(bomb)projectile.velocity.y-=10*30*30*unit/60;
        const auto distance=length(step);bool hit=false,terrain_hit=false;Vec3 impact_normal{},impact_point{};
        if(const auto wall=g.level.raycast(from,step,distance+0.04f);wall && wall->distance<=distance+0.04f) {
            object.position=wall->point+wall->normal*0.025f;hit=terrain_hit=true;impact_normal=wall->normal;impact_point=wall->point;
        } else object.position+=step;
        if(ice_shot) {
            // 8007d85c: the moby query (radius 700, attack 01000000) ends the
            // flight on any moby it reaches; 8003dc64 (radius 400) does the same
            // for solid mobys' collision.
            for(const auto& target:g.retail_objects)if(container_contact(target,*models,object.position,700*unit)){hit=true;break;}
            freeze_contacts(g,*models,object.position,700*unit);
            if(g.overlay_scripts.attack_query(object.position,700*unit,flags))hit=true;
            if(projectile.age%4==1)emit_sparkle(object.position,{0,0,0},{0.75f,0.95f,1.25f},16);
        } else for(const auto& target:g.retail_objects)if(container_contact(target,*models,object.position,(bomb?300:400)*unit)) {
            g.interactions.attack_sphere(g.retail_objects,*models,object.position,(bomb?300:400)*unit,flags);
            // Retail superflame passes through wooden/metal/vase containers.
            if(bomb || !is_breakable(target.type) || (g.upgrades.enabled && projectile.upgrade>=2)){hit=true;break;}
        }
        // Script-owned mobys (the overlay's own classes: Metropolis' sheep
        // saucers, Sunny Beach's turtle boxes, ...) are reached through the
        // retail moby-sphere query, which delivers the shot's attack word to
        // their +18. The superflame shot (class 1E3, Sunny Beach 8007de40)
        // makes it every flying tick after its terrain test, whether or not
        // that test stopped it (a shot striking a solid moby's collision bank
        // still finds the moby), and once at birth with radius 600.
        if(!ice_shot && (!hit || terrain_hit) && g.overlay_scripts.attack_query(object.position,(bomb?300:projectile.age<=1?600:400)*unit,flags))hit=true;
        if(projectile.kind==PowerShot::Superflame) {
            projectile.trail.push_front(object.position);while(projectile.trail.size()>24)projectile.trail.pop_back();
            if(!projectile.fragment && projectile.age%8==0)emit_visual(g,*models,PowerVisualKind::FireTrail,object.position,object.rotation);
        }
        if(projectile.kind==PowerShot::Underwater && projectile.age%6==0)
            emit_visual(g,*models,PowerVisualKind::WaterTrail,object.position,object.rotation);
        // 8007fbc4: the freeze shot's +0C = 180, counted down by 2 a tick.
        const auto lifetime=projectile.fragment?45u:bomb?300u:projectile.kind==PowerShot::Underwater || ice_shot?180u:110u;
        if(projectile.kind==PowerShot::Underwater && !hit && projectile.age>=lifetime) {
            object.collected=true;object.state=-1;projectile.exploding=true;projectile.blast_age=40;continue;
        }
        if(hit || projectile.age>=lifetime) {
            if(hit && projectile.kind!=PowerShot::Underwater && !ice_shot)
                g.destruction.impact(g,terrain_hit?impact_point:object.position,length(impact_normal)>.5f?impact_normal:normalized(projectile.velocity)*-1,
                    bomb?TerrainAttack::GreenBomb:TerrainAttack::Superflame,models,projectile.fragment);
            if(hit && g.upgrades.enabled && projectile.kind==PowerShot::Superflame && projectile.upgrade>=2 && !projectile.fragment) {
                auto direction=normalized(projectile.velocity);
                if(length(impact_normal)>0.1f)direction=direction-impact_normal*(2*dot(direction,impact_normal));
                splits.push_back({object.position+impact_normal*0.15f,normalized(direction)});
            }
            projectile.exploding=true;object.collected=true;object.state=-1;
            if(bomb) {
                ++bombs_exploded;
                g.camera.shake(5000,30);
                if(const auto floor=g.level.raycast(object.position+Vec3{0,unit,0},{0,-1,0},200))object.position.y=floor->point.y+700*unit;
                else object.position.y+=700*unit;
                g.sound_events.push_back({SoundScope::Object,0,object.type,object.position});
            }
            explosion_tick(g,*models,projectile);
        } else if(bomb && projectile.age%6==0) {
            const auto velocity=Vec3{float(random_.next()&3),20,float(random_.next()&3)}*(30*unit);
            particles_.push_back({object.position,object.position,velocity,{1,255.0f/128,0},96*unit,0,30,PowerParticleKind::Green});
        }
    }
    std::erase_if(projectiles_,[](const auto& p){return p.exploding && p.blast_age>=(p.kind==PowerShot::GreenBomb?60u:p.kind==PowerShot::Underwater?40u:20u);});
    for(const auto& [position,direction]:splits)for(const float angle:{-0.65f,0.0f,0.65f}) {
        const auto aim=normalized(retail::rotate_model(direction,angle)+Vec3{0,0.12f,0});
        spawn_shot(g,PowerShot::Superflame,position+aim*0.25f,aim,true);
    }
    // Script-driven mobys (RetailScripts) receive these bits on their +18
    // attack word and run their own original hit/death code instead.
    for(auto& object:g.retail_objects)if(object.power_target && !object.enemy && !object.retail_script && !object.scripted_contact && !object.frozen_ticks && !object.collected && object.state>=0) {
        if(power_target_accepts(object,object.attack_flags)) {
            burst(object.position,attack_strength(object.attack_flags)>=3,24,0.2f);
            object.state=-1;object.collected=true;++targets_destroyed;
            g.sound_events.push_back({SoundScope::Object,object.power_target->scenery?2u:0u,object.type,object.position});
        }
        object.attack_flags=0;
    }
    // The gate dispatch spawns its authored held-class symbol at +0x580.
    // Keep these presentation objects outside the persistent Moby ID space.
    for(const auto& gate:g.retail_objects)if(gate.powerup_gate && !gate.collected && gate.powerup_gate->unlocked) {
        const auto bank=models->models.find(gate.held_gem);if(bank==models->models.end())continue;
        PowerVisual v;v.kind=PowerVisualKind::GateIcon;v.lifetime=2;v.object.type=gate.held_gem;
        // 80075a40: classes under 0D take material 0x2a606060; 0B/0E turn
        // about Z (2 per VBlank), 0C about X (4); other icons keep the gate's
        // heading and animate their own clip.
        auto& o=v.object;o.position=o.previous_position=gate.position+Vec3{0,0x580*unit,0};o.size=32;o.material_color=o.type<0xd?0x2a606060u:0u;
        o.rotation=o.previous_rotation=gate.rotation;
        if(o.type==0xc){o.rotation[0]+=phase_*4;o.previous_rotation[0]=o.rotation[0]-4;}
        else if(o.type==0xb || o.type==0xe){o.rotation[2]+=phase_*2;o.previous_rotation[2]=o.rotation[2]-2;}
        if(bank->second.animated) {
            auto [state,fresh]=icon_animations_.try_emplace(gate.id,start_object_animation(bank->second,0));
            o.previous_animation=state->second;
            if(!fresh)tick_object_animation(bank->second,state->second);
            o.animation=state->second;
        }
        v.scale=v.previous_scale=bank->second.animated?object_model_scale(o):1;v.previous_material=o.material_color;
        visuals_.push_back(std::move(v));
    }
    if(const auto ice_model=models->models.find(models->effect_type(0x271));ice_model!=models->models.end())for(const auto& troll:g.retail_objects)
        if(troll.frozen_ticks && !troll.collected && troll.state>=0 && ((g.level.wad_slot==61 && troll.type==0x1ee) || troll.element_frozen)) {
        PowerVisual v;v.kind=PowerVisualKind::IceShell;v.lifetime=2;auto& o=v.object;
        o.type=static_cast<std::uint16_t>(models->effect_type(0x271));o.position=o.previous_position=troll.position;o.rotation=o.previous_rotation=troll.rotation;o.size=32;o.material_color=0x2a808080;
        if(troll.element_frozen && troll.type!=0x1ee) {
            // PC Ice element: fit the Cloud Temples shell around any enemy.
            const auto& shell=ice_model->second;const float shell_radius=shell.animated?shell.animations.front().radius:shell.meshes.front().radius;
            float body=.45f;if(const auto m=models->models.find(troll.type);m!=models->models.end())
                body=(m->second.animated?m->second.animations.at(std::min<std::size_t>(troll.animation.clip,m->second.animations.size()-1)).radius*object_model_scale(troll):m->second.meshes.front().radius)*1.1f;
            o.size=static_cast<std::uint8_t>(std::clamp(32*shell_radius/std::max(.1f,body),8.0f,160.0f));
        }
        if(ice_model->second.animated)o.animation=o.previous_animation=start_object_animation(ice_model->second,0);
        v.scale=v.previous_scale=(troll.element_frozen && troll.type!=0x1ee)?32.0f/o.size:ice_model->second.animated?object_model_scale(o):1;
        v.previous_material=o.material_color;visuals_.push_back(v);
    }
}
void Powerups::reset_interpolation(){
    for(auto& p:particles_)p.previous=p.position;
    for(auto& e:visuals_){e.previous_scale=e.scale;e.previous_material=e.object.material_color;e.object.previous_animation=e.object.animation;}
    for(auto& p:sparks_){p.previous=p.position;p.previous_tip=p.tip;}
    for(auto& c:crackles_){c.previous_head=c.head;c.previous_tail=c.tail;}
}
Vec3 Powerups::tint(const Game& g)const {
    // The any-level cheat keeps Spyro's normal appearance. Only its original
    // projectile and explosion use the green power-up palette.
    if(g.cheats.green_firebomb)return {1,1,1};
    if(g.ripto.green_bomb())return {.35f,1.3f,.3f};
    if(invincibility.remaining && (invincibility.remaining>180 || (invincibility.remaining&16)))return {144/128.0f,32/128.0f,16/128.0f};
    if(ice.remaining)return {0.6f,0.9f,1.6f};
    if(flame_enabled(g)) {
        const auto timer=fire.remaining?fire.remaining:phase_;
        const auto angle=fire.remaining && timer<180?1440-timer*6:timer*2;
        const auto green=std::abs(g.camera.settings.cos_angle((angle&255)*16)>>5)+32;
        return {144/128.0f,green/128.0f,16/128.0f};
    }
    return {1,1,1};
}
float Powerups::meter(const Game& g)const {
    if(g.ripto.enabled && g.ripto.player_power_ticks)return float(g.ripto.player_power_ticks)/g.ripto.power_duration();
    if(ice.remaining)return float(ice.remaining)/std::max(1u,ice.duration);
    if(invincibility.remaining)return float(invincibility.remaining)/std::max(1u,invincibility.duration);
    if(flight.remaining && !g.cheats.superfly)return float(flight.remaining)/std::max(1u,flight.duration);
    if(fire.remaining && !g.cheats.superflame && !permanent_fire)return float(fire.remaining)/std::max(1u,fire.duration);
    return 0;
}
const char* Powerups::meter_name(const Game& g)const {
    if(g.ripto.green_bomb())return "Green fire bombs";
    if(g.ripto.supercharge())return "Supercharge";
    if(g.ripto.enabled && g.ripto.stage==RiptoStage::Bird)return "Superfly + Superflame";
    if(ice.remaining)return "Superfreeze";
    if(invincibility.remaining)return "Invincibility";
    return flight.remaining && !g.cheats.superfly?(g.level.wad_slot==67?"Superfly + Superflame":"Superfly"):"Superflame";
}
} // namespace spyro

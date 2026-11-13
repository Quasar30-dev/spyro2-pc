#include "spyro/ripto.hpp"
#include "spyro/runtime.hpp"
#include <numbers>
#include <bit>

namespace spyro {
namespace {
constexpr float pi=std::numbers::pi_v<float>,unit=1/retail::units_per_world,dt=1.0f/60;
float flat(Vec3 p){return std::hypot(p.x,p.z);}
Vec3 horizontal(Vec3 p){p.y=0;return p;}
float smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
Vec3 curve(Vec3 a,Vec3 b,Vec3 c,Vec3 d,float t){const float s=1-t;return a*(s*s*s)+b*(3*s*s*t)+c*(3*s*t*t)+d*(t*t*t);}
float segment_distance(Vec3 p,Vec3 a,Vec3 b){const auto d=b-a;const float q=dot(d,d);return length(p-(a+d*(q>1e-8f?std::clamp(dot(p-a,d)/q,0.f,1.f):0.f)));}
bool clear(const Game& g,Vec3 a,Vec3 b){const auto d=b-a;const auto h=g.level.raycast(a,d,length(d));return !h || h->distance>=length(d)-.04f;}
void notice(Game& g,const std::string& text,unsigned ticks=180){g.custom_notice=text;g.custom_notice_ticks=ticks;}
void clip(RetailObject& o,const ObjectModels& models,unsigned id,bool restart=false) {
    const auto& m=models.models.at(o.type);
    if(id>=m.animations.size() || m.animations[id].frames.empty())throw FormatError("Ripto encounter references a missing retail animation");
    auto& a=o.animation;if(!restart && a.active && a.next_clip==id)return;
    if(!a.active)a=start_object_animation(m,id);
    else {a.clip=a.next_clip;a.frame=a.next_frame;a.next_clip=static_cast<std::uint8_t>(id);a.next_frame=0;a.blend=0x72;a.flags=0;}
}
unsigned animation_time(const RetailObject& o,const ObjectModels& models) {
    const auto& a=o.animation;const auto& m=models.models.at(o.type);
    if(a.clip!=a.next_clip)return 0;
    return (m.animations.at(a.clip).frames.at(a.frame).metadata&511)*2+(a.blend&15);
}
bool animation_end(const RetailObject& o){return o.animation.clip==o.animation.next_clip && (o.animation.flags&2);}
void face(RetailObject& o,Vec3 target,float rate=2) {
    const auto d=target-o.position;const auto wanted=retail::original_heading(std::atan2(d.x,d.z))/16;
    o.rotation[2]+=std::clamp(std::remainder(wanted-o.rotation[2],256.0f),-rate,rate);
}
Vec3 forward(const RetailObject& o){const auto yaw=retail::native_heading(o.rotation[2]*16);return {std::sin(yaw),0,std::cos(yaw)};}
bool ground_move(Game& g,RetailObject& o,Vec3 destination,float speed) {
    auto delta=horizontal(destination-o.position);const auto distance=length(delta);if(distance<.01f)return false;
    face(o,destination);auto step=normalized(delta)*std::min(speed*dt,distance);auto next=o.position+step;
    const auto center=g.ripto.center;
    const float edge=g.ripto.retail_arena?(o.type==0x2b0?13.0f:15.0f):(o.type==0x2b0?9.0f:10.5f);
    if(flat(next-center)>edge){const auto bounded=center+normalized(horizontal(next-center))*edge;next.x=bounded.x;next.z=bounded.z;}
    if(!clear(g,o.position+Vec3{0,.6f,0},next+Vec3{0,.6f,0}))return false;
    if(!g.ripto.retail_arena && (o.type==0x8f || o.type==0x2b0)) {
        const auto other=o.type==0x8f?g.ripto.gulp:g.ripto.crush;
        if(other<g.retail_objects.size()) {const auto& body=g.retail_objects[other];
            if(!body.hidden && flat(next-body.position)<2.8f && flat(next-body.position)<flat(o.position-body.position))return false;}
    }
    const auto floor=g.level.raycast(next+Vec3{0,.7f,0},{0,-1,0},1.3f);
    if(!floor || floor->normal.y<.6f || floor->point.y<center.y-.8f || floor->surface_type==1)return false;
    next.y=floor->point.y+.02f;const bool moved=flat(next-o.position)>.001f;o.position=next;return moved;
}
RetailObject visual(const ObjectModels& models,unsigned type,Vec3 at,unsigned size=32) {
    RetailObject o;o.type=static_cast<std::uint16_t>(type);o.position=o.previous_position=at;o.size=static_cast<std::uint8_t>(size);
    o.runtime_spawned=true;o.held_gem=255;o.draw_flags=0x40;
    const auto& m=models.models.at(type);if(m.animated)o.animation=start_object_animation(m);
    o.previous_animation=o.animation;
    if(std::any_of(m.meshes.begin(),m.meshes.end(),[](const auto& mesh){return std::any_of(mesh.faces.begin(),mesh.faces.end(),[](const auto& f){return !f.palette_valid;});}))o.material_color=0x2a808080;
    return o;
}
Vec3 staff(const RetailObject& o,const ObjectModels& models) {
    const auto p=sample_object_attachment(models.models.at(o.type),o.animation,models.animation_blends,0);
    return o.position+object_basis(o)*(p*object_model_scale(o));
}
Vec3 gulp_socket(const RetailObject& o,const ObjectModels& models,unsigned first) {
    const auto& m=models.models.at(o.type);
    const auto p=(sample_object_attachment(m,o.animation,models.animation_blends,first)+
                  sample_object_attachment(m,o.animation,models.animation_blends,first+1))*.5f;
    return o.position+object_basis(o)*(p*object_model_scale(o));
}
}
void decode_ripto_arena(std::span<const std::uint8_t> archive,Level& level) {
    if(level.wad_slot!=71)return;
    // The August test arena precedes the shipped boss actor/route layout.
    // Do not attach the retail three-phase encounter to absent actors.
    constexpr std::array required_actors{0x2afu,0x2adu,0x2acu};
    if(read_u32(archive,0x20)==read_u32(archive,0x10) &&
       std::any_of(required_actors.begin(),required_actors.end(),
           [&](unsigned type){return std::none_of(level.objects.begin(),level.objects.end(),[&](const auto& o){return o.type==type;});}))return;
    const auto data=archive.subspan(read_u32(archive,24),read_u32(archive,28));
    const auto actor=[&](unsigned type)->const RetailObject& {
        const auto it=std::find_if(level.objects.begin(),level.objects.end(),[&](const auto& o){return o.type==type;});
        if(it==level.objects.end())throw FormatError("Ripto arena is missing an authored actor");return *it;
    };
    const auto path=[&](unsigned at,unsigned* initial=nullptr) {
        if(at>data.size() || data.size()-at<12)throw FormatError("Invalid Ripto route pointer");
        const auto count=read_u16(data,at),start=read_u16(data,at+2);
        if(!count || count>256 || start>=count || data.size()-at-12<count*16u)throw FormatError("Invalid Ripto route length");
        if(initial)*initial=start;std::vector<Vec3> points;
        for(unsigned i=0;i<count;++i){const auto p=at+12+i*16;points.push_back(retail::to_world({std::bit_cast<int>(read_u32(data,p)),std::bit_cast<int>(read_u32(data,p+4)),std::bit_cast<int>(read_u32(data,p+8))}));}
        return points;
    };
    const auto& r=actor(0x2af);const auto& b=actor(0x2ad);const auto& h=actor(0x2ac);
    auto& a=level.ripto_arena.emplace();
    // 8007ef20: +18 centre, +20 aerial Spyro start, +24 Gulp,
    // +28 bird. Bird +34 and Hunter +4 are their authored flight paths.
    a.center=path(read_u32(data,r.data_offset+0x18)).front();
    a.flight_spawn=path(read_u32(data,r.data_offset+0x20)).front()-retail::origin_above_feet;
    a.gulp_spawn=path(read_u32(data,r.data_offset+0x24)).front();
    a.bird_spawn=path(read_u32(data,r.data_offset+0x28)).front();
    a.bird_route=path(read_u32(data,b.data_offset+0x34),&a.bird_initial);
    a.hunter_route=path(read_u32(data,h.data_offset+4),&a.hunter_initial);
}
RiptoEyeSocket ripto_eye_socket(std::span<const Vec3> p) {
    if(p.size()!=222)throw FormatError("Cyborg Ripto needs the retail 222-vertex head topology");
    // Textured right eye: 168/165/161/157; 18/19/77 belong to the brow.
    const auto outer=(p[168]+p[165])*.5f,inner=(p[157]+p[161])*.5f;
    const auto upper=(p[168]+p[157])*.5f,lower=(p[165]+p[161])*.5f;
    auto right=normalized(outer-inner),up=normalized(upper-lower);
    auto n=normalized(cross(right,up));if(dot(n,p[158]-p[167])<0)n=n*-1;
    up=normalized(cross(n,right));if(dot(up,upper-lower)<0)up=up*-1;
    const auto center=lerp(inner,outer,.61f);
    return {center,right,up,n,length(outer-inner),length(upper-lower)};
}
void install_ripto_encounter(Level& level,ObjectModels& models,const Level& retail_level,const ObjectModels& donor) {
    if(!level.custom_arena)throw FormatError("Cyborg Ripto requires the Crownworks arena");
    const unsigned types[]{0x2af,0x2b0,0x2ad,0x2ac,0x2b7,0x2eb,0x2b9,0x2c3,0x289,0x2da,0x2e6,0x2e8,0x2a1};
    import_object_models(models,donor,types);
    unsigned id=0;for(const auto& o:level.objects)id=std::max(id,o.id+1);
    for(const unsigned type:{0x2af,0x2b0,0x2ad,0x2ac}) {
        const auto found=std::find_if(retail_level.objects.begin(),retail_level.objects.end(),[&](const auto& o){return o.type==type;});
        if(found==retail_level.objects.end())throw FormatError("Retail Ripto actor is absent from the mounted game");
        auto o=*found;o.id=id++;o.source_id=UINT32_MAX;o.position=o.previous_position={0,.02f,0};
        o.rotation=o.previous_rotation={0,0,128};o.state=0;o.hidden=type!=0x2af && type!=0x2ac;o.collected=false;
        o.enemy.reset();o.power_target.reset();o.attack_flags=0;o.runtime_spawned=true;
        o.animation=start_object_animation(models.models.at(type));o.previous_animation=o.animation;
        if(type!=0x2ac){o.scripted_contact=true;o.power_target=PowerTarget{1,false};}
        else{o.position=o.previous_position={0,8,-13};}
        level.objects.push_back(std::move(o));
    }
    level.custom_arena->ripto_encounter=true;
}
void RiptoBoss::initialize(Game& g,const ObjectModels* models) {
    retail_arena=g.level.wad_slot==71 && g.level.custom_name.empty() && !g.level.universe && g.level.ripto_arena.has_value();
    enabled=models && (retail_arena || (g.level.custom_arena && g.level.custom_arena->ripto_encounter));if(!enabled)return;
    actor=gulp=bird=hunter=crush=UINT32_MAX;
    for(unsigned i=0;i<g.retail_objects.size();++i) {
        auto& o=g.retail_objects[i];if(!retail_arena && !o.runtime_spawned)continue;
        if(o.type==0x2af)actor=i;if(o.type==0x2b0)gulp=i;if(o.type==0x2ad)bird=i;if(o.type==0x2ac)hunter=i;
        if(!retail_arena && o.type==0x8f)crush=i;
        if(o.type==0x2af || o.type==0x2b0 || o.type==0x2ad){o.scripted_contact=true;o.scripted_transient=true;o.enemy.reset();o.power_target=PowerTarget{1,false};}
    }
    if(actor==UINT32_MAX || gulp==UINT32_MAX || bird==UINT32_MAX || hunter==UINT32_MAX)throw FormatError("Incomplete Ripto encounter actor set");
    center=retail_arena?g.level.ripto_arena->center:Vec3{};
    if(const auto floor=g.level.raycast(center+Vec3{0,2,0},{0,-1,0},5))center.y=floor->point.y;
}
void RiptoBoss::reset(Game& g,const ObjectModels* models) {
    if(!enabled || !models || stage==RiptoStage::Defeated)return;
    if(ticking_){reset_pending_=true;return;}reset_pending_=false;
    if(cinematic!=CrownCinematic::None)end_cinematic(g);
    // Crownworks: a lost life restarts the phase Spyro had reached, not the
    // whole fight (the retail arena keeps its original full restart).
    const auto resume=!retail_arena && engaged?stage:RiptoStage::Foot;
    ++resets;stage=RiptoStage::Foot;fury_damage_=crush_fury_damage_=0;action=RiptoAction::Waiting;health=max_health=60;engaged=false;
    ticks=action_ticks=damage_cooldown=sequence_=orb_clock_=orb_color_=last_damage_=spell_shots_=boss_power_=boss_power_ticks_=0;
    player_power=player_power_ticks=spyro_orbs=ripto_orbs=0;
    hud_ticks=0;hud_spyro_blink=36;hud_ripto_blink=0;
    stagger_ticks_=aerial_sequence_=0;marks_.clear();dive_trail_.clear();
    effect_origin=previous_effect_origin=center;effect_charge=previous_effect_charge=0;
    crush_health=crush_ticks=crush_cooldown_=crush_sequence_=0;crush_action=CrushAction::Dormant;
    finale_ticks=gulp_frozen=crush_frozen=gulp_freeze_ready_=crush_freeze_ready_=0;finisher_.reset();chain_to_crush_=chain_to_gulp_=false;
    if(crush<g.retail_objects.size()){auto& c=g.retail_objects[crush];c.hidden=true;c.boss_body.reset();c.attack_flags=0;c.animation=start_object_animation(models->models.at(c.type));c.previous_animation=c.animation;}
    normal_cooldown_=0;bird_waypoint_=retail_arena?g.level.ripto_arena->bird_initial:0;hunter_waypoint_=retail_arena?g.level.ripto_arena->hunter_initial:0;
    shots_.clear();orbs_.clear();beams_.clear();flashes_.clear();visuals_.clear();
    for(const auto index:{actor,gulp,bird}) {
        auto& o=g.retail_objects[index];o.hidden=index!=actor;o.collected=false;o.state=0;o.attack_flags=0;
        o.frozen_ticks=0;o.boss_body.reset();o.position=o.previous_position=center+Vec3{0,.02f,0};
        if(retail_arena && index==actor){o.position=g.level.objects[index].position;if(const auto floor=g.level.raycast(o.position+Vec3{0,1,0},{0,-1,0},3))o.position.y=floor->point.y+.02f;o.previous_position=o.position;}
        o.rotation=o.previous_rotation={0,0,128};o.animation=start_object_animation(models->models.at(o.type));o.previous_animation=o.animation;
    }
    g.retail_objects[actor].boss_body=BossBody{.38f,.35f,1.0f};
    auto& h=g.retail_objects[hunter];h.hidden=false;h.state=0;h.collected=false;h.position=h.previous_position=retail_arena?g.level.objects[hunter].position:Vec3{0,8,-13};
    if(resume>RiptoStage::Foot){advance_to(g,*models,resume);++phase_restarts;}
}
void RiptoBoss::restore(Game& g,const ObjectModels* models) {
    if(!enabled || !retail_arena)return;
    // Boss attempts restart at the arena entrance; completion is persistent.
    stage=RiptoStage::Foot;initialize(g,models);reset(g,models);
    g.player.position=g.player.previous_position=g.level.spawn;g.player.velocity={};g.powers.reset();
    g.player.motion=Motion::Fall;g.player.air={};g.restore_player_pose();
    if(g.guide_awards.boss){stage=RiptoStage::Defeated;health=0;engaged=true;action=RiptoAction::Waiting;
        for(const auto index:{actor,gulp,bird,hunter}){g.retail_objects[index].hidden=true;g.retail_objects[index].boss_body.reset();}}
}
const char* RiptoBoss::phase_name()const {
    if(action==RiptoAction::Finale)return "CROWN CORE - UNLEASH A FURY";
    switch(stage){case RiptoStage::Foot:return retail_arena?"RIPTO":"CYBORG RIPTO";case RiptoStage::MechaGulp:return "MECHA-GULP";case RiptoStage::Bird:return "RIPTO - FINAL FLIGHT";default:return "RIPTO DEFEATED";}
}
const char* RiptoBoss::action_name()const {
    switch(action){case RiptoAction::Waiting:return "Waiting";case RiptoAction::Seek:return "Pursuit";case RiptoAction::Cast:return "Scepter volley";
    case RiptoAction::Shockwaves:return "Jump the shockwaves";case RiptoAction::FireSpiral:return "Rotating fireballs";case RiptoAction::ShieldRush:return "Shield rush";
    case RiptoAction::Evade:return "Evading";case RiptoAction::Hit:return "Hit reaction";case RiptoAction::Transform:return "Transformation";
    case RiptoAction::GulpStomp:return "Mecha-Gulp charge";case RiptoAction::GulpLasers:return "Sweeping lasers";case RiptoAction::GulpFire:return "Fire stream";
    case RiptoAction::GulpFireTrain:return "Explosive fire train";case RiptoAction::BirdFlight:return "Flight";case RiptoAction::BirdVolley:return "Homing aerial volley";
    case RiptoAction::PowerUp:return "Power acquisition";case RiptoAction::ReturnCenter:return "Returning to centre";
    case RiptoAction::CyborgLance:return "Optic lance - dodge the red sightline";case RiptoAction::CyborgNova:return "Scepter nova - jump the pulses";
    case RiptoAction::GulpBombard:return "Crown bombardment - leave the marked circles";
    case RiptoAction::BirdDive:return "Sky talon - dodge the locked dive path";
    case RiptoAction::GulpPincer:return "Hammer and anvil - dodge sideways!";
    case RiptoAction::Finale:return "Crown core exposed - any Fury ends it";
    default:return "Defeated";}
}
void RiptoBoss::enter(Game& g,const ObjectModels& models,RiptoAction next) {
    if(action==RiptoAction::GulpPincer && next!=action && crush_action==CrushAction::Combo && crush_health) {
        crush_action=CrushAction::Recover;crush_ticks=0;clip(g.retail_objects[crush],models,0,true);
    }
    action=next;action_ticks=0;fired_=false;spell_shots_=0;beams_.clear();marks_.clear();dive_trail_.clear();
    auto& r=g.retail_objects[actor];auto& m=g.retail_objects[gulp];auto& b=g.retail_objects[bird];
    unsigned c=0;
    switch(next) {
    case RiptoAction::Cast:case RiptoAction::Shockwaves:case RiptoAction::BirdVolley:c=2;break;
    case RiptoAction::FireSpiral:c=5;break;
    case RiptoAction::ShieldRush:c=1;break;
    case RiptoAction::Seek:case RiptoAction::ReturnCenter:c=1;break;
    case RiptoAction::Evade:c=13;break;
    case RiptoAction::Hit:case RiptoAction::Finale:c=6;break;
    case RiptoAction::Transform:c=7;break;
    case RiptoAction::Death:c=7;break;
    case RiptoAction::PowerUp:case RiptoAction::CyborgLance:case RiptoAction::CyborgNova:case RiptoAction::GulpBombard:case RiptoAction::BirdDive:c=14;break;
    default:break;
    }
    clip(r,models,c,true);
    if(stage==RiptoStage::MechaGulp) {
        const unsigned mc=next==RiptoAction::Hit?3:(next==RiptoAction::GulpLasers || next==RiptoAction::GulpBombard)?7:next==RiptoAction::GulpFire?14:
            next==RiptoAction::GulpFireTrain?17:next==RiptoAction::GulpStomp?1:0;
        clip(m,models,mc,true);
    }
    if(stage==RiptoStage::Bird)clip(b,models,next==RiptoAction::Hit?2:0,true);
    aim_=g.player.position+retail::origin_above_feet;
    rush_direction_=normalized(horizontal(aim_-(stage==RiptoStage::MechaGulp?m.position:r.position)));
    if(next==RiptoAction::Cast || next==RiptoAction::BirdVolley || next==RiptoAction::Shockwaves)++attacks;
    if(next==RiptoAction::Shockwaves)notice(g,"Jump over Ripto's expanding shockwaves");
    if(next==RiptoAction::ShieldRush){notice(g,"Ripto's shield rush - dodge sideways");++attacks;++attack_counts[3];
        for(unsigned i=0;i<14 && shots_.size()<96;++i){shoot(g,models,RiptoShotKind::ShieldOrb,r.position,{},0);shots_.back().age=i*30;shots_.back().life=840;}}
    if(next==RiptoAction::FireSpiral){notice(g,"Ripto is unleashing rotating fireballs");++attacks;++attack_counts[2];}
    if(next==RiptoAction::GulpLasers || next==RiptoAction::GulpFire || next==RiptoAction::GulpFireTrain || next==RiptoAction::GulpStomp){++attacks;++attack_counts[4];notice(g,action_name());}
    if(next==RiptoAction::CyborgLance || next==RiptoAction::CyborgNova){++attacks;++attack_counts[next==RiptoAction::CyborgLance?6:7];notice(g,action_name());}
    if(next==RiptoAction::GulpBombard) {
        ++attacks;++attack_counts[8];notice(g,action_name(),240);
        const auto side=Vec3{rush_direction_.z,0,-rush_direction_.x};
        auto target=g.player.position+horizontal(g.player.velocity)*.3f;
        if(flat(target-center)>9)target=center+normalized(horizontal(target-center))*9;
        for(unsigned i=0;i<3;++i) {
            const auto p=target+side*(float(i)-1)*2.7f;
            const auto floor=g.level.raycast({p.x,center.y+15,p.z},{0,-1,0},24);
            bombard_targets_[i]=floor?floor->point+Vec3{0,.06f,0}:Vec3{p.x,center.y+.06f,p.z};
        }
    }
    if(next==RiptoAction::BirdDive) {
        ++attacks;++attack_counts[9];notice(g,action_name(),180);dive_start_=b.position;
    }
    if(next==RiptoAction::GulpPincer) {
        ++attacks;++attack_counts[10];++crush_attacks;notice(g,action_name(),240);
        auto& c=g.retail_objects[crush];crush_action=CrushAction::Combo;crush_ticks=0;
        clip(c,models,5,true);clip(m,models,7,true);crush_aim_=g.player.position;
    }
}
void RiptoBoss::shoot(Game& g,const ObjectModels& models,RiptoShotKind kind,Vec3 from,Vec3 direction,float speed) {
    if(shots_.size()>=96)return;
    RiptoShot s;s.kind=kind;s.velocity=normalized(direction)*speed;
    const unsigned type=kind==RiptoShotKind::Homing?0x2eb:(kind==RiptoShotKind::Fireball || kind==RiptoShotKind::Mortar)?0x289:
        kind==RiptoShotKind::GulpFlame?0x2da:kind==RiptoShotKind::FireTrain?0x2e6:kind==RiptoShotKind::ShieldOrb?0x2c3:0x2b7;
    s.visual=visual(models,type,from,kind==RiptoShotKind::ShieldOrb?26:64);
    s.visual.rotation[2]=retail::original_heading(std::atan2(direction.x,direction.z))/16;
    s.visual.previous_rotation=s.visual.rotation;
    s.life=kind==RiptoShotKind::Shockwave?160:kind==RiptoShotKind::Homing?360:kind==RiptoShotKind::FireTrain?420:240;
    s.radius=kind==RiptoShotKind::Shockwave?.25f:kind==RiptoShotKind::ShieldOrb?.20f:.24f;
    s.previous_radius=s.radius;shots_.push_back(s);
    if(kind==RiptoShotKind::Scepter){++attack_counts[0];g.sound_events.push_back({SoundScope::Object,0,0x2b7,from});}
    if(kind==RiptoShotKind::Homing){++attack_counts[5];g.sound_events.push_back({SoundScope::Object,0,0x2eb,from});}
    if(kind==RiptoShotKind::Shockwave)++attack_counts[1];
}
bool RiptoBoss::telegraphing()const {
    // The warning part of each attack (at least 30 ticks, 6.5.3).
    switch(action) {
    case RiptoAction::CyborgLance:return action_ticks<72;
    case RiptoAction::CyborgNova:return action_ticks<105;
    case RiptoAction::GulpLasers:return action_ticks<40;
    case RiptoAction::GulpStomp:return action_ticks<50;
    case RiptoAction::GulpBombard:return action_ticks<96;
    case RiptoAction::GulpPincer:case RiptoAction::BirdDive:return action_ticks<60;
    case RiptoAction::ShieldRush:return action_ticks<45;
    default:return false;
    }
}
void RiptoBoss::finale_fury(Game&,Element element) {
    if(crownworks() && action==RiptoAction::Finale && !finisher_)finisher_=element;
}
void RiptoBoss::take_hits(Game& g,const ObjectModels& models) {
    std::uint32_t flags=0;bool fury=false,chain=false,freeze=false;unsigned element=0;
    for(const auto index:{actor,gulp,bird}) {
        auto& o=g.retail_objects[index];
        if(!o.hidden){flags|=o.attack_flags;fury|=o.fury_hit;element=std::max(element,o.element_damage);chain|=o.element_chain;freeze|=index==gulp && o.element_freeze;}
        o.attack_flags=0;o.fury_hit=o.element_chain=o.element_freeze=false;o.element_damage=0;
        // Bosses are never frozen; Mecha-Gulp only by the Ice rule below.
        o.frozen_ticks=index==gulp?gulp_frozen:0;o.element_frozen=o.frozen_ticks>0;
    }
    const bool powered=(flags&0x540000)!=0;
    if(!retail_arena && stage==RiptoStage::MechaGulp && action!=RiptoAction::Transform) {
        // Ice freezes a mech for at most 1.5 s, once per 10 s (6.4).
        if(freeze && health && !gulp_frozen && !gulp_freeze_ready_) {
            gulp_frozen=mech_freeze;gulp_freeze_ready_=mech_freeze_cooldown;++mech_freezes;
            flashes_.push_back({g.retail_objects[gulp].position+Vec3{0,1.2f,0},0,1.6f,{.7f,.9f,1}});
        }
        // An Electric breath on one mech chains 1 HP to the other.
        if(chain && crush_health)chain_to_crush_=true;
        if(chain_to_gulp_){chain_to_gulp_=false;if(health && !damage_cooldown){flags|=flame_attack;element=std::max(element,1u);++chain_hits;}}
    }
    // The exposed crown core ignores ordinary damage: a Fury ends it
    // (finale_fury). Without any Fury to use, a powered hit does instead.
    if(action==RiptoAction::Finale) {
        if(powered && !finisher_) {
            const auto& e=g.upgrades.elements;bool any=false;
            for(unsigned k=0;k<element_count;++k)any|=g.upgrades.enabled && e.tier(static_cast<Element>(k))>=3;
            if(!any)finisher_=Element::Fire;
        }
        return;
    }
    if(!flags || (retail_arena && !powered?normal_cooldown_:damage_cooldown) || action==RiptoAction::Transform || stage==RiptoStage::Defeated || action==RiptoAction::Waiting)return;
    // Retail +0x60 uses 70 ticks for powered hits; +0x64 uses 120 for
    // ordinary flinches. Crownworks lets those ordinary hits chip HP as asked.
    unsigned damage=(flags&0x400000)?15:(flags&0x140000)?10:(flags&headbash_attack)?5:(flags&charge_attack)?2:1;
    if(retail_arena)damage=powered?10:0;
    // Crownworks reads elemental hits from the element table, not the masks.
    else if(element)damage=(flags&0x400000)?std::max(15u,element):element;
    // Crownworks relays (shield, repair, overdrive) act in the two-mech phase only.
    if(g.level.custom_arena && g.level.custom_arena->boss_shield && stage==RiptoStage::MechaGulp)damage=std::max(1u,(damage+1)/2);
    if(!retail_arena && action==RiptoAction::ShieldRush && damage<5)damage=1;
    // Elemental Furies deal at most 20 HP, and never more than a third of the
    // current phase's health, so no Fury ends a phase by itself.
    if(fury){const auto cap=std::min(20u,max_health/3);damage=std::min(damage,cap>fury_damage_?cap-fury_damage_:0u);fury_damage_+=damage;if(!damage)return;}
    health-=std::min(health,damage);++hits;if(damage)last_damage_=ticks;
    if(retail_arena && !powered)normal_cooldown_=120;else damage_cooldown=damage>=5?70:35;
    const auto& o=g.retail_objects[stage==RiptoStage::MechaGulp?gulp:stage==RiptoStage::Bird?bird:actor];
    flashes_.push_back({o.position+Vec3{0,.8f,0},0,.7f,{1,.8f,.2f}});
    g.sound_events.push_back({SoundScope::Object,powered?8u:14u,0x2af,o.position});
    if(!health){
        if(!retail_arena && stage==RiptoStage::MechaGulp && crush_health){
            auto& mount=g.retail_objects[gulp];mount.hidden=true;mount.boss_body.reset();g.retail_objects[actor].hidden=true;
            enter(g,models,RiptoAction::Waiting);notice(g,"Mecha-Gulp down - finish Mecha-Crush!");
        }else finish_phase(g,models);return;
    }
    knockback_=normalized(horizontal(o.position-g.player.position))*2.5f;
    // Chip damage still hurts, but cannot indefinitely cancel the custom
    // attack rotation. Heavy/powered hits always retain their interrupt.
    if(!retail_arena && damage<5 && stagger_ticks_)return;
    if(!retail_arena)stagger_ticks_=180;
    enter(g,models,RiptoAction::Hit);
    if(retail_arena && !powered)clip(g.retail_objects[actor],models,15,true);
}
void RiptoBoss::finish_phase(Game& g,const ObjectModels& models) {
    ++transitions;fury_damage_=crush_fury_damage_=0;shots_.clear();beams_.clear();orbs_.clear();marks_.clear();dive_trail_.clear();ripto_orbs=spyro_orbs=0;boss_power_ticks_=0;
    if(stage==RiptoStage::Bird && !retail_arena) {
        // Crownworks finale (6.3): the bird falls, Ripto clings to the exposed
        // crown core, and for 20 s any one Fury ends the fight.
        auto& r=g.retail_objects[actor];auto& b=g.retail_objects[bird];
        b.hidden=true;b.boss_body.reset();
        r.hidden=false;r.position=r.previous_position=center+Vec3{0,.02f,0};r.boss_body=BossBody{.38f,.35f,1.0f};
        enter(g,models,RiptoAction::Finale);finale_ticks=finale_window;health=0;finisher_.reset();
        flashes_.push_back({center+Vec3{0,1,0},0,3,ripto_tell::arena});
        notice(g,"The crown core is exposed - unleash a Fury!",300);
        g.sound_events.push_back({SoundScope::Global,28,0,center});
        return;
    }
    if(stage==RiptoStage::Bird){stage=RiptoStage::Defeated;enter(g,models,RiptoAction::Death);notice(g,"Ripto's power crystal is breaking!",240);return;}
    enter(g,models,RiptoAction::Transform);phase_deadline_=150;damage_cooldown=180;
    notice(g,stage==RiptoStage::Foot?"Ripto is summoning Mecha-Gulp!":retail_arena?"Ripto is taking flight - SUPERFLY and SUPERFLAME!":"Ripto is taking flight - jump, then jump again to superfly!",300);
    g.sound_events.push_back({SoundScope::Global,28,0,g.retail_objects[actor].position});
    start_cinematic(g,stage==RiptoStage::Foot?CrownCinematic::Summon:CrownCinematic::Flight);
}
void RiptoBoss::prepare_capture_stage(Game& g,const ObjectModels& models,RiptoStage requested) {
    if(!enabled || requested>RiptoStage::Bird)throw FormatError("Capture requires a playable Ripto phase");
    engaged=false;reset(g,&models);advance_to(g,models,requested);
}
void RiptoBoss::advance_to(Game& g,const ObjectModels& models,RiptoStage requested) {
    // Runs the real transformations (Foot -> mechs -> bird) to reach a phase,
    // without their cutscenes.
    struct Quiet {bool& flag;bool kept;~Quiet(){flag=kept;}} quiet{quiet_,quiet_};quiet_=true;
    engaged=true;enter(g,models,RiptoAction::Seek);
    while(stage<requested) {
        finish_phase(g,models);
        for(unsigned i=0;i<180 && action==RiptoAction::Transform;++i) {
            tick_object_animations(g.retail_objects,models);tick(g,&models);
        }
        if(action==RiptoAction::Transform)throw FormatError("Ripto phase transformation did not finish");
    }
    g.sound_events.clear();
}
void RiptoBoss::attach_rider(Game& g,const ObjectModels& models) {
    if(stage==RiptoStage::Foot || action==RiptoAction::Finale || (stage==RiptoStage::Defeated && action!=RiptoAction::Death))return;
    if(stage==RiptoStage::Defeated && g.retail_objects[bird].hidden)return;   // the Crownworks finale: he fell with the core
    auto& r=g.retail_objects[actor];const auto& m=g.retail_objects[stage==RiptoStage::MechaGulp?gulp:bird];
    const auto& model=models.models.at(m.type);
    // Overlay 70: Ripto takes the mean of Mecha-Gulp attachments 14/15,
    // or attachment 0 of the bird. Keep the original animated mount sockets.
    const auto attachment=stage==RiptoStage::MechaGulp?
        (sample_object_attachment(model,m.animation,models.animation_blends,14)+sample_object_attachment(model,m.animation,models.animation_blends,15))*.5f:
        sample_object_attachment(model,m.animation,models.animation_blends,0);
    r.position=m.position+object_basis(m)*(attachment*object_model_scale(m));r.rotation=m.rotation;
}
void RiptoBoss::tick_shots(Game& g,const ObjectModels& models) {
    const auto origin=g.player.position+retail::origin_above_feet;
    for(auto& s:shots_) {
        auto& o=s.visual;o.previous_animation=o.animation;o.previous_position=o.position;o.previous_rotation=o.rotation;
        if(o.animation.active)tick_object_animation(models.models.at(o.type),o.animation);
        s.previous_radius=s.radius;++s.age;
        if(s.kind==RiptoShotKind::ShieldOrb) {
            if(action!=RiptoAction::ShieldRush){s.spent=true;continue;}
            const auto a=s.age*.06f;o.position=g.retail_objects[actor].position+Vec3{std::cos(a)*.9f,.75f+std::sin(a*2)*.15f,std::sin(a)*.9f};
        } else if(s.kind==RiptoShotKind::Shockwave) {
            // 8007be8c: +400 original units per two VBlanks, 450/550
            // radial/vertical hit tolerances. Floors/cover occlude the hit.
            s.radius+=400*30*unit*dt;const auto d=origin-o.position;
            const bool crossing=flat(d)>=s.previous_radius-.44f && flat(d)<=s.radius+.44f && std::abs(d.y)<550*unit;
            if(crossing && clear(g,o.position+Vec3{0,.18f,0},origin) && g.hurt(o.position))++damage_to_spyro;
        } else {
            if(s.kind==RiptoShotKind::Homing) {
                // 8007a634 caps homing pitch/yaw steps at 4/24 Q12 units.
                const auto goal=origin-o.position;const float yaw=std::atan2(s.velocity.x,s.velocity.z),pitch=std::atan2(s.velocity.y,flat(s.velocity));
                const float y=yaw+std::clamp(std::remainder(std::atan2(goal.x,goal.z)-yaw,2*pi),-12*retail::angle_to_radians,12*retail::angle_to_radians);
                const float p=pitch+std::clamp(std::atan2(goal.y,flat(goal))-pitch,-2*retail::angle_to_radians,2*retail::angle_to_radians);
                const auto speed=length(s.velocity);s.velocity=Vec3{std::sin(y)*std::cos(p),std::sin(p),std::cos(y)*std::cos(p)}*speed;
            }
            if(s.kind==RiptoShotKind::FireTrain) {
                const auto target=normalized(horizontal(origin-o.position));
                s.velocity=normalized(lerp(normalized(horizontal(s.velocity)),target,.025f))*7.3f;
                if(const auto floor=g.level.raycast(o.position+Vec3{0,.5f,0},{0,-1,0},5))o.position.y=std::lerp(o.position.y,floor->point.y+.55f,.2f);
                if(s.age>75) {
                    const auto& mount=g.retail_objects[gulp];
                    if(container_contact(mount,models,o.position,.45f)) {g.retail_objects[gulp].attack_flags|=0x100000;s.spent=true;}
                }
            }
            if(s.kind==RiptoShotKind::Mortar)s.velocity.y-=18*dt;
            const auto step=s.velocity*dt;const auto wall=g.level.raycast(o.position,step,length(step)+.02f);
            if(wall && wall->distance<=length(step)+.02f){o.position=wall->point;s.spent=true;}
            else o.position+=step;
            o.rotation[2]=retail::original_heading(std::atan2(s.velocity.x,s.velocity.z))/16;
            o.rotation[1]=std::atan2(s.velocity.y,flat(s.velocity))/(16*retail::angle_to_radians);
        }
        // Fire goes out under water (plan 6.4): Gulp's flame and fire train,
        // fireballs and mortars hiss out at the Crownworks terrace flood's
        // waterline, with a puff of steam instead of a blast.
        if(!s.spent && (s.kind==RiptoShotKind::GulpFlame || s.kind==RiptoShotKind::FireTrain || s.kind==RiptoShotKind::Fireball || s.kind==RiptoShotKind::Mortar))
            if(const auto water=g.level.water_at(o.position);water && !g.level.dynamic_water.empty() && water->height>o.position.y) {
                s.spent=true;s.doused=true;++doused;
                flashes_.push_back({Vec3{o.position.x,water->height+.05f,o.position.z},0,s.kind==RiptoShotKind::FireTrain?1.1f:.5f,{.85f,.9f,.95f}});
                g.sound_events.push_back({SoundScope::Global,7,0,o.position});
                s.trail.clear();continue;
            }
        if(s.kind!=RiptoShotKind::Shockwave && segment_distance(origin,o.previous_position,o.position)<s.radius+g.tuning.radius &&
           clear(g,o.previous_position,origin)) {
            if(g.hurt(o.previous_position))++damage_to_spyro;
            if(s.kind!=RiptoShotKind::ShieldOrb)s.spent=true;
        }
        if(s.kind==RiptoShotKind::FireTrain)for(unsigned i=20;i<s.trail.size();i+=20) {
            if(length(origin-s.trail[i])<.6f && clear(g,s.trail[i],origin) && g.hurt(s.trail[i])){++damage_to_spyro;s.spent=true;}
            if(s.age>90 && container_contact(g.retail_objects[gulp],models,s.trail[i],.4f)){g.retail_objects[gulp].attack_flags|=0x100000;s.spent=true;}
        }
        if(s.spent && (s.kind==RiptoShotKind::FireTrain || s.kind==RiptoShotKind::Mortar) && length(origin-o.position)<1.5f && clear(g,o.position+Vec3{0,.08f,0},origin) && g.hurt(o.position))++damage_to_spyro;
        if(s.spent) {
            flashes_.push_back({o.position,0,(s.kind==RiptoShotKind::FireTrain || s.kind==RiptoShotKind::Mortar)?1.5f:.38f,{1,.3f,.04f}});
            g.sound_events.push_back({SoundScope::Global,28,0,o.position});
        }
        s.trail.push_front(o.position);while(s.trail.size()>(s.kind==RiptoShotKind::FireTrain?121u:22u))s.trail.pop_back();
    }
    std::erase_if(shots_,[](const auto& s){return s.spent || s.age>=s.life;});
    for(auto& f:flashes_)++f.age;std::erase_if(flashes_,[](const auto& f){return f.age>=24;});
    if(flashes_.size()>96)flashes_.erase(flashes_.begin(),flashes_.end()-96);
}
void RiptoBoss::tick_orbs(Game& g,const ObjectModels& models) {
    if(stage==RiptoStage::Bird || stage==RiptoStage::Defeated || action==RiptoAction::Transform)return;
    auto& h=g.retail_objects[hunter];begin_object_transform(h,1);
    if(retail_arena) {
        const auto& route=g.level.ripto_arena->hunter_route;const auto target=route[hunter_waypoint_]+Vec3{0,3,0};
        const auto delta=target-h.position;h.position+=normalized(delta)*std::min(length(delta),5.f*dt);face(h,target);
        if(length(delta)<.3f)hunter_waypoint_=(hunter_waypoint_+1)%route.size();
    } else {h.position=center+Vec3{std::sin(ticks*.004f)*11,7+std::sin(ticks*.01f)*.8f,-13};face(h,center);}
    // Hunter limits the supply to three, then replenishes it more quickly
    // when the arena is empty (80075520). Timers are VBlanks, not 30 Hz frames.
    if(++orb_clock_>=(retail_arena?(orbs_.empty()?90u:300u):110u) && orbs_.size()<(retail_arena?3u:6u)) {
        orb_clock_=0;clip(h,models,1,true);
        const float a=(orb_color_%9)*2*pi/9;Vec3 landing=center+Vec3{std::sin(a)*(stage==RiptoStage::MechaGulp?8.f:5.5f),.15f,std::cos(a)*(stage==RiptoStage::MechaGulp?8.f:5.5f)};
        RiptoOrb orb;orb.color=orb_color_++%3;orb.visual=visual(models,0x2b9,h.position,32);
        orb.visual.material_color=orb.color==0?1:orb.color==1?2:8;
        constexpr float gravity=20*30*30*unit,flight_time=1.25f;
        orb.velocity=(landing-h.position)/flight_time+Vec3{0,.5f*gravity*flight_time,0};orbs_.push_back(orb);
    }
    for(auto& o:orbs_) {
        ++o.age;o.visual.previous_position=o.visual.position;o.visual.previous_rotation=o.visual.rotation;o.visual.rotation[2]+=1;
        if(!o.resting) {
            o.velocity.y-=20*30*30*unit*dt;const auto delta=o.velocity*dt;
            const auto hit=g.level.raycast(o.visual.position+Vec3{0,.15f,0},delta,length(delta)+.3f);
            if(hit && hit->normal.y>.6f && o.velocity.y<0 && o.visual.position.y+delta.y<=hit->point.y+150*unit) {
                o.visual.position=hit->point+Vec3{0,150*unit,0};
                if(std::abs(o.velocity.y)<24*30*unit){o.resting=true;o.velocity={};}
                else {o.velocity.y=-o.velocity.y*5/8;o.velocity.x*=.7f;o.velocity.z*=.7f;}
            } else o.visual.position+=delta;
        }
        if(!o.resting)continue;
        const auto origin=g.player.position+retail::origin_above_feet;
        if(!player_power_ticks && flat(origin-o.visual.position)<1000*unit && std::abs(origin.y-o.visual.position.y)<700*unit && clear(g,origin,o.visual.position)) {
            ++spyro_orbs;player_power=o.color;o.age=1800;g.sound_events.push_back({SoundScope::Object,0,0x2b9,o.visual.position});
            if(spyro_orbs==3) {
                player_power=o.color;player_power_ticks=power_duration();
                if(player_power==0)g.powers.fire={player_power_ticks,player_power_ticks};
                notice(g,player_power==0?"Three orbs - SUPERFLAME!":player_power==1?"Three orbs - GREEN FIRE BOMBS!":"Three orbs - SUPERCHARGE!",240);
            }
        } else if(health && !boss_power_ticks_ && o.age>60) {
            const auto& body=g.retail_objects[stage==RiptoStage::MechaGulp?gulp:actor];
            if(flat(body.position-o.visual.position)<(stage==RiptoStage::MechaGulp?1.6f:800*unit) && std::abs(body.position.y-o.visual.position.y)<1) {
                ++ripto_orbs;boss_power_=o.color;o.age=1800;
                if(ripto_orbs==3){boss_power_ticks_=900;enter(g,models,RiptoAction::PowerUp);notice(g,"Ripto collected three power orbs!",150);}
            }
        }
    }
    std::erase_if(orbs_,[&](const auto& o){return o.age>=1800 || o.visual.position.y<center.y-5;});
}
void RiptoBoss::tick(Game& g,const ObjectModels* mp) {
    if(!enabled || !mp || g.dialogue.active() || g.portal_travel.active() || (g.scene_locked && cinematic==CrownCinematic::None))return;
    const auto& models=*mp;difficulty=g.cheats.boss_difficulty;
    if(actor>=g.retail_objects.size() || gulp>=g.retail_objects.size() || bird>=g.retail_objects.size())throw FormatError("Ripto encounter lost an actor");
    auto& r=g.retail_objects[actor];auto& m=g.retail_objects[gulp];auto& b=g.retail_objects[bird];
    ticking_=true;
    for(const auto index:{actor,gulp,bird})begin_object_transform(g.retail_objects[index],1);
    tick_light(g);
    // A cutscene holds the arena; only a transformation or the death sequence
    // it is showing keeps running.
    if(tick_cinematic(g,models) && action!=RiptoAction::Transform && action!=RiptoAction::Death) {
        attach_rider(g,models);ticking_=false;if(reset_pending_)reset(g,mp);return;
    }
    // Assist difficulty stretches every attack's warning by half (6.5.6).
    ++ticks;if(!(difficulty==BossDifficulty::Assist && !retail_arena && telegraphing() && ticks%3==0))++action_ticks;
    if(damage_cooldown)--damage_cooldown;if(normal_cooldown_)--normal_cooldown_;if(stagger_ticks_)--stagger_ticks_;
    if(gulp_freeze_ready_)--gulp_freeze_ready_;if(crush_freeze_ready_)--crush_freeze_ready_;
    marks_.clear();previous_effect_origin=effect_origin;previous_effect_charge=effect_charge;effect_charge=0;
    if(player_power_ticks && !--player_power_ticks)spyro_orbs=0;
    if(boss_power_ticks_ && !--boss_power_ticks_)ripto_orbs=0;
    if(!engaged && flat(g.player.position-center)<(retail_arena?20.f:32.f) && stage!=RiptoStage::Defeated){engaged=true;
        start_cinematic(g,CrownCinematic::Intro);   // the Crownworks intro, when enabled; it enters the fight when it ends
        if(cinematic==CrownCinematic::Intro)enter(g,models,RiptoAction::Waiting);
        else {enter(g,models,RiptoAction::Seek);notice(g,retail_arena?"RIPTO - Collect three of Hunter's orbs to power up!":"CYBORG RIPTO - Collect Hunter's orbs. Break relays to weaken him.",300);}}
    take_hits(g,models);
    if(!engaged){ticking_=false;return;}
    if((hud_ticks++&1)==0) {
        if(!hud_spyro_blink || !--hud_spyro_blink)hud_spyro_blink=(hud_random_.next()&63)+36;
        if((hud_ticks/2)%5==0)hud_ripto_blink=hud_ripto_blink?0:unsigned((hud_random_.next()&15)==0);
    }
    const auto* arena=g.level.custom_arena && stage==RiptoStage::MechaGulp?&*g.level.custom_arena:nullptr;   // relays: phase 2 only
    // Hard difficulty keeps the overdrive relay's faster attacks in every phase.
    const bool overdrive=(arena && arena->boss_overdrive) || (!retail_arena && difficulty==BossDifficulty::Hard);
    if(arena && arena->boss_repair && health && health<max_health && ticks>last_damage_+600 && ticks%180==0 && action!=RiptoAction::Transform)++health;
    if(stage==RiptoStage::Bird) {
        g.powers.flight={600,600};g.powers.fire={600,600};
    }
    const auto origin=g.player.position+retail::origin_above_feet;
    beams_.clear();
    // The custom dive owns its path; ordinary casts continue along the route.
    if(stage==RiptoStage::Bird && action!=RiptoAction::BirdDive && action!=RiptoAction::Finale) {
        Vec3 desired;
        if(retail_arena){const auto& route=g.level.ripto_arena->bird_route;desired=route[bird_waypoint_];if(length(desired-b.position)<1)bird_waypoint_=(bird_waypoint_+1)%route.size();}
        else {const float t=ticks*.007f;desired=center+Vec3{std::sin(t)*9,4+std::sin(t*.7f)*1.2f,std::cos(t)*9};}
        const auto delta=desired-b.position;b.position+=normalized(delta)*std::min(length(delta),300*30*unit*dt);face(b,desired,1.5f);
        b.rotation[1]=std::clamp(std::atan2(delta.y,flat(delta))/(16*retail::angle_to_radians),-12.f,12.f);
        attach_rider(g,models);
    }
    // A frozen Mecha-Gulp holds still, and Ripto with him.
    const bool held=stage==RiptoStage::MechaGulp && gulp_frozen && action!=RiptoAction::Transform;
    if(held){--gulp_frozen;if(action_ticks)--action_ticks;}
    if(!held)switch(action) {
    case RiptoAction::Waiting:
        if(retail_arena && defeated() && action_ticks>=240 && g.portal_travel.allowed) {
            auto& travel=g.portal_travel;travel.request={57,g.level.campaign_scene};travel.start=g.level.spawn;travel.yaw=g.level.spawn_yaw;travel.fade=1;travel.phase=PortalTravel::Phase::Loading;
        }break;
    case RiptoAction::Seek: {
        auto& body=stage==RiptoStage::MechaGulp?m:r;
        Vec3 destination=g.player.position;bool seek_orb=false;float nearest=100;
        if(!boss_power_ticks_)for(const auto& o:orbs_)if(o.resting && o.age>60) {const auto dist=flat(o.visual.position-body.position);if(dist<nearest){nearest=dist;destination=o.visual.position;seek_orb=true;}}
        if(!seek_orb){const auto away=normalized(horizontal(body.position-g.player.position));destination=g.player.position+away*(stage==RiptoStage::MechaGulp?5.f:5.8f);}
        const bool moving=ground_move(g,body,destination,(seek_orb?250:210)*30*unit);
        face(body,g.player.position);clip(body,models,moving?1:0);
        if(action_ticks>=(overdrive?80u:140u) && crush_action!=CrushAction::Slam && crush_action!=CrushAction::Blast && clear(g,body.position+Vec3{0,1,0},origin)) {
            const auto n=sequence_++;
            if(stage==RiptoStage::MechaGulp) {
                const RiptoAction moves[]{RiptoAction::Cast,RiptoAction::GulpStomp,RiptoAction::GulpLasers,RiptoAction::GulpFire,RiptoAction::GulpFireTrain,RiptoAction::GulpBombard};
                if(!retail_arena && crush_health && n%7==6)enter(g,models,RiptoAction::GulpPincer);
                else enter(g,models,moves[boss_power_ticks_?2+(boss_power_%3):retail_arena?1:n%7%6]);
            } else {
                const RiptoAction moves[]{RiptoAction::Cast,RiptoAction::Shockwaves,RiptoAction::FireSpiral,RiptoAction::ShieldRush};
                if(!retail_arena && n%6>=4)enter(g,models,n%6==4?RiptoAction::CyborgLance:RiptoAction::CyborgNova);
                else enter(g,models,moves[boss_power_ticks_?1+boss_power_:retail_arena?0:n%6]);
            }
        }break;
    }
    case RiptoAction::PowerUp:
        if(animation_end(r) || action_ticks>=75) {
            if(stage==RiptoStage::MechaGulp){const RiptoAction moves[]{RiptoAction::GulpLasers,RiptoAction::GulpFire,RiptoAction::GulpFireTrain};enter(g,models,moves[boss_power_]);}
            else {const RiptoAction moves[]{RiptoAction::Shockwaves,RiptoAction::FireSpiral,RiptoAction::ShieldRush};powered_action_=moves[boss_power_];enter(g,models,powered_action_==RiptoAction::ShieldRush?powered_action_:RiptoAction::ReturnCenter);}
        }break;
    case RiptoAction::ReturnCenter:
        ground_move(g,r,center,250*30*unit);
        if(flat(r.position-center)<.95f || action_ticks>=240)enter(g,models,powered_action_);break;
    case RiptoAction::Cast:case RiptoAction::BirdVolley:case RiptoAction::Shockwaves: {
        face(r,g.player.position);const auto t=animation_time(r,models);
        if(!fired_ && t>50 && r.animation.next_frame) {
            fired_=true;const auto from=staff(r,models);aim_=origin;
            if(action==RiptoAction::Shockwaves)shoot(g,models,RiptoShotKind::Shockwave,{r.position.x,center.y+.10f,r.position.z},{},0);
            else shoot(g,models,action==RiptoAction::BirdVolley?RiptoShotKind::Homing:RiptoShotKind::Scepter,from,aim_-from,500*30*unit);
        }
        if(animation_end(r) || action_ticks>300) {
            if(++spell_shots_<(action==RiptoAction::Shockwaves?3u:2u)){fired_=false;clip(r,models,2,true);}
            else {boss_power_ticks_=ripto_orbs=0;enter(g,models,stage==RiptoStage::Bird?RiptoAction::BirdFlight:RiptoAction::Seek);}
        }break;
    }
    case RiptoAction::FireSpiral: {
        r.rotation[2]+=3.5f;
        if(action_ticks%4==0)shoot(g,models,RiptoShotKind::Fireball,r.position+forward(r)*.88f+Vec3{0,.65f,0},forward(r),440*30*unit);
        if(action_ticks>=172){boss_power_ticks_=ripto_orbs=0;enter(g,models,RiptoAction::Seek);}break;
    }
    case RiptoAction::ShieldRush: {
        if(action_ticks<=45){face(r,g.player.position);rush_direction_=normalized(horizontal(g.player.position-r.position));}
        else {ground_move(g,r,r.position+rush_direction_*5,270*30*unit);
            if(length(origin-(r.position+Vec3{0,.45f,0}))<1.2f && g.hurt(r.position))++damage_to_spyro;}
        if(retail_arena && action_ticks>45){face(r,g.player.position,.75f);rush_direction_=forward(r);}
        if(action_ticks>=(retail_arena?420u:150u)){boss_power_ticks_=ripto_orbs=0;enter(g,models,RiptoAction::Evade);}break;
    }
    case RiptoAction::Evade: {
        ground_move(g,r,r.position+normalized(horizontal(r.position-g.player.position))*4,190*30*unit);
        if(action_ticks>=90)enter(g,models,RiptoAction::Seek);break;
    }
    case RiptoAction::Hit: {
        if(stage==RiptoStage::Foot)ground_move(g,r,r.position+knockback_,length(knockback_));
        if(animation_end(r) || action_ticks>64)enter(g,models,stage==RiptoStage::Bird?RiptoAction::BirdFlight:RiptoAction::Seek);break;
    }
    case RiptoAction::Transform: {
        if(action_ticks==30)flashes_.push_back({r.position+Vec3{0,1,0},0,3,{.8f,.2f,1}});
        if(action_ticks>=phase_deadline_) {
            health=max_health=80;last_damage_=ticks;r.boss_body.reset();player_power_ticks=spyro_orbs=0;
            if(!retail_arena && difficulty==BossDifficulty::Assist)g.player.health=std::max(g.player.health,3u);   // assist: full Sparx each phase
            if(stage==RiptoStage::Foot){stage=RiptoStage::MechaGulp;m.hidden=false;m.boss_body=BossBody{1.12f,.75f,1.4f};m.position=m.previous_position=retail_arena?g.level.ripto_arena->gulp_spawn:center+Vec3{0,.02f,0};clip(m,models,0,true);enter(g,models,RiptoAction::Seek);}
            else {stage=RiptoStage::Bird;aerial_sequence_=0;r.hidden=false;m.hidden=true;m.boss_body.reset();b.hidden=false;b.boss_body=BossBody{.8f,-.12f,.38f};b.position=b.previous_position=retail_arena?g.level.ripto_arena->bird_spawn:center+Vec3{0,4,0};
                g.retail_objects[hunter].hidden=true;
                if(retail_arena){g.player.position=g.player.previous_position=g.level.ripto_arena->flight_spawn;g.player.yaw=g.player.previous_yaw=std::atan2(center.x-g.player.position.x,center.z-g.player.position.z);g.player.velocity={};g.player.grounded=false;g.player.air={};g.player.jump={};
                    g.powers.flight={600,600};g.powers.fire={600,600};auto flight=g.powers.checkpoint();flight.flying=true;flight.takeoff=true;g.powers.restore_checkpoint(flight);g.player.motion=Motion::Superfly;g.restore_player_pose();g.camera.reset(g.level,g.player.position,g.player.yaw);}
                enter(g,models,RiptoAction::BirdFlight);}
            if(!retail_arena && crush<g.retail_objects.size()) {
                auto& c=g.retail_objects[crush];c.hidden=stage!=RiptoStage::MechaGulp;c.boss_body.reset();
                if(c.hidden){crush_health=0;crush_action=CrushAction::Dormant;}
                if(!c.hidden){crush_health=60;crush_ticks=crush_cooldown_=crush_sequence_=0;crush_action=CrushAction::Approach;
                    c.position=c.previous_position=center+Vec3{5,.02f,3};c.rotation=c.previous_rotation={0,0,128};c.boss_body=BossBody{.85f,.45f,1.8f};clip(c,models,3,true);}
            }
            clip(r,models,0,true);damage_cooldown=90;
        }break;
    }
    case RiptoAction::GulpStomp: {
        if(action_ticks<50){face(m,g.player.position);rush_direction_=normalized(horizontal(g.player.position-m.position));}
        else ground_move(g,m,m.position+rush_direction_*5,300*30*unit);
        if(length(origin-(m.position+Vec3{0,.7f,0}))<1.8f && action_ticks>=50 && g.hurt(m.position))++damage_to_spyro;
        if(action_ticks==115){flashes_.push_back({m.position,0,2,{1,.65f,.2f}});g.camera.shake(2500,18);}
        if(action_ticks>=145)enter(g,models,RiptoAction::Seek);break;
    }
    case RiptoAction::GulpLasers: {
        face(m,g.player.position,1.125f);const bool warning=action_ticks<40 || action_ticks>200;
        // Retail clips 7/8/9 and 10/11/12 are the two laser sweep directions.
        if(action_ticks==40)clip(m,models,8);if(action_ticks==105)clip(m,models,9);
        if(action_ticks==120)clip(m,models,10);if(action_ticks==145)clip(m,models,11);if(action_ticks==200)clip(m,models,12);
        const auto yaw=retail::native_heading(m.rotation[2]*16)+std::sin((float(action_ticks)-40)*pi/80)*225*retail::angle_to_radians;
        for(unsigned side=0;side<2;++side) {
            // 80079f0c/20: gun mouths are the means of 16/17 and 18/19.
            const auto from=gulp_socket(m,models,16+side*2);
            const float distance=std::max(3.f,flat(origin-from));
            auto direction=normalized(Vec3{std::sin(yaw),(origin.y-from.y)/distance,std::cos(yaw)});auto to=from+direction*35;
            if(const auto hit=g.level.raycast(from,direction,35))to=hit->point;
            beams_.push_back({from,to,warning?.025f:.15f,warning,ripto_tell::mech});
            if(!warning && segment_distance(origin,from,to)<.39f && g.hurt(from))++damage_to_spyro;
        }
        if(action_ticks>=240){boss_power_ticks_=ripto_orbs=0;enter(g,models,RiptoAction::Seek);}break;
    }
    case RiptoAction::GulpFire: {
        face(m,g.player.position,.7f);
        if(action_ticks==50)clip(m,models,15);if(action_ticks==165)clip(m,models,16);
        if(action_ticks>=55 && action_ticks<165 && action_ticks%8==0) {
            // The original alternating flame emitters are pairs 20/21 and 22/23.
            const auto from=gulp_socket(m,models,20+(action_ticks/8%2)*2);auto direction=forward(m);
            direction.y=(origin.y-from.y)/std::max(3.f,flat(origin-from));shoot(g,models,RiptoShotKind::GulpFlame,from,direction,13);
        }
        if(action_ticks>=210){boss_power_ticks_=ripto_orbs=0;enter(g,models,RiptoAction::Seek);}break;
    }
    case RiptoAction::GulpFireTrain: {
        face(m,g.player.position);
        // 8007e2c0/8007e7ec: a pursuing head and six following bodies.
        // Luring the chain back into Mecha-Gulp applies his powered hit mask.
        if(!fired_ && animation_time(m,models)>54) {
            fired_=true;const auto from=gulp_socket(m,models,12);
            shoot(g,models,RiptoShotKind::FireTrain,from,origin-from,7.3f);
            notice(g,"Lure the fire train back into Mecha-Gulp!",240);
        }
        if(action_ticks>=180){boss_power_ticks_=ripto_orbs=0;enter(g,models,RiptoAction::Seek);}break;
    }
    case RiptoAction::BirdFlight: {
        // The authored route passes beyond the arena rim. Terrain must
        // collide with the bolts, not indefinitely suppress the cast loop.
        if(action_ticks>=100)enter(g,models,!retail_arena && ++aerial_sequence_%2==0?RiptoAction::BirdDive:RiptoAction::BirdVolley);break;
    }
    case RiptoAction::CyborgLance: {
        // Authored PC choreography: brace/power pose, held aim, recoil and
        // recovery. The damaging beam follows the same posed eye as rendering.
        if(action_ticks<60){face(r,g.player.position);aim_=origin;}
        if(action_ticks==60)clip(r,models,2,true);
        auto p=sample_object_animation(models.models.at(r.type),r.animation,models.animation_blends);pose(p,1);
        const auto eye=ripto_eye_socket(p.vertices);const auto from=r.position+object_basis(r)*(eye.center*object_model_scale(r));
        const auto direction=normalized(aim_-from);auto to=from+direction*30;
        if(const auto wall=g.level.raycast(from,direction,30))to=wall->point;
        if(action_ticks<105) {
            const bool warning=action_ticks<72;beams_.push_back({from,to,warning?.016f:.12f,warning});
            if(!warning && segment_distance(origin,from,to)<.12f+g.tuning.radius && g.hurt(from))++damage_to_spyro;
        }
        if(action_ticks==72){g.sound_events.push_back({SoundScope::Object,0,0x2eb,from});flashes_.push_back({from,0,.45f,{1,.04f,.02f}});}
        if(action_ticks==105)clip(r,models,13,true);
        if(action_ticks>=155)enter(g,models,RiptoAction::Seek);break;
    }
    case RiptoAction::CyborgNova: {
        if(action_ticks<45)face(r,g.player.position);
        if(action_ticks==45)clip(r,models,5,true);
        if(action_ticks>=45 && action_ticks<105)r.rotation[2]+=4.25f;
        if(action_ticks==105){clip(r,models,2,true);g.camera.shake(2200,18);g.sound_events.push_back({SoundScope::Global,28,0,r.position});}
        if(action_ticks==105 || action_ticks==137 || action_ticks==169) {
            shoot(g,models,RiptoShotKind::Shockwave,{r.position.x,center.y+.1f,r.position.z},{},0);
            flashes_.push_back({r.position+Vec3{0,.1f,0},0,2.2f,{.8f,.13f,1}});
        }
        if(action_ticks>=210)enter(g,models,RiptoAction::Seek);break;
    }
    case RiptoAction::GulpBombard: {
        if(action_ticks<50)face(m,bombard_targets_[1],.8f);
        if(action_ticks==50)clip(m,models,8);
        if(action_ticks==112)clip(m,models,9);
        for(unsigned i=0;i<3;++i) {
            const unsigned launch=60+i*18;
            if(action_ticks<=launch)marks_.push_back({bombard_targets_[i],1.5f,std::min(1.f,float(action_ticks)/launch)});
            if(action_ticks==launch && shots_.size()<96) {
                const auto from=gulp_socket(m,models,16+(i%2)*2),target=bombard_targets_[i];
                constexpr float flight=1.05f;
                const Vec3 velocity=(target-from)/flight+Vec3{0,9*(flight+dt),0};
                shoot(g,models,RiptoShotKind::Mortar,from,velocity,length(velocity));
                shots_.back().impact=target;shots_.back().life=120;
                flashes_.push_back({from,0,.5f,{1,.45f,.04f}});g.sound_events.push_back({SoundScope::Object,0,0x2b7,from});
            }
        }
        if(action_ticks>=205)enter(g,models,RiptoAction::Seek);break;
    }
    case RiptoAction::GulpPincer: {
        auto& c=g.retail_objects[crush];
        if(!crush_health || !health){if(crush_health){crush_action=CrushAction::Recover;crush_ticks=0;clip(c,models,0,true);}enter(g,models,health?RiptoAction::Seek:RiptoAction::Waiting);break;}
        if(action_ticks<=45){crush_aim_=g.player.position;face(c,crush_aim_,1.5f);face(m,crush_aim_,1.5f);rush_direction_=normalized(horizontal(crush_aim_-c.position));}
        const auto side=Vec3{rush_direction_.z,0,-rush_direction_.x};
        if(action_ticks<110)for(float sign:{-1.f,1.f}) {
            const auto start=crush_aim_+side*(sign*1.8f)-rush_direction_*5+Vec3{0,.25f,0},end=start+rush_direction_*10;
            beams_.push_back({start,end,.026f,true,ripto_tell::mech});
        }
        if(action_ticks==60){clip(c,models,9,true);clip(m,models,8,true);g.sound_events.push_back({SoundScope::Object,0,0x8f,c.position});}
        if(action_ticks>=60 && action_ticks<110) {
            ground_move(g,c,c.position+rush_direction_*5,6.4f);
            if(segment_distance(origin,c.previous_position+Vec3{0,.6f,0},c.position+Vec3{0,.6f,0})<1.05f && g.hurt(c.position)){++crush_damage;++damage_to_spyro;}
        }
        if(action_ticks==82)for(float sign:{-1.f,1.f}) {
            const auto from=gulp_socket(m,models,sign<0?16:18),target=crush_aim_+side*(sign*1.8f)+Vec3{0,.4f,0};
            shoot(g,models,RiptoShotKind::Scepter,from,target-from,10);
        }
        if(action_ticks==110){clip(c,models,7,true);clip(m,models,9,true);}
        const auto impact=c.position+forward(c)*1.6f+Vec3{0,.1f,0};
        if(action_ticks>=110 && action_ticks<160)marks_.push_back({impact,2.1f,(action_ticks-110)/50.f});
        if(action_ticks==160){shoot(g,models,RiptoShotKind::Shockwave,{impact.x,center.y+.1f,impact.z},{},0);shots_.back().color={.12f,.55f,1};
            flashes_.push_back({impact,0,2.1f,{.2f,.6f,1}});g.camera.shake(2600,22);g.sound_events.push_back({SoundScope::Object,1,0x8f,impact});}
        if(action_ticks>=240){crush_action=CrushAction::Recover;crush_ticks=0;clip(c,models,0,true);enter(g,models,RiptoAction::Seek);}break;
    }
    case RiptoAction::BirdDive: {
        const auto peak=dive_start_+Vec3{0,3,0};
        if(action_ticks<=60) {
            b.position=lerp(dive_start_,peak,smooth(action_ticks/60.f));
            if(action_ticks<=30) {
                aim_=origin+g.player.velocity*.2f;
                if(flat(aim_-center)>9){const auto d=normalized(horizontal(aim_-center))*9;aim_.x=center.x+d.x;aim_.z=center.z+d.z;}
                const auto floor=g.level.raycast({aim_.x,center.y+16,aim_.z},{0,-1,0},24);
                aim_.y=std::max(std::clamp(aim_.y,center.y+.95f,center.y+8),floor?floor->point.y+.95f:center.y+.95f);
                rush_direction_=normalized(horizontal(aim_-peak));if(flat(rush_direction_)<.01f)rush_direction_=forward(b);
                dive_end_=aim_+rush_direction_*4+Vec3{0,4,0};
            }
            face(b,aim_,1.5f);b.rotation[1]=std::lerp(b.rotation[1],8.f,.1f);
            marks_.push_back({aim_+Vec3{0,-.7f,0},1.3f,action_ticks/60.f,ripto_tell::ripto});beams_.push_back({peak,aim_,.025f,true,ripto_tell::ripto});
            if(action_ticks==60){clip(b,models,1,true);clip(r,models,2,true);g.sound_events.push_back({SoundScope::Object,0,0x2eb,b.position});}
        } else {
            const auto old=b.position;Vec3 next;
            if(action_ticks<=104){const float t=(action_ticks-60)/44.f;next=curve(peak,peak+rush_direction_*2,aim_-rush_direction_*2,aim_,t);}
            else {const float t=smooth((action_ticks-104)/56.f);next=curve(aim_,aim_+rush_direction_*2,dive_end_-rush_direction_*2,dive_end_,t);}
            const auto delta=next-old;
            if(const auto wall=g.level.raycast(old,delta,length(delta)+.85f);wall && wall->distance<length(delta)+.85f) {
                flashes_.push_back({old,0,.8f,{.3f,.6f,1}});enter(g,models,RiptoAction::BirdFlight);break;
            }
            b.position=next;face(b,b.position+rush_direction_,3);
            b.rotation[1]=std::lerp(b.rotation[1],std::atan2(delta.y,std::max(.0001f,flat(delta)))/(16*retail::angle_to_radians),.2f);
            if(segment_distance(origin,old,next)<.8f+g.tuning.radius && clear(g,old,origin) && g.hurt(old))++damage_to_spyro;
            dive_trail_.push_front(next);while(dive_trail_.size()>28)dive_trail_.pop_back();
            if(action_ticks==104){clip(b,models,0,true);flashes_.push_back({b.position,0,1.1f,{.22f,.6f,1}});}
            if(action_ticks>=160)enter(g,models,RiptoAction::BirdFlight);
        }break;
    }
    case RiptoAction::Finale: {
        // Ripto clings to the exposed crown core; it pulses white (an arena tell).
        face(r,g.player.position,1);
        if(action_ticks%30==1)flashes_.push_back({center+Vec3{0,1.1f,0},0,1.6f,ripto_tell::arena});
        if(finisher_) {
            // Each element's Fury finishes him with its own colour burst.
            constexpr std::array<Vec3,element_count> burst{Vec3{1,.45f,.08f},Vec3{.2f,.55f,1},Vec3{.75f,.92f,1},Vec3{.95f,.9f,.25f},Vec3{.6f,.15f,1}};
            const auto color=burst[static_cast<unsigned>(*finisher_)];
            for(unsigned k=0;k<6;++k)flashes_.push_back({center+Vec3{std::sin(k*1.05f)*1.4f,.8f+k*.35f,std::cos(k*1.05f)*1.4f},0,2.f+k*.35f,color});
            g.camera.shake(4200,40);g.sound_events.push_back({SoundScope::Global,27,0,center});
            b.position=b.previous_position=r.position+Vec3{0,1.2f,0};
            stage=RiptoStage::Defeated;finale_ticks=0;finisher_.reset();enter(g,models,RiptoAction::Death);
            notice(g,"The crown core shatters!",240);
            start_cinematic(g,CrownCinematic::Outro);
        } else if(finale_ticks && !--finale_ticks) {
            // Missed: the core seals and Ripto takes off again at a quarter health.
            ++finales_missed;health=max_health/4;
            b.hidden=false;b.boss_body=BossBody{.8f,-.12f,.38f};b.position=b.previous_position=center+Vec3{0,4,0};r.boss_body.reset();
            enter(g,models,RiptoAction::BirdFlight);attach_rider(g,models);
            notice(g,"The crown core sealed - knock Ripto down again!",240);
        }
        break;
    }
    case RiptoAction::Death: {
        b.position.y-=dt*1.8f;b.rotation[0]+=1.0f;
        if(action_ticks%12==0)flashes_.push_back({b.position,0,1.3f,{1,.55f,.07f}});
        if(action_ticks>=150) {
            r.hidden=m.hidden=b.hidden=true;r.boss_body.reset();m.boss_body.reset();b.boss_body.reset();
            action=RiptoAction::Waiting;action_ticks=0;g.retail_objects[hunter].hidden=true;
            if(retail_arena){g.guide_awards.boss=true;g.ripto_defeated=true;notice(g,"RIPTO DEFEATED - Returning to Winter Tundra",300);}
            else {auto& a=*g.level.custom_arena;a.complete=true;a.boss_shield=a.boss_repair=a.boss_overdrive=false;
                for(auto& p:a.pieces)if(p.kind>=ArenaKind::ShieldRelay && p.kind<=ArenaKind::OverdriveRelay)p.destroyed=true;
                g.award_orb(0);notice(g,"CYBORG RIPTO DEFEATED - Return Home is open!",420);}
            g.sound_events.push_back({SoundScope::Global,27,0,g.player.position});
        }break;
    }
    }
    tick_crush(g,models);attach_rider(g,models);tick_orbs(g,models);tick_shots(g,models);
    if(!retail_arena && (action==RiptoAction::CyborgNova || action==RiptoAction::CyborgLance)) {
        auto p=sample_object_animation(models.models.at(r.type),r.animation,models.animation_blends);pose(p,1);
        const auto at=action==RiptoAction::CyborgNova?p.vertices[123]:ripto_eye_socket(p.vertices).center;
        effect_origin=r.position+object_basis(r)*(at*object_model_scale(r));
        const float release=action==RiptoAction::CyborgNova?105.f:72.f;
        effect_charge=action_ticks<release?smooth(action_ticks/release):std::max(0.f,1-(action_ticks-release)/25.f);
    }
    for(const auto& s:shots_)if(s.kind==RiptoShotKind::Mortar)marks_.push_back({s.impact,1.5f,std::min(1.f,s.age/63.f)});
    // A moving solid boss can push Spyro, but never through arena walls.
    auto player_center=g.player.position+retail::origin_above_feet;
    for(const auto index:{actor,gulp,bird,crush})if(index<g.retail_objects.size())if(const auto contact=container_contact(g.retail_objects[index],models,player_center,g.tuning.radius)) {
        auto candidate=player_center+contact->normal*(contact->penetration+.001f);Vec3 velocity=g.player.velocity;bool grounded=g.player.grounded;
        candidate=g.level.resolve_sphere(candidate,g.tuning.radius,velocity,grounded);
        g.player.position=candidate-retail::origin_above_feet;g.player.velocity=velocity;player_center=candidate;
    }
    visuals_.clear();for(const auto& s:shots_)if(s.kind!=RiptoShotKind::Shockwave) {
        visuals_.push_back(s.visual);
        if(s.kind==RiptoShotKind::FireTrain)for(unsigned i=20;i<s.trail.size();i+=20) {
            auto link=visual(models,0x2e8,s.trail[i],80);link.animation=link.previous_animation=s.visual.animation;
            link.previous_position=s.trail[std::min(i+1,static_cast<unsigned>(s.trail.size()-1))];
            const auto d=s.trail[i-1]-s.trail[i];link.rotation[2]=link.previous_rotation[2]=retail::original_heading(std::atan2(d.x,d.z))/16;
            visuals_.push_back(link);
        }
    }
    for(const auto& o:orbs_)visuals_.push_back(o.visual);
    ticking_=false;if(reset_pending_)reset(g,mp);
}
void RiptoBoss::pose(ObjectPose& p,float alpha)const {
    if(retail_arena || p.vertices.size()!=222 || (action!=RiptoAction::CyborgLance && action!=RiptoAction::CyborgNova))return;
    const float t=std::max(0.f,float(action_ticks)-1+alpha);
    const bool lance=action==RiptoAction::CyborgLance;
    // Smooth additive torso bend/twist over the original keyframes; legs
    // retain their stance. Use one continuous weight field across shared mesh
    // vertices, including the staff, to avoid separated arm/neck seams.
    const float envelope=lance?std::sin(std::clamp(t/155.f,0.f,1.f)*pi):std::sin(std::clamp(t/210.f,0.f,1.f)*pi);
    const float bend=lance?-.16f*envelope:.28f*envelope*std::cos(t*pi/105);
    const float twist=lance?.12f*envelope:.32f*envelope*std::sin(t*pi/105);
    for(auto& v:p.vertices){const Vec3 pivot{0,.32f,0};const auto d=v-pivot;const float w=std::clamp((v.y-.25f)/.55f,0.f,1.f);const float b=bend*w,z=twist*w;
        const Vec3 tilted{d.x,d.y*std::cos(b)-d.z*std::sin(b),d.y*std::sin(b)+d.z*std::cos(b)};
        v=pivot+Vec3{tilted.x*std::cos(z)+tilted.z*std::sin(z),tilted.y,-tilted.x*std::sin(z)+tilted.z*std::cos(z)};}
}
#include "ripto_crush.inl"
#include "ripto_director.inl"
} // namespace spyro

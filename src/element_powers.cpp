// Original elemental breaths, projectiles, headbash shockwaves and Furies.
// None of these reuse the retail flame streams or projectile mobys: geometry,
// collision and timing are new procedural PS1-style effects.
#include "spyro/elements.hpp"
#include "spyro/runtime.hpp"
#include "spyro/spyro_dark.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace spyro {
namespace {
constexpr float dt=1.0f/60,pi=std::numbers::pi_v<float>;
Vec3 target_center(const RetailObject& o) {
    return o.boss_body?o.position+Vec3{0,(o.boss_body->bottom+o.boss_body->top)*.5f,0}:o.position+Vec3{0,.32f,0};
}
float target_body(const RetailObject& o){return o.boss_body?o.boss_body->radius:.35f;}
// Drawn objects big enough to spoil a shot (pillars, gem lamps, big gems), as
// bounding spheres from their first mesh's box. Objects the target stands at
// and small pickups are left out.
struct ShotObstacle {Vec3 center;float radius{};};
std::vector<ShotObstacle> shot_obstacles(const Game& g,Vec3 target,float reach) {
    std::vector<ShotObstacle> result;
    const auto* models=g.object_models();if(!models)return result;
    for(const auto& o:g.retail_objects) {
        if(o.collected || o.hidden || o.state<0 || o.script_undrawn || length(o.position-target)>reach+10)continue;
        const auto bank=models->models.find(o.type);if(bank==models->models.end())continue;
        const auto mesh=std::find_if(bank->second.meshes.begin(),bank->second.meshes.end(),[](const auto& m){return !m.vertices.empty();});
        if(mesh==bank->second.meshes.end())continue;
        Vec3 lo=mesh->vertices[0],hi=lo;
        for(const auto v:mesh->vertices){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
        const float scale=object_model_scale(o),r=length(hi-lo)*.5f*scale*.85f+.2f;
        if(r<.45f)continue;
        const auto c=o.position+object_basis(o)*((lo+hi)*(.5f*scale));
        if(length(c-target)<r+.3f)continue;
        result.push_back({c,r});
    }
    return result;
}
// How far along target->dir (up to reach) the first obstacle blocks the line.
float object_block(std::span<const ShotObstacle> obstacles,Vec3 target,Vec3 dir,float reach) {
    float limit=reach;
    for(const auto& o:obstacles) {
        const auto rel=o.center-target;const float along=dot(rel,dir),d2=dot(rel,rel)-along*along;
        if(along<=0 || along>reach+o.radius || d2>=o.radius*o.radius)continue;
        limit=std::min(limit,along-std::sqrt(o.radius*o.radius-d2));
    }
    return limit;
}
// The camera's distance from the nearest obstacle's surface.
float object_clearance(std::span<const ShotObstacle> obstacles,Vec3 eye) {
    float c=1e9f;for(const auto& o:obstacles)c=std::min(c,length(eye-o.center)-o.radius);
    return c;
}
float segment_distance(Vec3 p,Vec3 a,Vec3 b) {
    const auto ab=b-a;const float n=dot(ab,ab);const float t=n>1e-8f?std::clamp(dot(p-a,ab)/n,0.0f,1.0f):0;
    return length(p-(a+ab*t));
}
Vec3 rotate_about_up(Vec3 v,float angle){return retail::rotate_model(v,angle);}
EffectSprite sprite_for(Element e) {
    switch(e){case Element::Fire:return EffectSprite::Ember;case Element::Water:return EffectSprite::Droplet;case Element::Ice:return EffectSprite::Snowflake;
        case Element::Electric:return EffectSprite::Spark;case Element::Convexity:return EffectSprite::Mote;}
    return EffectSprite::Spark;
}
unsigned status_for(Element e) {
    switch(e){case Element::Fire:return element_status::burn;case Element::Water:return element_status::soak|element_status::push;
        case Element::Ice:return element_status::freeze;case Element::Electric:return element_status::stun;case Element::Convexity:return element_status::void_mark;}
    return 0;
}
}
void ElementSystem::update_mouth(Game& g,const SpyroModel* spyro) {
    const auto origin=g.player.position+retail::origin_above_feet;
    mouth_direction_=retail::rotate_model({0,0,1},g.player.yaw,g.player.pitch,0);
    Vec3 local{0,.05f,.34f};bool snout=false;
    // The posed snout (body + flame head gesture) is the true mouth; the frame
    // attachment only tracks the head joint, so it is a fallback for other meshes.
    if(const auto pose=posed(g,spyro))if(const auto m=spyro_mouth(*spyro,*pose)){local=*m;snout=true;}
    if(!snout && spyro && g.animation.ready()) {
        const auto& state=g.animation.state();
        if(const auto clip=spyro->animations.find(state.clip);clip!=spyro->animations.end() && state.frame<clip->second.frames.size()) {
            const auto a=clip->second.frames[state.frame].attachment;if(length(a)>.02f && length(a)<1.2f)local=a;
        }
    }
    mouth_=origin+retail::rotate_model(local,g.player.yaw,g.player.pitch,g.player.roll)+(snout?Vec3{}:mouth_direction_*.05f);
}
void ElementSystem::breath_tick(Game& g,const ObjectModels* models) {
    (void)models;beam_.active=false;
    auto& e=g.upgrades.elements;
    if(breath_active_) {
        ++breath_ticks_;
        const bool free=dark_ticks_ && breath_element_==Element::Convexity;
        if(!free){e.energy=std::max(0.0f,e.energy-breath_drain*dt);if(e.energy<=0)breath_active_=false;}
    }
    if((breath_active_ || !nodes_.empty()) && (++local_age_>=8 || length(mouth_-local_center_)>3 || local_.empty())){gather_local(g.level,mouth_,11);local_age_=0;}
    if(breath_active_) {
        switch(breath_element_) {
        case Element::Water:water_breath(g);break;
        case Element::Ice:ice_breath(g);break;
        case Element::Electric:electric_breath(g);break;
        case Element::Convexity:void_breath(g,models);break;
        case Element::Fire:break;
        }
    }
    // Breath bodies (Torrent jets and Frost puffs) travel, collide and hit.
    for(std::size_t i=0;i<nodes_.size();++i) {
        auto& n=nodes_[i];++n.age;
        const bool frost=n.phase<0;
        if(n.stopped){n.radius+=frost?.012f:.004f;continue;}
        if(frost){n.velocity=n.velocity*.93f;n.velocity.y-=.4f*dt;n.radius=std::min(.62f,n.radius+.026f);}
        else {n.velocity.y-=7.5f*dt;n.radius=std::min(.34f,n.radius+.011f);}
        const auto step=n.velocity*dt;
        if(const auto wall=local_raycast(local_,n.position,step,length(step)+.04f)) {
            n.position=wall->point+wall->normal*.04f;n.stopped=true;n.velocity={};
            if(frost)spray(n.position,{0,.3f,0},Element::Ice,2,.5f,.08f,EffectSprite::Snowflake,30,1,-.5f);
            else {
                spray(n.position,wall->normal*1.8f+Vec3{0,1.2f,0},Element::Water,4,1.4f,.05f,EffectSprite::Droplet,26,0,9);
                spray(n.position,{0,.4f,0},Element::Water,1,.3f,.12f,EffectSprite::Foam,18,0);
                if(wall->normal.y>.5f && g.headbash.apply_element(n.position,.9f,Element::Water)){
                    ++cooled_lava;spray(n.position,{0,1.6f,0},Element::Water,10,.8f,.22f,EffectSprite::Steam,46,0,-1.2f);}
            }
        } else n.position+=step;
        if(n.age%2==0) {
            const auto element=frost?Element::Ice:Element::Water;
            hit_sphere(g,n.position,n.radius+.12f,{element,1,mouth_,status_for(element),false,frost?0.0f:7.0f},false);
            if(frost && n.age%6==0 && g.headbash.apply_element(n.position,.7f,Element::Ice)){++cooled_lava;
                spray(n.position,{0,1.2f,0},Element::Ice,8,.6f,.2f,EffectSprite::Steam,40,0,-.8f);}
            // Frost settling on water freezes the whole body (Winter Tundra-style ice).
            if(frost && n.age%4==0)
                if(const auto water=g.level.water_at(n.position-Vec3{0,.6f,0});water && n.position.y<water->height+.7f)
                    freeze_at(g,{n.position.x,water->height,n.position.z});
        }
    }
    std::erase_if(nodes_,[](const auto& n){return n.age>=n.lifetime;});
}
void ElementSystem::water_breath(Game& g) {
    if(g.player.swim.mode==WaterMode::Underwater) {
        // Underwater Torrent: a jet-propelled swim burst with a bubble wake.
        g.player.velocity=g.player.velocity+mouth_direction_*(9*dt);
        spray(mouth_-mouth_direction_*.6f,mouth_direction_*-1.5f,Element::Water,2,.6f,.06f,EffectSprite::Foam,30,0,-2);
        return;
    }
    for(unsigned k=0;k<2;++k) {
        if(nodes_.size()>=96)break;
        const auto r=[&]{return (float(random_.next()%2001)-1000)/1000.0f;};
        // A coherent jet: slight wobble only. The farther half-step node is pushed
        // first so the chain runs monotonically from the jet's tip back to the mouth.
        BreathNode n;n.velocity=mouth_direction_*15.5f+Vec3{r()*.1f,.8f+r()*.08f,r()*.1f};
        n.position=n.previous=mouth_+n.velocity*(dt*.5f*float(1-k));
        n.radius=n.previous_radius=.07f;n.lifetime=24;n.phase=float(breath_ticks_*2+k)*.35f;nodes_.push_back(n);
    }
    if(breath_ticks_%2==0)spray(mouth_+mouth_direction_*.25f,mouth_direction_*6,Element::Water,1,1.4f,.045f,EffectSprite::Droplet,22,1,8);
}
void ElementSystem::ice_breath(Game& g) {
    (void)g;
    for(unsigned k=0;k<3 && nodes_.size()<96;++k) {
        const float yaw=(float(random_.next()%2001)-1000)/1000.0f*.38f,lift=(float(random_.next()%2001)-1000)/1000.0f*.22f;
        const auto dir=normalized(rotate_about_up(mouth_direction_,yaw)+Vec3{0,lift,0});
        BreathNode n;n.position=n.previous=mouth_;n.velocity=dir*(8.5f+float(random_.next()%100)/50.0f);
        n.radius=n.previous_radius=.1f;n.lifetime=21;n.phase=-1.0f-float(random_.next()%100)/100.0f;nodes_.push_back(n);
    }
    if(breath_ticks_%3==0)spray(mouth_,mouth_direction_*11,Element::Ice,1,2.0f,.07f,EffectSprite::Crystal,24,1);
    spray(mouth_,mouth_direction_*7.5f,Element::Ice,2,2.6f,.05f,EffectSprite::Snowflake,26,1,-.6f);
}
void ElementSystem::electric_breath(Game& g) {
    if(++bolt_clock_%2)return;
    const auto wet=[&](const RetailObject& o){return o.soak_ticks>0;};
    // Primary target: nearest valid target in a forward cone with line of sight.
    std::vector<std::pair<float,std::uint32_t>> candidates;
    for(std::size_t i=0;i<g.retail_objects.size();++i) {
        const auto& o=g.retail_objects[i];if(!element_target(o))continue;
        const auto d=target_center(o)-mouth_;const float n=length(d);
        if(n>7.5f || n<.05f || dot(d/n,mouth_direction_)<.78f)continue;
        candidates.push_back({n,static_cast<std::uint32_t>(i)});
    }
    std::sort(candidates.begin(),candidates.end());
    std::optional<std::uint32_t> primary;
    for(unsigned k=0;k<candidates.size() && k<4;++k) {
        const auto c=target_center(g.retail_objects[candidates[k].second]);
        if(!g.level.raycast(mouth_,c-mouth_,std::max(0.0f,length(c-mouth_)-.2f))){primary=candidates[k].second;break;}
    }
    const bool strike=bolt_clock_%6==0;
    spray(mouth_,mouth_direction_*2,Element::Electric,2,1.6f,.04f,EffectSprite::Spark,10,1);
    if(!primary) {
        auto end=mouth_+mouth_direction_*6.0f;
        if(const auto wall=local_raycast(local_,mouth_,mouth_direction_,6.0f))end=wall->point;
        bolt(mouth_,end,Element::Electric,3,.32f,2);
        spray(end,{0,.6f,0},Element::Electric,3,1.8f,.05f,EffectSprite::Spark,12,1);
        return;
    }
    std::vector<std::uint32_t> chain{*primary};
    bolt(mouth_,target_center(g.retail_objects[*primary]),Element::Electric,3,.22f,2);
    // Chain to up to three further targets (wet targets double the reach).
    while(chain.size()<=max_chain) {
        const auto& from=g.retail_objects[chain.back()];const float reach=wet(from)?7.0f:3.5f;
        std::optional<std::uint32_t> next;float best=reach;
        for(std::size_t i=0;i<g.retail_objects.size();++i) {
            const auto& o=g.retail_objects[i];if(!element_target(o) || std::find(chain.begin(),chain.end(),i)!=chain.end())continue;
            const float d=length(target_center(o)-target_center(from));if(d<best){best=d;next=static_cast<std::uint32_t>(i);}
        }
        if(!next)break;
        bolt(target_center(from),target_center(g.retail_objects[*next]),Element::Electric,3,.18f,1);
        chain.push_back(*next);if(strike)++chains;
    }
    if(strike)for(const auto index:chain) {
        auto& o=g.retail_objects[index];
        queue_element_hit(o,{Element::Electric,1,mouth_,element_status::stun,false,1.5f});++status_hits;
        spray(target_center(o),{0,.5f,0},Element::Electric,5,2.2f,.05f,EffectSprite::Spark,14,1);
    }
}
void ElementSystem::void_breath(Game& g,const ObjectModels* models) {
    auto end=mouth_+mouth_direction_*9.0f;
    const auto wall=g.level.raycast(mouth_,mouth_direction_,9.0f);
    if(wall)end=wall->point;
    // Terrain: the beam scars what it touches and, held for 0.6 s, cuts it.
    // A sweep while held tilts the cut through the beam and its path.
    if(slice_cooldown_)--slice_cooldown_;
    if(wall) {
        if(!slice_ticks_)slice_start_=end;
        ++slice_ticks_;
        if(slice_ticks_%20==10 && g.destruction.erode(g,end,wall->normal,models))++erosions;
        if(slice_ticks_>=36 && !slice_cooldown_) {
            if(g.destruction.slice(g,mouth_,end,mouth_direction_,end-slice_start_,models,wall->normal)) {
                ++slices;g.camera.shake(2600,20);g.sound_events.push_back({SoundScope::Global,0x14,0,end});
                spray(end,{0,1.2f,0},Element::Convexity,30,2.6f,.12f,EffectSprite::Flake,60,2,1.5f);
                spray(end,{0,.4f,0},Element::Convexity,12,1.2f,.2f,EffectSprite::Ring,30,1);
            }
            slice_cooldown_=90;slice_ticks_=0;
        }
    } else slice_ticks_=0;
    beam_.active=true;beam_.from=mouth_;beam_.to=end;++beam_.age;beam_.width=.15f+.03f*std::sin(float(phase_)*.45f);
    if(!previous_beam_.active)previous_beam_=beam_;
    // Spiralling dark motes and travelling violet rings along the beam.
    const auto axis=normalized(end-mouth_);const auto side=normalized(cross(std::abs(axis.y)>.95f?Vec3{1,0,0}:Vec3{0,1,0},axis)),up=cross(axis,side);
    for(unsigned k=0;k<2;++k) {
        const float t=float(random_.next()%1000)/1000.0f,a=float(phase_)*.5f+t*12;
        const auto p=lerp(mouth_,end,t)+(side*std::cos(a)+up*std::sin(a))*.22f;
        spray(p,axis*2.5f,Element::Convexity,1,.15f,.06f,EffectSprite::Mote,18,2);
    }
    if(phase_%5==0)spray(mouth_,axis*14,Element::Convexity,1,0,.16f,EffectSprite::Ring,std::max(4u,static_cast<unsigned>(length(end-mouth_)/14*60)),1);
    spray(end,{0,.4f,0},Element::Convexity,2,1.4f,.07f,EffectSprite::Flake,24,2,-.5f);
    if(beam_.age%4==0)for(std::size_t i=0;i<g.retail_objects.size();++i) {
        auto& o=g.retail_objects[i];if(!element_target(o))continue;
        if(segment_distance(target_center(o),mouth_,end)>.45f+target_body(o))continue;
        queue_element_hit(o,{Element::Convexity,2,mouth_,element_status::void_mark,false,2.5f});++status_hits;
    }
}
void ElementSystem::fire_shot(Game& g) {
    auto& e=g.upgrades.elements;const auto el=e.equipped;
    const bool free=dark_ticks_ && el==Element::Convexity;
    if(!free)e.energy=std::max(0.0f,e.energy-shot_cost);
    shot_cooldown_=el==Element::Electric?44:el==Element::Ice?24:el==Element::Convexity?36:22;regen_delay_=0;++shots_fired;
    const auto start=mouth_+mouth_direction_*.18f;
    const auto make=[&](ShotKind kind,Vec3 velocity,unsigned life,float radius) {
        if(shots_.size()>=max_shots)shots_.erase(shots_.begin());
        ElementShot s;s.kind=kind;s.element=el;s.position=s.previous=start;s.velocity=velocity;s.lifetime=life;s.radius=radius;shots_.push_back(std::move(s));
    };
    switch(el) {
    case Element::Fire:make(ShotKind::Ember,mouth_direction_*16+Vec3{0,.6f,0},70,.13f);g.sound_events.push_back({SoundScope::Global,3,0,start});break;
    case Element::Water:make(ShotKind::Bubble,mouth_direction_*10+Vec3{0,.4f,0},120,.26f);g.sound_events.push_back({SoundScope::Global,8,0,start});break;
    case Element::Ice:for(const float a:{-.21f,0.0f,.21f})make(ShotKind::Shard,rotate_about_up(mouth_direction_,a)*22,50,.07f);
        g.sound_events.push_back({SoundScope::Global,0x1d,0,start});break;
    case Element::Electric:make(ShotKind::SparkOrb,mouth_direction_*6+Vec3{0,.3f,0},240,.2f);g.sound_events.push_back({SoundScope::Global,2,0,start});break;
    case Element::Convexity:make(ShotKind::RiftOrb,mouth_direction_*7,90,.22f);g.sound_events.push_back({SoundScope::Global,0x14,0,start});break;
    }
}
void ElementSystem::explode(Game& g,ElementShot& s) {
    if(s.exploding)return;
    s.exploding=true;s.blast=0;s.velocity={};
    switch(s.kind) {
    case ShotKind::Ember:
        hit_sphere(g,s.position,1.0f,{Element::Fire,1,s.position,element_status::burn,false,3,s.fury?HitSource::Fury:HitSource::Shot},false);
        spray(s.position,{0,1.2f,0},Element::Fire,16,3.0f,.09f,EffectSprite::Ember,26,1,4);
        spray(s.position,{0,.2f,0},Element::Fire,1,0,.3f,EffectSprite::Ring,12,1);break;
    case ShotKind::Bubble:
        hit_sphere(g,s.position,1.35f,{Element::Water,2,s.position,element_status::soak|element_status::push,false,8,HitSource::Shot},false);
        spray(s.position,{0,1.5f,0},Element::Water,20,3.2f,.06f,EffectSprite::Droplet,30,0,9);
        spray(s.position,{0,.1f,0},Element::Water,1,0,.4f,EffectSprite::Ring,14,0);
        g.headbash.apply_element(s.position,1.2f,Element::Water);
        g.sound_events.push_back({SoundScope::Global,8,0,s.position});break;
    case ShotKind::Shard:
        spray(s.position,{0,.8f,0},Element::Ice,8,2.2f,.05f,EffectSprite::Crystal,22,1,8);break;
    case ShotKind::SparkOrb:
        hit_sphere(g,s.position,1.6f,{Element::Electric,2,s.position,element_status::stun,false,4,HitSource::Shot},false);
        for(unsigned k=0;k<6;++k){const float a=k*pi/3;bolt(s.position,s.position+Vec3{std::sin(a)*1.4f,.2f+float(k%2)*.4f,std::cos(a)*1.4f},Element::Electric,6,.18f,0);}
        spray(s.position,{0,.6f,0},Element::Electric,16,3.0f,.05f,EffectSprite::Spark,16,1);
        g.sound_events.push_back({SoundScope::Global,2,0,s.position});break;
    case ShotKind::RiftOrb:
        hit_sphere(g,s.position,2.5f,{Element::Convexity,2,s.position,element_status::void_mark,false,6,HitSource::Shot},false);
        spray(s.position,{0,.3f,0},Element::Convexity,26,2.6f,.1f,EffectSprite::Flake,40,2,-1);
        spray(s.position,{0,0,0},Element::Convexity,1,0,.6f,EffectSprite::Ring,16,2);
        g.demons.summon(g,g.object_models(),DemonKind::Imp,s.position,1);
        g.camera.shake(700,10);break;
    }
}
void ElementSystem::shots_tick(Game& g,const ObjectModels* models) {
    (void)models;
    for(auto& s:shots_) {
        if(s.exploding){++s.blast;continue;}
        ++s.age;s.spin+=s.kind==ShotKind::Shard?.5f:.18f;
        switch(s.kind) {
        case ShotKind::Ember:s.velocity.y-=3.0f*dt;if(s.age%2==0)spray(s.position,s.velocity*-.15f+Vec3{0,.6f,0},Element::Fire,1,.4f,.08f,EffectSprite::Ember,16,1,-1);break;
        case ShotKind::Bubble:s.velocity=s.velocity*.995f;s.velocity.y+=std::sin(float(s.age)*.2f)*.05f;
            if(s.age%9==0)spray(s.position,{0,-.4f,0},Element::Water,1,.2f,.035f,EffectSprite::Droplet,20,0,8);break;
        case ShotKind::Shard:if(s.age%2==0)spray(s.position,{0,0,0},Element::Ice,1,.2f,.04f,EffectSprite::Snowflake,14,1);break;
        case ShotKind::SparkOrb: {
            // Seek, re-using the Superflame Seek rule (forward cone + line of sight).
            if(s.age%6==0) {
                float best=10;Vec3 goal{};bool found=false;
                for(const auto& o:g.retail_objects) {
                    if(!element_target(o))continue;const auto d=target_center(o)-s.position;const float n=length(d);
                    if(n>=best || n<.01f || dot(d/n,normalized(s.velocity))<.15f)continue;
                    if(g.level.raycast(s.position,d,std::max(0.0f,n-.25f)))continue;best=n;goal=d;found=true;
                }
                if(found)s.velocity=normalized(lerp(normalized(s.velocity),normalized(goal),.35f))*6.0f;
            }
            if(s.age%8==0) {
                unsigned zapped=0;
                for(auto& o:g.retail_objects) {
                    if(zapped>=2 || !element_target(o))continue;const auto c=target_center(o);if(length(c-s.position)>3)continue;
                    bolt(s.position,c,Element::Electric,5,.2f,1);queue_element_hit(o,{Element::Electric,1,s.position,element_status::stun,false,1});++zapped;++status_hits;
                }
            }
            if(s.age%3==0){const float a=float(s.age)*.9f;bolt(s.position,s.position+Vec3{std::sin(a),std::cos(a*1.3f),std::cos(a)}*.32f,Element::Electric,3,.05f,0);}
            break;
        }
        case ShotKind::RiftOrb:
            if(s.age%2==0)for(auto& o:g.retail_objects) {
                if(!element_target(o) || !o.enemy)continue;if(length(target_center(o)-s.position)>4)continue;
                queue_element_hit(o,{Element::Convexity,0,s.position,element_status::pull,false,4.5f});
            }
            if(s.age%2==0){const float a=float(s.age)*.7f;const auto p=s.position+Vec3{std::sin(a)*1.2f,.2f,std::cos(a)*1.2f};
                spray(p,(s.position-p)*3,Element::Convexity,1,.1f,.06f,EffectSprite::Mote,20,2);}
            s.velocity=s.velocity*.985f;break;
        }
        s.trail.push_front(s.position);while(s.trail.size()>(s.kind==ShotKind::Shard?6u:10u))s.trail.pop_back();
        const auto step=s.velocity*dt;const float distance=length(step);
        bool hit_wall=false;
        if(distance>0)if(const auto wall=g.level.raycast(s.position,step,distance+s.radius)) {
            s.position=wall->point+wall->normal*(s.radius*.6f);hit_wall=true;
        }
        if(!hit_wall)s.position+=step;
        if(s.kind==ShotKind::Shard && !s.exploding)
            if(const auto water=g.level.water_at(s.position);water && s.position.y<water->height+.2f) {
                freeze_at(g,{s.position.x,water->height,s.position.z});explode(g,s);
            }
        // Object contact. Shards pierce their first target and stop at the next.
        for(std::size_t i=0;i<g.retail_objects.size() && !s.exploding;++i) {
            auto& o=g.retail_objects[i];if(!element_target(o))continue;
            if(length(target_center(o)-s.position)>s.radius+target_body(o))continue;
            if(std::find(s.hit.begin(),s.hit.end(),static_cast<std::uint32_t>(i))!=s.hit.end())continue;
            s.hit.push_back(static_cast<std::uint32_t>(i));
            if(s.kind==ShotKind::Shard) {
                queue_element_hit(o,{Element::Ice,2,s.position,element_status::freeze,false,2,HitSource::Shot});++status_hits;
                spray(s.position,{0,.6f,0},Element::Ice,6,1.6f,.05f,EffectSprite::Crystal,18,1,6);
                if(s.hit.size()>=2)explode(g,s);
            } else if(s.kind!=ShotKind::RiftOrb || length(target_center(o)-s.position)<.5f)explode(g,s);
        }
        if(!s.exploding && (hit_wall || s.age>=s.lifetime))explode(g,s);
    }
    std::erase_if(shots_,[](const auto& s){return s.exploding && s.blast>=16;});
}
void ElementSystem::headbash(Game& g,const ObjectModels* models,Vec3 feet) {
    const auto& u=g.upgrades;
    if(!u.enabled || g.dialogue.active() || g.scene_locked || wave_cooldown_)return;
    const auto& e=u.elements;
    if(dark_ticks_)g.demons.summon(g,models,DemonKind::Maw,feet,1);
    const bool elemental=e.tier(e.equipped)>=3 || dark_ticks_;
    if(!elemental && !u.headbash)return;   // the base dust wave belongs to the headbash upgrade
    const auto element=dark_ticks_?Element::Convexity:e.equipped;
    // A new wave refreshes the oldest; at most two expand at once.
    if(waves_.size()>=max_waves)waves_.erase(waves_.begin());
    Shockwave w;w.element=element;w.plain=!elemental;w.dark=dark_ticks_>0;w.shape=shock_shape(element,w.plain);
    w.center=feet;w.max_radius=shock_radius(element,w.plain);
    const float reach=w.max_radius+1.2f;
    for(const auto index:g.level.collision_candidates(feet.x-reach,feet.z-reach,feet.x+reach,feet.z+reach)) {
        const auto& t=g.level.triangles[index];
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        if(std::min({t.a.x,t.b.x,t.c.x})>feet.x+reach || std::max({t.a.x,t.b.x,t.c.x})<feet.x-reach ||
           std::min({t.a.z,t.b.z,t.c.z})>feet.z+reach || std::max({t.a.z,t.b.z,t.c.z})<feet.z-reach ||
           std::min({t.a.y,t.b.y,t.c.y})>feet.y+2.5f || std::max({t.a.y,t.b.y,t.c.y})<feet.y-2.5f)continue;
        w.local.push_back(t);
    }
    // Crater interplay: recolour/flag passes only, never another carve.
    if(!w.plain)cooled_lava+=g.headbash.apply_element(feet,w.max_radius,element)*(element==Element::Water || element==Element::Ice?1u:0u);
    const unsigned cue=w.plain?0x14u:element==Element::Water?8u:element==Element::Electric?2u:element==Element::Ice?0x1du:element==Element::Convexity?0x14u:3u;
    waves_.push_back(std::move(w));++waves;wave_cooldown_=18;
    g.camera.shake(element==Element::Electric || element==Element::Convexity?2200:1400,16);
    g.sound_events.push_back({SoundScope::Global,cue,0,feet});
    const auto& placed=waves_.back();
    if(!placed.plain) {
        if(element==Element::Ice) {
            decal(g,feet,placed.max_radius*.92f,{.55f,.75f,.95f},240,0);
            for(unsigned k=0;k<9;++k) {
                const float a=k*2*std::numbers::pi_v<float>/8,r=k?placed.max_radius*.7f:0;
                const Vec3 at=feet+Vec3{std::sin(a)*r,.3f,std::cos(a)*r};
                if(const auto water=g.level.water_at(at-Vec3{0,.6f,0});water && at.y<water->height+1.2f)freeze_at(g,{at.x,water->height,at.z});
            }
        }
        else if(element==Element::Water){decal(g,feet,placed.max_radius*.85f,{.10f,.14f,.22f},180,2);pocket(g,feet);}   // a swimmable pocket
        else if(element==Element::Fire)decal(g,feet,placed.max_radius*.8f,{.16f,.10f,.07f},200,2);
        else if(element==Element::Convexity)decal(g,feet,placed.max_radius*.7f,{.22f,.18f,.28f},150,2);
    }
}
void ElementSystem::waves_tick(Game& g,const ObjectModels* models) {
    if(wave_cooldown_)--wave_cooldown_;
    for(auto& w:waves_) {
        ++w.age;
        const float t=std::clamp(float(w.age)/w.expand,0.0f,1.0f);
        w.radius=w.max_radius*(1-(1-t)*(1-t));
        const bool expanding=w.age<=w.expand;
        if(w.shape==ShockShape::Ripple && expanding) {
            ShockRing ring;ring.radius=w.radius;
            for(unsigned k=0;k<32;++k) {
                const float a=k*2*pi/32;const Vec3 top=w.center+Vec3{std::sin(a)*w.radius,1.3f,std::cos(a)*w.radius};
                if(const auto hit=local_raycast(w.local,top,{0,-1,0},3.0f);hit && hit->normal.y>.35f){ring.height[k]=hit->point.y;ring.valid[k]=true;}
            }
            w.rings.push_front(ring);while(w.rings.size()>6)w.rings.pop_back();
        } else if(!w.rings.empty() && w.age%2==0)w.rings.pop_back();
        // Convexity implodes first: pull everything inward before the blast.
        if(!w.plain && w.element==Element::Convexity && w.age<=8)
            for(auto& o:g.retail_objects)if(element_target(o) && o.enemy && length(target_center(o)-w.center)<w.max_radius+1)
                queue_element_hit(o,{Element::Convexity,0,w.center,element_status::pull,false,5});
        if(expanding) {
            for(std::size_t i=0;i<g.retail_objects.size();++i) {
                auto& o=g.retail_objects[i];if(!element_target(o))continue;
                if(std::find(w.hit.begin(),w.hit.end(),static_cast<std::uint32_t>(i))!=w.hit.end())continue;
                const auto c=target_center(o);const auto delta=c-w.center;
                const float d=w.shape==ShockShape::Ripple?std::hypot(delta.x,delta.z):length(delta);
                if(w.shape==ShockShape::Ripple && std::abs(delta.y)>1.1f)continue;   // ripples stay on their own floor
                if(d>w.radius+target_body(o) || d<w.previous_radius-.35f)continue;
                w.hit.push_back(static_cast<std::uint32_t>(i));
                // Walls stop the wave; slopes and kerbs do not (two source x two target heights).
                bool visible=false;
                for(const float rise:{.3f,1.0f})for(const float lift:{0.0f,.45f}) {
                    if(visible)break;
                    const auto eye=w.center+Vec3{0,rise,0};const auto ray=c+Vec3{0,lift,0}-eye;
                    visible=length(ray)<=.4f || !g.level.raycast(eye,ray,length(ray)-.25f);
                }
                if(!visible)continue;
                ++wave_hits;
                const bool core=d<=w.max_radius*.5f;
                if(w.plain) {
                    if(core){o.attack_flags|=headbash_attack|charge_attack;o.attack_source=w.center;o.attack_sourced=true;}
                    else if(o.enemy){auto away=delta;away.y=0;o.push_velocity+=normalized(away)*4;}
                    continue;
                }
                const float push=w.element==Element::Water?9.0f:w.element==Element::Convexity?8.0f:w.element==Element::Fire?3.0f:2.0f;
                queue_element_hit(o,{w.element,core?2u:0u,w.center,status_for(w.element)|(w.element==Element::Water?element_status::push:0),false,push,w.fury?HitSource::Fury:HitSource::Wave});
                if(w.element==Element::Electric)bolt(c+Vec3{(float(random_.next()%200)-100)/150.0f,4.5f,(float(random_.next()%200)-100)/150.0f},c,Element::Electric,6,.35f,2);
            }
        }
        // Presentation particles along the moving front.
        if(expanding) {
            const unsigned count=w.plain?5:w.fury && w.shape==ShockShape::Sphere?18:7;
            for(unsigned k=0;k<count;++k) {
                const float a=float(random_.next()%6283)/1000.0f;
                Vec3 p=w.center+Vec3{std::sin(a)*w.radius,.08f,std::cos(a)*w.radius};
                if(w.shape==ShockShape::Sphere) {
                    // Fury blasts fill the whole shell (down to just below the
                    // centre); other spheres keep to the upper half.
                    const float z=float(random_.next()%2001)/1000.0f-1,r=std::sqrt(std::max(0.0f,1-z*z));
                    p=w.center+Vec3{std::sin(a)*r,w.fury?std::max(z,-.35f):std::abs(z)*.8f+.1f,std::cos(a)*r}*w.radius;
                }
                const auto out=normalized(Vec3{std::sin(a),0,std::cos(a)});
                if(w.plain){const auto before=particles_.size();spray(p,out*1.2f+Vec3{0,1.1f,0},Element::Fire,1,.4f,.18f,EffectSprite::Steam,30,0,1.5f);
                    if(particles_.size()>before)particles_.back().color={.55f,.46f,.34f};}
                else switch(w.element) {
                case Element::Fire:spray(p,Vec3{0,2.2f,0}+out*.6f,Element::Fire,1,.5f,.13f,EffectSprite::Ember,24,1,-1.5f);break;
                case Element::Water:spray(p,Vec3{0,3.0f,0}+out*1.5f,Element::Water,1,.7f,.06f,EffectSprite::Droplet,28,0,10);
                    if(k%2==0)spray(p,{0,.5f,0},Element::Water,1,.3f,.18f,EffectSprite::Foam,20,0);break;
                case Element::Ice:spray(p,{0,1.0f,0},Element::Ice,1,.5f,.06f,EffectSprite::Snowflake,30,1,-.4f);break;
                case Element::Electric:spray(p,out*2,Element::Electric,1,1.0f,.05f,EffectSprite::Spark,12,1);break;
                case Element::Convexity:spray(p,out*-1.5f,Element::Convexity,1,.5f,.09f,EffectSprite::Mote,26,2);break;
                }
            }
            if(!w.plain && w.element==Element::Ice && w.age%2==0 && spikes_.size()<max_spikes && !w.rings.empty()) {
                const unsigned k=random_.next()%32;const auto& ring=w.rings.front();
                if(ring.valid[k]) {
                    const float a=k*2*pi/32;IceSpike s;s.base={w.center.x+std::sin(a)*ring.radius,ring.height[k],w.center.z+std::cos(a)*ring.radius};
                    s.axis=normalized(Vec3{std::sin(a)*.35f,1,std::cos(a)*.35f});s.height=.5f+float(random_.next()%40)/100.0f;
                    s.width=.12f+float(random_.next()%6)/100.0f;s.yaw=a;s.lifetime=80;spikes_.push_back(s);
                }
            }
            if(!w.plain && w.element==Element::Electric && w.age%3==0) {
                const float a=float(random_.next()%6283)/1000.0f,b=a+1.2f;
                bolt(w.center+Vec3{std::sin(a)*w.radius,.4f,std::cos(a)*w.radius},w.center+Vec3{std::sin(b)*w.radius,w.radius*.6f,std::cos(b)*w.radius},Element::Electric,4,.25f,1);
            }
        }
        // Convexity: demons erupt around the ring as the blast completes.
        if(!w.plain && w.element==Element::Convexity && w.age==w.expand) {
            const unsigned imps=w.dark?3:2;
            for(unsigned k=0;k<imps;++k) {
                const float a=float(k)*2*pi/imps+float(random_.next()%100)/100.0f;
                g.demons.summon(g,models,DemonKind::Imp,w.center+Vec3{std::sin(a),0,std::cos(a)}*(w.max_radius*.75f),1);
            }
        }
    }
    std::erase_if(waves_,[](const auto& w){return w.age>=w.lifetime;});
}
void ElementSystem::fury_start(Game& g,const ObjectModels* models) {
    (void)models;auto& e=g.upgrades.elements;if(e.fury<100)return;
    e.fury=0;++furies;fury_={e.equipped,0,0,0,g.player.position};fury_pose_ticks_=fury_pose_duration;breath_active_=false;
    g.ripto.finale_fury(g,e.equipped);   // Crownworks' exposed crown core: any one Fury ends the fight
    g.sound_events.push_back({SoundScope::Global,0x1d,0,g.player.position});g.camera.shake(900,30);
}
namespace {
constexpr unsigned max_fury_waves=8;
}
void ElementSystem::fury_tick(Game& g,const ObjectModels* models) {
    const auto origin=g.player.position+retail::origin_above_feet;
    const auto rnd=[&]{return float(random_.next()%2001)/1000.0f-1;};
    // A Fury ring: like a headbash wave, but larger and outside the headbash cap.
    const auto ring=[&](Element element,ShockShape shape,float radius,unsigned expand) {
        Shockwave w;w.element=element;w.plain=false;w.fury=true;w.shape=shape;w.center=fury_.center;w.max_radius=radius;w.expand=expand;w.lifetime=expand+14;
        const float reach=radius+1;const auto c=fury_.center;
        for(const auto index:g.level.collision_candidates(c.x-reach,c.z-reach,c.x+reach,c.z+reach)) {
            const auto& t=g.level.triangles[index];
            if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
            if(std::min({t.a.x,t.b.x,t.c.x})>c.x+reach || std::max({t.a.x,t.b.x,t.c.x})<c.x-reach || std::min({t.a.z,t.b.z,t.c.z})>c.z+reach ||
               std::max({t.a.z,t.b.z,t.c.z})<c.z-reach || std::min({t.a.y,t.b.y,t.c.y})>c.y+3.0f || std::max({t.a.y,t.b.y,t.c.y})<c.y-3.0f)continue;
            w.local.push_back(t);
        }
        if(waves_.size()>=max_fury_waves)waves_.erase(waves_.begin());
        waves_.push_back(std::move(w));
    };
    const auto spike_ring=[&](float radius,unsigned count,float height) {
        for(unsigned k=0;k<count && spikes_.size()<max_spikes;++k) {
            const float a=float(k)*2*pi/float(count)+rnd()*.15f;auto base=fury_.center+Vec3{std::sin(a)*radius,0,std::cos(a)*radius};
            if(const auto f=g.level.raycast(base+Vec3{0,2,0},{0,-1,0},5))base=f->point;else continue;
            IceSpike sp;sp.base=base;sp.axis=normalized(Vec3{std::sin(a)*.3f,1,std::cos(a)*.3f});sp.height=height+rnd()*.25f;sp.width=.18f+rnd()*.04f;
            sp.yaw=a;sp.lifetime=150;sp.delay=k%3;spikes_.push_back(sp);
        }
    };
    // Up to three blast waves radiating through the air from Spyro's chest,
    // each wider, slower to spread and harder-hitting than the last.
    const auto blast=[&](unsigned k) {
        Shockwave w;w.element=fury_.element;w.plain=false;w.fury=true;w.shape=ShockShape::Sphere;
        w.center=fury_.center+Vec3{0,retail::spyro_origin_height+.35f,0};
        w.max_radius=7.5f+3.75f*float(k);w.expand=22+6*k;w.lifetime=w.expand+18;
        if(waves_.size()>=max_fury_waves)waves_.erase(waves_.begin());
        waves_.push_back(std::move(w));++fury_blasts;
        g.camera.shake(3600+1600*k,26+6*k);g.sound_events.push_back({SoundScope::Global,k?2u:0x14u,0,origin});
        spray(origin,{0,.4f,0},fury_.element,28+10*k,5.5f+1.5f*k,.16f,sprite_for(fury_.element),44,fury_.element==Element::Convexity?2:1);
    };
    const auto ahead=Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)},beside=Vec3{ahead.z,0,-ahead.x};
    // Keep a scripted camera clear of walls and objects. A shot is a path:
    // `eye_at(u)` for u from 0 to 1 over its hold. On a cut the whole path
    // turns about Spyro until every point of it is clean (nothing between
    // camera and Spyro, nothing crowding the lens); the turn holds for the
    // shot, and anything still crowding the lens eases the camera in.
    const auto frame=[&](const auto& eye_at,Vec3 target,float u,bool cut) {
        const auto obstacles=shot_obstacles(g,target,length(eye_at(u)-target)+6);
        // Drawn-only scenery counts too: tree canopies have no collision (as
        // in the original), so collision rays alone let the lens into them.
        std::vector<Triangle> drawn;
        {
            const float reach=length(eye_at(u)-target)+8;const auto& v=g.level.visual_vertices;
            for(const auto i:g.level.visual_candidates(target.x-reach,target.z-reach,target.x+reach,target.z+reach))
                if(i+2<v.size())drawn.push_back(Triangle{v[i].position,v[i+1].position,v[i+2].position});
        }
        const auto solid=[&](Vec3 from,Vec3 dir,float reach)->float {
            float r=reach+1;
            if(const auto hit=g.level.raycast(from,dir,reach))r=hit->distance;
            if(const auto hit=local_raycast(drawn,from,dir,std::min(r,reach)))r=std::min(r,hit->distance);
            return r;
        };
        const auto turned=[&](Vec3 e,float a){const auto d=e-target;return target+Vec3{d.x*std::cos(a)+d.z*std::sin(a),d.y,d.z*std::cos(a)-d.x*std::sin(a)};};
        const auto blocked=[&](Vec3 from,Vec3 dir,float reach) {
            const float r=std::min(object_block(obstacles,from,dir,reach),solid(from,dir,reach));
            return r<reach;
        };
        // Crowded: terrain or an object within `margin` beside or above the
        // lens, or within twice that along the view's corner rays (what
        // would fill a corner of the frame). The floor below is fine.
        const auto crowded=[&](Vec3 e,float margin) {
            constexpr float h=.7071f;
            for(const Vec3 d:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,0,1},Vec3{0,0,-1},Vec3{h,0,h},Vec3{-h,0,h},Vec3{h,0,-h},Vec3{-h,0,-h},Vec3{0,1,0}})
                if(solid(e,d,margin)<=margin)return true;
            if(object_clearance(obstacles,e)<margin)return true;
            const auto forward=normalized(target-e),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
            for(const float sx:{-1.0f,1.0f})for(const float sy:{-1.0f,1.0f})
                if(blocked(e,normalized(forward+right*(.95f*sx)+up*(.55f*sy)),margin*2))return true;
            return false;
        };
        const auto clean=[&](Vec3 e) {
            const auto ray=e-target;const float n=length(ray);
            return n<.1f || (!blocked(target,ray/n,n) && !crowded(e,1.0f));
        };
        if(cut) {
            fury_camera_turn_=0;
            for(const float a:{0.0f,.45f,-.45f,.9f,-.9f,1.35f,-1.35f,1.8f,-1.8f,2.4f,-2.4f,pi}) {
                bool ok=true;
                for(float v=u;v<=1.001f && ok;v+=.2f)ok=clean(turned(eye_at(std::min(v,1.0f)),a));
                if(ok){fury_camera_turn_=a;break;}
            }
        }
        auto eye=turned(eye_at(u),fury_camera_turn_);
        const auto ray=eye-target;const float n=length(ray);
        if(n>.1f) {
            float d=n;
            if(blocked(target,ray/n,n)) {
                const float r=std::min({n,object_block(obstacles,target,ray/n,n),solid(target,ray/n,n)});
                d=std::max(.6f,r-.25f);
            }
            while(d>1.2f && crowded(target+ray/n*d,.5f))d-=.25f;
            eye=target+ray/n*d;
        }
        const CameraView view{eye,normalized(target-eye)};
        if(cut)g.camera.cut(view);else g.camera.scripted_view=view;
    };
    if(fury_pose_ticks_) {
        // Gear-up: Spyro levitates while the element spirals in, faster each
        // stage. Each stage cuts to a new angle that drifts as it holds: a low
        // front three-quarter, a tracking side profile, then a high orbit.
        const unsigned elapsed=fury_pose_duration-fury_pose_ticks_,stage=elapsed/fury_stage_ticks;
        const float within=float(elapsed%fury_stage_ticks)/fury_stage_ticks,progress=(float(stage)+within)/3;
        for(unsigned k=0;k<2+unsigned(progress*7);++k) {
            const float a=float(phase_)*(.6f+progress*.5f)+float(k)*2.1f,r=2.4f-progress*1.6f;
            spray(origin+Vec3{std::sin(a)*r,-.4f+progress*.8f,std::cos(a)*r},Vec3{-std::sin(a)*2.4f,1.4f,-std::cos(a)*2.4f},fury_.element,1,.3f,.09f+progress*.07f,
                sprite_for(fury_.element),24,fury_.element==Element::Convexity?2:1);
        }
        const auto chest=origin+Vec3{0,.25f,0};
        const bool cut=elapsed%fury_stage_ticks==0;
        if(stage==0)frame([&](float v){return chest+ahead*3.1f+beside*(1.5f-v*.9f)+Vec3{0,-.3f+v*.25f,0};},chest+Vec3{0,.25f,0},within,cut);
        else if(stage==1)frame([&](float v){return chest+beside*4.4f+ahead*(1.0f-v*2.0f)+Vec3{0,.7f,0};},chest,within,cut);
        else frame([&](float v){const float a=g.player.yaw+pi*.8f+v*1.3f;return chest+Vec3{std::sin(a)*3.6f,3.4f,std::cos(a)*3.6f};},chest,within,cut);
        if(elapsed && elapsed%fury_stage_ticks==0) {
            // A new stage: the gathered element flares and the screen kicks.
            spray(origin,{0,1.2f,0},fury_.element,18+8*stage,2.4f,.12f,sprite_for(fury_.element),30,fury_.element==Element::Convexity?2:1);
            g.camera.shake(1800+900*stage,14);g.sound_events.push_back({SoundScope::Global,0x1d,0,origin});
        }
        if(fury_pose_ticks_%10==0)g.camera.shake(600+unsigned(progress*2600),10);
        --fury_pose_ticks_;
        // Release at a stage's end unless the Fury input is still held (and
        // always after the third).
        const unsigned reached=(elapsed+1)/fury_stage_ticks;
        const bool stage_end=(elapsed+1)%fury_stage_ticks==0;
        if(fury_pose_ticks_ && !(stage_end && (!fury_hold_ || reached>=3)))return;
        fury_pose_ticks_=0;fury_.blasts=std::clamp(reached,1u,3u);
        fury_.stage=1;fury_.ticks=0;fury_.center=g.player.position;
        g.camera.shake(5200,50);
        blast(0);
        switch(fury_.element) {
        case Element::Fire:
            if(g.upgrades.headbash>=2)g.headbash.stamp(g,g.player.position,HeadbashMode::Lava,models);
            decal(g,g.player.position,7,{.18f,.08f,.04f},420,2);
            break;
        case Element::Water: {
            flood(g);
            std::vector<Vec3> spots;
            for(unsigned k=0;k<10;++k){const float a=float(k)*2*pi/10;spots.push_back(g.player.position+Vec3{std::sin(a),0,std::cos(a)}*(3+float(k%2)*2.5f));}
            for(const auto& o:g.retail_objects)if(spots.size()<16 && element_target(o) && o.enemy && length(o.position-g.player.position)<12)spots.push_back(o.position);
            for(std::size_t k=0;k<spots.size() && geysers_.size()<max_geysers;++k) {
                auto base=spots[k];if(const auto f=g.level.raycast(base+Vec3{0,1.5f,0},{0,-1,0},4))base=f->point;
                geysers_.push_back({base,0,0,70,static_cast<unsigned>(k*4)});
            }
            break;
        }
        case Element::Ice: {
            const auto view=g.camera.sample(1);
            for(auto& o:g.retail_objects) {
                if(!element_target(o) || o.boss_body || (!o.enemy && !o.power_target))continue;
                const auto d=o.position-view.eye;const float n=length(d);
                if(n>28 || (n>.1f && dot(d/n,view.forward)<.1f))continue;
                if(!o.frozen_ticks)++freezes;
                o.frozen_ticks=900;o.element_frozen=true;o.attack_flags=0;o.stun_ticks=0;
            }
            decal(g,g.player.position,9,{.6f,.8f,1},480,0);
            spike_ring(2.4f,10,.9f);
            break;
        }
        case Element::Electric:break;
        case Element::Convexity:
            // Crownworks: no Dark Spyro during the interlude, finale or outro.
            if(!g.ripto.blocks_dark()) {
                dark_ticks_=dark_duration;++dark_entries;exhaustion_ticks_=0;
                g.demons.summon(g,models,DemonKind::Hound,g.player.position,1);
                g.demons.summon(g,models,DemonKind::Wraith,g.player.position,1);
            }
            break;
        }
        return;
    }
    // Geysers lift targets, then slam them down.
    for(auto& geyser:geysers_) {
        if(geyser.delay){--geyser.delay;continue;}
        ++geyser.age;geyser.height=std::min(4.0f,geyser.height+.25f);
        if(geyser.age>=6 && geyser.age<=40 && geyser.age%3==0)
            for(auto& o:g.retail_objects)if(element_target(o) && o.enemy && std::hypot(o.position.x-geyser.base.x,o.position.z-geyser.base.z)<1.4f)
                queue_element_hit(o,{Element::Water,0,geyser.base,element_status::lift|element_status::soak,false,6});
        if(geyser.age==44)hit_sphere(g,geyser.base+Vec3{0,.5f,0},1.6f,{Element::Water,2,geyser.base,element_status::soak,false,3,HitSource::Fury},false);
        spray(geyser.base+Vec3{0,geyser.height*float(random_.next()%100)/100.0f,0},{0,2.5f,0},Element::Water,2,.6f,.08f,EffectSprite::Foam,18,0,4);
        if(geyser.age%2==0)spray(geyser.base+Vec3{0,geyser.height,0},{0,1.5f,0},Element::Water,2,2.2f,.05f,EffectSprite::Droplet,26,0,10);
    }
    std::erase_if(geysers_,[](const auto& geyser){return geyser.age>=geyser.lifetime;});
    if(!fury_.stage)return;
    const auto t=++fury_.ticks;
    // The remaining blasts, filmed from a wide angle behind the release point
    // until the last has spread; then the gameplay camera returns.
    for(unsigned k=1;k<fury_.blasts;++k)if(t==k*fury_blast_gap)blast(k);
    const unsigned release_shot=(fury_.blasts-1)*fury_blast_gap+40;
    if(t<release_shot) {
        const auto chest=fury_.center+retail::origin_above_feet+Vec3{0,.4f,0};
        frame([&](float v){return chest-ahead*(6.5f+float(fury_.blasts))+beside*2.4f+Vec3{0,3.4f+v*float(release_shot)*.012f,0};},chest,float(t)/float(release_shot),t==1);
    } else if(t==release_shot)g.camera.scripted_view.reset();
    switch(fury_.element) {
    case Element::Fire: {
        // Inferno: widening fire rings and an eight-second meteor rain.
        if(t==45)ring(Element::Fire,ShockShape::Ripple,10,36);
        if(t==95)ring(Element::Fire,ShockShape::Ripple,13,42);
        if(t==160)ring(Element::Fire,ShockShape::Ripple,16,48);
        if(t%3==0 && t<480 && shots_.size()<max_shots) {
            const float a=float(random_.next()%6283)/1000.0f,r=1.5f+float(random_.next()%850)/100.0f;
            ElementShot m;m.kind=ShotKind::Ember;m.element=Element::Fire;m.fury=true;
            m.position=m.previous=fury_.center+Vec3{std::sin(a)*r+rnd(),9+rnd(),std::cos(a)*r+rnd()};
            m.velocity={rnd()*1.5f,-15,rnd()*1.5f};m.radius=.24f;m.lifetime=90;shots_.push_back(m);
        }
        if(t%20==0)g.camera.shake(1400,8);
        if(t>=480)fury_.stage=0;
        break;
    }
    case Element::Water:
        // Deluge: rain over the rising flood while the geysers erupt.
        if(t<540)for(unsigned k=0;k<4;++k)
            spray(g.player.position+Vec3{rnd()*11,7+rnd(),rnd()*11},{rnd()*.3f,-9,rnd()*.3f},Element::Water,1,.2f,.04f,EffectSprite::Droplet,50,0,6);
        if(t>=540)fury_.stage=0;
        break;
    case Element::Ice:
        // Glacier: a blizzard and rising spike rings, then everything shatters.
        if(t<360)for(unsigned k=0;k<4;++k)
            spray(g.player.position+Vec3{rnd()*12,3+rnd()*3,rnd()*12},{2.5f+rnd(),-1.2f,rnd()},Element::Ice,1,.4f,.07f,EffectSprite::Snowflake,70,1);
        if(t==40)spike_ring(4.8f,14,1.2f);
        if(t==80)spike_ring(7.2f,18,1.5f);
        if(t==140)spike_ring(9.6f,22,1.8f);
        if(t==360) {
            for(auto& o:g.retail_objects)if(o.element_frozen && o.frozen_ticks)queue_element_hit(o,{Element::Ice,3,g.player.position,0,false,3,HitSource::Fury});
            freeze_all(g);
            ring(Element::Ice,ShockShape::Sphere,10,22);
            spray(origin,{0,2,0},Element::Ice,40,5,.1f,EffectSprite::Crystal,60,1,4);
            g.camera.shake(4200,30);g.sound_events.push_back({SoundScope::Global,2,0,origin});fury_.stage=0;
        }
        break;
    case Element::Electric: {
        // Storm: a dark canopy and a ten-second bolt barrage.
        if(t%2==0 && t<580) {
            const float a=float(random_.next()%6283)/1000.0f,r=float(random_.next()%650)/100.0f;
            const auto before=particles_.size();
            spray(g.player.position+Vec3{std::sin(a)*r,3.2f+float(random_.next()%50)/100.0f,std::cos(a)*r},{0,.05f,0},Element::Electric,1,.25f,1.1f,EffectSprite::Steam,70,2,0);
            if(particles_.size()>before)particles_.back().color=particles_.back().color*.32f;
        }
        if(t%4==0 && t<600) {
            // Targets every 8 ticks; ambient ground strikes in between.
            std::vector<std::uint32_t> visible;
            if(t%8==0)
                for(std::size_t i=0;i<g.retail_objects.size();++i){const auto& o=g.retail_objects[i];if(element_target(o) && length(o.position-g.player.position)<18)visible.push_back(static_cast<std::uint32_t>(i));}
            Vec3 strike;
            if(!visible.empty()) {
                auto& o=g.retail_objects[visible[random_.next()%visible.size()]];strike=target_center(o);
                queue_element_hit(o,{Element::Electric,2,strike+Vec3{0,4,0},element_status::stun,false,2,HitSource::Fury});++status_hits;
            } else {
                const float a=float(random_.next()%6283)/1000.0f,r=1.6f+float(random_.next()%600)/100.0f;strike=g.player.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                if(const auto f=g.level.raycast(strike+Vec3{0,3,0},{0,-1,0},6))strike=f->point;
            }
            bolt(strike+Vec3{rnd(),3.5f,rnd()},strike,Element::Electric,8,.45f,3);   // from the canopy
            hit_sphere(g,strike,1.2f,{Element::Electric,2,strike,element_status::stun,false,2,HitSource::Fury},false);
            spray(strike,{0,1,0},Element::Electric,12,2.8f,.06f,EffectSprite::Spark,16,1);
            g.camera.shake(900,6);if(t%8==0)g.sound_events.push_back({SoundScope::Global,2,0,strike});
        }
        if(t==300)ring(Element::Electric,ShockShape::Sphere,9,26);
        if(t==450)ring(Element::Electric,ShockShape::Sphere,12,30);
        if(t>=600)fury_.stage=0;
        break;
    }
    case Element::Convexity:
        // The rift keeps opening: void motes stream in, then the Maw and imps rise.
        if(t<260 && t%2==0)spray(origin+Vec3{rnd()*7,rnd()*2.5f,rnd()*7},{0,.4f,0},Element::Convexity,2,1.2f,.1f,EffectSprite::Mote,40,2,-.6f);
        if(t==40)g.demons.summon(g,models,DemonKind::Maw,g.player.position+Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*3,1);
        if(t==70)g.demons.summon(g,models,DemonKind::Imp,g.player.position,2);
        if(t==160)g.demons.summon(g,models,DemonKind::Imp,g.player.position,2);
        if(t==200)ring(Element::Convexity,ShockShape::Sphere,12,34);
        if(t>=300)fury_.stage=0;
        break;
    }
}
} // namespace spyro

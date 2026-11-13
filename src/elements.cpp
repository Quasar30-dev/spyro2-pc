#include "spyro/elements.hpp"
#include "spyro/runtime.hpp"
#include "spyro/spyro_dark.hpp"
#include <algorithm>
#include <numbers>

namespace spyro {
namespace {
constexpr float dt=1.0f/60;
}
const char* element_name(Element e) {
    switch(e){case Element::Fire:return "Fire";case Element::Water:return "Water";case Element::Ice:return "Ice";
        case Element::Electric:return "Electricity";case Element::Convexity:return "Convexity";}
    return "Element";
}
const char* element_breath_name(Element e) {
    switch(e){case Element::Fire:return "Flame";case Element::Water:return "Torrent";case Element::Ice:return "Frost";
        case Element::Electric:return "Arc";case Element::Convexity:return "Void Beam";}
    return "";
}
const char* element_shot_name(Element e) {
    switch(e){case Element::Fire:return "Ember";case Element::Water:return "Bubble Burst";case Element::Ice:return "Ice Shards";
        case Element::Electric:return "Spark Orb";case Element::Convexity:return "Rift Orb";}
    return "";
}
const char* element_fury_name(Element e) {
    switch(e){case Element::Fire:return "Inferno";case Element::Water:return "Geyser Field";case Element::Ice:return "Glacier";
        case Element::Electric:return "Storm";case Element::Convexity:return "Dark Spyro";}
    return "";
}
Vec3 element_color(Element e) {
    switch(e){case Element::Fire:return {1.0f,.42f,.08f};case Element::Water:return {.16f,.48f,1.0f};case Element::Ice:return {.55f,.85f,1.0f};
        case Element::Electric:return {.95f,.9f,.3f};case Element::Convexity:return {.45f,.12f,.75f};}
    return {1,1,1};
}
Vec3 element_core_color(Element e) {
    switch(e){case Element::Fire:return {1.0f,.9f,.45f};case Element::Water:return {.75f,.95f,1.0f};case Element::Ice:return {.95f,1.0f,1.0f};
        case Element::Electric:return {1.0f,1.0f,.92f};case Element::Convexity:return {.92f,.8f,1.0f};}
    return {1,1,1};
}
bool metal_enemy(const RetailObject& o) {
    return o.enemy && (o.enemy->kind==EnemyKind::Robot || o.enemy->kind==EnemyKind::MetalIdol || o.enemy->kind==EnemyKind::GearGrinder);
}
bool element_target(const RetailObject& o) {
    if(o.collected || o.state<0 || o.hidden || o.pc_actor || o.pc_carried || o.powerup_gate)return false;
    // Boss bodies (Ripto, the mechs, the bird) are scripted-contact actors
    // that still take elemental hits; test them before that exclusion.
    if(o.boss_body)return true;
    if(o.scripted_contact)return false;
    if(o.enemy)return !o.enemy->defeated;
    if(is_breakable(o.type) || is_heavy_container(o.type))return true;
    return o.power_target.has_value();
}
std::uint32_t element_retail_mask(Element element,unsigned s,const RetailObject& o) {
    if(!s)return 0;
    if(is_breakable(o.type) || is_heavy_container(o.type)) {
        // Every elemental breath, shot and wave breaks every container: 0x160000
        // satisfies baskets (C8), vases (C9), both chests (CA/CB) and bottles (D1).
        (void)element;return 0x160000u;
    }
    if(o.enemy) {
        const bool metal=metal_enemy(o);
        const bool fire_creature=o.enemy->kind==EnemyKind::FireBlubber || o.enemy->kind==EnemyKind::ElephantSnail;
        switch(element) {
        case Element::Fire:return s>=2?superflame_attack:flame_attack;
        case Element::Water:
            if(metal)return 0;
            if(fire_creature)return flame_attack;   // water defeats fire creatures through their own weakness
            return s>=2?charge_attack:0;
        case Element::Ice:return 0;                  // Ice freezes; impacts shatter
        case Element::Electric:return metal?(charge_attack|flame_attack):flame_attack;
        case Element::Convexity:return s>=3?(0x570000u|headbash_attack):(superflame_attack|headbash_attack);
        }
        return 0;
    }
    if(o.power_target) {
        switch(element) {
        case Element::Fire:return s>=2?superflame_attack:flame_attack;
        case Element::Water:return s>=2?(0x100000u|charge_attack):0;
        case Element::Ice:return 0;
        case Element::Electric:return s>=2?superflame_attack:flame_attack;
        case Element::Convexity:return s>=3?0x570000u:superflame_attack;
        }
    }
    return 0;
}
std::uint32_t element_boss_flags(Element element,unsigned s) {
    // Ripto/Crush: 1, 2, 5 or 10 HP through the existing take_hits table.
    // Never 0x400000 (15 HP), and the boss cooldown/invulnerability still apply.
    if(!s)return 0;
    if(s>=3)return 0x140000u;
    if(s==2)return headbash_attack;
    return element==Element::Water || element==Element::Electric || element==Element::Convexity?charge_attack:flame_attack;
}
unsigned boss_element_damage(const ElementHit& hit) {
    // Crownworks' table: breath 1 (Fire, Water, Ice), 2 (Electric), 3
    // (Convexity); shot 3; headbash wave 4; each Fury hit 4 within its cap.
    if(!hit.strength)return 0;
    if(hit.from==HitSource::Fury || hit.strength>=3)return 4;
    if(hit.from==HitSource::Wave)return 4;
    if(hit.from==HitSource::Shot)return 3;
    return hit.element==Element::Convexity?3:hit.element==Element::Electric?2:1;
}
bool convexity_milestone(const Game& g) {
    const auto& e=g.upgrades.elements;
    const bool mastery=e.tier(Element::Fire)>=3 && e.tier(Element::Water)>=3 && e.tier(Element::Ice)>=3 && e.tier(Element::Electric)>=3;
    return g.ripto_defeated || mastery;
}
bool element_purchasable(const Game& g,Element element) {
    const auto& u=g.upgrades;const auto& e=u.elements;
    if(!u.enabled || !u.element_cost(element))return false;
    if(element==Element::Convexity && !e.owns(Element::Convexity))
        return e.owns(Element::Water) && e.owns(Element::Ice) && e.owns(Element::Electric) && convexity_milestone(g);
    return true;
}
void unlock_all_elements(Game& g) {
    auto& e=g.upgrades.elements;e.tiers={3,3,3,3,3};e.energy=100;e.fury=100;
}
float shock_radius(Element element,bool plain) {
    if(plain)return 3.0f;
    switch(element){case Element::Fire:return 3.6f;case Element::Ice:return 4.0f;case Element::Water:case Element::Convexity:return 4.5f;case Element::Electric:return 5.0f;}
    return 3.0f;
}
ShockShape shock_shape(Element element,bool plain) {
    // Earthbound waves ripple along the floor; energy waves expand as spheres.
    if(plain)return ShockShape::Ripple;
    return element==Element::Electric || element==Element::Convexity?ShockShape::Sphere:ShockShape::Ripple;
}

bool ElementSystem::breath_override(const Game& g)const {
    const auto& u=g.upgrades;
    if(!u.enabled || u.elements.equipped==Element::Fire || !u.elements.owns(u.elements.equipped))return false;
    // Level gate powers keep priority while their timers run.
    return !(g.powers.fire.remaining || g.powers.ice.remaining || g.ripto.green_bomb() || g.ripto.player_power_ticks);
}
bool ElementSystem::dark(const Game& g)const {return g.cheats.dark_spyro || dark_ticks_>0;}
float ElementSystem::movement(const Game& g,MoveStat stat)const {
    if(!dark(g))return exhaustion_ticks_?dark_tuning.exhausted:1.0f;
    switch(stat){case MoveStat::Run:return dark_tuning.run;case MoveStat::Charge:return dark_tuning.charge;case MoveStat::Jump:return dark_tuning.jump;
        case MoveStat::Glide:return dark_tuning.glide;case MoveStat::Swim:return dark_tuning.swim;case MoveStat::Flight:return dark_tuning.flight;}
    return 1;
}
bool ElementSystem::shrug_off(Game& g,Vec3 source) {
    if(!dark_ticks_)return false;
    auto away=g.player.position-source;away.y=0;away=normalized(away);
    if(length(away)<0.01f)away={-std::sin(g.player.yaw),0,-std::cos(g.player.yaw)};
    g.player.velocity=away*3.5f+Vec3{0,2,0};g.player.grounded=false;g.player.airborne_ticks=4;g.player.invulnerable_ticks=40;
    spray(g.player.position+retail::origin_above_feet,{0,1.2f,0},Element::Convexity,12,2.5f,.12f,EffectSprite::Mote,26,2);
    return true;
}
void ElementSystem::reset(Game& g) {
    for(auto& o:g.retail_objects){o.element_hit.reset();o.stun_ticks=o.soak_ticks=0;o.void_mark=o.demon_hit=false;o.push_velocity={};
        if(o.element_frozen){o.frozen_ticks=0;o.element_frozen=false;}}
    waves_.clear();particles_.clear();nodes_.clear();bolts_.clear();shots_.clear();spikes_.clear();geysers_.clear();decals_.clear();
    beam_=previous_beam_={};for(auto& t:melee_)t.points.clear();for(auto& t:eye_trails_)t.clear();
    // A Fury interrupted mid gear-up or release leaves no scripted camera behind.
    if(fury_pose_ticks_ || fury_.stage)g.camera.scripted_view.reset();
    defeated_seen_.clear();fury_={};breath_active_=breath_queued_=shot_queued_=fury_queued_=glacier_release_=false;
    breath_ticks_=breath_idle_=regen_delay_=bolt_clock_=smoke_clock_=slice_ticks_=slice_cooldown_=0;fury_pose_ticks_=dark_ticks_=exhaustion_ticks_=0;shot_cooldown_=wave_cooldown_=0;
    smoke.reset();last_gems_=g.player.gems;local_.clear();
    water_.clear();g.level.dynamic_water.clear();
    for(const auto& f:ice_)for(const auto id:f.collision)if(id<g.level.triangles.size()){g.level.triangles[id].disabled=true;ice_free_.push_back(id);}
    ice_.clear();
    if(swim_flags_saved_){g.level.surface_swimming=saved_surface_;g.level.underwater_swimming=saved_underwater_;swim_flags_saved_=false;}
    for(std::size_t i=0;i<g.retail_objects.size();++i){const auto& o=g.retail_objects[i];if(o.enemy && o.enemy->defeated)defeated_seen_.push_back(static_cast<std::uint32_t>(i));}
}
void ElementSystem::reset_interpolation() {
    for(auto& p:particles_)p.previous=p.position;
    for(auto& n:nodes_){n.previous=n.position;n.previous_radius=n.radius;}
    for(auto& s:shots_){s.previous=s.position;s.previous_spin=s.spin;}
    for(auto& w:waves_)w.previous_radius=w.radius;
    for(auto& w:water_)w.previous_height=w.height;
    previous_beam_=beam_;smoke.reset_interpolation();
}
void ElementSystem::begin_tick(Game& g,InputState& input) {
    auto& u=g.upgrades;auto& e=u.elements;
    if(shot_cooldown_)--shot_cooldown_;
    if(!u.enabled){breath_active_=false;shot_queued_=fury_queued_=false;return;}
    const bool blocked=g.dialogue.active() || g.scene_locked || g.player.hurt_ticks || g.player.recoil_ticks || g.portal_travel.active() ||
        g.whirlwind.index!=UINT32_MAX || g.player.dodge.active || fury_pose_ticks_;
    if(input.element_select>=0 && input.element_select<static_cast<int>(element_count) && !g.dialogue.active()) {
        const auto chosen=static_cast<Element>(input.element_select);
        if(e.owns(chosen) && e.equipped!=chosen){e.equipped=chosen;breath_active_=false;g.sound_events.push_back({SoundScope::Global,0x1d,0,{}});}
    }
    if(input.element_cycle && !g.dialogue.active()) {
        for(unsigned step=1;step<element_count;++step) {
            const auto next=static_cast<Element>((static_cast<unsigned>(e.equipped)+element_count+(input.element_cycle>0?step:element_count-step))%element_count);
            if(e.owns(next)){e.equipped=next;breath_active_=false;g.sound_events.push_back({SoundScope::Global,0x1d,0,{}});break;}
        }
    }
    const bool swimming=g.player.swim.mode!=WaterMode::Dry;
    const bool can_breathe=!blocked && !g.powers.flying && !g.abilities.attacking() &&
        (!swimming || (e.equipped==Element::Water && g.player.swim.mode==WaterMode::Underwater));
    const bool free_breath=dark_ticks_ && e.equipped==Element::Convexity;
    if(breath_override(g) && input.flame_pressed && can_breathe && !exhaustion_ticks_ && (e.energy>=6 || free_breath)) {
        if(!breath_active_){breath_active_=true;breath_element_=e.equipped;breath_ticks_=0;++breaths;
            const unsigned cue=breath_element_==Element::Water?8u:breath_element_==Element::Electric?2u:breath_element_==Element::Ice?0x1du:3u;
            g.sound_events.push_back({SoundScope::Global,cue,0,{}});}
    }
    if(breath_active_ && (!can_breathe || !breath_override(g) || breath_element_!=e.equipped))breath_active_=false;
    if(breath_active_ && !input.flame_held && breath_ticks_>=30)breath_active_=false;
    if(breath_active_ && breath_ticks_>=180)breath_active_=false;
    // Keep the retail head gesture open while a held breath continues.
    if(breath_active_ && input.flame_held && breath_ticks_ && breath_ticks_%26==0)input.flame_pressed=true;
    if(input.shot_pressed && !blocked && !swimming && !exhaustion_ticks_ && e.tier(e.equipped)>=2 && !shot_cooldown_ && (e.energy>=shot_cost || free_breath) &&
       !(e.equipped==Element::Fire && (g.powers.flame_enabled(g) || g.cheats.green_firebomb)))shot_queued_=true;
    if(input.fury_pressed && !blocked && !swimming && e.tier(e.equipped)>=3 && e.fury>=100 && !dark_ticks_ && !exhaustion_ticks_)fury_queued_=true;
    fury_hold_=input.fury_held;
}
void ElementSystem::hit_sphere(Game& g,Vec3 center,float radius,const ElementHit& hit,bool) {
    for(auto& o:g.retail_objects) {
        if(!element_target(o))continue;
        const float body=o.boss_body?o.boss_body->radius:.35f;
        const auto c=o.boss_body?o.position+Vec3{0,(o.boss_body->bottom+o.boss_body->top)*.5f,0}:o.position+Vec3{0,.32f,0};
        if(length(c-center)>radius+body)continue;
        queue_element_hit(o,hit);++status_hits;
    }
}
void ElementSystem::resolve(Game& g,const ObjectModels* models) {
    (void)models;
    for(std::size_t i=0;i<g.retail_objects.size();++i) {
        auto& o=g.retail_objects[i];if(!o.element_hit)continue;
        const auto hit=*o.element_hit;o.element_hit.reset();
        if(!element_target(o))continue;
        if(hit.demon)o.demon_hit=true;
        if(o.boss_body) {
            // Crownworks (EXPANSION_LEVEL_PLANS 6.4): demons cannot hit Ripto
            // while he evades; the boss reads the element and source itself.
            if(hit.demon && g.ripto.evading(static_cast<unsigned>(i)))continue;
            o.attack_flags|=element_boss_flags(hit.element,hit.strength);
            o.fury_hit|=hit.strength>=3 || hit.from==HitSource::Fury;
            o.element_damage=std::max(o.element_damage,boss_element_damage(hit));
            o.element_chain|=hit.strength && hit.element==Element::Electric && hit.from==HitSource::Breath;
            o.element_freeze|=(hit.status&element_status::freeze)!=0;
            continue;
        }
        if(hit.element==Element::Fire && hit.strength){o.soak_ticks=0;if(o.element_frozen){o.frozen_ticks=0;o.element_frozen=false;}}
        // Frozen + any impact shatters into ice chips (Ice breath only refreshes).
        if(o.frozen_ticks && o.element_frozen && hit.element!=Element::Fire && (hit.element!=Element::Ice || hit.strength>=2)) {
            o.frozen_ticks=0;o.element_frozen=false;o.attack_flags|=flame_attack|charge_attack|headbash_attack|0x100000u;
            o.attack_source=hit.source;o.attack_sourced=true;++shatters;++element_kills;
            spray(o.position+Vec3{0,.35f,0},{0,1.5f,0},Element::Ice,18,3.2f,.07f,EffectSprite::Crystal,40,1,9);
            g.sound_events.push_back({SoundScope::Global,2,0,o.position});continue;
        }
        const auto mask=element_retail_mask(hit.element,hit.strength,o);
        if(mask){o.attack_flags|=mask;o.attack_source=hit.source;o.attack_sourced=true;if(is_breakable(o.type) || is_heavy_container(o.type))++crates;}
        if((hit.status&element_status::soak) && o.enemy){if(!o.soak_ticks)++soaks;o.soak_ticks=360;}
        if((hit.status&element_status::freeze) && (o.enemy || o.power_target) && !o.boss_body) {
            const bool susceptible=o.power_target || enemy_accepts_attack(o,flame_attack) || metal_enemy(o) || o.soak_ticks;
            if(susceptible) {
                if(!o.frozen_ticks)++freezes;
                o.frozen_ticks=o.soak_ticks?450:300;o.element_frozen=true;o.soak_ticks=0;
                o.attack_flags&=~(mask&~(charge_attack|superflame_attack|green_bomb_attack));
            }
        }
        if((hit.status&element_status::stun) && o.enemy){if(!o.stun_ticks)++stuns;o.stun_ticks=o.soak_ticks?150:90;}
        if((hit.status&element_status::void_mark) && o.enemy)o.void_mark=true;
        if(hit.push!=0 && o.enemy && !o.enemy->defeated) {
            auto away=o.position-hit.source;away.y=0;
            const float d=length(away);away=d>0.01f?away/d:Vec3{0,0,1};
            const bool pull=(hit.status&element_status::pull)!=0;
            o.push_velocity+=away*(pull?-hit.push:hit.push);
            if(hit.status&element_status::lift)o.push_velocity.y+=hit.push*.9f;
        }
    }
}
void ElementSystem::status_tick(Game& g) {
    for(std::size_t i=0;i<g.retail_objects.size();++i) {
        auto& o=g.retail_objects[i];
        if(o.collected || o.state<0)continue;
        if(o.element_frozen && !o.frozen_ticks)o.element_frozen=false;
        if(o.soak_ticks) {
            --o.soak_ticks;
            if(o.soak_ticks%10==0)spray(o.position+Vec3{0,.5f,0},{0,-.6f,0},Element::Water,1,.4f,.05f,EffectSprite::Droplet,24,0,9);
        }
        if(o.stun_ticks) {
            --o.stun_ticks;
            if(o.stun_ticks%6==0)bolt(o.position+Vec3{0,.65f,0},o.position+Vec3{(float(random_.next()%200)-100)/220.0f,.15f,(float(random_.next()%200)-100)/220.0f},
                Element::Electric,4,.06f,0);
        }
        if(o.void_mark && o.enemy && !o.enemy->defeated && phase_%9==0)
            spray(o.position+Vec3{0,.55f,0},{0,.5f,0},Element::Convexity,1,.5f,.06f,EffectSprite::Mote,30,2);
        if(length(o.push_velocity)>0.01f && o.enemy && !o.enemy->defeated) {
            const auto step=o.push_velocity*dt;const auto from=o.position+Vec3{0,.3f,0};
            if(const auto wall=g.level.raycast(from,{step.x,0,step.z},std::hypot(step.x,step.z)+.3f);!wall)o.position+=Vec3{step.x,0,step.z};
            o.position.y+=step.y;
            if(const auto floor=g.level.raycast(o.position+Vec3{0,.6f,0},{0,-1,0},3.5f)) {
                if(o.position.y<floor->point.y){o.position.y=floor->point.y;o.push_velocity.y=std::max(0.0f,o.push_velocity.y);}
                else if(o.position.y>floor->point.y)o.push_velocity.y-=18*dt;
            }
            o.push_velocity.x*=.86f;o.push_velocity.z*=.86f;
            if(o.enemy){o.enemy->home=o.position;}
            if(length(o.push_velocity)<0.05f)o.push_velocity={};
        } else o.push_velocity={};
        // Newly defeated targets: Fury gain, void demons and demon rewards.
        if(o.enemy && o.enemy->defeated && std::find(defeated_seen_.begin(),defeated_seen_.end(),static_cast<std::uint32_t>(i))==defeated_seen_.end()) {
            defeated_seen_.push_back(static_cast<std::uint32_t>(i));
            if(g.upgrades.enabled)g.upgrades.elements.fury=std::min(100.0f,g.upgrades.elements.fury+fury_per_defeat);
            if(o.void_mark) {
                spray(o.position+Vec3{0,.4f,0},{0,1.5f,0},Element::Convexity,22,2.8f,.09f,EffectSprite::Flake,46,2,-1);
                g.demons.summon(g,g.object_models(),DemonKind::Imp,o.position,1);o.void_mark=false;
            }
            if(o.demon_hit)demon_kill_reward(g,g.object_models(),o);
        }
    }
    if(g.upgrades.enabled && g.player.gems>last_gems_)
        g.upgrades.elements.fury=std::min(100.0f,g.upgrades.elements.fury+fury_per_gem*float(g.player.gems-last_gems_));
    last_gems_=g.player.gems;
}
void ElementSystem::spray(Vec3 at,Vec3 velocity,Element element,unsigned count,float spread,float radius,EffectSprite sprite,unsigned lifetime,unsigned blend,float gravity) {
    for(unsigned i=0;i<count && particles_.size()<max_particles;++i) {
        const auto jitter=[&]{return (float(random_.next()%2001)-1000)/1000.0f;};
        const Vec3 v=velocity+Vec3{jitter(),jitter(),jitter()}*spread;
        const float mix=float(random_.next()%100)/100.0f;
        const auto color=lerp(element_color(element),element_core_color(element),mix*.6f);
        particles_.push_back({at,at,v,color,radius*(.75f+.5f*mix),float(random_.next()%628)/100.0f,0,0,
            lifetime*(80+random_.next()%40)/100,sprite,blend,gravity});
    }
}
void ElementSystem::bolt(Vec3 from,Vec3 to,Element element,unsigned lifetime,float jitter,unsigned branches) {
    if(bolts_.size()>=max_bolts)bolts_.erase(bolts_.begin());
    ElementBolt b;b.color=element_color(element);b.core=element_core_color(element);b.lifetime=lifetime;b.width=element==Element::Electric?.035f:.05f;
    const auto axis=to-from;const float n=length(axis);
    const auto side=normalized(cross(std::abs(axis.y)>n*.9f?Vec3{1,0,0}:Vec3{0,1,0},axis)),up=normalized(cross(axis,side));
    const unsigned segments=std::clamp(static_cast<unsigned>(n*6),5u,20u);   // short kinks read as lightning
    const auto r=[&]{return (float(random_.next()%2001)-1000)/1000.0f;};
    for(unsigned k=0;k<=segments;++k) {
        const float t=float(k)/segments,bulge=std::sin(t*std::numbers::pi_v<float>);
        b.points.push_back(from+axis*t+(side*r()+up*r())*(jitter*bulge*std::min(1.5f,n*.35f+.4f)));
    }
    bolts_.push_back(b);
    for(unsigned k=0;k<branches && bolts_.size()<max_bolts;++k) {
        const auto root=b.points[1+random_.next()%(segments-1)];
        const auto tip=root+(axis*.25f+(side*r()+up*r())*.45f*n)*.45f;
        ElementBolt twig;twig.color=b.color;twig.core=b.core;twig.lifetime=lifetime;twig.width=b.width*.6f;
        for(unsigned s=0;s<=3;++s)twig.points.push_back(lerp(root,tip,s/3.0f)+(side*r()+up*r())*jitter*.4f*(s?1.0f:0.0f));
        bolts_.push_back(std::move(twig));
    }
}
void ElementSystem::gather_local(const Level& level,Vec3 center,float radius) {
    local_.clear();local_center_=center;local_radius_=radius;
    for(const auto index:level.collision_candidates(center.x-radius,center.z-radius,center.x+radius,center.z+radius)) {
        const auto& t=level.triangles[index];
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        if(std::min({t.a.x,t.b.x,t.c.x})>center.x+radius || std::max({t.a.x,t.b.x,t.c.x})<center.x-radius ||
           std::min({t.a.z,t.b.z,t.c.z})>center.z+radius || std::max({t.a.z,t.b.z,t.c.z})<center.z-radius ||
           std::min({t.a.y,t.b.y,t.c.y})>center.y+radius || std::max({t.a.y,t.b.y,t.c.y})<center.y-radius)continue;
        local_.push_back(t);
    }
}
std::optional<RayHit> local_raycast(std::span<const Triangle> faces,Vec3 origin,Vec3 direction,float limit) {
    direction=normalized(direction);if(length(direction)<.5f)return {};
    std::optional<RayHit> result;
    for(const auto& t:faces) {
        const auto e1=t.b-t.a,e2=t.c-t.a,p=cross(direction,e2);
        const auto det=dot(e1,p);if(std::abs(det)<1e-8f)continue;
        const auto inv=1/det;const auto rel=origin-t.a;
        const auto u=dot(rel,p)*inv;if(u<-1e-6f || u>1+1e-6f)continue;
        const auto q=cross(rel,e1);const auto v=dot(direction,q)*inv;if(v<-1e-6f || u+v>1+1e-6f)continue;
        const auto d=dot(e2,q)*inv;if(d<0 || d>limit)continue;
        auto n=normalized(cross(e1,e2));if(dot(n,direction)>0)n=n*-1;
        result=RayHit{d,origin+direction*d,n,t.source_id,t.surface_type};limit=d;
    }
    return result;
}
void ElementSystem::decal(Game& g,Vec3 center,float radius,Vec3 color,unsigned lifetime,unsigned blend) {
    if(decals_.size()>=max_decals)decals_.erase(decals_.begin());
    std::vector<Triangle> faces;
    for(const auto index:g.level.collision_candidates(center.x-radius,center.z-radius,center.x+radius,center.z+radius)) {
        const auto& t=g.level.triangles[index];
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        if(std::min({t.a.x,t.b.x,t.c.x})>center.x+radius || std::max({t.a.x,t.b.x,t.c.x})<center.x-radius ||
           std::min({t.a.z,t.b.z,t.c.z})>center.z+radius || std::max({t.a.z,t.b.z,t.c.z})<center.z-radius ||
           std::min({t.a.y,t.b.y,t.c.y})>center.y+1.5f || std::max({t.a.y,t.b.y,t.c.y})<center.y-1.5f)continue;
        faces.push_back(t);
    }
    constexpr unsigned rings=4,sectors=16;
    std::array<std::array<std::optional<Vec3>,sectors>,rings+1> grid{};
    for(unsigned r=0;r<=rings;++r)for(unsigned s=0;s<sectors;++s) {
        const float rad=radius*float(r)/rings,a=s*2*std::numbers::pi_v<float>/sectors;
        const Vec3 top=center+Vec3{std::sin(a)*rad,1.2f,std::cos(a)*rad};
        if(const auto hit=local_raycast(faces,top,{0,-1,0},2.6f);hit && hit->normal.y>.45f)grid[r][s]=hit->point+Vec3{0,.03f,0};
    }
    GroundDecal d;d.color=color;d.lifetime=lifetime;d.blend=blend;
    for(unsigned r=0;r<rings;++r)for(unsigned s=0;s<sectors;++s) {
        const auto& a=grid[r][s];const auto& b=grid[r][(s+1)%sectors];const auto& c=grid[r+1][s];const auto& e=grid[r+1][(s+1)%sectors];
        if(r==0){if(a && c && e)d.faces.push_back({*a,*c,*e});continue;}
        if(a && b && c)d.faces.push_back({*a,*c,*b});
        if(b && c && e)d.faces.push_back({*b,*c,*e});
    }
    if(!d.faces.empty())decals_.push_back(std::move(d));
}
std::optional<SpyroPose> ElementSystem::posed(const Game& g,const SpyroModel* spyro)const {
    if(!spyro || !g.animation.ready())return {};
    const auto appearance=g.cheats.supercharge_appearance;
    auto pose=g.animation.sample(1,appearance);
    pose=spyro->secondary_track(g.animation.state().clip,std::move(pose),g.secondary.sample(1,appearance),appearance);
    if(g.abilities.attacking() && pose.vertices.size()!=230)pose=spyro->frame(0,0);
    g.abilities.pose(pose,1,&g,spyro);
    return pose;
}
void ElementSystem::tick(Game& g,const ObjectModels* models,const SpyroModel* spyro) {
    ++phase_;previous_beam_=beam_;
    for(auto& p:particles_)p.previous=p.position;
    for(auto& n:nodes_){n.previous=n.position;n.previous_radius=n.radius;}
    for(auto& s:shots_){s.previous=s.position;s.previous_spin=s.spin;}
    for(auto& w:waves_)w.previous_radius=w.radius;
    dark_tick(g,spyro);
    melee_tick(g,spyro);
    update_mouth(g,spyro);
    if(!g.upgrades.enabled) {
        breath_active_=false;beam_.active=false;nodes_.clear();
        if(fury_pose_ticks_ || fury_.stage)g.camera.scripted_view.reset();
        if(fury_pose_ticks_ || dark_ticks_){fury_pose_ticks_=0;dark_ticks_=0;}
        fury_.stage=0;
        if(!water_.empty()){water_.clear();water_tick(g,spyro);}
        effects_tick(g);return;
    }
    auto& e=g.upgrades.elements;
    if(!breath_active_ && !shot_cooldown_){if(++regen_delay_>=regen_delay)e.energy=std::min(100.0f,e.energy+regen*dt);}
    else regen_delay_=0;
    // The Fury meter also fills on its own (about 20 s from empty) between Furies.
    if(!fury_pose_ticks_ && !dark_ticks_ && !fury_.stage && e.tier(e.equipped)>=3)e.fury=std::min(100.0f,e.fury+fury_per_second*dt);
    if(!breath_active_)slice_ticks_=0;
    // Upgraded Fire (tier 2+): the retail flame also breaks the containers it
    // normally only heats or bounces off (metal-capped vases C9, chests CA/CB),
    // so every element's breath can destroy every crate. Tier 1 stays retail.
    if(e.tier(Element::Fire)>=2 && !breath_override(g) && g.interactions.flame_active() && phase_%4==0)
        for(auto& o:g.retail_objects)if((is_heavy_container(o.type) || o.type==0xc9) && element_target(o)) {
            const auto d=o.position+Vec3{0,.32f,0}-mouth_;const float n=length(d);
            if(n<2.4f && (n<.3f || dot(d/n,mouth_direction_)>.55f))queue_element_hit(o,{Element::Fire,2,mouth_,element_status::burn,false,0});
        }
    breath_tick(g,models);
    if(shot_queued_){shot_queued_=false;fire_shot(g);}
    shots_tick(g,models);
    waves_tick(g,models);
    if(fury_queued_){fury_queued_=false;fury_start(g,models);}
    fury_tick(g,models);
    water_tick(g,spyro);
    ice_tick(g);
    status_tick(g);
    // Tier-3 supercharge leaves an elemental trail (Dark Fury: a rift trail).
    const auto eq=e.equipped;
    const bool supercharging=g.player.motion==Motion::Supercharge || g.player.motion==Motion::SuperchargeJump;
    if(supercharging && (e.tier(eq)>=3 || dark_ticks_) && phase_%3==0) {
        const auto el=dark_ticks_?Element::Convexity:eq;const auto feet=g.player.position+Vec3{0,.15f,0};
        const unsigned status=el==Element::Water?element_status::soak|element_status::push:el==Element::Ice?element_status::freeze:
            el==Element::Electric?element_status::stun:el==Element::Convexity?element_status::void_mark:element_status::burn;
        hit_sphere(g,feet,.9f,{el,1,feet,status,false,el==Element::Water?4.0f:0},false);
        spray(feet,{0,.6f,0},el,3,.6f,.09f,el==Element::Water?EffectSprite::Droplet:el==Element::Ice?EffectSprite::Snowflake:
            el==Element::Electric?EffectSprite::Spark:el==Element::Convexity?EffectSprite::Mote:EffectSprite::Ember,26,el==Element::Convexity?2:1,el==Element::Water?6.0f:0);
    }
    effects_tick(g);
}
void ElementSystem::effects_tick(Game& g) {
    (void)g;
    for(auto& p:particles_) {
        p.velocity.y-=p.gravity*dt;p.position+=p.velocity*dt;p.radius+=p.growth*dt;
        if(p.sprite==EffectSprite::Steam || p.sprite==EffectSprite::Mote || p.sprite==EffectSprite::Flake)p.velocity=p.velocity*.97f;
        p.spin+=.08f;++p.age;
    }
    std::erase_if(particles_,[](const auto& p){return p.age>=p.lifetime;});
    for(auto& b:bolts_)++b.age;
    std::erase_if(bolts_,[](const auto& b){return b.age>=b.lifetime;});
    for(auto& s:spikes_)if(s.delay)--s.delay;else ++s.age;
    std::erase_if(spikes_,[](const auto& s){return s.age>=s.lifetime;});
    for(auto& d:decals_)++d.age;
    std::erase_if(decals_,[](const auto& d){return d.age>=d.lifetime;});
}
void ElementSystem::dark_tick(Game& g,const SpyroModel* spyro) {
    if(dark_ticks_ && !g.dialogue.active() && !g.scene_locked && !g.portal_travel.active()) {
        if(!--dark_ticks_){exhaustion_ticks_=exhaustion_duration;g.demons.clear(g);
            spray(g.player.position+retail::origin_above_feet,{0,.8f,0},Element::Convexity,26,2.2f,.11f,EffectSprite::Flake,50,2);}
    } else if(exhaustion_ticks_)--exhaustion_ticks_;
    const bool active=dark(g);
    {
        // The supercharge smoke emitter, drifting up and back off his shoulders.
        // Standing still halves the emission so the puffs never stack into a
        // solid disc; moving keeps the retail every-other-tick cadence.
        const Vec3 forward{std::sin(g.player.yaw),0,std::cos(g.player.yaw)};
        const bool moving=std::hypot(g.player.velocity.x,g.player.velocity.z)>2;
        const bool cadence=moving || (++smoke_clock_%4)<2;
        smoke.tick(active && g.cheats.dark_smoke && g.player.swim.mode==WaterMode::Dry && cadence,
            g.player.position+retail::origin_above_feet+Vec3{0,.12f,0}-forward*.08f,g.player.velocity*.3f-forward*.6f+Vec3{0,1.5f,0});
    }
    const auto pose=active?posed(g,spyro):std::nullopt;
    const auto eyes=pose && spyro?spyro_eye_centers(*spyro,*pose,&spyro->clip(g.animation.state().clip,g.cheats.supercharge_appearance)):std::nullopt;
    for(unsigned eye=0;eye<2;++eye) {
        auto& trail=eye_trails_[eye];
        if(eyes) {
            const auto origin=g.player.position+retail::origin_above_feet;
            trail.push_front(origin+retail::rotate_model((*eyes)[eye],g.player.yaw,g.player.pitch,g.player.roll));
            while(trail.size()>5)trail.pop_back();   // a short, faint streak
        } else if(!trail.empty())trail.pop_back();
    }
}
void ElementSystem::melee_tick(Game& g,const SpyroModel* spyro) {
    const auto& a=g.abilities;
    const bool striking=a.attacking() && a.combo_tick>=6 && a.combo_tick<=(a.combo==2?21u:17u);
    const auto pose=striking?posed(g,spyro):std::nullopt;
    const auto& e=g.upgrades.elements;
    const bool elemental=g.upgrades.enabled && e.owns(e.equipped) && (e.equipped!=Element::Fire || e.tier(Element::Fire)>=2);
    const Vec3 color=dark(g)?Vec3{.55f,.25f,.95f}:elemental?element_color(e.equipped):Vec3{1,.86f,.55f};
    std::array<std::optional<Vec3>,3> tips{};
    if(pose && pose->vertices.size()==230) {
        const auto& v=pose->vertices;
        const auto mid=[&](unsigned x,unsigned y){return (v[x]+v[y])*.5f;};
        // Striking front paw tips (carry_pose.cpp limbs 0 / 1) and the tail tip.
        if(a.combo==0)tips[0]=mid(37,38);
        else if(a.combo==1)tips[1]=mid(85,86);
        else {tips[0]=mid(37,38);tips[1]=mid(85,86);tips[2]=v[spyro_tail_tip(*spyro)];}
    }
    const auto origin=g.player.position+retail::origin_above_feet;
    for(unsigned k=0;k<3;++k) {
        auto& trail=melee_[k];
        if(tips[k]) {
            if(trail.points.empty())trail.color=color;
            trail.points.push_front(origin+retail::rotate_model(*tips[k],g.player.yaw,g.player.pitch,g.player.roll));
            trail.color=color;++melee_trail_samples;
            while(trail.points.size()>10)trail.points.pop_back();
        } else if(!trail.points.empty()){trail.points.pop_back();if(!trail.points.empty())trail.points.pop_back();}
    }
    // Tier-3 bonus: the finisher releases a small elemental burst.
    if(g.upgrades.enabled && a.attacking() && a.combo==2 && a.combo_tick==14 && (e.tier(e.equipped)>=3 || dark_ticks_)) {
        const auto el=dark_ticks_?Element::Convexity:e.equipped;
        const auto at=origin+Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*.4f;
        const unsigned status=el==Element::Water?element_status::soak|element_status::push:el==Element::Ice?element_status::freeze:
            el==Element::Electric?element_status::stun:el==Element::Convexity?element_status::void_mark:0;
        hit_sphere(g,at,1.4f,{el,1,origin,status,false,5},false);
        spray(at,{0,1,0},el,18,2.4f,.1f,el==Element::Water?EffectSprite::Droplet:el==Element::Ice?EffectSprite::Snowflake:
            el==Element::Electric?EffectSprite::Spark:el==Element::Convexity?EffectSprite::Mote:EffectSprite::Ember,28,el==Element::Convexity?2:1);
    }
}
} // namespace spyro

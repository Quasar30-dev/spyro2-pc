#include "spyro/demons.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <numbers>

namespace spyro {
namespace {
constexpr float dt=1.0f/60,pi=std::numbers::pi_v<float>;
float flat(Vec3 v){return std::hypot(v.x,v.z);}
float height_of(DemonKind k){switch(k){case DemonKind::Imp:return .78f;case DemonKind::Hound:return .82f;case DemonKind::Wraith:return 1.15f;case DemonKind::Maw:return 2.3f;}return 1;}
unsigned lifetime_of(DemonKind k){switch(k){case DemonKind::Imp:return 12*60;case DemonKind::Hound:return 15*60;case DemonKind::Wraith:return 20*60;case DemonKind::Maw:return 72;}return 600;}
float speed_of(DemonKind k){switch(k){case DemonKind::Imp:return 4.4f;case DemonKind::Hound:return 6.4f;case DemonKind::Wraith:return 5.0f;case DemonKind::Maw:return 0;}return 4;}
Vec3 center_of(const RetailObject& o){return o.boss_body?o.position+Vec3{0,(o.boss_body->bottom+o.boss_body->top)*.5f,0}:o.position+Vec3{0,.32f,0};}
bool protected_near(const Game& g,Vec3 p) {
    const auto& l=g.level;
    if(length(p-l.spawn)<1.0f)return true;
    for(const auto& npc:l.npcs)if(npc.object<g.retail_objects.size() && length(g.retail_objects[npc.object].position-p)<1.3f)return true;
    for(const auto& script:l.scripts)if(script.object<g.retail_objects.size() && length(g.retail_objects[script.object].position-p)<1.2f)return true;
    for(const auto& portal:l.portals){Vec3 c{};for(auto q:portal.aperture)c+=q/5;if(length(c-p)<1.6f)return true;}
    for(const auto& wind:l.whirlwinds)if(wind.object<g.retail_objects.size() && length(g.retail_objects[wind.object].position-p)<1.4f)return true;
    for(const auto& t:l.home_transports)if(t.object<g.retail_objects.size() && length(g.retail_objects[t.object].position-p)<1.4f)return true;
    for(const auto& o:g.retail_objects)if(o.powerup_gate && length(o.position-p)<1.4f)return true;
    for(const auto& objective:l.custom_objectives)for(auto c:objective.checkpoints)if(length(c-p)<.9f)return true;
    return false;
}
}
const char* demon_name(DemonKind k) {
    switch(k){case DemonKind::Imp:return "Rift Imp";case DemonKind::Hound:return "Gloom Hound";case DemonKind::Wraith:return "Hollow Wraith";case DemonKind::Maw:return "Abyssal Maw";}
    return "Demon";
}
std::optional<Vec3> demon_spawn_point(const Game& g,Vec3 near,unsigned salt) {
    for(unsigned ring=0;ring<3;++ring)for(unsigned k=0;k<(ring?8u:1u);++k) {
        const float a=(k+salt*.37f)*2*pi/8,r=ring*.8f;
        const Vec3 probe=near+Vec3{std::sin(a)*r,1.6f,std::cos(a)*r};
        const auto hit=g.level.raycast(probe,{0,-1,0},4.5f);
        if(!hit || hit->normal.y<.72f || hit->surface_type!=UINT32_MAX)continue;          // static, ordinary floor only
        if(const auto water=g.level.water_at(hit->point+Vec3{0,.05f,0});water && water->height>hit->point.y-.05f)continue;
        if(const auto above=g.level.raycast(hit->point+Vec3{0,.05f,0},{0,1,0},1.2f))continue;   // no headroom
        if(protected_near(g,hit->point))continue;
        return hit->point;
    }
    return {};
}
void demon_kill_reward(Game& g,const ObjectModels* models,const RetailObject& victim) {
    ++g.demons.kills;
    if(!models || !models->models.contains(0x10))return;
    // Capped chance: every third demon defeat, at most six per scene visit.
    if(g.demons.kills%3!=1 || g.demons.butterflies>=6)return;
    RetailObject food;food.type=0x10;food.runtime_spawned=true;food.source_id=victim.id;food.held_gem=255;food.draw_flags=0x10;
    food.position=food.previous_position=victim.position+Vec3{0,.5f,0};
    food.animation=food.previous_animation=start_object_animation(models->models.at(0x10));
    food.butterfly=ButterflyState{food.position};
    for(const auto& o:g.retail_objects)if((o.type==0x10 || o.type==0xd3) && o.runtime_spawned && o.source_id==victim.id && !o.collected)return;
    g.retail_objects.push_back(std::move(food));++g.demons.butterflies;
}
std::uint32_t DemonSystem::allocate(Game& g,const ObjectModels& models,DemonKind kind,Vec3 position) {
    RetailObject o;o.type=demon_type(kind);o.pc_actor=static_cast<std::uint16_t>(kind);o.runtime_spawned=true;o.held_gem=255;
    o.size=32;o.persistence=255;o.draw_flags=0x40;o.position=o.previous_position=position;o.source_id=UINT32_MAX;
    o.animation=o.previous_animation=start_object_animation(models.models.at(o.type),static_cast<unsigned>(DemonClip::Erupt));
    o.hidden=true;
    for(std::size_t i=0;i<g.retail_objects.size();++i) {
        auto& slot=g.retail_objects[i];
        if(slot.pc_actor && slot.collected){slot=std::move(o);return static_cast<std::uint32_t>(i);}
    }
    g.retail_objects.push_back(std::move(o));return static_cast<std::uint32_t>(g.retail_objects.size()-1);
}
void DemonSystem::erupt_at(Game& g,Vec3 position,bool floating) {
    if(eruptions_.size()>=12)eruptions_.erase(eruptions_.begin());
    Eruption e;e.center=position;e.floating=floating;e.radius=.95f;
    if(floating)++floating_rifts;
    else {
        // Seven jagged floor cracks hugging the sampled surface.
        for(unsigned arm=0;arm<7;++arm) {
            const float a=arm*2*pi/7+float(random_.next()%100)/250.0f;std::optional<Vec3> previous=position+Vec3{0,.03f,0};
            for(unsigned s=1;s<=4;++s) {
                const float r=s*.26f,w=(float(random_.next()%100)-50)/400.0f;
                const Vec3 probe=position+Vec3{std::sin(a+w)*r,.6f,std::cos(a+w)*r};
                const auto hit=g.level.raycast(probe,{0,-1,0},1.4f);if(!hit)break;
                const auto p=hit->point+Vec3{0,.03f,0};e.cracks.push_back({*previous,p});previous=p;
            }
        }
        g.destruction.emit_chips(g,position,.7f,8,0x5eed+ticks_*13);
    }
    eruptions_.push_back(std::move(e));
    g.camera.shake(600,10);g.sound_events.push_back({SoundScope::Global,0x14,0,position});
}
bool DemonSystem::summon(Game& g,const ObjectModels* models,DemonKind kind,Vec3 near,unsigned count) {
    if(!models || !models->models.contains(demon_type(kind)) || g.dialogue.active() || g.scene_locked || g.portal_travel.active()){refunded+=count;return false;}
    bool any=false;
    for(unsigned n=0;n<count;++n) {
        // Rate limit: at most two eruptions per second.
        std::erase_if(eruption_ticks_,[&](unsigned t){return ticks_-t>=60;});
        if(eruption_ticks_.size()>=max_eruptions_per_second && kind!=DemonKind::Maw){++refunded;continue;}
        if(kind==DemonKind::Maw) {
            const auto maw=std::find_if(demons_.begin(),demons_.end(),[](const auto& d){return d.kind==DemonKind::Maw && d.phase!=DemonPhase::Sink;});
            if(maw!=demons_.end()){maw->age=0;maw->hits=0;++refreshed;any=true;continue;}
        }
        if(demons_.size()>=max_active) {
            // Beyond the cap the oldest demon is refreshed into the new summon.
            auto oldest=std::max_element(demons_.begin(),demons_.end(),[](const auto& a,const auto& b){return a.age<b.age;});
            if(oldest->object<g.retail_objects.size())g.retail_objects[oldest->object].collected=true;
            demons_.erase(oldest);++refreshed;
        }
        bool floating=false;
        auto point=demon_spawn_point(g,near+Vec3{float(n)*.6f,0,float(n%2)*.5f},n+ticks_);
        if(!point) {
            // No eligible floor here (water, special surface): a floating rift
            // opens above the nearest valid floor to Spyro, or the summon fails.
            point=demon_spawn_point(g,g.player.position,n);
            if(!point){++refunded;continue;}
            floating=true;*point+=Vec3{0,kind==DemonKind::Wraith?0.0f:0.0f,0};
        }
        Demon d;d.kind=kind;d.floor=*point;d.last=*point;d.lifetime=lifetime_of(kind);
        d.object=allocate(g,*models,kind,*point-Vec3{0,height_of(kind),0});
        auto& body=g.retail_objects[d.object];
        const auto face=g.player.position-*point;body.rotation[2]=body.previous_rotation[2]=retail::original_heading(std::atan2(face.x,face.z))/16;
        demons_.push_back(d);erupt_at(g,*point,floating);eruption_ticks_.push_back(ticks_);++summoned;any=true;
    }
    return any;
}
void DemonSystem::sink(Game& g,Demon& d) {
    if(d.phase==DemonPhase::Sink)return;
    d.phase=DemonPhase::Sink;d.phase_ticks=0;
    if(d.object<g.retail_objects.size()) {
        auto& o=g.retail_objects[d.object];const auto& models=*g.object_models();
        o.animation=start_object_animation(models.models.at(o.type),static_cast<unsigned>(DemonClip::Sink));
        if(d.gem!=UINT32_MAX && d.gem<g.retail_objects.size())g.retail_objects[d.gem].position.y=std::max(g.retail_objects[d.gem].position.y,d.floor.y);
    }
    if(eruptions_.size()<12){Eruption e;e.center=d.floor;e.radius=.7f;e.lifetime=24;eruptions_.push_back(e);}
}
void DemonSystem::clear(Game& g) {
    for(const auto& d:demons_)if(d.object<g.retail_objects.size() && g.retail_objects[d.object].pc_actor)g.retail_objects[d.object].collected=true;
    demons_.clear();eruptions_.clear();eruption_ticks_.clear();
}
bool DemonSystem::absorb_hit(Game& g) {
    for(auto& d:demons_)if(d.kind==DemonKind::Wraith && d.phase==DemonPhase::Act && !d.absorbed) {
        d.absorbed=true;++hits_absorbed;
        if(d.object<g.retail_objects.size())g.elements.spray(g.retail_objects[d.object].position+Vec3{0,.8f,0},{0,1,0},Element::Convexity,20,2.4f,.1f,EffectSprite::Flake,36,2);
        return true;
    }
    return false;
}
void DemonSystem::tick(Game& g,const ObjectModels* models) {
    ++ticks_;
    for(auto& e:eruptions_)++e.age;
    std::erase_if(eruptions_,[](const auto& e){return e.age>=e.lifetime;});
    if(demons_.empty())return;
    if(!models || g.dialogue.active() || g.scene_locked || g.portal_travel.active() || g.whirlwind.index!=UINT32_MAX){clear(g);return;}
    const auto spyro=g.player.position;
    for(auto& d:demons_) {
        if(d.object>=g.retail_objects.size() || !g.retail_objects[d.object].pc_actor){d.phase=DemonPhase::Sink;d.phase_ticks=99;continue;}
        auto& o=g.retail_objects[d.object];
        begin_object_transform(o,1);++d.age;++d.phase_ticks;d.previous_rise=d.rise;
        const float h=height_of(d.kind);
        switch(d.phase) {
        case DemonPhase::Crack:
            o.hidden=true;
            if(d.phase_ticks>=8){d.phase=DemonPhase::Rise;d.phase_ticks=0;o.hidden=false;
                o.animation=start_object_animation(models->models.at(o.type),static_cast<unsigned>(DemonClip::Erupt));}
            break;
        case DemonPhase::Rise:
            d.rise=std::min(1.0f,d.phase_ticks/12.0f);
            o.material_color=0x3e000000u|0x00a01858u;   // violet emergence flash
            if(d.phase_ticks>=12){d.phase=DemonPhase::Act;d.phase_ticks=0;o.material_color=0;
                o.animation=start_object_animation(models->models.at(o.type),static_cast<unsigned>(d.kind==DemonKind::Maw?DemonClip::Attack:DemonClip::Idle));}
            break;
        case DemonPhase::Act:
            d.rise=1;act(g,*models,d,o);
            if(d.age>=d.lifetime)sink(g,d);
            // Leash: re-erupt beside Spyro when stuck or left far behind.
            if(d.kind!=DemonKind::Maw && (d.stuck>120 || flat(o.position-spyro)>16)) {
                if(const auto p=demon_spawn_point(g,spyro-Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*1.4f,ticks_)) {
                    erupt_at(g,*p,false);d.floor=*p;o.position=o.previous_position=*p-Vec3{0,h,0};d.phase=DemonPhase::Rise;d.phase_ticks=0;d.rise=0;d.stuck=0;
                    ++stuck_reerupts;
                } else sink(g,d);
            }
            if(d.kind!=DemonKind::Wraith && d.phase==DemonPhase::Act)
                if(const auto water=g.level.water_at(o.position+Vec3{0,.2f,0});water && water->height>o.position.y+.15f) {
                    g.elements.spray(o.position+Vec3{0,.4f,0},{0,1.5f,0},Element::Water,10,.8f,.2f,EffectSprite::Steam,40,0,-1);sink(g,d);
                }
            break;
        case DemonPhase::Sink:
            d.rise=std::max(0.0f,1-d.phase_ticks/16.0f);
            if(d.phase_ticks%3==0)g.elements.spray(o.position+Vec3{0,h*.5f*d.rise,0},{0,.6f,0},Element::Convexity,2,.8f,.08f,EffectSprite::Flake,30,2,-.5f);
            break;
        }
        // Vertical placement from the eruption floor (rise/sink through it).
        if(d.phase!=DemonPhase::Act || d.kind==DemonKind::Maw) {
            const float base=d.kind==DemonKind::Wraith?d.floor.y+.5f*d.rise:d.floor.y;
            o.position.y=base-(1-d.rise)*h;
            if(d.phase!=DemonPhase::Act){o.position.x=d.phase==DemonPhase::Sink?o.position.x:d.floor.x;o.position.z=d.phase==DemonPhase::Sink?o.position.z:d.floor.z;}
        }
    }
    for(auto& d:demons_)if(d.phase==DemonPhase::Sink && d.phase_ticks>=16 && d.object<g.retail_objects.size() && g.retail_objects[d.object].pc_actor)
        g.retail_objects[d.object].collected=true;
    std::erase_if(demons_,[](const auto& d){return d.phase==DemonPhase::Sink && d.phase_ticks>=16;});
}
void DemonSystem::act(Game& g,const ObjectModels& models,Demon& d,RetailObject& o) {
    const auto spyro=g.player.position;
    const auto set_clip=[&](DemonClip clip) {
        if(o.animation.next_clip!=static_cast<unsigned>(clip))o.animation=start_object_animation(models.models.at(o.type),static_cast<unsigned>(clip));
    };
    const auto face=[&](Vec3 toward){const auto v=toward-o.position;if(flat(v)>.01f)o.rotation[2]=retail::original_heading(std::atan2(v.x,v.z))/16;};
    const auto move=[&](Vec3 goal,float stop)->bool {
        auto delta=goal-o.position;delta.y=0;const float n=flat(delta);
        if(n<=stop+.02f){set_clip(DemonClip::Idle);return true;}   // tolerance: float steps never land exactly on `stop`
        const auto dir=delta/n;const auto step=dir*std::min(n-stop,speed_of(d.kind)*dt);
        face(goal);set_clip(DemonClip::Move);
        if(d.kind==DemonKind::Wraith){o.position+=step;return false;}
        if(g.level.raycast(o.position+Vec3{0,.35f,0},dir,length(step)+.28f)){++d.stuck;return false;}
        const auto next=o.position+step;
        if(const auto floor=g.level.raycast(next+Vec3{0,.9f,0},{0,-1,0},2.4f);floor && floor->normal.y>.5f) {
            if(const auto water=g.level.water_at(floor->point+Vec3{0,.05f,0});water && water->height>floor->point.y+.2f){++d.stuck;return false;}
            o.position={next.x,floor->point.y,next.z};d.floor=o.position;d.stuck=d.stuck>2?d.stuck-2:0;
        } else ++d.stuck;
        return false;
    };
    const auto valid_enemy=[&](const RetailObject& t){return element_target(t) && (t.enemy || (t.power_target && !t.power_target->scenery));};
    const auto nearest=[&](auto predicate,float range)->std::uint32_t {
        std::uint32_t best=UINT32_MAX;float best_d=range;
        for(std::size_t i=0;i<g.retail_objects.size();++i) {
            const auto& t=g.retail_objects[i];if(!predicate(t))continue;
            const float dist=length(t.position-o.position);if(dist>=best_d || flat(t.position-spyro)>leash+2)continue;
            if(g.level.raycast(o.position+Vec3{0,.5f,0},center_of(t)-(o.position+Vec3{0,.5f,0}),std::max(0.0f,dist-.4f)))continue;
            best_d=dist;best=static_cast<std::uint32_t>(i);
        }
        return best;
    };
    const auto strike=[&](RetailObject& t,unsigned strength,unsigned status,float push) {
        queue_element_hit(t,{Element::Convexity,strength,o.position,status,true,push});++d.hits;
        g.elements.spray(center_of(t),{0,.8f,0},Element::Convexity,6,1.6f,.07f,EffectSprite::Mote,22,2);
    };
    if(d.cooldown)--d.cooldown;
    switch(d.kind) {
    case DemonKind::Imp: {
        const auto container=[](const RetailObject& t){return is_breakable(t.type) && !t.collected && t.state>=0 && !t.pc_carried && !t.attack_flags;};
        if(d.target>=g.retail_objects.size() || (!valid_enemy(g.retail_objects[d.target]) && !container(g.retail_objects[d.target])))d.target=UINT32_MAX;
        if(d.target==UINT32_MAX && d.age%15==0)d.target=nearest(valid_enemy,9);
        // No enemy: open a nearby basket/vase so Sparx can collect its gems.
        if(d.target==UINT32_MAX && d.age%15==7)d.target=nearest(container,6);
        if(d.target!=UINT32_MAX) {
            auto& t=g.retail_objects[d.target];
            if(move(t.position,.75f) && !d.cooldown) {
                face(t.position);set_clip(DemonClip::Attack);d.cooldown=30;
                if(is_breakable(t.type)){strike(t,2,0,0);++containers_opened;d.target=UINT32_MAX;}
                else strike(t,2,0,4);
            }
        } else {
            const auto behind=spyro-Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*1.8f+Vec3{std::sin(float(d.object))*.9f,0,std::cos(float(d.object))*.9f};
            move(behind,.5f);
        }
        break;
    }
    case DemonKind::Hound: {
        if(d.carrying) {
            // Deliver the fetched gem beside Spyro, where Sparx collects it.
            if(d.gem<g.retail_objects.size() && !g.retail_objects[d.gem].collected) {
                auto& gem=g.retail_objects[d.gem];
                gem.position=gem.previous_position=o.position+retail::rotate_model({0,.45f,.42f},retail::native_heading(o.rotation[2]*16));
                const auto drop=spyro+Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*1.1f;
                if(move(drop,.4f)){gem.position=gem.previous_position=Vec3{drop.x,spyro.y+.25f,drop.z};gem.gem_age=0;d.carrying=false;d.gem=UINT32_MAX;++gems_fetched;}
            } else {d.carrying=false;d.gem=UINT32_MAX;}
            break;
        }
        if(d.target>=g.retail_objects.size() || !valid_enemy(g.retail_objects[d.target]))d.target=UINT32_MAX;
        if(d.target==UINT32_MAX && d.age%12==0)d.target=nearest(valid_enemy,8);
        if(d.target!=UINT32_MAX) {
            auto& t=g.retail_objects[d.target];
            if(move(t.position,.7f)) {
                face(t.position);set_clip(DemonClip::Attack);
                if(!d.cooldown){strike(t,0,element_status::stun,0);d.cooldown=40;d.hits=0;}
                if(d.cooldown==1)strike(t,3,0,1);   // pinned, then dragged under
            }
            break;
        }
        if(d.age%20==0) {
            // Fetch a resting loose gem Spyro has not reached (far or out of reach).
            float best=14;
            for(std::size_t i=0;i<g.retail_objects.size();++i) {
                const auto& gem=g.retail_objects[i];
                if(gem.type!=1 || gem.collected || gem.state!=0 || gem.runtime_spawned || !gem.gem_value)continue;
                const float from_spyro=length(gem.position-spyro);
                if(from_spyro<3.2f && std::abs(gem.position.y-spyro.y)<1.2f)continue;
                if(from_spyro<best && std::abs(gem.position.y-o.position.y)<1.4f){best=from_spyro;d.gem=static_cast<std::uint32_t>(i);}
            }
        }
        if(d.gem<g.retail_objects.size()) {
            auto& gem=g.retail_objects[d.gem];
            if(gem.collected){d.gem=UINT32_MAX;break;}
            if(move(gem.position,.45f))d.carrying=true;
            break;
        }
        move(spyro-Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*2.2f,.6f);
        break;
    }
    case DemonKind::Wraith: {
        // Hover by Spyro; intercept enemy projectiles; fire void bolts.
        d.hover+=.05f;
        const auto side=Vec3{std::cos(g.player.yaw),0,-std::sin(g.player.yaw)};
        const auto goal=spyro+side*-1.1f-Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*.6f+Vec3{0,1.0f+std::sin(d.hover)*.15f,0};
        move(goal,.2f);o.position.y=std::lerp(o.position.y,goal.y,.12f);d.floor={o.position.x,spyro.y,o.position.z};
        face(spyro+Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*4);
        for(auto& shot:g.retail_objects)if(shot.enemy_projectile && !shot.collected && (length(shot.position-spyro-retail::origin_above_feet)<2.6f || length(shot.position-o.position)<1.6f)) {
            shot.collected=true;shot.state=-1;++projectiles_blocked;
            g.elements.bolt(o.position+Vec3{0,.6f,0},shot.position,Element::Convexity,6,.12f,1);
            g.elements.spray(shot.position,{0,.4f,0},Element::Convexity,8,1.2f,.07f,EffectSprite::Flake,24,2);
        }
        if(!d.cooldown) {
            const auto t=nearest(valid_enemy,9);
            if(t!=UINT32_MAX) {
                auto& target=g.retail_objects[t];face(target.position);set_clip(DemonClip::Attack);
                g.elements.bolt(o.position+Vec3{0,.65f,0},center_of(target),Element::Convexity,8,.16f,1);
                strike(target,2,element_status::void_mark,2);d.cooldown=45;
            }
        }
        break;
    }
    case DemonKind::Maw: {
        set_clip(DemonClip::Attack);
        if(d.age>=10 && d.age<=26)
            for(auto& t:g.retail_objects) {
                if(!valid_enemy(t) || t.boss_body)continue;   // boss exclusion
                const auto delta=t.position-d.floor;if(flat(delta)>3.2f || std::abs(delta.y)>1.5f)continue;
                if(d.age%4==0)queue_element_hit(t,{Element::Convexity,0,d.floor,element_status::pull,true,6});
                if(d.age==24)strike(t,3,0,0);
            }
        if(d.age%2==0)g.elements.spray(d.floor+Vec3{0,.2f,0},{0,1.2f,0},Element::Convexity,3,1.8f,.1f,EffectSprite::Mote,26,2);
        break;
    }
    }
}
} // namespace spyro

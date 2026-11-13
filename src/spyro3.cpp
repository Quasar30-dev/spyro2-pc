#include "spyro/spyro3.hpp"
#include "spyro/enemy.hpp"
#include "spyro/object_interaction.hpp"
#include <algorithm>
#include <cmath>

namespace spyro {
RetailObject make_yotd_reward_gem(const RetailObject& source,const ObjectModels& models,
    Vec3 origin,bool charging,retail::Random& random) {
    const auto value=source.held_gem;
    if(value>=models.gem_variants.size())throw FormatError("Invalid YOTD reward denomination");
    const auto variant=models.gem_variants[value];
    if(variant>=5 || models.gem_values[variant]!=value)throw FormatError("Unmapped YOTD reward denomination");
    RetailObject gem;gem.source_game=GameBuild::YearOfTheDragonUS;gem.source_type=gem.type=1;
    gem.runtime_spawned=true;gem.source_id=source.id;gem.draw_flags=0x18;gem.held_gem=value;
    gem.variant=static_cast<std::uint8_t>(variant);gem.model=static_cast<std::uint8_t>(models.gem_meshes[variant]);
    gem.material_color=models.gem_colors[variant];gem.gem_value=value;gem.state=2;gem.bounces=3;
    gem.position=gem.previous_position=source.position+Vec3{0,.25f,0};
    // Reuse S2's state-2 launch speed and state-3 charge delivery. This is a
    // provisional native release, not a claim of YOTD overlay equivalence.
    if(charging && length(gem.position-origin)<2.5f) {
        gem.state=3;gem.gem_flight_origin=gem.position;
        for(auto& spin:gem.gem_spin)spin=static_cast<int>(random.next()&14)-7;
    } else gem.script_velocity={static_cast<int>(random.next()&31)-16,static_cast<int>(random.next()&31)-16,140};
    return gem;
}
void initialize_yotd_enemy_objects(Level& level,const ObjectModels& models) {
    if(level.source_game!=GameBuild::YearOfTheDragonUS || level.wad_slot!=99 || level.source_area)return;
    // Actual first-frame model/record identification: Sunny Villa FD small
    // shield Rhynoc (12), EE large club Rhynoc (9). F9 lion and 1BB cinematic
    // copy are deliberately excluded. See docs/SPYRO3_FORMATS.md.
    for(auto& o:level.objects)if(o.type==0xfd || o.type==0xee) {
        const auto bank=models.models.find(o.type);
        if(bank==models.models.end() || !bank->second.animated || !bank->second.cylinder)continue;
        EnemyState e;e.kind=o.type==0xfd?EnemyKind::SmallLizard:EnemyKind::ClubLizard;
        e.home=o.position;e.home_heading=o.rotation[2];e.notice=7;e.reach=o.type==0xfd?1.0f:1.3f;
        e.speed=o.type==0xfd?1.8f:1.2f;e.initialized=true;o.enemy=std::move(e);
    }
}
std::optional<EnemyHit> EnemySystem::yotd_tick(std::vector<RetailObject>& objects,const ObjectModels& models,
    const Level& level,Vec3 origin,bool protected_player) {
    const bool script=(ticks_++&1)==0;std::optional<EnemyHit> hit;std::vector<RetailObject> pending;
    // The original class3F3 handler waits for its linked parent to die. Native
    // fallback releases at the next script tick; original delays are not mapped.
    if(script)for(auto& child:objects)if(child.source_type==0x3f3 && !child.collected && child.source_id<objects.size()) {
        const auto& parent=objects[child.source_id];
        if(parent.id==child.source_id && (parent.collected || parent.state<0)) {
            pending.push_back(make_yotd_reward_gem(child,models,origin,false,random_));
            child.collected=true;child.state=-1;
        }
    }
    for(auto& o:objects) {
        if(o.source_game!=GameBuild::YearOfTheDragonUS || o.retail_script || !o.enemy || o.collected || o.state<0)continue;
        auto& e=*o.enemy;
        if(enemy_accepts_attack(o,o.attack_flags)) {
            const bool charging=(o.attack_flags&(charge_attack|0x100000))!=0;
            e.defeated=true;e.phase=EnemyPhase::Dying;o.collected=true;o.state=-1;
            if(o.held_gem!=255)pending.push_back(make_yotd_reward_gem(o,models,origin,charging,random_));
        }
        o.attack_flags=0;o.attack_sourced=false;
        if(e.defeated || o.frozen_ticks || o.stun_ticks)continue;
        if(e.cooldown)--e.cooldown;
        o.transform_phase=std::min(o.transform_phase+1,o.transform_span);
        if(!script)continue;
        begin_object_transform(o,2);e.age+=2;
        auto toward=origin-o.position;toward.y=0;const float distance=length(toward);
        const auto head=o.position+Vec3{0,.6f,0},ray=origin-head;
        const auto wall=level.raycast(head,ray,length(ray));
        const bool seen=distance<e.notice && std::abs(origin.y-o.position.y)<2 && (!wall || wall->distance>=length(ray)-.05f);
        if(!seen || protected_player){e.phase=EnemyPhase::Idle;continue;}
        const auto direction=distance>.001f?toward/distance:Vec3{0,0,1};
        o.rotation[2]=retail::original_heading(std::atan2(direction.x,direction.z))/16;
        if(distance<=e.reach) {
            e.phase=EnemyPhase::Swing;
            if(!e.cooldown){hit=EnemyHit{o.position,o.id};e.cooldown=90;}
            continue;
        }
        e.phase=EnemyPhase::Chase;
        auto next=o.position+direction*(e.speed/30);
        if(length(next-e.home)>8)continue;
        const auto floor=level.raycast(next+Vec3{0,.8f,0},{0,-1,0},1.6f);
        if(!floor || floor->normal.y<.55f || std::abs(floor->point.y-o.position.y)>.6f)continue;
        const auto bank=models.models.find(o.type);if(bank==models.models.end() || !bank->second.cylinder)continue;
        const float radius=bank->second.cylinder->radius/retail::units_per_world;
        Vec3 velocity=direction*e.speed;bool grounded=false;
        next.y=floor->point.y;
        next=level.resolve_sphere(next+Vec3{0,radius,0},radius,velocity,grounded)-Vec3{0,radius,0};
        o.position=next;
        // Keep the authored animation rather than guessing YOTD's attack or
        // death clip numbers. Pursuit/contact/damage rules above are native.
    }
    unsigned id=0;for(const auto& o:objects)id=std::max(id,o.id+1);
    for(auto& o:pending){o.id=id++;objects.push_back(std::move(o));}
    return hit;
}
} // namespace spyro

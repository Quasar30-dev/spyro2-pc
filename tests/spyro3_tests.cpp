#include "spyro/powerup_import.hpp"
#include "spyro/savegame.hpp"
#include "spyro/yotd_scripts.hpp"
#include <algorithm>
#include <cstdlib>
#include <bit>
#include <chrono>
#include <iostream>
#include <memory>
#include <numbers>
#include <set>
#include <sstream>

using namespace spyro;
namespace {
unsigned checks{},failures{},areas{},fallbacks{},projections{},gem_cases{},container_cases{},enemy_cases{},direct_rewards{},linked_rewards{},vm_enemy_cases{},vm_input_cases{},vm_clone_cases{},vm_fault_cases{},roll_cases{};
std::string context;
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(context+": "+why);}
template<class F> void stage(const char* name,F&& f) {
    try{f();}
    catch(const std::exception& e){++failures;std::cerr<<context<<" / "<<name<<": "<<e.what()<<'\n';}
}
std::uint64_t hash_model(const ObjectModel& m) {
    std::uint64_t h=1469598103934665603ull;
    const auto mix=[&](unsigned v){h^=v;h*=1099511628211ull;};
    mix(m.source_offset);mix(m.source_bytes);mix(m.animation_count);mix(unsigned(m.meshes.size()));
    for(const auto& mesh:m.meshes) {
        mix(unsigned(mesh.vertices.size()));mix(unsigned(mesh.faces.size()));
        for(const auto p:mesh.vertices){mix(std::bit_cast<unsigned>(p.x));mix(std::bit_cast<unsigned>(p.y));mix(std::bit_cast<unsigned>(p.z));}
        for(const auto& f:mesh.faces)for(const auto v:f.vertices)mix(v);
    }
    for(const auto& clip:m.animations)for(const auto& frame:clip.frames) {
        mix(frame.ticks);mix(frame.mesh);mix(unsigned(frame.vertices.size()));
        for(const auto p:frame.vertices){mix(std::bit_cast<unsigned>(p.x));mix(std::bit_cast<unsigned>(p.y));mix(std::bit_cast<unsigned>(p.z));}
    }
    return h;
}
void tick(Game& g,const SpyroModel& model,InputState in={}) {
    g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,120u);
    g.advance(FixedClock::step,in);
    check(finite(g.player.position) && finite(g.player.velocity),"Native controller produced a non-finite player");
    auto pose=model.secondary_track(g.animation.state().clip,g.animation.sample(.5f),g.secondary.sample(.5f));
    g.abilities.pose(pose,.5f,&g,&model);
    check(!pose.vertices.empty(),"Controller produced an empty Spyro pose");
    check(std::all_of(pose.vertices.begin(),pose.vertices.end(),[](Vec3 p){return finite(p);}),"Controller produced a non-finite composed pose");
    for(const auto& f:model.clip(g.animation.state().clip).faces)for(unsigned c=0;c<(f.quad?4u:3u);++c)
        check(f.vertices[c]<pose.vertices.size(),"Primary/secondary composition changed the face topology");
}
void ticks(Game& g,const SpyroModel& model,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)tick(g,model,in);}
void park(Game& g,Vec3 feet,Vec3 direction={0,0,1}) {
    g.player.position=g.player.previous_position=feet+Vec3{0,.015f,0};g.player.velocity={};
    g.player.yaw=g.player.previous_yaw=std::atan2(direction.x,direction.z);
    g.player.pitch=g.player.previous_pitch=g.player.roll=g.player.previous_roll=0;
    g.player.grounded=true;g.player.airborne_ticks=0;g.player.motion=Motion::Idle;
    g.player.air={};g.player.jump={};g.player.swim={};g.player.dodge={};g.player.skate={};g.player.slide={};
    g.player.hurt_ticks=g.player.recoil_ticks=0;g.player.charge_turn_velocity=0;
    g.powers.end_flight();g.flame_action.reset();g.interactions.stop_flame();
    g.animation.reset(g.spyro_model(),0);g.secondary.reset(g.spyro_model(),g.animation.state());
    g.camera.reset(g.level,g.player.position,g.player.yaw);
}
struct Walk {Vec3 point,direction;float reach{};};
Walk walk_point(const Level& level) {
    struct Candidate {Vec3 point;float distance;};std::vector<Candidate> candidates;
    for(const auto& t:level.triangles) {
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        const auto n=normalized(cross(t.b-t.a,t.c-t.a));
        if(std::abs(n.y)<.8f || length(cross(t.b-t.a,t.c-t.a))<.08f)continue;
        const auto p=(t.a+t.b+t.c)/3;
        candidates.push_back({p,length(p-level.spawn)});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    Walk best;float longest=0;
    for(unsigned c=0;c<std::min<std::size_t>(candidates.size(),160);++c)for(unsigned angle=0;angle<16;++angle) {
        const float a=angle*std::numbers::pi_v<float>/8;const Vec3 direction{std::sin(a),0,std::cos(a)};
        auto start=candidates[c].point;float reach=0;
        const auto base=level.raycast(start+Vec3{0,.25f,0},{0,-1,0},.5f);
        if(!base || std::abs(base->normal.y)<.8f)continue;start=base->point;
        if(level.raycast(start+Vec3{0,.1f,0},{0,1,0},2.5f) || level.water_at(start+retail::origin_above_feet))continue;
        for(float distance=.25f;distance<=12.0f;distance+=.25f) {
            const auto p=start+direction*distance;
            const auto floor=level.raycast(p+Vec3{0,.75f,0},{0,-1,0},1.5f);
            if(!floor || std::abs(floor->normal.y)<.75f || std::abs(floor->point.y-start.y)>.35f || level.water_at(floor->point+retail::origin_above_feet))break;
            if(level.raycast(start+Vec3{0,.65f,0},direction,distance+.25f))break;
            reach=distance;
        }
        if(reach>longest){longest=reach;best={start,direction,reach};}
        if(reach>=12)return best;
    }
    check(longest>=2.5f,"No clear supported route was found on the actual scene collision");return best;
}
void controller_check(bool ok,const char* why,const Game& g,const Walk& walk) {
    if(ok){check(true,why);return;}
    std::ostringstream out;out<<why<<" [route="<<walk.point.x<<','<<walk.point.y<<','<<walk.point.z
        <<" direction="<<walk.direction.x<<','<<walk.direction.z<<" reach="<<walk.reach
        <<" feet="<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z
        <<" vy="<<g.player.velocity.y<<" grounded="<<g.player.grounded<<" water="<<unsigned(g.player.swim.mode)
        <<" motion="<<unsigned(g.player.motion)<<" requested="<<g.animation.requested()
        <<" source="<<g.animation.state().clip<<':'<<g.animation.state().frame<<" pitch="<<g.player.pitch;
    if(const auto down=g.level.raycast(g.player.position+Vec3{0,.75f,0},{0,-1,0},20))
        out<<" floor="<<down->distance<<" normal="<<down->normal.y<<" surface="<<down->surface_type;
    else out<<" floor=none";
    if(const auto up=g.level.raycast(g.player.position+Vec3{0,.01f,0},{0,1,0},20))out<<" headroom="<<up->distance;
    else out<<" headroom=clear";
    out<<']';check(false,out.str().c_str());
}
void assets(const Level& level,const SpyroModel& model,const ObjectModels& objects,const PowerupAssets& effects,const YotdDonorBank& donor) {
    check(!level.visual_vertices.empty() && !level.triangles.empty() && !level.sky_vertices.empty(),"Actual scene geometry is empty");
    check(level.texture_rgba.size()==std::size_t(level.texture_width)*level.texture_height*4,"Terrain atlas extent is invalid");
    for(const auto& v:level.visual_vertices)check(finite(v.position) && finite(v.color) && std::isfinite(v.u) && std::isfinite(v.v) && v.material<level.terrain_materials.size(),"Invalid actual terrain vertex");
    for(const auto& t:level.triangles)check(finite(t.a) && finite(t.b) && finite(t.c),"Invalid actual collision triangle");
    for(const auto& v:level.sky_vertices)check(finite(v.direction) && finite(v.color),"Invalid actual sky vertex");
    check(model.source_game==GameBuild::YearOfTheDragonUS && objects.source_game==GameBuild::YearOfTheDragonUS,"Donor replaced the source game identity");
    for(unsigned clip:{0u,2u,5u,6u,12u,16u,21u,25u,33u,34u,35u,61u})check(model.animations.contains(clip) && !model.animations.at(clip).frames.empty(),"Required native or PC power animation is missing");
    for(const auto& [id,clip]:model.animations) {
        const auto pose=model.frame(id,0);check(!pose.vertices.empty(),"Empty decoded Spyro animation");
        for(const auto& f:clip.faces)for(unsigned c=0;c<(f.quad?4u:3u);++c)check(f.vertices[c]<pose.vertices.size(),"Invalid decoded Spyro animation topology");
    }
    for(const unsigned type:retail_power_types)check(objects.models.contains(objects.effect_type(type)) && objects.effect_type(type)>=0x8000,"Reserved PC projectile/effect model is missing");
    check(!effects.wing_trail.texture_rgba.empty() && !effects.fire_particle.texture_rgba.empty() && !effects.green_particle.texture_rgba.empty(),"PC power effect sprites are missing");
    check(!donor.hud.rgba.empty() && !donor.ui.rgba.empty(),"Donor HUD/interface assets are empty");
    for(const auto& o:level.objects)check(o.source_game==GameBuild::YearOfTheDragonUS && o.source_area==level.source_area,"Placed object source identity was changed");
}
void movement(Game g,const SpyroModel& model,const Walk& walk) {
    park(g,walk.point,walk.direction);ticks(g,model,10);
    check(g.player.grounded,"Spyro did not settle onto the actual scene floor");
    const auto start=g.player.position;InputState run;run.move_x=walk.direction.x;run.move_z=walk.direction.z;
    ticks(g,model,28,run);check(length(g.player.position-start)>.25f,"Safe-route walking did not move Spyro");
    check(g.player.position.y>walk.point.y-1 && g.player.grounded,"Safe-route walking lost actual floor support");
    park(g,walk.point,walk.direction);ticks(g,model,10);
    controller_check(g.player.grounded,"Jump fixture did not settle on actual terrain",g,walk);
    InputState jump;jump.jump_pressed=jump.jump_held=true;tick(g,model,jump);
    controller_check(!g.player.grounded && g.player.velocity.y>0,"Jump input did not launch from actual terrain",g,walk);
    InputState held;held.jump_held=true;ticks(g,model,10,held);tick(g,model);
    jump={};jump.jump_pressed=true;tick(g,model,jump);
    check(g.player.air.gliding || g.player.air.glide_pending || g.player.motion==Motion::Glide,"Second airborne tap did not request glide");
    for(unsigned frame=0;frame<90 && !g.player.air.gliding && !g.player.grounded;++frame)tick(g,model);
    controller_check(g.player.air.gliding && (g.animation.requested()==16 || g.player.motion==Motion::Glide),"Queued glide did not begin at the actual jump apex",g,walk);
    park(g,walk.point,walk.direction);InputState charge;charge.charge=true;tick(g,model,charge);
    check(g.player.motion==Motion::Charge || g.animation.requested()==12,"Charge input did not activate ordinary charge");
    park(g,walk.point,walk.direction);bool flame=false;InputState fire;fire.flame_pressed=true;tick(g,model,fire);
    for(unsigned i=0;i<45;++i){flame|=g.interactions.flame_active();tick(g,model);}
    check(flame,"Native flame input produced no active flame interval");
}
void powers(Game g,const SpyroModel& model,const ObjectModels& objects,const Walk& walk) {
    park(g,walk.point,walk.direction);g.cheats.supercharge=true;
    check(g.powers.charge_enabled(g),"Supercharge is unavailable with its real imported clip");
    InputState charge;charge.charge=true;bool supercharge=false;
    for(unsigned i=0;i<16;++i){tick(g,model,charge);supercharge|=g.player.motion==Motion::Supercharge || g.animation.requested()==61;}
    check(supercharge,"Supercharge cheat did not activate clip 61 through native input");
    g.cheats.supercharge=false;park(g,walk.point,walk.direction);ticks(g,model,10);g.cheats.superfly=true;
    InputState jump;jump.jump_pressed=jump.jump_held=true;tick(g,model,jump);
    controller_check(!g.powers.flying && !g.player.grounded && g.player.velocity.y>0,"Superfly first ordinary jump did not launch",g,walk);
    jump.jump_pressed=false;ticks(g,model,10,jump);jump.jump_pressed=true;tick(g,model,jump);
    controller_check(g.powers.flying,"Second airborne tap did not activate Superfly",g,walk);
    std::set<unsigned> flight_clips;
    // Pitch integrates stick input and the takeoff lift lasts 36 ticks.
    // Sweep through ascent, level and descent after that envelope releases.
    for(const auto [pitch,duration]:{std::pair{-1.0f,24u},std::pair{1.0f,64u}}) {
        InputState in;in.move_z=pitch;for(unsigned i=0;i<duration;++i) {
            // A dive ends once its clip has played or the ground is close: landing
            // ends Superfly by design (Icy Peak area 1's floor rises ahead).
            if(pitch>0 && flight_clips.contains(35) && (i>=8 || g.level.raycast(g.player.position+Vec3{0,.1f,0},{0,-1,0},1.5f)))break;
            tick(g,model,in);controller_check(g.powers.flying,"Superfly ended during the real input sweep",g,walk);flight_clips.insert(g.powers.flight_clip());
        }
    }
    for(unsigned clip:{33u,34u,35u})check(flight_clips.contains(clip),"Superfly did not exercise all three flight clips");
    check(!g.powers.wings()[0].empty() && !g.powers.wings()[1].empty(),"Superfly omitted animated wing trails");
    const auto flight_pose=g.animation.sample(1);
    for(unsigned wing=0;wing<2;++wing) {
        check(model.wing_tips[wing]<flight_pose.vertices.size(),"Imported flight wing anchor is outside the actual model");
        const auto expected=g.player.position+retail::origin_above_feet+retail::rotate_model(flight_pose.vertices[model.wing_tips[wing]],g.player.yaw,g.player.pitch,g.player.roll);
        check(length(g.powers.wings()[wing].front()-expected)<.0001f,"Imported flight trail detached from the animated wing tip");
    }
    g.cheats.superfly=false;g.powers.end_flight();park(g,walk.point,walk.direction);ticks(g,model,65);
    g.cheats.superflame=true;check(g.powers.flame_enabled(g),"Superflame is unavailable");
    InputState flame;flame.flame_pressed=true;tick(g,model,flame);ticks(g,model,48);
    check(g.powers.shots_fired>0,"Superflame input did not launch a real powered projectile");
    for(const auto& v:g.powers.visuals())check(v.object.type==objects.effect_type(v.object.type&0x7fff) && objects.models.contains(v.object.type),"Powered visual escaped the reserved model namespace");
    g.cheats={};g.powers.reset(false);g.upgrades.enabled=true;g.upgrades.points=10000;
    for(const auto upgrade:{Upgrade::Supercharge,Upgrade::Superflame,Upgrade::Superfly,Upgrade::Carry,Upgrade::Melee,Upgrade::Glide,Upgrade::Headbash,Upgrade::Roll})check(g.upgrades.buy(upgrade),"PC upgrade could not be purchased");
    check(g.upgrades.valid() && g.upgrades.carry && g.upgrades.melee && g.upgrades.glide && g.upgrades.headbash && g.upgrades.roll,"PC upgrade activation was not retained");
    check(g.upgrades.charge_multiplier()>1 && g.upgrades.flight_multiplier()>1,"Purchased power upgrades did not apply their speed multipliers");
    // Upgrade tiers modify an activated power; acquiring a tier does not
    // itself replace the gate/cheat activation mechanism.
    g.powers.charge=true;g.powers.fire={900,900};g.powers.flight={900,900};
    check(g.powers.charge_enabled(g) && g.powers.flame_enabled(g) && g.powers.fly_enabled(g),"Activated upgraded PC powers did not become available");
    park(g,walk.point,walk.direction);tick(g,model,charge);check(g.player.motion==Motion::Supercharge || g.animation.requested()==61,"Purchased Supercharge did not activate through input");
    g.powers.reset(false);park(g,walk.point,walk.direction);
    InputState melee;melee.melee_pressed=true;tick(g,model,melee);ticks(g,model,45);
    check(g.abilities.melee_swings>0,"Purchased melee did not activate through native input");
    if(g.roll_available()) {
        park(g,walk.point,walk.direction);ticks(g,model,10);InputState roll;roll.roll_left_pressed=true;tick(g,model,roll);
        check(g.player.dodge.active || g.player.motion==Motion::RollLeft,"Purchased roll did not activate with its real clip");++roll_cases;
        unsigned duration=0;for(const auto& frame:model.clip(10).frames)duration+=std::max(1u,unsigned(frame.ticks));
        const auto limit=std::clamp(duration*3+60,120u,600u);
        for(unsigned frame=0;frame<limit && g.player.dodge.active;++frame)tick(g,model);
        controller_check(!g.player.dodge.active,"Upgraded roll failed to exit after multiple native animation cycles",g,walk);
    }
    park(g,walk.point,walk.direction);ticks(g,model,10);InputState leap;leap.jump_pressed=leap.jump_held=true;tick(g,model,leap);
    leap.jump_pressed=false;ticks(g,model,10,leap);tick(g,model);leap={};leap.jump_pressed=true;tick(g,model,leap);
    for(unsigned frame=0;frame<90 && !g.player.air.gliding && !g.player.grounded;++frame)tick(g,model);
    check(g.player.air.gliding,"Purchased glide did not begin at the actual jump apex");
    tick(g,model,leap);check(g.player.glide_boosts>0,"Purchased glide boost did not activate on the third airborne tap");
    g.elements.reset(g);park(g,walk.point,walk.direction);const auto waves=g.elements.waves;
    g.elements.headbash(g,&objects,walk.point);
    check(g.elements.waves==waves+1 && g.elements.shockwaves().back().plain,"Purchased basic headbash upgrade did not emit its floor-bound wave");
}
void element(Game g,const SpyroModel& model,const ObjectModels& objects,const Walk& walk,Element element) {
    park(g,walk.point,walk.direction);g.upgrades.enabled=true;unlock_all_elements(g);
    InputState select;select.element_select=int(element);tick(g,model,select);
    check(g.upgrades.elements.equipped==element,"Element selection input was ignored");
    InputState breath;breath.flame_pressed=breath.flame_held=true;bool flame=false;
    tick(g,model,breath);breath.flame_pressed=false;
    for(unsigned i=0;i<29;++i){flame|=g.interactions.flame_active();tick(g,model,breath);}
    if(element==Element::Fire)check(flame,"Fire breath did not retain the active mounted flame");
    else {
        check(g.elements.breaths>0 && g.elements.breathing() && g.elements.breath_element()==element,"Element breath input did not activate");
        check(g.upgrades.elements.energy<96 && g.upgrades.elements.energy>85,"Element breath did not drain the energy meter");
        const bool geometry=!g.elements.breath_nodes().empty() || !g.elements.bolts().empty() || g.elements.beam().active;
        check(geometry,"Active element breath has no generated effect geometry");
    }
    ticks(g,model,65);check(g.upgrades.elements.energy>98,"Element energy did not recharge after release");
    g.elements.reset(g);park(g,walk.point,walk.direction);g.upgrades.elements.energy=100;
    InputState shot;shot.shot_pressed=true;tick(g,model,shot);
    check(g.elements.shots_fired>0 && g.upgrades.elements.energy<82 && g.upgrades.elements.energy>78,"Element shot did not fire and consume its energy cost");
    check(!g.elements.shots().empty() && g.elements.shots().front().element==element,"Element shot has no live matching projectile");
    g.elements.reset(g);park(g,walk.point,walk.direction);const auto before=g.elements.waves;
    // The native impact entry point consumes the actual scene floor. The
    // airborne controller itself is exercised separately above.
    g.elements.headbash(g,&objects,walk.point);
    check(g.elements.waves==before+1 && !g.elements.shockwaves().empty(),"Tier-three headbash impact emitted no wave");
    check(g.elements.shockwaves().back().element==element && !g.elements.shockwaves().back().local.empty(),"Element wave did not bind to actual scene collision");
    tick(g,model);check(g.elements.shockwaves().back().radius>0,"Element headbash wave did not expand");
    g.elements.reset(g);park(g,walk.point,walk.direction);g.upgrades.elements.fury=100;
    const auto furies=g.elements.furies;InputState fury;fury.fury_pressed=true;tick(g,model,fury);
    check(g.elements.furies==furies+1 && g.elements.fury_pose() && g.upgrades.elements.fury<1,"Full-meter Fury input did not activate and consume the meter");
    ticks(g,model,65);check(!g.elements.shockwaves().empty() || (element==Element::Convexity && g.elements.dark(g)),"Fury completed its windup without its element effect");
    check(g.elements.fury().element==element,"Fury changed the selected element");
}
void collect_rewards(Game& g,const SpyroModel& model,const std::set<unsigned>& sources,unsigned expected,unsigned before,unsigned level_before) {
    std::vector<unsigned> rewards;unsigned value=0;
    for(unsigned i=0;i<g.retail_objects.size();++i) {
        const auto& o=g.retail_objects[i];
        if(o.runtime_spawned && o.type==1 && sources.contains(o.source_id)){rewards.push_back(i);value+=o.gem_value;}
    }
    check(value==expected,"Real native defeat/break released the wrong gem value");
    for(const auto index:rewards) {
        for(unsigned frame=0;frame<180 && !g.retail_objects[index].collected;++frame){park(g,g.retail_objects[index].position);tick(g,model);}
        check(g.retail_objects[index].collected,"Actual released gem did not reach the native pickup controller");
    }
    check(g.player.gems>=before+expected,"Released gems did not award their actual decoded values");
    check(g.level_gems()>=level_before+expected,"Actual native rewards did not update level/HUD gem progress");
    if(expected)check(g.hud.visible(HudPart::Gems),"Actual reward pickup did not expose the gem HUD");
}
void interactions(Game g,const SpyroModel& model) {
    const auto gem=std::find_if(g.retail_objects.begin(),g.retail_objects.end(),[](const auto& o){return o.type==1 && o.gem_value && !o.collected && o.state>=0 && !o.hidden;});
    if(gem!=g.retail_objects.end()) {
        ++gem_cases;const auto index=std::size_t(gem-g.retail_objects.begin());const auto value=gem->gem_value;
        const auto before=g.player.gems;park(g,gem->position);
        for(unsigned i=0;i<90 && !g.retail_objects[index].collected;++i)tick(g,model);
        check(g.retail_objects[index].collected && g.player.gems>=before+value,"A real decoded loose gem did not collect");
        ticks(g,model,24);check(g.hud.visible(HudPart::Gems),"Real gem pickup did not expose the HUD gem counter");
        check(g.level_gems()>=value,"Real gem pickup did not update level progress");
    }
    std::set<unsigned> container_shapes;
    for(unsigned i=0;i<g.level.objects.size();++i) {
        const auto target=g.retail_objects[i];
        if(!target.container || target.state<0 || (target.source_type!=0xc8 && target.source_type!=0xc9))continue;
        std::vector<unsigned> children;unsigned expected=target.held_gem==255?0u:target.held_gem;
        for(unsigned child=0;child<g.level.objects.size();++child) {
            const auto& o=g.retail_objects[child];
            if(o.source_type==0x3f3 && o.source_id==target.id && !o.collected){children.push_back(child);expected+=o.held_gem;}
        }
        const auto shape=unsigned(target.source_type)*4+unsigned(target.held_gem!=255)+2*unsigned(!children.empty());
        if(!container_shapes.insert(shape).second)continue;
        ++container_cases;auto attempt=g;const auto balance=attempt.player.gems,level_balance=attempt.level_gems();park(attempt,target.position+Vec3{0,0,-.7f});
        check(container_accepts_attack(target,g.retail_objects,charge_attack),"Verified C8/C9 container rejected ordinary charge");
        attempt.retail_objects[i].attack_flags|=charge_attack;
        // The native container loop consumes attacks on alternating VBlanks.
        ticks(attempt,model,2);
        check(attempt.retail_objects[i].state<0 || attempt.retail_objects[i].collected,"Real C8/C9 container did not break");
        std::set<unsigned> sources{target.id};
        for(const auto child:children){sources.insert(g.retail_objects[child].id);check(attempt.retail_objects[child].collected && attempt.retail_objects[child].hidden,"Linked 3F3 source was not consumed while remaining hidden");}
        for(const auto& fragment:attempt.retail_objects)if(fragment.runtime_spawned && fragment.source_id==target.id && is_container_fragment(fragment.type&0x7fff))
            check(fragment.type==(target.source_type==0xc8?0x132u:0x133u) && attempt.object_models()->models.contains(fragment.type),"Real C8/C9 fragment did not use its actual mounted renderable model");
        direct_rewards+=target.held_gem!=255;linked_rewards+=!children.empty();
        collect_rewards(attempt,model,sources,expected,balance,level_balance);
    }
    for(unsigned i=0;i<g.level.objects.size();++i) {
        const auto target=g.retail_objects[i];
        if(!target.enemy || target.collected || target.state<0)continue;
        ++enemy_cases;auto attempt=g;const auto balance=attempt.player.gems,level_balance=attempt.level_gems();park(attempt,target.position+Vec3{0,0,-.7f});
        std::set<unsigned> reward_sources{target.id};unsigned reward_value=target.held_gem==255?0u:target.held_gem;
        std::vector<unsigned> linked;
        for(unsigned child=0;child<g.level.objects.size();++child) {
            const auto& o=g.retail_objects[child];
            if(o.source_type==0x3f3 && o.source_id==target.id && !o.collected){reward_sources.insert(o.id);reward_value+=o.held_gem;linked.push_back(child);}
        }
        if(target.source_type==0xee) {
            check(!enemy_accepts_attack(target,charge_attack) && enemy_accepts_attack(target,flame_attack),"Actual EE attack-mask evidence changed");
            attempt.retail_objects[i].attack_flags|=charge_attack;ticks(attempt,model,2);
            check(!attempt.retail_objects[i].enemy->defeated,"Actual EE enemy incorrectly died to ordinary charge");
            attempt.retail_objects[i].attack_flags|=flame_attack;
        } else if(target.source_type==0xfd) {
            check(enemy_accepts_attack(target,charge_attack) && enemy_accepts_attack(target,flame_attack),"Actual FD attack-mask evidence changed");
            attempt.retail_objects[i].attack_flags|=charge_attack;
        } else throw FormatError("Unverified enemy descriptor entered the YOTD test census");
        ticks(attempt,model,2);check(attempt.retail_objects[i].enemy->defeated && attempt.retail_objects[i].state<0,"Actual supported YOTD enemy did not die to its accepted attack");
        if(!linked.empty())ticks(attempt,model,2);
        for(const auto child:linked)check(attempt.retail_objects[child].collected,"Defeated native enemy did not consume its linked reward source");
        collect_rewards(attempt,model,reward_sources,reward_value,balance,level_balance);
    }
}
void original_sunny_vm(const Level& mounted,const Scene& scene,const SpyroModel& model,const ObjectModels& objects,WadArchive& wad,std::span<const std::uint8_t> executable) {
    const auto image=make_yotd_script_image(wad,executable,99,0);
    check(bool(image) && image->slot==99 && !image->area,"Actual Sunny Villa original script image was not built");
    auto level=mounted;level.yotd_script_image=image;
    // A live VM retains callbacks to its owning Game. Construct every case
    // freshly; copying an already active VM is not a valid fixture.
    const auto make=[&]{return Game(level,scene,{},&objects,&model);};
    const auto healthy=[&](const Game& g) {
        if(g.yotd_scripts.faults || !g.yotd_scripts.active()) {
            const auto why=std::string("Original Sunny VM failed: ")+g.yotd_scripts.fault;
            check(false,why.c_str());
        }
        check(g.yotd_scripts.active() && !g.yotd_scripts.faults,"Original Sunny VM is inactive or faulted");
    };
    const auto advance=[&](Game& g,unsigned frames,InputState input=InputState{}) {
        for(unsigned frame=0;frame<frames;++frame){tick(g,model,input);input.jump_pressed=input.flame_pressed=false;healthy(g);}
    };
    {
        auto g=make();healthy(g);advance(g,6);
        check(g.yotd_scripts.owned()==21,"Original dispatcher did not own all 21 actual Sunny Rhynocs");
        const auto owners=std::count_if(g.retail_objects.begin(),g.retail_objects.end(),[](const auto& o){return o.retail_script && o.enemy && (o.source_type==0xfd || o.source_type==0xee);});
        check(owners==21 && g.yotd_scripts.script_ticks && g.yotd_scripts.instructions,"Original active-list/dispatcher did not run its actual owned actors");
    }
    const auto finish=[&](Game& g,unsigned index,unsigned before,unsigned level_before) {
        const auto parent=level.objects[index];
        check(g.retail_objects[index].enemy && g.retail_objects[index].enemy->defeated,"Original enemy did not enter its real defeated state");
        // Original state8 is a positive death-animation state. Native removal
        // follows the authored death clip, up to 60 original 30 Hz steps.
        park(g,g.retail_objects[index].position+Vec3{0,0,-.7f});
        for(unsigned frame=0;frame<120 && g.retail_objects[index].state>=0;++frame)advance(g,1);
        check(g.retail_objects[index].state<0 && g.yotd_scripts.removed,"Original enemy did not complete its native death/removal path");
        check(g.retail_objects[index].held_gem==parent.held_gem,"Original bridge erased the authored held-gem identity");
        std::set<unsigned> sources{parent.id};unsigned expected=parent.held_gem==255?0u:parent.held_gem;
        for(const auto& child:level.objects)if(child.source_type==0x3f3 && child.source_id==parent.id){sources.insert(child.id);expected+=child.held_gem;}
        for(const auto& reward:g.retail_objects)if(reward.runtime_spawned && reward.type==1 && sources.contains(reward.source_id))
            check(!reward.script_gem && reward.source_game==GameBuild::YearOfTheDragonUS,"Original reward lost native parent-based gem accounting");
        collect_rewards(g,model,sources,expected,before,level_before);healthy(g);
        if(expected)check(g.yotd_scripts.spawned,"Original reward allocator produced no imported native gem");
    };
    unsigned actual=0,loader_removed=0;
    // The original loader's persistence pass (8002c4b0..8002c62c) removes some
    // placed actors outright (80055b18 marks them -3) and spawns their reward
    // in their place: Sunny main's three EEs 56, 63 and 84 with the boot-time
    // save tables. They never become live actors, so they cannot be attacked.
    const auto removed_at_load=[&](unsigned index){auto g=make();return g.yotd_scripts.active() && g.retail_objects[index].state<0;};
    for(unsigned index=0;index<level.objects.size();++index) {
        const auto target=level.objects[index];if(!target.enemy || (target.source_type!=0xfd && target.source_type!=0xee))continue;
        ++actual;const auto label="original VM actor "+std::to_string(target.id)+" class "+std::to_string(target.source_type);
        if(removed_at_load(index)){++loader_removed;std::cerr<<label<<": removed by the original loader at boot\n";continue;}
        stage(label.c_str(),[&]{
            auto g=make();healthy(g);park(g,target.position+Vec3{0,0,-.7f});
            advance(g,6);
            check(g.retail_objects[index].retail_script,"Actual enemy escaped original VM ownership before its attack");
            const auto before=g.player.gems,level_before=g.level_gems(),frames=g.yotd_scripts.script_ticks;
            const auto instructions=g.yotd_scripts.instructions;
            if(target.source_type==0xee) {
                g.retail_objects[index].attack_flags|=charge_attack;advance(g,4);
                check(!g.retail_objects[index].enemy->defeated && g.retail_objects[index].state>=0,"Original EE incorrectly accepted ordinary charge");
            }
            const auto mask=target.source_type==0xfd?charge_attack:flame_attack;
            g.retail_objects[index].attack_flags|=mask;
            for(unsigned frame=0;frame<120 && !g.retail_objects[index].enemy->defeated;++frame)advance(g,1);
            check(g.retail_objects[index].enemy->defeated,"Original FD charge/EE flame callback did not enter death");
            check((g.yotd_scripts.attacks_seen&mask)==mask && g.yotd_scripts.script_ticks>frames && g.yotd_scripts.instructions>instructions,"Accepted attack did not pass through the original dispatcher");
            finish(g,index,before,level_before);++vm_enemy_cases;
        });
    }
    check(actual==21,"Original VM actor census differs from the actual 21 placed Rhynocs");
    check(loader_removed==3,"The original loader no longer removes exactly Sunny main's three dormant EEs");
    // Separate full input-to-contact cases exercise the common collider and
    // flame stream as well as the original damage scripts above.
    for(const unsigned type:{0xfdu,0xeeu}) {
        const auto found=std::find_if(level.objects.begin(),level.objects.end(),[&](const auto& o){
            return o.enemy && o.source_type==type && !removed_at_load(static_cast<unsigned>(&o-level.objects.data()));});
        check(found!=level.objects.end(),"Original VM natural-input target is absent");
        const auto index=unsigned(found-level.objects.begin());const auto label="original VM input class "+std::to_string(type);
        stage(label.c_str(),[&]{
            auto g=make();healthy(g);park(g,found->position+Vec3{0,0,-.35f});advance(g,6);
            const auto before=g.player.gems,level_before=g.level_gems();InputState attack;
            if(type==0xfd)attack.charge=true;else attack.flame_pressed=attack.flame_held=true;
            bool activated=false;
            for(unsigned frame=0;frame<90 && !g.retail_objects[index].enemy->defeated;++frame) {
                advance(g,1,attack);attack.flame_pressed=false;
                activated|=type==0xfd?(g.player.motion==Motion::Charge || g.player.motion==Motion::ChargeJump):g.interactions.flame_active();
            }
            check(activated && g.retail_objects[index].enemy->defeated,"Real charge/flame input failed to defeat its original-owned actor");
            finish(g,index,before,level_before);++vm_input_cases;
        });
    }
    stage("original VM active clone isolation",[&]{
        const auto found=std::find_if(level.objects.begin(),level.objects.end(),[](const auto& o){return o.enemy && o.source_type==0xfd && o.held_gem!=255;});
        check(found!=level.objects.end(),"No actual FD reward actor is available for VM clone isolation");
        const auto index=unsigned(found-level.objects.begin());auto original=make();auto control=make();
        park(original,found->position+Vec3{0,0,-.7f});park(control,found->position+Vec3{0,0,-.7f});
        advance(original,6);advance(control,6);
        const auto address=original.yotd_scripts.moby_address(index);check(address!=0,"Original VM actor diagnostics have no actual moby address");
        const auto flags=original.yotd_scripts.peek(address+0x18),script_ticks=original.yotd_scripts.script_ticks;
        const auto instructions=original.yotd_scripts.instructions;const auto spawned=original.yotd_scripts.spawned,removed=original.yotd_scripts.removed;
        const auto state=original.retail_objects[index].state;const auto position=original.retail_objects[index].position;
        {
            auto clone=original;healthy(clone);
            clone.yotd_scripts.poke(address+0x18,flags^charge_attack);
            check(clone.yotd_scripts.peek(address+0x18)!=(original.yotd_scripts.peek(address+0x18)),"Active Game clone shares original VM RAM");
            check(original.yotd_scripts.peek(address+0x18)==flags,"Poking clone VM changed original actor flags");
            auto moved=std::move(clone);healthy(moved);
            const auto before=moved.player.gems,level_before=moved.level_gems();moved.retail_objects[index].attack_flags|=charge_attack;
            for(unsigned frame=0;frame<120 && !moved.retail_objects[index].enemy->defeated;++frame)advance(moved,1);
            check(moved.retail_objects[index].enemy->defeated,"Moved live clone did not execute the original FD damage callback");
            finish(moved,index,before,level_before);
            check(moved.yotd_scripts.spawned>spawned,"Moved live clone did not execute its rebound original reward observer");
        }
        check(original.yotd_scripts.peek(address+0x18)==flags && original.yotd_scripts.script_ticks==script_ticks && original.yotd_scripts.instructions==instructions && original.yotd_scripts.spawned==spawned && original.yotd_scripts.removed==removed,"Running or destroying a live clone changed original VM memory or counters");
        check(original.retail_objects[index].state==state && length(original.retail_objects[index].position-position)<1e-6f && !original.retail_objects[index].enemy->defeated,"Clone callback changed the original host actor");
        advance(original,8);advance(control,8);
        for(unsigned actor=0;actor<level.objects.size();++actor)if(original.retail_objects[actor].retail_script && original.retail_objects[actor].enemy) {
            const auto& a=original.retail_objects[actor];const auto& b=control.retail_objects[actor];
            check(a.state==b.state && length(a.position-b.position)<1e-6f && a.rotation==b.rotation && a.animation.clip==b.animation.clip && a.animation.frame==b.animation.frame,"Original host actor diverged from a fresh control after clone destruction");
            const auto left=original.yotd_scripts.moby_address(actor),right=control.yotd_scripts.moby_address(actor);
            for(unsigned offset=0;offset<88;offset+=4)check(original.yotd_scripts.peek(left+offset)==control.yotd_scripts.peek(right+offset),"Original actor VM bytes diverged from its untouched control");
        }
        healthy(original);++vm_clone_cases;
    });
    stage("original VM committed-death fault fallback",[&]{
        const auto found=std::find_if(level.objects.begin(),level.objects.end(),[](const auto& o){return o.enemy && o.source_type==0xfd && o.held_gem>0 && o.held_gem!=255;});
        check(found!=level.objects.end(),"No actual FD held reward is available for the fault fixture");
        const auto index=unsigned(found-level.objects.begin());auto g=make();park(g,found->position+Vec3{0,0,-.7f});advance(g,6);
        const auto address=g.yotd_scripts.moby_address(index),table=g.yotd_scripts.peek(0x800742d8);
        check(address && table>=0x80000000 && table<=0x80800000-0x100*4,"Actual original callback directory is outside VM RAM");
        const auto callback=g.yotd_scripts.peek(table+0xfd*4);
        check(callback>=0x80000000 && callback<0x80800000,"Actual FD update callback is not mapped before fault injection");
        // Commit the real original death state, then fault a real callback
        // before the allocator hands its held gem to the bridge.
        g.yotd_scripts.poke(address+0x48,8,1);g.retail_objects[index].attack_flags|=charge_attack;
        g.yotd_scripts.poke(table+0xfd*4,0x80900000);
        for(unsigned frame=0;frame<4 && !g.yotd_scripts.faults;++frame)tick(g,model);
        check(g.yotd_scripts.faults==1 && !g.yotd_scripts.active() && !g.yotd_scripts.fault.empty(),"Invalid original callback did not fail closed exactly once");
        unsigned released=0;
        for(unsigned actor=0;actor<level.objects.size();++actor)if(level.objects[actor].enemy && (level.objects[actor].source_type==0xfd || level.objects[actor].source_type==0xee)) {
            check(!g.retail_objects[actor].retail_script,"Original fault left an actual enemy under inactive VM ownership");++released;
        }
        check(released==21 && g.retail_objects[index].state<0 && g.retail_objects[index].enemy->defeated,"Original committed-death fault did not preserve defeat and release all 21 owners");
        const auto reward=[&] {
            unsigned count=0;for(const auto& o:g.retail_objects)if(o.runtime_spawned && o.type==1 && o.source_id==found->id) {
                ++count;check(!o.script_gem && o.source_game==GameBuild::YearOfTheDragonUS && o.gem_value==found->held_gem,"Fault recovery lost actual native held-gem provenance/value");
            }return count;
        };
        check(reward()==1,"Fault recovery did not release exactly one actual held gem");ticks(g,model,12);
        check(g.yotd_scripts.faults==1 && !g.yotd_scripts.active() && reward()==1,"Later native ticks duplicated the recovered reward or retried the failed VM");
        const auto other=std::find_if(g.retail_objects.begin(),g.retail_objects.begin()+level.objects.size(),[&](const auto& o){return o.id!=found->id && o.source_type==0xfd && o.enemy && !o.enemy->defeated && o.state>=0;});
        check(other!=g.retail_objects.begin()+level.objects.size(),"No surviving actual FD is available after VM fallback");
        const auto fallback=unsigned(other-g.retail_objects.begin());g.retail_objects[fallback].attack_flags|=charge_attack;ticks(g,model,2);
        check(g.retail_objects[fallback].enemy->defeated && g.retail_objects[fallback].state<0,"A released actual actor did not resume its native fallback after VM failure");
        check(reward()==1,"Another native fallback defeat duplicated the first actor's reward");++vm_fault_cases;
    });
}
void save(Game g,const SpyroModel& model,const ObjectModels& objects,const Walk& walk,const GameProfile& profile) {
    park(g,walk.point,walk.direction);g.upgrades.enabled=true;unlock_all_elements(g);g.upgrades.elements.equipped=Element::Ice;
    g.upgrades.elements.energy=42.5f;g.player.gems=123;g.cheats.supercharge=true;
    GameSave checkpoint;checkpoint.capture(g);check(checkpoint.retail_crc==profile.crc && checkpoint.slot==g.level.wad_slot,"Save lost the mounted YOTD identity");
    const auto file=std::filesystem::current_path()/"analysis-yotd"/("spyro3-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
    checkpoint.write(file);const auto loaded=GameSave::read(file);std::filesystem::remove(file);
    auto restored=g;restored.player.gems=0;loaded.apply(restored,&objects);
    check(restored.player.gems==123 && restored.upgrades.elements==g.upgrades.elements && restored.cheats.supercharge,"YOTD save round trip lost progression or powers");
    tick(restored,model);check(finite(restored.player.position),"Restored YOTD save cannot advance");
    auto mismatched=g;mismatched.play_scene.retail_crc=0xcdc0917c;bool rejected=false;
    try{loaded.apply(mismatched,&objects);}catch(const FormatError&){rejected=true;}
    check(rejected,"A YOTD checkpoint was accepted under the Spyro 2 profile");
}
}
int main(int argc,char** argv)try {
    if(argc!=3 && argc!=4)throw std::runtime_error("Usage: spyro_spyro3_tests <YOTD source> <Spyro 2 donor source> [July roll source]");
    AssetStore source(std::filesystem::u8path(argv[1]));const auto executable=source.read_all(find_game_executable(source));
    const auto& profile=game_profile(executable);check(profile.yotd(),"Test source is not the explicit YOTD profile");
    const auto donor=read_yotd_donor(std::filesystem::u8path(argv[2]));
    std::optional<SpyroRollBank> rolls;
    if(argc==4) {
        AssetStore roll_source(std::filesystem::u8path(argv[3]));const auto roll_exe=roll_source.read_all(find_game_executable(roll_source));
        const auto& roll_profile=game_profile(roll_exe);WadArchive roll_wad(roll_source,&roll_profile);
        rolls=decode_july_rolls(roll_wad.read(0),roll_exe);
    }
    bool rejected=false;try{(void)read_yotd_donor(std::filesystem::u8path(argv[1]));}catch(const FormatError&){rejected=true;}
    check(rejected,"YOTD was incorrectly accepted as the Spyro 2 PC effect donor");
    WadArchive wad(source,&profile);const auto global=wad.read(0);unsigned slots=0;
    for(const auto slot:profile.levels()) {
        ++slots;Bytes archive;unsigned count=0;
        try{archive=wad.read(slot);count=level_area_count(archive,profile);}catch(const std::exception& e){++failures;std::cerr<<slot<<": "<<e.what()<<'\n';continue;}
        for(unsigned area=0;area<count;++area) {
            ++areas;context=std::to_string(slot)+"/"+std::to_string(area);
            try {
                auto level=decode_level(archive,slot,profile,area);
                auto model=decode_spyro(global,archive,executable,{},0,{},{},{},{},area);
                auto objects=decode_object_models(archive,executable,area);PowerupAssets effects;
                std::map<unsigned,std::uint64_t> raw;for(const auto& [type,m]:objects.models)raw[type]=hash_model(m);
                const auto skin=model.texture_rgba,native_texture=objects.texture_rgba;
                import_yotd_pc_assets(model,objects,effects,donor);
                check(model.texture_rgba.size()>=skin.size() && std::equal(skin.begin(),skin.end(),model.texture_rgba.begin()),"PC import overwrote the mounted Spyro skin");
                check(objects.texture_rgba.size()>=native_texture.size() && std::equal(native_texture.begin(),native_texture.end(),objects.texture_rgba.begin()),"PC import overwrote mounted object texture pixels");
                for(const auto& [type,hash]:raw)check(objects.models.contains(type) && hash_model(objects.models.at(type))==hash,"PC import overwrote an original class model");
                const auto model_bytes=model.texture_rgba,object_bytes=objects.texture_rgba;const auto model_count=objects.models.size(),clip_count=model.animations.size();
                import_yotd_pc_assets(model,objects,effects,donor);
                check(model.texture_rgba==model_bytes && objects.texture_rgba==object_bytes && objects.models.size()==model_count && model.animations.size()==clip_count,"PC donor import is not idempotent");
                for(const auto& [type,hash]:raw)check(hash_model(objects.models.at(type))==hash,"Second PC import overwrote a mounted class");
                if(rolls) {
                    const auto pixels=model.texture_rgba;import_july_rolls(model,*rolls);
                    check(model.cut_rolls_imported && pixels==model.texture_rgba,"Genuine July roll import changed the mounted skin or failed provenance");
                } else check(!model.cut_rolls_imported,"Unverified native clips advertised July roll provenance");
                initialize_retail_objects(level,objects);
                fallbacks+=level.collision_from_terrain;projections+=level.spawn_projected;
                assets(level,model,objects,effects,donor);
                const auto walk=walk_point(level);Scene scene;scene.retail_crc=profile.crc;scene.wad_slot=slot;scene.spawn=level.spawn;
                const auto make=[&]{return Game(level,scene,{},&objects,&model);};
                stage("selected spawn support",[&]{auto g=make();ticks(g,model,90);check(g.player.grounded && g.player.position.y>=level.bounds.min.y-.5f,"Authored/projected scene spawn did not settle on actual collision");});
                stage("walk/jump/glide/charge/flame",[&]{movement(make(),model,walk);});
                stage("PC powers/upgrades",[&]{powers(make(),model,objects,walk);});
                for(unsigned e=0;e<element_count;++e){const auto label="element "+std::to_string(e);stage(label.c_str(),[&]{element(make(),model,objects,walk,static_cast<Element>(e));});}
                stage("real object interactions",[&]{interactions(make(),model);});
                if(slot==99 && !area)stage("Sunny original VM",[&]{original_sunny_vm(level,scene,model,objects,wad,executable);});
                if(slot==profile.first_level() && !area)stage("profile save",[&]{save(make(),model,objects,walk,profile);});
                std::cout<<context<<": actual scene/model import and gameplay stages complete\n";
            }catch(const std::exception& e){++failures;std::cerr<<context<<": "<<e.what()<<'\n';}
        }
    }
    stage("complete coverage",[&]{check(slots==37 && areas==75,"The complete 37-scene/75-area census was not exercised");
    check(fallbacks==7,"Unexpected packed collision/fallback coverage");
    // 21 placed Rhynocs: 18 live through the original VM, 3 removed by its loader.
    check(vm_enemy_cases==18 && vm_input_cases==2,"Original VM damage/reward cases or real charge/flame input cases were not completed");
    check(vm_clone_cases==1,"Active original VM clone/move isolation was not exercised");
    check(vm_fault_cases==1,"Committed original-death fault recovery was not exercised");
    if(rolls)check(roll_cases==75,"Genuine July roll activation was not exercised in all 75 areas");
    check(gem_cases>0,"No actual loose gem pickup/HUD path was exercised");
    check(container_cases>0 && direct_rewards>0 && linked_rewards>0 && enemy_cases>=21,"Verified C8/C9/direct/linked rewards or all 21 Sunny Villa enemies were not exercised");});
    std::cout<<checks<<" checks, "<<areas<<" areas, "<<failures<<" failures; "<<fallbacks<<" marked collision fallbacks, "<<projections<<" projected spawns, "<<gem_cases<<" real gem cases, "<<container_cases<<" verified container cases ("<<direct_rewards<<" direct, "<<linked_rewards<<" linked rewards), "<<enemy_cases<<" verified enemy cases, "<<vm_enemy_cases<<" original VM enemies, "<<vm_input_cases<<" original VM input cases, "<<vm_clone_cases<<" isolated active VM clone/move cases, "<<vm_fault_cases<<" original fault-recovery cases, "<<roll_cases<<" genuine July roll cases\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<context<<": "<<e.what()<<'\n';return 1;}

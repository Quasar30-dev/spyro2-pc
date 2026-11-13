#include "spyro/runtime.hpp"
#include <bit>
namespace spyro {
namespace {
float distance_to_segment(Vec3 p,Vec3 a,Vec3 b){const auto d=b-a;return length(p-(a+d*std::clamp(dot(p-a,d)/std::max(dot(d,d),0.000001f),0.0f,1.0f)));}
void clip(RetailObject& o,const ObjectModels* models,unsigned index){if(!models)return;const auto b=models->models.find(o.type);if(b!=models->models.end() && b->second.animated && index<b->second.animations.size())o.animation=start_object_animation(b->second,index);}
Vec3 direction(const Game& g){return retail::rotate_model({0,0,1},g.player.yaw,g.player.pitch,0);}
}
void LevelSystems::fire_cannon(Game& g,const ObjectModels* models,Vec3 origin,Vec3 dir,bool trolley) {
    Shot shot;shot.position=shot.previous=origin;shot.velocity=normalized(dir)*24;shot.trolley=trolley;
    // Breeze overlay 38, 8007a988: the projectile is the scene's C2 mesh.
    const unsigned type=g.level.wad_slot==39?0xc2:0x2ca;
    if(models && models->models.contains(type)) {
        RetailObject o;o.id=static_cast<unsigned>(g.retail_objects.size());o.type=static_cast<std::uint16_t>(type);o.size=32;o.held_gem=255;
        o.runtime_spawned=o.scripted_transient=true;o.position=o.previous_position=origin;o.material_color=0x2a808080;
        o.rotation[2]=retail::original_heading(std::atan2(dir.x,dir.z))/16;o.previous_rotation=o.rotation;clip(o,models,0);o.previous_animation=o.animation;
        const auto free=std::find_if(g.retail_objects.begin(),g.retail_objects.end(),[&](const auto& old){return old.runtime_spawned && old.scripted_transient && old.collected && old.type==type;});
        if(free!=g.retail_objects.end()){o.id=static_cast<unsigned>(free-g.retail_objects.begin());*free=o;}else g.retail_objects.push_back(o);shot.object=o.id;
    }
    shots_.push_back(shot);++cannon_shots;
    g.sound_events.push_back({SoundScope::Object,0,trolley?0x173u:0x10eu,origin});
}
void LevelSystems::tick_shots(Game& g,const ObjectModels* models) {
    for(auto& shot:shots_) {
        shot.previous=shot.position;shot.position+=shot.velocity/60;bool impact=false;
        const auto delta=shot.position-shot.previous;float nearest=length(delta);unsigned target=UINT32_MAX;
        if(const auto wall=g.level.raycast(shot.previous,delta,nearest)){nearest=wall->distance;impact=true;}
        for(const auto& s:g.level.scripts)if(s.role==ScriptRole::CannonTarget || s.role==ScriptRole::TrackSwitch || s.role==ScriptRole::TrackObstacle || (s.role==ScriptRole::HunterTarget && mode==ChallengeMode::Hunter)) {
            const auto& o=g.retail_objects[s.object];if(o.collected || o.hidden || o.state<0)continue;
            const float distance=length(o.position-shot.previous);
            if(distance_to_segment(o.position,shot.previous,shot.previous+normalized(delta)*nearest)<(s.role==ScriptRole::HunterTarget?1.1f:0.8f) && distance<nearest+1.1f){target=s.object;impact=true;nearest=std::min(nearest,distance);}
        }
        if(target!=UINT32_MAX){g.retail_objects[target].attack_flags|=0x10000000;++cannon_hits;}
        if(shot.object<g.retail_objects.size()) {auto& o=g.retail_objects[shot.object];begin_object_transform(o,1);o.position=shot.position;if(impact || shot.age>=180)o.collected=true;}
        if(impact) {g.sound_events.push_back({SoundScope::Object,1,0x10e,shot.position});shot.age=180;}
        else ++shot.age;
    }
    std::erase_if(shots_,[](const auto& s){return s.age>=180;});(void)models;
}
void LevelSystems::tick_machines(Game& g,const ObjectModels* models) {
    bool animation=false;unsigned cleared=0;
    for(std::size_t i=0;i<g.level.scripts.size();++i) {
        const auto& s=g.level.scripts[i];auto& state=state_[i];auto& o=g.retail_objects[s.object];
        if(s.role==ScriptRole::Burner) {
            if(!(state.flags&1) && (o.attack_flags&(flame_attack|superflame_attack|green_bomb_attack))) {
                state.flags|=1;o.variant=1;++activations;clip(o,models,1);g.sound_events.push_back({SoundScope::Object,0,o.type,o.position});
                for(auto& m:g.level.terrain_morphs)if(m.group==s.parameters[4]){m.node=std::min(2u,static_cast<unsigned>(m.steps.size()-1));m.remaining=m.steps[m.node].duration;m.flags=m.steps[m.node].flags&~3u;}
            }
            o.attack_flags=0;
        }else if(s.role==ScriptRole::Fan) {
            const bool lit=s.parameters[1]<g.retail_objects.size() && g.retail_objects[s.parameters[1]].variant==1;
            state.ticks=lit?std::min(512u,state.ticks+2):state.ticks?state.ticks-1:0;
            begin_object_transform(o,1);o.rotation[2]+=state.ticks/64.0f;
            if(s.parameters[0]<g.retail_objects.size())g.retail_objects[s.parameters[0]].state=state.ticks>=384?1:0;
        }else if(s.role==ScriptRole::Machine) {
            const auto lit=[&](unsigned id){return id==UINT32_MAX || (id<g.retail_objects.size() && g.retail_objects[id].variant==1);};
            if(!lit(s.parameters[3]) || !lit(s.parameters[4]))continue;
            if(!(state.flags&1)) {
                state.flags|=1;++activations;
                for(auto& m:g.level.terrain_morphs)if(m.group==s.parameters[1])m.flags&=~3u;
            }
            const float height=s.parameters[6]/retail::units_per_world;
            const unsigned duration=std::max(1u,s.parameters[5]);
            const float next=height*std::min(1.0f,float(++state.ticks)/duration);
            const float change=next-state.progress;
            if(change>0) {
                translate_scene_group(g.level,s.parameters[2],{0,change,0});
                // Riders follow the same moving collision surface, including water.
                const auto support=g.level.ground_support(g.player.position+retail::origin_above_feet,g.tuning.radius,0.3f+change);
                if(support && support->point.y>g.player.position.y-0.1f && support->point.y<g.player.position.y+change+0.2f)g.player.position.y+=change;
                state.progress=next;
            }
            state.ticks=std::min(state.ticks,duration);animation=true;
        }else if(s.role==ScriptRole::CannonTarget) {
            if((o.attack_flags&0x10100000) && !(state.flags&1)){state.flags|=1;o.collected=true;o.state=-1;g.sound_events.push_back({SoundScope::Object,0,o.type,o.position});}
            o.attack_flags=0;if(state.flags&1)++cleared;
        }else if(s.role==ScriptRole::TrackSwitch && o.attack_flags) {
            for(auto& route:trolley_routes_)if(route.address==s.parameters[0] && s.parameters[1]<route.points.size()) {
                auto& action=route.points[s.parameters[1]].action;
                if(!(state.flags&1)){action=static_cast<int>(s.parameters[2]|0x100);state.flags|=1;++track_switches;o.rotation[2]+=64;}
            }
            o.attack_flags=0;
        }else if(s.role==ScriptRole::TrackObstacle && o.attack_flags) {o.collected=true;o.attack_flags=0;}
    }
    if(animation || activations)advance_scene_morphs(g.level);
    if(cleared==8)g.award_orb(1);
}
void LevelSystems::start_trolley(Game& g,const ObjectModels* models) {
    const auto cart=std::find_if(g.level.scripts.begin(),g.level.scripts.end(),[](const auto& s){return s.role==ScriptRole::Trolley;});
    if(cart==g.level.scripts.end() || cart->routes.empty())return;
    if(mode!=ChallengeMode::TrolleyResults){start_position_=g.player.position;start_yaw_=g.player.yaw;}
    trolley_routes_=cart->routes;route_=0;point_=1;trolley_height_=trolley_vertical_=0;route_distance_=0;gears=elapsed=0;success=false;cooldown_=0;
    for(const auto& s:shots_)if(s.object<g.retail_objects.size())g.retail_objects[s.object].collected=true;shots_.clear();
    state_[static_cast<std::size_t>(cart-g.level.scripts.begin())].progress=0;
    mode=ChallengeMode::Trolley;cannon=UINT32_MAX;g.dialogue.dismiss(g.retail_objects);g.powers.end_flight();
    for(std::size_t i=0;i<g.level.scripts.size();++i) {
        const auto& s=g.level.scripts[i];if(s.role==ScriptRole::Gear || s.role==ScriptRole::TrackObstacle || s.role==ScriptRole::TrackSwitch) {
            g.retail_objects[s.object]=g.level.objects[s.object];state_[i].flags=0;
        }
    }
    auto& o=g.retail_objects[cart->object];o.position=o.previous_position=trolley_routes_[0].points[0].position;clip(o,models,0);
    g.player.position=g.player.previous_position=o.position+Vec3{0,0.25f,0};g.player.velocity={};g.player.pitch=g.player.roll=0;g.player.air={};g.player.jump={};g.player.swim={};
    g.camera.scripted_view.reset();
}
bool LevelSystems::tick_vehicle(Game& g,InputState& input,const ObjectModels* models) {
    if(cannon!=UINT32_MAX) {
        if(input.dialogue.interact || input.dialogue.cancel || input.jump_pressed){cannon=UINT32_MAX;leave_challenge(g);input={};return true;}
        auto& gun=g.retail_objects[cannon];g.player.yaw+=input.move_x*0.022f;g.player.pitch=std::clamp(g.player.pitch+input.move_z*0.018f,-0.2f,0.9f);
        begin_object_transform(gun,1);gun.rotation[2]=retail::original_heading(g.player.yaw)/16;gun.rotation[0]=-g.player.pitch/(16*retail::angle_to_radians);
        g.player.position=gun.position-direction(g)*0.65f;g.player.velocity={};g.player.motion=Motion::Idle;
        if(input.flame_pressed && !cooldown_){const auto origin=gun.position+Vec3{0,0.65f,0}+direction(g)*1.2f;clip(gun,models,1);fire_cannon(g,models,origin,direction(g),false);cooldown_=30;}
        g.camera.scripted_view=CameraView{g.player.position-direction(g)*2.8f+Vec3{0,1.0f,0},direction(g)};
        prompt="Arrows: Aim    F: Fire    E / Space: Leave";return true;
    }
    if(mode!=ChallengeMode::Trolley)return false;
    if(input.dialogue.cancel || input.dialogue.interact){leave_challenge(g);input={};return true;}
    const auto cart=std::find_if(g.level.scripts.begin(),g.level.scripts.end(),[](const auto& s){return s.role==ScriptRole::Trolley;});
    if(cart==g.level.scripts.end() || trolley_routes_.empty()){leave_challenge(g);return true;}
    auto& o=g.retail_objects[cart->object];begin_object_transform(o,1);++elapsed;
    if(input.jump_pressed && trolley_height_<=0.001f){trolley_vertical_=0x80*30/retail::units_per_world;++trolley_jumps;}
    trolley_vertical_-=0xc*30*30/retail::units_per_world/60;
    trolley_height_=std::max(0.0f,trolley_height_+trolley_vertical_/60);if(trolley_height_==0)trolley_vertical_=0;
    // 8007E680 normalizes the horizontal pursuit vector to 0x240 per
    // 30 Hz script step. 0x80 is the separate jump impulse, not cart speed.
    float distance=0x240*30/retail::units_per_world/60;
    o.position.y-=state_[static_cast<std::size_t>(cart-g.level.scripts.begin())].progress;
    for(unsigned guard=0;distance>0 && guard<8;++guard) {
        auto& path=trolley_routes_.at(route_);point_%=static_cast<unsigned>(path.points.size());
        const auto target=path.points[point_].position;const auto delta=target-o.position;const float n=length(delta);
        if(n>0.000001f){o.position+=delta/n*std::min(n,distance);g.player.yaw=std::atan2(delta.x,delta.z);}
        distance-=n;if(distance<0)break;
        const auto action=static_cast<unsigned>(path.points[point_].action),branch=action&127;
        // 8007F674: terminal/derail markers enter the failure sequence.
        if(action&0x400){mode=ChallengeMode::TrolleyResults;success=false;result_ticks_=0;break;}
        const bool turn=(action&0x100) || ((action&0x80)?input.move_x>0.2f:input.move_x<-0.2f);
        if(branch && branch<=trolley_routes_.size() && turn) {
            route_=branch-1;const auto& next=trolley_routes_[route_];float best=1e30f;
            for(unsigned p=0;p<next.points.size();++p)if(const float d=length(next.points[p].position-o.position);d<best){best=d;point_=p;}
            ++track_switches;
        }
        const auto& selected=trolley_routes_[route_];
        point_=(point_+static_cast<unsigned>(selected.points.size())+selected.direction)%static_cast<unsigned>(selected.points.size());
    }
    o.position.y+=trolley_height_;state_[static_cast<std::size_t>(cart-g.level.scripts.begin())].progress=trolley_height_;
    o.rotation[2]=retail::original_heading(g.player.yaw)/16;g.player.position=o.position+Vec3{0,0.25f,0};g.player.velocity={};g.player.grounded=false;g.player.motion=Motion::Idle;
    g.player.pitch=g.player.roll=0;
    if(input.flame_pressed && !cooldown_){const auto muzzle=o.position+Vec3{0,0.65f,0}+direction(g);fire_cannon(g,models,muzzle,direction(g),true);cooldown_=18;}
    // Save the cart transform before firing, which can grow the object vector.
    const auto cart_position=g.retail_objects[cart->object].position;
    for(const auto& s:g.level.scripts) {
        auto& target=g.retail_objects[s.object];if(target.collected || target.hidden)continue;
        if(s.role==ScriptRole::Gear && std::hypot(target.position.x-cart_position.x,target.position.z-cart_position.z)<1.0f && std::abs(target.position.y-cart_position.y)<1.1f) {
            target.collected=true;++gears;g.sound_events.push_back({SoundScope::Object,0,0x20,target.position});
        }else if(s.role==ScriptRole::TrackObstacle && s.type!=0x172 && distance_to_segment(target.position,g.player.previous_position,g.player.position)<0.6f) {
            mode=ChallengeMode::TrolleyResults;success=false;result_ticks_=0;
        }
    }
    if(gears==50){success=true;mode=ChallengeMode::TrolleyResults;result_ticks_=0;++challenge_wins;g.award_orb(0);}
    g.camera.tick(g.level,g.player.position,g.player.yaw,0,0,0);
    prompt="A / D: Branch    Space: Jump    F: Shoot    E: Leave";return true;
}
} // namespace spyro

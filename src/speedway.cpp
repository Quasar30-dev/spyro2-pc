#include "spyro/runtime.hpp"
#include <bit>
namespace spyro {
namespace {
// USA resident 80061920, four 0x58-byte speedway profiles, +20 bonuses.
constexpr unsigned bonuses[4][4]={{60,120,180,120},{120,180,60,120},{180,120,120,180},{60,60,180,120}};
float segment_distance(Vec3 p,Vec3 a,Vec3 b){const auto d=b-a;const float n=dot(d,d);return length(p-(a+d*(n>0?std::clamp(dot(p-a,d)/n,0.0f,1.0f):0)));}
void move_route(RetailObject& o,ScriptProgress& state,const ScriptRoute& route,float speed,bool loop=true) {
    if(route.points.empty())return;
    state.cursor%=static_cast<unsigned>(route.points.size());float distance=speed/60;
    begin_object_transform(o,1);
    for(unsigned guard=0;distance>0 && guard<8;++guard) {
        const auto delta=route.points[state.cursor].position-o.position;const float n=length(delta);
        if(n>0.00001f){const auto dir=delta/n;o.position+=dir*std::min(distance,n);o.rotation[2]=retail::original_heading(std::atan2(dir.x,dir.z))/16;}
        distance-=n;if(distance<0)break;
        if(!loop && state.cursor+1>=route.points.size()){state.flags|=2;break;}
        const auto previous=state.cursor;
        state.cursor=(state.cursor+static_cast<unsigned>(route.points.size())+route.direction)%static_cast<unsigned>(route.points.size());
        if(loop && (route.direction>0?state.cursor<previous:state.cursor>previous))++state.ticks;
    }
}
bool passed(const RetailObject& o,Vec3 previous,Vec3 current) {
    // The authored origin correction locates the middle of the aperture.
    const float correction=std::bit_cast<std::int16_t>(read_u16(o.record,56))/retail::units_per_world;
    const auto center=o.position-Vec3{0,correction,0};
    const auto normal=retail::rotate_model({0,0,1},retail::native_heading(o.rotation[2]*16),0,0);
    const float a=dot(previous-center,normal),b=dot(current-center,normal);
    if((a<0)==(b<0) || std::abs(a-b)<0.000001f)return false;
    const auto p=lerp(previous,current,std::clamp(a/(a-b),0.0f,1.0f))-center;
    const auto side=Vec3{normal.z,0,-normal.x};
    const float width=o.type==0x142 || o.type==0x2c8?1.75f:2.9f,height=o.type==0x142 || o.type==0x2c8?1.75f:3.4f;
    return std::abs(dot(p,side))<width && std::abs(p.y)<height;
}
}
void LevelSystems::reset(Game& g,const ObjectModels* models) {
    *this=LevelSystems{};
    for(const auto& s:g.level.scripts)state_.push_back({s.object,s.type,0,0,s.routes.empty()?0:s.routes.front().initial,0});
    const auto slot=g.level.universe?0:g.level.wad_slot;flight_index=slot==29?0:slot==43?1:slot==53?2:slot==63?3:UINT32_MAX;
    if(speedway())start_trial(g,models);
}
void LevelSystems::start_trial(Game& g,const ObjectModels* models,bool free_flight) {
    if(!speedway())return;
    for(auto& o:g.retail_objects)if(o.runtime_spawned && o.scripted_transient)o.collected=true;
    mode=free_flight?ChallengeMode::FreeFlight:ChallengeMode::Trial;counts={};elapsed=0;remaining=1800;bonus_ticks=last_bonus=0;
    success=false;result_ticks_=hunter_count=hunter_total=0;cannon=UINT32_MAX;shots_.clear();prompt.clear();
    g.dialogue.dismiss(g.retail_objects);g.powers.end_flight();g.powers.flying=true;
    g.player.position=g.player.previous_position=g.play_scene.spawn;g.player.yaw=g.player.previous_yaw=g.level.spawn_yaw;
    g.player.velocity={};g.player.pitch=g.player.previous_pitch=g.player.roll=g.player.previous_roll=0;g.player.air={};g.player.jump={};g.player.swim={};g.player.grounded=false;g.player.motion=Motion::Superfly;
    for(std::size_t i=0;i<g.level.scripts.size();++i) {
        const auto& s=g.level.scripts[i];auto& o=g.retail_objects.at(s.object);o=g.level.objects.at(s.object);
        state_[i]={s.object,s.type,0,0,s.routes.empty()?0:s.routes.front().initial,0};
        if(s.role==ScriptRole::HunterTarget)o.hidden=true;
        // Metro spawners own the actual bungee jumper and slow-sign actor.
        // Replace only the native presentation of that spawner, preserving IDs.
        if(models && (s.type==0x206 || s.type==0x64)) {
            const unsigned type=s.type==0x206?0x207:0x63;
            if(const auto bank=models->models.find(type);bank!=models->models.end()) {
                o.type=static_cast<std::uint16_t>(type);o.model=0;o.size=32;o.scripted_contact=true;o.scripted_transient=true;
                if(bank->second.animated)o.animation=o.previous_animation=start_object_animation(bank->second,0);
            }
        }
    }
    g.camera.scripted_view.reset();g.camera.reset(g.level,g.player.position,g.player.yaw);g.restore_player_pose();
}
void LevelSystems::finish_trial(Game& g,bool won) {
    success=won;mode=ChallengeMode::Results;result_ticks_=0;g.player.velocity={};g.powers.end_flight();
    for(unsigned i=0;i<5;++i)if(i<4?counts[i]>=8:won) {
        const bool awarded=(g.guide_awards.flight_tasks&(1u<<i))!=0;
        g.complete_flight_task(i,i==4?elapsed:0);if(!awarded)g.player.gems+=80;
    }
    if(won){++challenge_wins;const unsigned threshold=flight_index==0 || flight_index==3?4200:4500;if(elapsed && elapsed<threshold)g.award_skill_point(9+flight_index);}
    g.sound_events.push_back({SoundScope::Global,won?28u:29u,0,{}});
}
void LevelSystems::leave_challenge(Game& g) {
    cannon=UINT32_MAX;
    for(const auto& s:shots_)if(s.object<g.retail_objects.size())g.retail_objects[s.object].collected=true;
    shots_.clear();
    if(speedway()) {
        const unsigned destination=g.level.wad_slot==29?15:g.level.wad_slot==63?57:33;
        g.portal_travel.phase=PortalTravel::Phase::Loading;g.portal_travel.fade=1;g.portal_travel.request={destination,flight_index==0?25u:flight_index==1?35u:flight_index==2?45u:55u};
    }else {
        mode=ChallengeMode::None;g.player.position=g.player.previous_position=start_position_;g.player.yaw=start_yaw_;
        g.camera.scripted_view.reset();g.player.velocity={};g.player.grounded=false;g.powers.end_flight();
    }
}
void LevelSystems::start_hunter(Game& g,const ObjectModels* models) {
    if(!speedway())return;
    const auto start=g.player.position;const float yaw=g.player.yaw;
    start_trial(g,models,true);mode=ChallengeMode::Hunter;elapsed=0;remaining=0;start_position_=start;start_yaw_=yaw;
    hunter_count=0;hunter_total=0;
    for(std::size_t i=0;i<g.level.scripts.size();++i) {
        const auto& s=g.level.scripts[i];auto& o=g.retail_objects.at(s.object);
        if(s.role==ScriptRole::FlightTarget)o.hidden=true;
        if(s.role==ScriptRole::HunterTarget){o.hidden=flight_index!=0;++hunter_total;}
        if(s.role==ScriptRole::Hunter && !s.routes.empty()) {
            o.position=o.previous_position=s.routes[0].points[0].position;state_[i].cursor=1;
            const auto delta=s.routes[0].points[1].position-o.position;
            g.player.position=g.player.previous_position=o.position-normalized(delta)*2;
            g.player.yaw=g.player.previous_yaw=std::atan2(delta.x,delta.z);
        }
    }
    g.powers.flying=true;g.camera.reset(g.level,g.player.position,g.player.yaw);
}
bool LevelSystems::before_move(Game& g,InputState& input,const ObjectModels* models) {
    prompt.clear();if(cooldown_)--cooldown_;
    if(results()) {
        ++result_ticks_;g.player.velocity={};
        if(result_ticks_>15 && (input.dialogue.next || input.jump_pressed || input.dialogue.choice>=0 || input.dialogue.cancel)) {
            const auto choice=input.dialogue.cancel?1:std::max(0,input.dialogue.choice);
            if(choice==2)leave_challenge(g);
            else if(mode==ChallengeMode::TrolleyResults){if(choice==0)start_trolley(g,models);else leave_challenge(g);}
            else if(mode==ChallengeMode::HunterResults){if(choice==0)start_hunter(g,models);else start_trial(g,models,true);}
            else start_trial(g,models,choice==1);
        }
        input={};return true;
    }
    if(mode==ChallengeMode::FreeFlight || mode==ChallengeMode::Trial)for(const auto& s:g.level.scripts)if(s.role==ScriptRole::Hunter || s.role==ScriptRole::Worker) {
        const auto& o=g.retail_objects.at(s.object);
        if(length(o.position-g.player.position)<4) {
            const bool available=s.role==ScriptRole::Hunter || (g.guide_awards.flight_tasks&16);
            prompt=available?"E: Hunter challenge":"Complete the course to unlock this challenge";
            if(available && input.dialogue.interact){start_hunter(g,models);input={};return true;}
        }
    }
    if(mode==ChallengeMode::Trial || mode==ChallengeMode::Hunter) {
        if(!g.dialogue.active()) {
            ++elapsed;if(remaining)--remaining;if(bonus_ticks)--bonus_ticks;
            if(mode==ChallengeMode::Trial && !remaining){finish_trial(g,false);input={};return true;}
        }
    }
    if(mode==ChallengeMode::Hunter && flight_index==3) {
        // Canyon's flight is on Hunter's authored airplane route; arrows aim.
        g.player.yaw+=input.move_x*0.022f;g.player.pitch=std::clamp(g.player.pitch+input.move_z*0.018f,-0.7f,0.7f);
        if(input.flame_pressed && !cooldown_){fire_cannon(g,models,g.player.position+retail::origin_above_feet,retail::rotate_model({0,0,1},g.player.yaw,g.player.pitch,0),false);cooldown_=18;}
        tick_shots(g,models);return true;
    }
    if(g.level.wad_slot==39) {
        // Vehicle movement is handled separately, before ordinary walking.
        if(tick_vehicle(g,input,models))return true;
        for(const auto& s:g.level.scripts)if(s.role==ScriptRole::Cannon || s.role==ScriptRole::Trolley) {
            const auto& o=g.retail_objects.at(s.object);if(length(o.position-g.player.position)>2.4f)continue;
            prompt=s.role==ScriptRole::Cannon?"E: Use cannon":"E: Ride trolley";
            if(input.dialogue.interact) {
                if(s.role==ScriptRole::Trolley)start_trolley(g,models);
                else {cannon=s.object;cooldown_=0;start_position_=g.player.position;start_yaw_=g.player.yaw;g.player.yaw=retail::native_heading(o.rotation[2]*16);g.player.pitch=0;g.powers.end_flight();g.dialogue.dismiss(g.retail_objects);}
                input={};return true;
            }
        }
    }
    return false;
}
void LevelSystems::after_move(Game& g,const InputState& input,const ObjectModels* models) {
    if(const auto script=g.dialogue.take_level_script()) {
        if(g.level.wad_slot==39) {
            if(script->first==99 && script->second==6)start_trolley(g,models);
            if(script->first==98 && script->second==1000 && g.retail_objects[23].variant==1 && g.retail_objects[24].variant==1 && g.award_talisman()) {
                // Squawk's talisman line (8003a0d8 command 1) ends in 80016328's
                // state 4 like every talisman speaker's: the talisman rises
                // before Return Home does (update 150).
                unsigned reward=0;
                for(const auto& npc:g.level.npcs)if(npc.object==script->first)
                    for(const auto& line:npc.lines)if(line.talisman>0){reward=static_cast<unsigned>(line.talisman);break;}
                if(reward){g.reward_this_visit=true;g.start_reward_show(script->first,reward,true);g.custom_notice_ticks=0;}
            }
        }else if(speedway() && (script->second==5 || script->second==6))start_hunter(g,models);
    }
    if(g.level.wad_slot==39){tick_shots(g,models);tick_machines(g,models);return;}
    if(!speedway() || results())return;
    unsigned hunter_waypoint=0,hunter_lap=0,hunter_points=0;bool hunter_finished=false;Vec3 hunter_position;
    for(std::size_t i=0;i<g.level.scripts.size();++i)if(g.level.scripts[i].role==ScriptRole::Hunter){hunter_waypoint=state_[i].cursor;hunter_lap=state_[i].ticks;if(!g.level.scripts[i].routes.empty())hunter_points=static_cast<unsigned>(g.level.scripts[i].routes[0].points.size());hunter_finished=(state_[i].flags&2)!=0;hunter_position=g.retail_objects[g.level.scripts[i].object].position;}
    const auto fail=[&]{success=false;mode=ChallengeMode::HunterResults;result_ticks_=0;g.player.velocity={};g.powers.end_flight();};
    const auto origin=g.player.position+retail::origin_above_feet,previous=g.player.previous_position+retail::origin_above_feet;
    for(std::size_t i=0;i<g.level.scripts.size();++i) {
        const auto& s=g.level.scripts[i];auto& state=state_[i];auto& o=g.retail_objects.at(s.object);
        if(o.collected || o.state<0)continue;
        if(s.role==ScriptRole::HunterTarget && mode==ChallengeMode::Hunter) {
            const unsigned start=flight_index==1?s.parameters[1]:flight_index==2?s.parameters[3]:flight_index==3?s.parameters[1]:0;
            // Icy 290 +08 waits for route wraps before +0C can activate.
            // +10 can precede +0C, meaning the deadline is on the next lap.
            const unsigned cursor=flight_index==2?hunter_lap*hunter_points+hunter_waypoint:hunter_waypoint;
            const unsigned activation=flight_index==2?s.parameters[2]*hunter_points+start:start;
            if(o.hidden && (start==UINT32_MAX || cursor>=activation))o.hidden=false;
            if(!o.hidden) {
                ++state.ticks;
                if(flight_index==0 && length(hunter_position-o.position)<2.0f)state.flags|=4;
                if(flight_index==0 && (state.flags&4)) {state.progress+=1; o.size=static_cast<std::uint8_t>(std::max(1.0f,32-state.progress*0.16f));if(state.progress>=195){fail();return;}}
                if(flight_index==2 && cursor>=s.parameters[2]*hunter_points+s.parameters[4]+(s.parameters[4]<start?hunter_points:0)){fail();return;}
                if(flight_index==3 && hunter_waypoint>s.parameters[2]){fail();return;}
                if(flight_index==1) {
                    // 80076cc0 spawns class 237 when Hunter knocks a burglar
                    // off the wall. The collectible is the falling loot.
                    if(!(state.flags&4) && hunter_waypoint>=s.parameters[1]) {
                        state.flags|=4;state.progress=0;
                        if(models && models->models.contains(0x237)) {o.type=0x237;o.model=0;o.size=32;const auto& bank=models->models.at(0x237);if(bank.animated)o.animation=o.previous_animation=start_object_animation(bank,0);}
                    }
                    if(state.flags&4){begin_object_transform(o,1);state.progress+=4.0f/60;o.position.y-=state.progress/60;
                        if(const auto water=g.level.water_at(o.position);water && o.position.y<water->height){fail();return;}}
                }
            }
        }
        if(o.hidden)continue;
        if(s.role==ScriptRole::Hunter && mode==ChallengeMode::Hunter && !s.routes.empty()) {
            move_route(o,state,s.routes[0],flight_index==0?4.8f:flight_index==1?3.2f:flight_index==2?3.5f:7.0f,flight_index==2);
            if(flight_index==3){g.player.position=o.position+Vec3{0,0.65f,0};g.player.velocity={};g.player.grounded=false;g.player.motion=Motion::Superfly;g.camera.tick(g.level,g.player.position,retail::native_heading(o.rotation[2]*16),33,g.player.pitch,0);}
            continue;
        }
        if(s.role!=ScriptRole::FlightTarget && s.role!=ScriptRole::HunterTarget)continue;
        const bool hunter=s.role==ScriptRole::HunterTarget;if(hunter && mode!=ChallengeMode::Hunter)continue;
        if(!s.routes.empty() && !(hunter && flight_index==1))move_route(o,state,s.routes[0],hunter?1.2f:flight_index==0?(s.category==2?4.4f:4.8f):5.0f);
        else if(!hunter && flight_index==1 && s.category==0) {
            begin_object_transform(o,1);o.position.y=g.level.objects[s.object].position.y-1.5f+1.2f*std::sin(float(elapsed+s.object*30)*0.035f);
        }
        const bool touch=hunter?(s.pass_through?passed(o,previous,origin):flight_index==3?(o.attack_flags!=0):segment_distance(o.position,previous,origin)<1.1f):
            s.pass_through?passed(o,previous,origin):(o.attack_flags&(flame_attack|charge_attack|superflame_attack|green_bomb_attack))!=0;
        o.attack_flags=0;
        if(!touch)continue;
        o.collected=true;state.flags|=1;
        if(hunter)++hunter_count;
        else if(mode==ChallengeMode::Trial && s.category<4) {
            counts[s.category]=std::min(8u,counts[s.category]+1);last_bonus=bonuses[flight_index][s.category];remaining+=last_bonus;bonus_ticks=60;
        }
        g.sound_events.push_back({SoundScope::Global,27,0,o.position});
    }
    if(mode==ChallengeMode::Trial && std::all_of(counts.begin(),counts.end(),[](auto n){return n==8;}))finish_trial(g,true);
    if(mode==ChallengeMode::Hunter && hunter_total && hunter_count==hunter_total) {
        success=true;mode=ChallengeMode::HunterResults;result_ticks_=0;++challenge_wins;g.award_orb(0);g.player.velocity={};g.powers.end_flight();
    }
    if(mode==ChallengeMode::Hunter && hunter_finished && ++result_ticks_>180)fail();
    if((mode==ChallengeMode::Trial || mode==ChallengeMode::Hunter) && g.level.water_at(g.player.position) && g.player.position.y<=g.level.water_at(g.player.position)->height+0.1f) {
        if(mode==ChallengeMode::Trial)finish_trial(g,false);else fail();
    }
    if((mode==ChallengeMode::Trial || mode==ChallengeMode::Hunter) && g.player.position.y<g.level.bounds.min.y-3) {
        if(mode==ChallengeMode::Trial)finish_trial(g,false);else{success=false;mode=ChallengeMode::HunterResults;result_ticks_=0;}
    }
    (void)input;
}
} // namespace spyro

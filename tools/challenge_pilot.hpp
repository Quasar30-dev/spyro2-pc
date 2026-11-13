#pragma once
// Orb challenge autopilots, shared by the headless probe (tools/challenges.cpp,
// spyro_challenges) and the workbench's hidden check (--challenge-check NAME,
// src/desktop_challenge_check.inl). A pilot is a list of steps; each game tick
// it returns the pad-equivalent input for the next tick (stick, X, Square,
// Circle, Triangle). The only things it does besides input are the declared
// test fixtures: the first approach to the giver (a teleport in front of him,
// "stand_before"), and any setup a challenge lists (e.g. a power-up already
// earned). Every fixture is counted in `fixtures` and named in `fixture_notes`.
#include "spyro/npc.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace challenge_pilot {
using namespace spyro;
enum class Status {Running,Done,Failed};
struct Pilot;
using Step=std::function<Status(Pilot&,Game&,InputState&,unsigned)>;
struct Pilot {
    std::string name,what;unsigned slot{},giver{},orb_bit{};
    std::vector<std::pair<std::string,Step>> steps;
    unsigned index=0,ticks=0,total=0,fixtures=0;
    std::vector<std::string> fixture_notes;
    std::string failure,photo;   // photo: a moment worth a screenshot this tick (the workbench takes it)
    bool trace=false;
    unsigned orbs_before=0;
    bool finished()const{return index>=steps.size() || !failure.empty();}
    bool awarded(const Game& g)const{return ((g.guide_awards.orbs&~orbs_before)>>orb_bit)&1;}
    void fixture(const std::string& note){++fixtures;fixture_notes.push_back(note);}
    InputState next(Game& g) {
        InputState in;photo.clear();
        if(finished())return in;
        if(!total)orbs_before=g.guide_awards.orbs;
        ++total;
        const auto status=steps[index].second(*this,g,in,ticks++);
        if(status==Status::Done){if(trace)std::printf("pilot %s: step %s done at %u\n",name.c_str(),steps[index].first.c_str(),total);++index;ticks=0;}
        else if(status==Status::Failed)failure=steps[index].first;
        return in;
    }
};
inline std::uint32_t ram(Game& g,std::uint32_t a,unsigned size=4){return g.overlay_scripts.peek(a,size);}
inline float flat(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.z-b.z);}
inline InputState toward(const Game& g,Vec3 target,float strength=1) {
    InputState in;auto d=target-g.player.position;d.y=0;const float l=length(d);
    if(l>.05f){in.move_x=d.x/l*strength;in.move_z=d.z/l*strength;}
    return in;
}
inline float heading_error(const Game& g,Vec3 target) {
    const auto d=target-g.player.position;
    return std::remainder(std::atan2(d.x,d.z)-g.player.yaw,2*3.14159265f);
}
// The stick means different things by Spyro's state. On foot (and swimming)
// it is a world direction here (scripted input is not camera relative). In a
// charge (overlay 18 8006dca8: the stick's x eases a turn rate toward 25, 35
// supercharged) and a glide (x turns, y below 0 brakes) it is relative to
// Spyro: steering there with a world direction turns him at random. A charge
// takes the stick's world direction only on its first tick.
inline bool charging(const Game& g){const auto m=g.player.motion;return m==Motion::Charge || m==Motion::ChargeJump || m==Motion::Supercharge || m==Motion::SuperchargeJump;}
inline InputState steer(const Game& g,Vec3 target,float strength=1) {
    if(g.player.air.gliding || charging(g)) {
        InputState in;in.move_x=std::clamp(heading_error(g,target)*3,-1.0f,1.0f);in.move_z=0;
        return in;
    }
    return toward(g,target,strength);
}
inline InputState charge_toward(const Game& g,Vec3 target){auto in=steer(g,target);in.charge=true;return in;}
// A moby's route (the header's +0 for route followers such as the thieves:
// a 12-byte head, +0 node count, +2 the node it runs to, +10 its direction
// (1 / -1); 16-byte nodes x, y, z, action; 80037fcc follows it).
struct RouteView{std::vector<Vec3> nodes;unsigned target=0;int direction=1;};
inline RouteView route_of(Game& g,unsigned object,unsigned header_offset=0) {
    RouteView r;const auto h=ram(g,g.overlay_scripts.moby_address(object));
    if(h<0x80000000u || h>=0x80800000u)return r;
    const auto route=ram(g,h+header_offset);if(route<0x80000000u || route>=0x80800000u)return r;
    const unsigned n=ram(g,route,2);if(!n || n>256)return r;
    r.target=ram(g,route+2,2);r.direction=static_cast<std::int16_t>(ram(g,route+10,2));
    for(unsigned k=0;k<n;++k){const auto a=route+12+k*16;r.nodes.push_back(retail::to_world({static_cast<std::int32_t>(ram(g,a)),static_cast<std::int32_t>(ram(g,a+4)),static_cast<std::int32_t>(ram(g,a+8))}));}
    return r;
}
inline int find_live(const Game& g,unsigned type) {
    for(std::size_t i=0;i<g.retail_objects.size();++i)if(g.retail_objects[i].type==type && g.retail_objects[i].state>=0 && g.retail_objects[i].retail_script)return static_cast<int>(i);
    return -1;
}
// The fixture: Spyro before a speaker (its heading), facing it.
inline bool stand_before(Game& g,unsigned object,float distance=1.3f) {
    const auto o=g.retail_objects[object];const float yaw=retail::native_heading(o.rotation[2]*16);
    for(float spread:{0.0f,0.6f,-0.6f,1.2f,-1.2f,2.0f,-2.0f,3.14f}) {
        const Vec3 d{std::sin(yaw+spread),0,std::cos(yaw+spread)};
        const auto f=g.level.raycast(o.position+d*distance+Vec3{0,2,0},{0,-1,0},4);
        if(!f || std::abs(f->point.y-o.position.y)>1)continue;
        g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);
        g.camera.reset(g.level,g.player.position,g.player.yaw);
        return true;
    }
    return false;
}
// ---- Steps.
inline Step wait(unsigned n){return [n](Pilot&,Game&,InputState&,unsigned t){return t+1>=n?Status::Done:Status::Running;};}
// 8003873c(point, box): a box is {x, y, z centre, half a (+0C), half b (+10),
// angle (+14)}; the point's XZ offset rotated by the angle (cos / sin tables
// 80061c58 / 80061bd8) must lie within both halves. No height test.
inline bool in_box(Game& g,std::uint32_t box,Vec3 world) {
    const auto p=retail::from_world(world);
    const int dx=static_cast<int>(p.x)-static_cast<std::int32_t>(ram(g,box)),dy=static_cast<int>(p.y)-static_cast<std::int32_t>(ram(g,box+4));
    const auto angle=ram(g,box+0x14)&0xff;
    const int c=static_cast<std::int16_t>(ram(g,0x80061c58+angle*2,2)),s=static_cast<std::int16_t>(ram(g,0x80061bd8+angle*2,2));
    const int a=std::abs(((dx*c)>>12)-((dy*s)>>12)),b=std::abs(((dx*s)>>12)+((dy*c)>>12));
    return a<static_cast<std::int32_t>(ram(g,box+0xc)) && b<static_cast<std::int32_t>(ram(g,box+0x10));
}
inline Vec3 box_centre(Game& g,std::uint32_t box){return retail::to_world({static_cast<std::int32_t>(ram(g,box)),static_cast<std::int32_t>(ram(g,box+4)),static_cast<std::int32_t>(ram(g,box+8))});}
// Walk straight to a point (XZ) with the stick, jumping when stuck. Done
// within `radius`; fails after `limit` ticks.
inline Step walk_to(Vec3 point,float radius=.8f,unsigned limit=1200) {
    auto last=std::make_shared<Vec3>();
    return [=](Pilot&,Game& g,InputState& in,unsigned t) {
        if(t==0)*last=g.player.position;
        if(flat(point,g.player.position)<radius)return Status::Done;
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        in=toward(g,point);
        const bool stuck=flat(g.player.position,*last)<.4f;
        if(t%20==19){if(stuck){in.jump_pressed=true;in.jump_held=true;}*last=g.player.position;}
        else if(t%20<8 && t%20>0 && stuck)in.jump_held=true;
        return t<limit?Status::Running:Status::Failed;
    };
}
// Teleport before the giver (counted fixture), press Triangle until he talks.
inline Step approach_and_talk(unsigned object) {
    return [object](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){if(!stand_before(g,object))return Status::Failed;p.fixture("teleport before speaker "+std::to_string(object));}
        if(g.dialogue.active())return Status::Done;
        in.dialogue.interact=(t%8)==1;
        return t<400?Status::Running:Status::Failed;
    };
}
// Read a conversation through: Cross for the next page, `answer` (0 yes, 1 no)
// for a question. Done once it has closed (or none opened within `grace`).
inline Step read_dialogue(int answer=0,unsigned grace=2) {
    return [answer,grace](Pilot&,Game& g,InputState& in,unsigned t) {
        if(!g.dialogue.active())return t>=grace?Status::Done:Status::Running;
        if(t%30==29){const auto* l=g.dialogue.line();if(l && l->yes>=0 && l->no>=0)in.dialogue.choice=answer;else in.dialogue.next=true;}
        return t<6000?Status::Running:Status::Failed;
    };
}
// Walk back to a speaker with the stick (Cross over steps and rails), come
// round to the point before its face (8003a98c: within 0xA00, the speaker's
// +46 heading within 0x20 of Spyro beyond 0x500, 0x40 nearer, and Spyro facing
// it), press Triangle. Falls back to the teleport fixture after `limit` ticks.
inline Step return_and_talk(unsigned object,unsigned limit=1800) {
    struct S{Vec3 last;bool arrived=false,fallback=false;unsigned fallback_ticks=0;};
    auto s=std::make_shared<S>();
    return [object,limit,s](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(g.dialogue.active())return Status::Done;
        if(t==0){*s={};s->last=g.player.position;}
        if(s->fallback) {
            if(s->fallback_ticks++==0){stand_before(g,object);p.fixture("walk back to speaker "+std::to_string(object)+" failed: teleport");}
            in.dialogue.interact=(s->fallback_ticks%8)==1;
            return s->fallback_ticks<400?Status::Running:Status::Failed;
        }
        const auto at=g.retail_objects[object].position;
        const float yaw=retail::native_heading(g.retail_objects[object].rotation[2]*16);
        const Vec3 front=at+Vec3{std::sin(yaw),0,std::cos(yaw)}*1.3f;
        const float df=flat(front,g.player.position);
        if(df<.45f)s->arrived=true;
        if(s->arrived && df>1.2f)s->arrived=false;
        in=toward(g,s->arrived?at:front);
        if(s->arrived) {
            if(std::abs(heading_error(g,at))>.3f){in.move_x*=.25f;in.move_z*=.25f;}else in.move_x=in.move_z=0;
            in.dialogue.interact=(t%8)==0;
        } else {
            if(df<1.5f){in.move_x*=.5f;in.move_z*=.5f;}
            const bool stuck=flat(g.player.position,s->last)<.4f;
            if(t%20==19){if(stuck){in.jump_pressed=true;in.jump_held=true;}s->last=g.player.position;}
            else if(t%20<8 && t%20>0 && stuck)in.jump_held=true;
        }
        if(t>=limit)s->fallback=true;
        return Status::Running;
    };
}
// Talk (and re-talk) until the scripts' challenge mode word holds `mode`.
inline Step until_word(unsigned object,std::uint32_t address,std::uint32_t value) {
    auto talk=std::make_shared<Step>(return_and_talk(object));auto read=std::make_shared<Step>(read_dialogue());
    auto phase=std::make_shared<unsigned>(0);auto phase_ticks=std::make_shared<unsigned>(0);
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*phase=0;*phase_ticks=0;}
        if(ram(g,address)==value)return Status::Done;
        if(g.dialogue.active() && *phase!=1){*phase=1;*phase_ticks=0;}
        if(*phase==1){if((*read)(p,g,in,(*phase_ticks)++)!=Status::Running){*phase=0;*phase_ticks=0;}}
        else if(*phase==0){if(++*phase_ticks>600){*phase=2;*phase_ticks=0;}}
        else if((*talk)(p,g,in,(*phase_ticks)++)!=Status::Running){*phase=1;*phase_ticks=0;}
        return t<6000?Status::Running:Status::Failed;
    };
}
// After the challenge: the reward line (the giver may talk by himself) and the
// reward presentation (80016328, about 210 updates).
inline Step reward(unsigned ticks=900) {
    auto read=std::make_shared<Step>(read_dialogue());auto reading=std::make_shared<unsigned>(0);
    auto shown=std::make_shared<unsigned>(0),talked=std::make_shared<unsigned>(0);
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*shown=0;*talked=0;}
        if(g.dialogue.active() || *reading){if((*read)(p,g,in,(*reading)++)!=Status::Running)*reading=0;}
        if(g.reward_show.active() && ++*shown==100)p.photo="reward";
        if(g.dialogue.active() && !*talked){*talked=1;p.photo="talk";}
        return t+1>=ticks?Status::Done:Status::Running;
    };
}

// ---- Colossus hockey (Brother Kipp, class 10F; overlay 20).
// Kipp spawns the puck (class 153). The puck (800828b0) asks to be picked up
// (8006a1fc = 10000001) when Spyro skates on ice (8002b078(8006a104) == 4)
// within 600..850 units of it, facing it within 32/256 of a turn; Circle spits
// it (head state 4) and it slides. A goal is the puck entering a 117 keeper's
// box (header +10 centre, +1C / +20 half sizes, 8003873c): Spyro's red goal is
// keeper 22's (121.37, 101.01, half 3.4 x 1.0); the keeper's header +34 says
// whose goal it is (4: the opponent's, 80067084, else Spyro's, 80066fec).
// One on one (80066f30 = 2): the red skater (113, record 21) carries the puck
// too (puck state 5); a flame on him (+18 & 10000) knocks it loose (state 4).
inline Step hockey(Vec3 goal,unsigned keeper,float half_width,int skater,unsigned limit) {
    struct S{bool started=false,staging=false;unsigned goals=0,shots=0,flames=0;};
    auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        const auto mode=ram(g,0x80066f30);
        if(mode)s->started=true;
        if(t==240)p.photo="play";
        if(const auto goals=ram(g,0x80066fec);s->started && goals!=s->goals){s->goals=goals;if(goals==1 || goals==3)p.photo="goal-"+std::to_string(goals);}
        if(s->started && !mode){if(p.trace)std::printf("hockey over: goals %u shots %u flames %u\n",s->goals,s->shots,s->flames);return Status::Done;}
        if(g.dialogue.active())return Status::Done;
        const int puck=find_live(g,0x153);
        if(p.trace && t%120==0 && puck>=0) {
            const auto& q=g.retail_objects[puck];
            std::printf("  hockey t %u goals %u/%u time %u puck %.1f,%.1f s%d spyro %.1f,%.1f head %u shots %u\n",t,ram(g,0x80066fec),ram(g,0x80067084),ram(g,0x8006708c),
                q.position.x,q.position.z,q.state,g.player.position.x,g.player.position.z,g.overlay_scripts.head_state(),s->shots);
        }
        if(!g.overlay_scripts.carrying() && puck>=0 && skater>=0 && g.retail_objects[puck].state==5) {
            const auto at=g.retail_objects[static_cast<unsigned>(skater)].position;
            in=toward(g,at);
            if(flat(at,g.player.position)<3.0f && std::abs(heading_error(g,at))<.4f && t%10==0){in.flame_pressed=true;++s->flames;}
        } else if(g.overlay_scripts.carrying()) {
            // Skating turns slowly: come in from straight out in front of the
            // goal (+z), aim at the side of the mouth the keeper is not
            // covering and shoot once lined up; too close or too wide, circle
            // back out instead of skating into the keeper (who knocks the puck
            // loose).
            Vec3 aim=goal;
            if(keeper<g.retail_objects.size())aim.x=goal.x+(g.retail_objects[keeper].position.x>goal.x?-1:1)*half_width*.7f;
            // Hysteresis: once heading out to the staging point (10 out), get
            // there before turning in again; turning in, shoot when lined up.
            const float out=g.player.position.z-goal.z;
            static const float stage=std::getenv("HOCKEY_STAGE")?std::strtof(std::getenv("HOCKEY_STAGE"),nullptr):7.5f;
            static const float reach=std::getenv("HOCKEY_REACH")?std::strtof(std::getenv("HOCKEY_REACH"),nullptr):8.0f;
            static const float spread=std::getenv("HOCKEY_SPREAD")?std::strtof(std::getenv("HOCKEY_SPREAD"),nullptr):.6f;
            if(keeper<g.retail_objects.size())aim.x=goal.x+(g.retail_objects[keeper].position.x>goal.x?-1:1)*half_width*spread;
            if(s->staging && out>stage-.5f && std::abs(g.player.position.x-goal.x)<half_width+2)s->staging=false;
            if(!s->staging && (out<2.5f || std::abs(g.player.position.x-goal.x)>half_width+5))s->staging=true;
            in=toward(g,s->staging?Vec3{goal.x,goal.y,goal.z+stage+.5f}:aim);
            // Shoot only with the keeper clear of the line to the aim point (where
            // the line crosses his row, 2 in front of the goal).
            bool clear=true;
            if(keeper<g.retail_objects.size()) {
                const auto k=g.retail_objects[keeper].position;const auto from=g.player.position;
                const float f=(k.z-from.z)/std::min(-.01f,aim.z-from.z);
                const float cross_x=from.x+(aim.x-from.x)*std::clamp(f,0.0f,1.0f);
                static const float gap=std::getenv("HOCKEY_GAP")?std::strtof(std::getenv("HOCKEY_GAP"),nullptr):1.3f;
                clear=std::abs(k.x-cross_x)>gap;
            }
            if(!s->staging && clear && flat(aim,g.player.position)<reach && std::abs(heading_error(g,aim))<.16f && g.overlay_scripts.head_state()==3){in.flame_pressed=true;++s->shots;}
        } else if(puck>=0) {
            const auto& q=g.retail_objects[puck];
            in=toward(g,q.position+(q.position-q.previous_position)*6);
        }
        return t<limit?Status::Running:Status::Failed;
    };
}

// ---- Power-ups: defeat enemies for a power-up gate, then cross the gate.
// Each spirit carrier still standing: a teleport beside it (a counted
// fixture), then Circle (flame) every 15 ticks, then a charge (Square toward
// it) if the flame does not take it, until `spirits` spirits have reached the
// level's gates (the gate opens on the arrived count, 80066efc).
inline Step defeat_for_gate(unsigned gate,unsigned only_type=0) {   // only_type: just that class's carriers
    struct S{std::vector<unsigned> targets;unsigned at=0,ticks=0;bool listed=false;};
    auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(gate>=g.retail_objects.size() || !g.retail_objects[gate].powerup_gate)return Status::Failed;
        if(g.retail_objects[gate].powerup_gate->unlocked)return Status::Done;
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        if(!s->listed) {
            s->listed=true;const auto at=g.retail_objects[gate].position;
            for(unsigned i=0;i<g.level.objects.size() && i<g.retail_objects.size();++i) {
                const auto& o=g.retail_objects[i];
                if((o.record[83]&127) && !spirit_released(o) && !o.collected && o.state>=0 && (!only_type || o.type==only_type))s->targets.push_back(i);
            }
            std::sort(s->targets.begin(),s->targets.end(),[&](unsigned a,unsigned b){return length(g.retail_objects[a].position-at)<length(g.retail_objects[b].position-at);});
        }
        while(s->at<s->targets.size() && (spirit_released(g.retail_objects[s->targets[s->at]]) || g.retail_objects[s->targets[s->at]].collected))s->at++,s->ticks=0;
        if(s->at>=s->targets.size())return ++s->ticks<1200?Status::Running:Status::Failed;   // the last spirits are flying to the gate
        const auto i=s->targets[s->at];const auto& o=g.retail_objects[i];
        if(s->ticks++==0) {
            // Beside it, on its floor, facing it.
            bool placed=false;
            for(unsigned k=0;k<16 && !placed;++k) {
                // Its front first (the way it faces), then round it.
                const float a=retail::native_heading(o.rotation[2]*16)+((k%8)&1?1:-1)*float((k%8+1)/2)*3.14159265f/4,r=k<8?1.8f:2.8f;const Vec3 q=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<2.5f) {
                    g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);placed=true;
                }
            }
            p.fixture("teleport beside enemy "+std::to_string(i));
        }
        if(s->ticks<180) {   // face it and breathe fire
            if(std::abs(heading_error(g,o.position))>.25f)in=toward(g,o.position,.35f);
            in.flame_pressed=s->ticks%15==5;
        } else if(s->ticks<(only_type?900u:300u))in=toward(g,o.position),in.charge=true;   // then charge it (chase a fleeing one)
        else {s->at++;s->ticks=0;}
        return t<12000?Status::Running:Status::Failed;
    };
}
// Visit each target (a teleport beside it: a counted fixture), face it and
// attack: Circle (flame) every 15 ticks, then a Square charge into it, until
// `done(g, target)`; then the next. Done when every target is done.
inline Step attack_each(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> done,bool charge_first=false) {
    struct S{unsigned at=0,ticks=0,hits=0;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29){const auto* l=g.dialogue.line();if(l && l->yes>=0 && l->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;}return Status::Running;}
        while(s->at<targets.size() && done(g,targets[s->at])){if(s->ticks && ++s->hits<=3)p.photo="hit-"+std::to_string(s->hits);++s->at;s->ticks=0;}
        if(s->at>=targets.size()) {   // all done, or go round again for the ones missed
            if(std::all_of(targets.begin(),targets.end(),[&](unsigned k){return done(g,k);}))return Status::Done;
            s->at=0;s->ticks=0;return t<30000?Status::Running:Status::Failed;
        }
        const auto i=targets[s->at];const auto& o=g.retail_objects[i];
        if(s->ticks++==0) {
            for(unsigned k=0;k<16;++k) {
                // Its front first (the way it faces), then round it.
                const float a=retail::native_heading(o.rotation[2]*16)+((k%8)&1?1:-1)*float((k%8+1)/2)*3.14159265f/4,r=k<8?1.8f:2.8f;const Vec3 q=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<2.5f) {
                    g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);break;
                }
            }
            p.fixture("teleport beside target "+std::to_string(i));
            if(p.trace && std::getenv("ATTACK_TERRAIN"))for(const float r:{0.0f,1.0f,2.0f,3.0f})for(unsigned k=0;k<(r>0?8u:1u);++k) {
                const float a=k*3.14159265f/4;Vec3 q=o.position+Vec3{std::sin(a)*r,5,std::cos(a)*r};
                std::printf("    column r %.0f a %u:",r,k);
                for(int n=0;n<5;++n){const auto hit=g.level.raycast(q,{0,-1,0},12);if(!hit)break;std::printf(" %.2f(n%.1f)",hit->point.y,hit->normal.y);q=hit->point-Vec3{0,.02f,0};}
                std::printf("\n");
            }
        }
        const unsigned flame_end=charge_first?300:180,charge_from=charge_first?0:180,charge_end=charge_first?120:300;
        if(s->ticks>=charge_from && s->ticks<charge_end){in=toward(g,o.position);in.charge=true;}
        else if(s->ticks<flame_end) {
            if(std::abs(heading_error(g,o.position))>.25f)in=toward(g,o.position,.35f);
            in.flame_pressed=s->ticks%15==5;
        } else {++s->at;s->ticks=0;}
        if(s->ticks==60 && (s->at==0 || s->at+1==targets.size()))p.photo="target-"+std::to_string(i);
        if(p.trace && std::getenv("ATTACK_DEBUG") && s->ticks%10==1){const auto m=g.overlay_scripts.moby_address(i);const auto sp=ram(g,m+8);
            std::printf("    attack t %u target %u st %x +18 %08x spyro %.2f,%.2f,%.2f motion %s target %.2f,%.2f,%.2f d %.2f sph %08x %08x %08x\n",t,i,ram(g,m+0x48,1),ram(g,m+0x18),
                g.player.position.x,g.player.position.y,g.player.position.z,motion_name(g.player.motion),o.position.x,o.position.y,o.position.z,length(o.position-g.player.position),
                sp,sp>=0x80000000u?ram(g,sp+8):0u,sp>=0x80000000u?ram(g,sp+12):0u);}
        return t<30000?Status::Running:Status::Failed;
    };
}
// Charge into each target (Cloud Temples' bells): a teleport (a counted
// fixture) to a floor level with its hit sphere and in sight of it, for a
// plain charge; failing that, to the floor below it, for a jump and then a
// charge in the air (800351d8 state 15 keeps the jump's vertical speed, so the
// charge carries Spyro at the jump's height; a charge jump from the floor
// stays low). Again until `done`.
inline Step charge_jump_each(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> done) {
    struct S{unsigned at=0,ticks=0,hits=0;Vec3 spot,sphere;bool level=false;float closest=1e9f;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        while(s->at<targets.size() && done(g,targets[s->at])){if(s->ticks && ++s->hits<=3)p.photo="rung-"+std::to_string(s->hits);++s->at;s->ticks=0;}
        if(s->at>=targets.size()) {
            if(std::all_of(targets.begin(),targets.end(),[&](unsigned k){return done(g,k);}))return Status::Done;
            s->at=0;s->ticks=0;return t<30000?Status::Running:Status::Failed;
        }
        const auto i=targets[s->at];const auto& o=g.retail_objects[i];
        if(s->ticks++==0) {
            // Its first hit sphere (8004055c kind 0: an unrotated offset).
            s->sphere=o.position;
            if(const auto sp=ram(g,g.overlay_scripts.moby_address(i)+8);sp>=0x80000000u && sp<0x80800000u && ((ram(g,sp)>>16)&0xff)==0 && ((ram(g,sp+4)>>8)&0xff)==0) {
                const auto w=ram(g,sp+8);
                s->sphere=s->sphere+retail::to_world({static_cast<std::int32_t>(w)>>16,static_cast<std::int16_t>(w&0xffff),static_cast<std::int32_t>(ram(g,sp+12))})-retail::to_world({0,0,0});
            }
            bool found=false;
            for(int pass=0;pass<2 && !found;++pass)for(const float r:{2.6f,3.2f,3.8f,4.4f,2.0f}) {
                for(unsigned k=0;k<16 && !found;++k) {
                    const float a=k*3.14159265f/8;Vec3 q=o.position+Vec3{std::sin(a)*r,4,std::cos(a)*r};
                    for(int n=0;n<6;++n) {
                        const auto hit=g.level.raycast(q,{0,-1,0},14);if(!hit)break;q=hit->point-Vec3{0,.02f,0};
                        if(hit->normal.y<.6f)continue;
                        const float rise=s->sphere.y-hit->point.y;
                        const bool ok=pass==0?(rise>-.4f && rise<1.0f):(rise>1.5f && rise<3.5f);
                        if(!ok)continue;
                        const auto from=hit->point+Vec3{0,.5f,0},to=Vec3{s->sphere.x,from.y,s->sphere.z};
                        if(pass==0)if(const auto block=g.level.raycast(from,to-from,length(to-from));block && block->distance<length(to-from)-.6f)continue;
                        s->spot=hit->point+Vec3{0,.01f,0};s->level=pass==0;found=true;break;
                    }
                }
                if(found)break;
            }
            if(found) {
                g.player.position=g.player.previous_position=s->spot;g.player.velocity={};
                g.player.yaw=g.player.previous_yaw=std::atan2(s->sphere.x-s->spot.x,s->sphere.z-s->spot.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
            }
            p.fixture("teleport "+std::string(s->level?"level with":"below")+" target "+std::to_string(i));
            if(p.trace)std::printf("    target %u sphere %.2f,%.2f,%.2f: Spyro %s at %.2f,%.2f,%.2f\n",i,s->sphere.x,s->sphere.y,s->sphere.z,s->level?"level":"below",
                g.player.position.x,g.player.position.y,g.player.position.z);
        }
        const unsigned k=s->ticks%120;
        const unsigned charge_frame=24;   // frame 9 of the jump (the scan for bell 25 found nothing better)
        if(k<15){if(std::abs(heading_error(g,s->sphere))>.1f)in=toward(g,s->sphere,.25f);}
        else if(k<70) {   // the charge lasts 20 frames, then back
            if(s->level){if(k<40)in=charge_toward(g,s->sphere);}
            else {   // X held, then Square on the way up (frame 9 of the jump)
                if(k==15)in.jump_pressed=true;
                in.jump_held=k<40;
                if(k>=charge_frame && k<charge_frame+20)in=charge_toward(g,s->sphere),in.jump_held=k<40;
            }
        } else if(k<115 && flat(s->spot,g.player.position)>.4f)in=toward(g,s->spot,.6f);   // back to the start
        if(s->ticks==30 && s->at==0)p.photo="target-"+std::to_string(i);
        if(p.trace && std::getenv("ATTACK_DEBUG")) {
            const float d=length(s->sphere-(g.player.position+retail::origin_above_feet));
            if(k==0)s->closest=1e9f;s->closest=std::min(s->closest,d);
            if(k==69)std::printf("    charge attempt %u at %u: closest %.2f to its sphere (reach 0.89 for a 550 sphere)\n",s->ticks/120,i,s->closest);
        }
        if(s->ticks>1200){++s->at;s->ticks=0;}
        return t<30000?Status::Running:Status::Failed;
    };
}
// Walk into a whirlwind (class 400, native travel), ride it up, then glide
// (X pressed again at the top, held) toward `land` until Spyro stands at its
// height. Done on the ground within 1.2 units of `land` in height.
inline Step ride_whirlwind(unsigned wind,Vec3 land,unsigned limit=2400) {
    struct S{bool rode=false;unsigned air=0;Vec3 last;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        const bool riding=g.whirlwind.index!=UINT32_MAX;
        if(p.trace && std::getenv("FREEZE_DEBUG") && t%10==0 && (riding || s->rode)) {
            const auto& o=g.retail_objects[wind];const Whirlwind* w=nullptr;for(const auto& x:g.level.whirlwinds)if(x.object==wind)w=&x;
            std::printf("    ride t %u riding %d ticks %u spyro %.2f,%.2f,%.2f v %.2f,%.2f,%.2f %s grounded %d wind %.2f,%.2f,%.2f yaw %.0f h %.2f r %.2f kind %u active %d\n",t,riding?1:0,g.whirlwind.ticks,
                g.player.position.x,g.player.position.y,g.player.position.z,g.player.velocity.x,g.player.velocity.y,g.player.velocity.z,motion_name(g.player.motion),g.player.grounded?1:0,
                o.position.x,o.position.y,o.position.z,o.rotation[2],w?w->height:0.0f,w?w->radius:0.0f,w?w->kind:99u,w && w->active?1:0);
        }
        if(riding){if(!s->rode)p.photo="whirlwind";s->rode=true;return Status::Running;}
        if(s->rode && g.player.grounded && std::abs(g.player.position.y-land.y)<1.2f)return Status::Done;
        if(!s->rode) {   // walk to the whirlwind (jump when stuck)
            const auto at=g.retail_objects[wind].position;
            in=toward(g,at);
            if(t==1)s->last=g.player.position;
            const bool stuck=flat(g.player.position,s->last)<.4f;
            if(t%20==19){if(stuck){in.jump_pressed=true;in.jump_held=true;}s->last=g.player.position;}
            else if(t%20<8 && t%20>0 && stuck)in.jump_held=true;
            return t<limit?Status::Running:Status::Failed;
        }
        // Released: glide toward the landing point (X again in the air), then walk.
        in=steer(g,land);
        if(!g.player.grounded){++s->air;if(s->air==6)in.jump_pressed=true;in.jump_held=true;}
        else {s->air=0;if(g.player.position.y<land.y-1.2f && flat(g.player.position,land)>2){s->rode=false;}}   // fell short: ride again
        return t<limit?Status::Running:Status::Failed;
    };
}
// Cloud Temples' third bell (#25) hangs in the top room of the tower behind
// Murgen (floor 30, the bell 3.3 units over it), 0.6 units higher than the
// others. After the first bell Murgen zeroes the kind of the whirlwind behind
// him (#10), which lifts Spyro to the tower's walkway (27.5); from there the
// trolls are the steps: the freeze shot (Circle with the superfreeze, class
// 270, attack 01000000) turns a troll (1EE, case 800750ac) into an ice block
// (271, the troll's header +00), solid, for 300 updates. One on the walkway
// (#180) lifts Spyro into the room; one in the room (#66 / #67) under the bell
// lets him charge it from a jump. A charge bonk on a block (8006a040 = D)
// shatters it, so every run-up is a walk.
// freeze_climb: of `trolls`, the one nearest `goal` (in XZ) at Spyro's floor
// height; walk to it (a teleport, a counted fixture, only if the stick has not
// got there in 600 ticks), shoot it until it is a block, jump onto the block,
// then either jump from it to the floor at `goal` (bell == UINT32_MAX; done
// standing there) or charge the bell from a jump off it (done when rung).
inline Step freeze_climb(std::vector<unsigned> trolls,Vec3 goal,unsigned bell,std::function<bool(Game&,unsigned)> done) {
    struct S{unsigned phase=0,ticks=0,shots=0,tries=0,troll=UINT32_MAX;Vec3 last,sphere;bool placed=false,aimed=false;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        const bool to_bell=bell!=UINT32_MAX;
        if(to_bell && done(g,bell))return Status::Done;
        if(!to_bell && g.player.grounded && std::abs(g.player.position.y-goal.y)<.3f && flat(g.player.position,goal)<4.5f)return Status::Done;
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        auto& vs=g.overlay_scripts;
        if(s->troll==UINT32_MAX || s->phase==0) {   // the troll nearest the goal, standing at Spyro's height
            float best=1e9f;
            for(const auto k:trolls) {
                const auto& c=g.retail_objects[k];
                if(c.state<0 || std::abs(c.position.y-g.player.position.y)>1.0f)continue;
                const float d=flat(c.position,goal);if(d<best){best=d;s->troll=k;}
            }
            if(s->troll==UINT32_MAX)s->troll=trolls.front();
        }
        const auto troll=s->troll;
        const auto tm=vs.moby_address(troll);const auto th=ram(g,tm);
        const auto& o=g.retail_objects[troll];
        const auto block=th>=0x80000000u && th<0x80800000u?ram(g,th):0u;
        const bool frozen=ram(g,tm+0x48,1)==4 && block>=0x80000000u && block<0x80800000u;
        const auto block_at=frozen?retail::to_world({static_cast<std::int32_t>(ram(g,block+0xc)),static_cast<std::int32_t>(ram(g,block+0x10)),static_cast<std::int32_t>(ram(g,block+0x14))}):o.position;
        if(to_bell && !s->aimed) {   // the bell's first hit sphere (kind 0: an unrotated offset)
            const auto& b=g.retail_objects[bell];s->sphere=b.position;s->aimed=true;
            if(const auto sp=ram(g,vs.moby_address(bell)+8);sp>=0x80000000u && sp<0x80800000u && ((ram(g,sp)>>16)&0xff)==0 && ((ram(g,sp+4)>>8)&0xff)==0) {
                const auto w=ram(g,sp+8);
                s->sphere=s->sphere+retail::to_world({static_cast<std::int32_t>(w)>>16,static_cast<std::int16_t>(w&0xffff),static_cast<std::int32_t>(ram(g,sp+12))})-retail::to_world({0,0,0});
            }
        }
        if(!to_bell)s->sphere=goal;
        ++s->ticks;
        const bool on_block=frozen && g.player.grounded && flat(g.player.position,block_at)<1.2f && g.player.position.y>block_at.y+.25f;
        if(p.trace && std::getenv("FREEZE_DEBUG") && s->ticks%20==1)
            std::printf("    freeze t %u phase %u troll %u %.2f,%.2f,%.2f st %x block %08x at %.2f,%.2f,%.2f spyro %.2f,%.2f,%.2f %s grounded %d ice %u solids %u shots %u\n",t,s->phase,troll,
                o.position.x,o.position.y,o.position.z,ram(g,tm+0x48,1),block,block_at.x,block_at.y,block_at.z,g.player.position.x,g.player.position.y,g.player.position.z,
                motion_name(g.player.motion),g.player.grounded?1:0,g.powers.ice.remaining,vs.solid_mobys,s->shots);
        if(s->phase==0) {   // walk to within 3.2 units of the troll, level with it
            if(s->ticks==1)s->last=g.player.position;
            const float d=flat(g.player.position,o.position);
            if(d<3.2f && std::abs(g.player.position.y-o.position.y)<1.0f){s->phase=1;s->ticks=0;return Status::Running;}
            in=toward(g,o.position);
            const bool stuck=flat(g.player.position,s->last)<.4f;
            if(s->ticks%20==19){if(stuck){in.jump_pressed=true;in.jump_held=true;}s->last=g.player.position;}
            else if(s->ticks%20<8 && s->ticks%20>0 && stuck)in.jump_held=true;
            if(s->ticks>600 && !s->placed) {   // the stick did not get there: the fixture
                s->placed=true;
                for(unsigned k=0;k<16;++k) {
                    const float a=k*3.14159265f/8;const Vec3 q=o.position+Vec3{std::sin(a)*2.6f,0,std::cos(a)*2.6f};
                    if(const auto hit=g.level.raycast(q+Vec3{0,2,0},{0,-1,0},4);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<.6f) {
                        g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                        g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);break;
                    }
                }
                p.fixture("teleport beside troll "+std::to_string(troll));
            }
            return t<30000?Status::Running:Status::Failed;
        }
        if(s->phase==1) {   // face the troll and shoot until it is a block
            if(frozen){s->phase=2;s->ticks=0;p.photo="frozen-"+std::to_string(troll);return Status::Running;}
            if(!g.powers.ice.remaining)return Status::Failed;   // the power ran out
            const auto aim=o.position+(o.position-o.previous_position)*8;
            if(std::abs(heading_error(g,aim))>.12f)in=toward(g,aim,.3f);
            else if(s->ticks%26==5){in.flame_pressed=true;++s->shots;}
            if(flat(g.player.position,o.position)>4.5f)in=toward(g,o.position,.6f);
            return s->ticks<1200?Status::Running:Status::Failed;
        }
        if(!frozen){s->phase=1;s->ticks=0;++s->tries;return s->tries<6?Status::Running:Status::Failed;}   // thawed or shattered: freeze it again
        if(s->phase==2) {   // onto the block: walk to it, then a held jump onto its top
            if(on_block){s->phase=3;s->ticks=0;p.photo="on-block-"+std::to_string(troll);return Status::Running;}
            in=toward(g,block_at);
            const unsigned k=s->ticks%60;
            if(flat(g.player.position,block_at)<1.6f && k<30){if(k==0)in.jump_pressed=true;in.jump_held=true;}
            return s->ticks<1800?Status::Running:Status::Failed;
        }
        // phase 3, on the block: step to its side away from the target, face it,
        // then a held jump; for the bell, the charge at the jump's frame 9.
        const unsigned k=s->ticks%120;
        auto away=g.player.position-s->sphere;away.y=0;const float al=length(away);
        const Vec3 spot=block_at+(al>.01f?away/al:Vec3{1,0,0})*.45f;
        if(k<20){if(flat(spot,g.player.position)>.15f)in=toward(g,spot,.35f);}
        else if(k<35){if(std::abs(heading_error(g,s->sphere))>.1f)in=toward(g,s->sphere,.2f);}
        else if(k<90) {
            if(to_bell){if(k>=44 && k<64)in=charge_toward(g,s->sphere);}
            else in=toward(g,s->sphere);
            if(k==35)in.jump_pressed=true;
            in.jump_held=k<60;
        }
        if(!on_block && k>=95 && g.player.grounded){s->phase=2;s->ticks=0;}   // fell off: back on
        return t<30000?Status::Running:Status::Failed;
    };
}
// Attack each holder (a teleport beside it: a counted fixture; flames, then a
// charge) until it goes, then walk to what it dropped (the nearest live
// `drop_type`, once it has landed) until `counter` counts it; the next holder.
// A drop not reached by the stick in 600 ticks is a teleport (fixture).
inline Step hit_and_collect(std::vector<unsigned> holders,std::function<bool(Game&,unsigned)> gone,unsigned drop_type,std::uint32_t counter,unsigned total) {
    struct S{unsigned at=0,ticks=0,phase=0,still=0,count=0,photos=0;int drop=-1;Vec3 last,seen;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(ram(g,counter)>=total)return Status::Done;
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        if(s->at>=holders.size())return t<30000?Status::Running:Status::Failed;
        const auto i=holders[s->at];const auto& o=g.retail_objects[i];
        if(s->phase==0) {
            if(gone(g,i)) {
                if(++s->photos<=3)p.photo="hit-"+std::to_string(s->photos);
                s->phase=1;s->ticks=0;s->drop=-1;s->still=0;s->count=ram(g,counter);return Status::Running;
            }
            if(s->ticks++==0) {
                for(unsigned k=0;k<16;++k) {
                    const float a=retail::native_heading(o.rotation[2]*16)+((k%8)&1?1:-1)*float((k%8+1)/2)*3.14159265f/4,r=k<8?1.8f:2.8f;const Vec3 q=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                    if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<2.5f) {
                        g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                        g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);break;
                    }
                }
                p.fixture("teleport beside holder "+std::to_string(i));
            }
            if(s->ticks==40 && s->at==0)p.photo="holder-"+std::to_string(i);
            const unsigned k=s->ticks%300;
            if(k>=180){in=toward(g,o.position);in.charge=true;}
            else {if(std::abs(heading_error(g,o.position))>.25f)in=toward(g,o.position,.35f);in.flame_pressed=k%15==5;}
            if(s->ticks>1500){++s->at;s->ticks=0;}   // give up on this one for now
            return t<30000?Status::Running:Status::Failed;
        }
        // Collect the drop.
        ++s->ticks;
        if(ram(g,counter)>s->count) {
            if(p.trace)std::printf("    collected drop of holder %u at t %u (counter %u)\n",i,t,ram(g,counter));
            if(s->photos<=4)p.photo="bone-"+std::to_string(ram(g,counter));
            ++s->at;s->phase=0;s->ticks=0;return Status::Running;
        }
        if(s->drop<0 || g.retail_objects[static_cast<unsigned>(s->drop)].state<0 || g.retail_objects[static_cast<unsigned>(s->drop)].type!=drop_type) {
            s->drop=-1;float best=1e9f;
            for(std::size_t k=g.level.objects.size();k<g.retail_objects.size();++k) {
                const auto& d=g.retail_objects[k];
                if(d.type!=drop_type || d.state<0)continue;
                const float dist=length(d.position-o.position);if(dist<best){best=dist;s->drop=static_cast<int>(k);}
            }
            if(s->drop<0)return s->ticks<300?Status::Running:(++s->at,s->phase=0,s->ticks=0,Status::Running);
            s->seen=g.retail_objects[static_cast<unsigned>(s->drop)].position;s->still=0;
        }
        const auto& d=g.retail_objects[static_cast<unsigned>(s->drop)];
        if(length(d.position-s->seen)<.02f)++s->still;else s->still=0;
        s->seen=d.position;
        if(s->still<10)return Status::Running;   // still flying
        if(s->ticks>900 && s->ticks%300==0) {
            if(const auto f=g.level.raycast(d.position+Vec3{0,2,0},{0,-1,0},6)) {
                g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};g.camera.reset(g.level,g.player.position,g.player.yaw);
                p.fixture("teleport to drop of holder "+std::to_string(i));
            }
        }
        in=toward(g,d.position);
        const bool stuck=flat(g.player.position,s->last)<.3f;
        if(s->ticks%20==19){if(stuck){in.jump_pressed=true;in.jump_held=true;}s->last=g.player.position;}
        else if(s->ticks%20<8 && s->ticks%20>0 && stuck)in.jump_held=true;
        if(p.trace && s->ticks%60==0)std::printf("    drop %d of holder %u at %.2f,%.2f,%.2f spyro %.2f,%.2f,%.2f counter %u\n",s->drop,i,d.position.x,d.position.y,d.position.z,
            g.player.position.x,g.player.position.y,g.player.position.z,ram(g,counter));
        return t<30000?Status::Running:Status::Failed;
    };
}
// Robotica Farms' crow bugs (class 17A, overlay 64 case 8007b908). A bug's
// header lists its perches (+00 + level x 10, three moby indices: the
// scarecrows), +30 the current one, +34 its joint (slot), +38 the level.
// Perched (state 0) it flies off (2, then 3 to the next perch, 4 landing on a
// free joint) once Spyro is within +28 + 4 x level in 2D (4..7 units here);
// it checks nothing while landing or taking off. A flame or charge (+18 &
// 30000) in any state but 5 / 5F..63 catches it (80066fec + 1, state 5, then
// 63 underground); 1800 frames later it comes back and 80066fec drops again.
// Applebee (10D kind 2) waits for all four in state 63 at once.
// The pilot comes up to a scarecrow from the side of the next one (beyond
// every flee radius), steps onto that line inside the smallest one, and
// flames the bugs from the top of a jump as they take off over him.
inline Step ambush_perchers(std::vector<unsigned> bugs,std::function<bool(Game&)> done,unsigned limit=30000) {
    struct S{int phase=0,bug=-1;unsigned ticks=0,qmoby=UINT32_MAX,k=0,caught=0,quiet=0;Vec3 from,stand,approach,last;};auto s=std::make_shared<S>();
    const auto st=[](Game& g,unsigned i){return ram(g,g.overlay_scripts.moby_address(i)+0x48,1);};
    const auto caught=[st](Game& g,unsigned i){const auto v=st(g,i);return g.retail_objects[i].state<0 || v==5 || v==0x5f || v==0x61 || v==0x63;};   // 60: flying back
    const auto perch=[](Game& g,unsigned i,int offset){const auto h=ram(g,g.overlay_scripts.moby_address(i));const auto lv=ram(g,h+0x38);
        const unsigned idx=(ram(g,h+0x30)+static_cast<unsigned>(offset))%3;return ram(g,h+lv*16+idx*4);};
    const auto flee=[](Game& g,unsigned i){const auto h=ram(g,g.overlay_scripts.moby_address(i));return static_cast<float>(ram(g,h+0x28+ram(g,h+0x38)*4))/1024.0f;};
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(done(g))return Status::Done;
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        if(t>=limit)return Status::Failed;
        ++s->ticks;
        unsigned now=0;for(const auto b:bugs)if(caught(g,b))++now;
        if(now>s->caught && now<=3)p.photo="caught-"+std::to_string(now);
        if(now!=s->caught && p.trace)std::printf("    bugs t %u: %u caught (80066fec %u)\n",t,now,ram(g,0x80066fec));
        s->caught=now;
        const auto go=[&](Vec3 point,float strength=1) {
            if(flat(point,g.player.position)>5 && g.player.grounded)in=charge_toward(g,point);else in=toward(g,point,strength);
            const bool stuck=flat(g.player.position,s->last)<.3f;
            if(s->ticks%20==19){if(stuck){in.charge=false;in.jump_pressed=true;in.jump_held=true;}s->last=g.player.position;}
            else if(s->ticks%20<8 && s->ticks%20>0 && stuck){in.charge=false;in.jump_held=true;}
        };
        const auto teleport=[&](Vec3 point,const std::string& why) {
            if(const auto f=g.level.raycast(point+Vec3{0,3,0},{0,-1,0},8)) {
                g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};g.camera.reset(g.level,g.player.position,g.player.yaw);
                p.fixture(why);
            }
        };
        // Any free bug close by and not perched: flame it from a jump.
        int target=-1;float best=1e9f;
        for(const auto b:bugs) {
            if(caught(g,b))continue;
            const auto v=st(g,b);if(v!=2 && v!=3 && v!=4)continue;
            const auto& o=g.retail_objects[b];const auto lead=o.position+(o.position-o.previous_position)*8;
            const float d=std::min(flat(o.position,g.player.position),flat(lead,g.player.position));
            if(d<4.0f && d<best){best=d;target=static_cast<int>(b);}
        }
        if(p.trace && std::getenv("AMBUSH_DEBUG") && !g.interactions.flame_query_points().empty())for(const auto b:bugs)if(!caught(g,b)) {
            const auto& o=g.retail_objects[b];float m=1e9f;Vec3 at{};
            for(const auto q:g.interactions.flame_query_points())if(length(q-o.position)<m){m=length(q-o.position);at=q-o.position;}
            if(m<1.5f)std::printf("      flame t %u bug %u st %x closest %.2f (%.2f,%.2f,%.2f) k %u\n",t,b,st(g,b),m,at.x,at.y,at.z,s->k);
        }
        if(s->k) {   // a jump flame under way: X held 20 ticks, Circle on the way up
            const unsigned k=s->k++;
            if(k<20)in.jump_held=true;
            in.flame_pressed=k==4;
            if(k>=30)s->k=0;
            return Status::Running;
        }
        if(target>=0 && s->phase>=2) {
            const auto& o=g.retail_objects[static_cast<unsigned>(target)];
            const auto lead=o.position+(o.position-o.previous_position)*8;
            const float dl=flat(lead,g.player.position);
            s->quiet=0;
            // Its height over Spyro's feet: the standing flame reaches about
            // 0.4 .. 0.8 (+ 0.36 for the query and its spheres), the jump's
            // top adds about 1.4.
            const float dy=lead.y-g.player.position.y;
            if(g.player.grounded) {
                if(std::abs(heading_error(g,lead))>.15f)in=toward(g,lead,.25f);
                else if(dy>1.1f && dy<2.7f && dl<2.6f){s->k=1;in.jump_pressed=true;in.jump_held=true;}
                else if(dy<=1.1f && dl<2.2f)in.flame_pressed=t%12==0;
            }
            if(p.trace && std::getenv("AMBUSH_DEBUG"))std::printf("    ambush t %u bug %d st %x d %.2f lead %.2f dy %.2f heading %.2f\n",t,target,st(g,static_cast<unsigned>(target)),best,dl,
                o.position.y-g.player.position.y,heading_error(g,lead));
            return Status::Running;
        }
        if(s->phase==0) {   // the perch with the most free bugs (then the nearest)
            s->bug=-1;float score=-1e9f;
            for(const auto b:bugs) {
                if(caught(g,b) || st(g,b)!=0)continue;
                unsigned together=0;for(const auto c:bugs)if(!caught(g,c) && st(g,c)==0 && perch(g,c,0)==perch(g,b,0))++together;
                const float sc=together*100.0f-flat(g.retail_objects[b].position,g.player.position);
                if(sc>score){score=sc;s->bug=static_cast<int>(b);}
            }
            if(s->bug<0)return Status::Running;
            const auto bug=static_cast<unsigned>(s->bug);
            const auto here=perch(g,bug,0);s->qmoby=perch(g,bug,1);
            if(here>=g.retail_objects.size() || s->qmoby>=g.retail_objects.size())return Status::Running;
            float rmin=1e9f,rmax=0;
            for(const auto c:bugs)if(!caught(g,c) && st(g,c)==0 && perch(g,c,0)==here){rmin=std::min(rmin,flee(g,c));rmax=std::max(rmax,flee(g,c));}
            s->from=g.retail_objects[bug].position;   // its own joint, not the scarecrow's centre
            auto d=g.retail_objects[s->qmoby].position-s->from;d.y=0;const float l=std::max(length(d),.01f);d=d/l;
            s->stand=s->from+d*std::max(1.5f,rmin-.8f);s->approach=s->from+d*(rmax+2.5f);
            s->phase=1;s->ticks=0;
            if(p.trace)std::printf("    bugs t %u: bug %d at perch %u (r %.1f..%.1f) to perch %u: approach %.2f,%.2f stand %.2f,%.2f\n",t,s->bug,here,rmin,rmax,s->qmoby,
                s->approach.x,s->approach.z,s->stand.x,s->stand.z);
            return Status::Running;
        }
        const auto bug=static_cast<unsigned>(s->bug);
        if(s->phase==1) {   // round to the far side, outside every flee radius
            if(st(g,bug)!=0){s->phase=0;return Status::Running;}
            if(flat(s->approach,g.player.position)<.8f){s->phase=2;s->ticks=0;s->quiet=0;return Status::Running;}
            go(s->approach);
            if(s->ticks==900)teleport(s->approach,"teleport behind perch "+std::to_string(perch(g,bug,0)));
            if(s->ticks>1200)s->phase=0;
            return Status::Running;
        }
        // In along the line; the bugs take off over Spyro.
        if(p.trace && std::getenv("AMBUSH_DEBUG") && t%5==0){const auto& o=g.retail_objects[bug];std::printf("    in t %u bug %u st %x d %.2f dy %.2f spyro %.2f,%.2f,%.2f stand %.2f\n",t,bug,st(g,bug),flat(o.position,g.player.position),
            o.position.y-g.player.position.y,g.player.position.x,g.player.position.y,g.player.position.z,flat(s->stand,g.player.position));}
        if(p.trace && std::getenv("AMBUSH_DEBUG") && s->phase==1 && t%20==0)std::printf("    round t %u spyro %.2f,%.2f to %.2f\n",t,g.player.position.x,g.player.position.z,flat(s->approach,g.player.position));
        if(st(g,bug)==0) {   // it settles on its joint while perched: follow its line
            auto d=g.retail_objects[s->qmoby].position-g.retail_objects[bug].position;d.y=0;const float l=std::max(length(d),.01f);
            s->from=g.retail_objects[bug].position;s->stand=s->from+d/l*std::max(1.5f,flee(g,bug)-.8f);
        }
        if(flat(s->stand,g.player.position)>.4f && st(g,bug)==0)go(s->stand,.6f);
        else if(std::abs(heading_error(g,s->from))>.15f && g.player.grounded)in=toward(g,s->from,.2f);
        if(st(g,bug)!=0 && ++s->quiet>120)s->phase=0;
        if(s->ticks>900)s->phase=0;
        return Status::Running;
    };
}
// Diagnostic: walk at a target and log the given objects' RAM states and positions.
inline Step watch_objects(unsigned approach,std::vector<unsigned> objects,unsigned limit) {
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        const auto& a=g.retail_objects[approach];
        if(flat(a.position,g.player.position)>1.5f)in=toward(g,a.position,.6f);
        if(t%10==0) {
            std::printf("    watch t %u spyro %.2f,%.2f,%.2f",t,g.player.position.x,g.player.position.y,g.player.position.z);
            for(const auto i:objects){const auto& o=g.retail_objects[i];const auto m=g.overlay_scripts.moby_address(i);const auto h=ram(g,m);
                std::printf(" | %u st %x %.2f,%.2f,%.2f idx %u slot %d d %.2f",i,ram(g,m+0x48,1),o.position.x,o.position.y,o.position.z,ram(g,h+0x30),static_cast<int>(ram(g,h+0x34)),flat(o.position,g.player.position));}
            std::printf(" | fec %u\n",ram(g,0x80066fec));
            if(t==0)for(const auto i:objects){const auto m=g.overlay_scripts.moby_address(i);const auto sp=ram(g,m+8);std::printf("    %u spheres %08x:",i,sp);if(sp>=0x80000000u)for(unsigned k=0;k<10;++k)std::printf(" %08x",ram(g,sp+k*4));std::printf("\n");}
        }
        return t<limit?Status::Running:Status::Done;
    };
}
// Lure, then attack: a teleport `distance` in front of each target (where it
// faces; a counted fixture), facing it, standing still until it comes out
// (its RAM state leaves `waiting`), then flames while facing it and a charge
// when close, until `done`.
// jump: flame from the top of a jump (a target that rises above Spyro's
// mouth, e.g. Skelos' class 26 toads of group 9, which hover 1900 units
// over the floor while Spyro is within 5500), and never walk closer than 2.
inline Step lure_and_attack(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> done,unsigned waiting,float distance,bool jump=false) {
    struct S{unsigned at=0,ticks=0;bool out=false;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        while(s->at<targets.size() && done(g,targets[s->at])){if(s->ticks)p.photo="hit-"+std::to_string(s->at+1);++s->at;s->ticks=0;s->out=false;}
        if(s->at>=targets.size()) {
            if(std::all_of(targets.begin(),targets.end(),[&](unsigned k){return done(g,k);}))return Status::Done;
            s->at=0;s->ticks=0;return t<30000?Status::Running:Status::Failed;
        }
        const auto i=targets[s->at];const auto& o=g.retail_objects[i];
        if(s->ticks++==0) {
            const float yaw=retail::native_heading(o.rotation[2]*16);
            for(const float spread:{0.0f,.4f,-.4f,.8f,-.8f})for(const float r:{distance,distance-1,distance+1}) {
                const Vec3 q=o.position+Vec3{std::sin(yaw+spread),0,std::cos(yaw+spread)}*r;
                if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<3.0f) {
                    g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                    goto placed;
                }
            }
            placed:p.fixture("teleport before target "+std::to_string(i));
        }
        const auto st=ram(g,g.overlay_scripts.moby_address(i)+0x48,1);
        if(st!=waiting)s->out=true;
        if(std::abs(heading_error(g,o.position))>.25f && (!jump || flat(o.position,g.player.position)>2.0f))in=toward(g,o.position,.3f);
        if(s->out && jump) {
            // X held 20 ticks (a jump gains speed for its first 13 held
            // VBlanks), Circle at 10, near the top.
            const unsigned k=s->ticks%36;
            if(k==0){in.jump_pressed=true;in.jump_held=true;}else if(k<20)in.jump_held=true;
            in.flame_pressed=k==10;
        } else if(s->out) {
            in.flame_pressed=s->ticks%12==3;
            if(flat(o.position,g.player.position)<3.0f && s->ticks%60>40){in=toward(g,o.position);in.charge=true;}
        }
        if(p.trace && std::getenv("LURE_DEBUG"))for(const auto q:g.interactions.flame_points())std::printf("    flame point %.2f,%.2f,%.2f to target %.2f (dy %.2f)\n",q.x,q.y,q.z,length(q-o.position),q.y-o.position.y);
        if(p.trace && std::getenv("LURE_DEBUG") && s->ticks%6==0)std::printf("    lure t %u target %u st %x out %d spyro %.2f,%.2f,%.2f target %.2f,%.2f,%.2f\n",t,i,st,s->out?1:0,
            g.player.position.x,g.player.position.y,g.player.position.z,o.position.x,o.position.y,o.position.z);
        if(s->ticks>900){++s->at;s->ticks=0;s->out=false;}
        return t<30000?Status::Running:Status::Failed;
    };
}
// Chase each target down (a teleport near it first: a counted fixture): run
// at it, flame when it is close in front, charge when closer; on to the next
// once `done`.
inline Step chase_each(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> done,unsigned per_target=1800) {
    struct S{unsigned at=0,ticks=0;Vec3 last;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        while(s->at<targets.size() && done(g,targets[s->at])){if(s->ticks)p.photo="caught-"+std::to_string(s->at+1);++s->at;s->ticks=0;}
        if(s->at>=targets.size()) {
            if(std::all_of(targets.begin(),targets.end(),[&](unsigned k){return done(g,k);}))return Status::Done;
            s->at=0;s->ticks=0;return t<40000?Status::Running:Status::Failed;
        }
        const auto i=targets[s->at];const auto& o=g.retail_objects[i];
        if(s->ticks++==0) {
            for(unsigned k=0;k<16;++k) {
                const float a=k*3.14159265f/8,r=k<8?2.5f:4.0f;const Vec3 q=o.position+Vec3{std::sin(a)*r,0,std::cos(a)*r};
                if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<2.0f) {
                    g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-q.x,o.position.z-q.z);g.camera.reset(g.level,g.player.position,g.player.yaw);break;
                }
            }
            p.fixture("teleport near target "+std::to_string(i));s->last=g.player.position;
        }
        const auto lead=o.position+(o.position-o.previous_position)*8;
        in=toward(g,lead);
        const float d=flat(lead,g.player.position);
        if(d<3.0f && std::abs(heading_error(g,lead))<.35f)in.flame_pressed=s->ticks%10==0;
        if(d<7.0f && std::abs(heading_error(g,lead))<.6f)in.charge=true;   // the charge outruns a fleeing thief
        if(s->ticks%20==19){if(flat(g.player.position,s->last)<.3f){in.jump_pressed=in.jump_held=true;}s->last=g.player.position;}
        if(s->ticks>per_target){++s->at;s->ticks=0;}
        return t<40000?Status::Running:Status::Failed;
    };
}
// Chase thieves along their own route (Mystic Marsh 1A, overlay 58 8007b01c).
// A thief waits (state 0) until Spyro is within 8 units, then runs (state 1)
// node to node away from him: 80038acc finds the node nearest Spyro, and the
// direction (+10) flips when that node lies ahead. Its speed is (4096 -
// distance to Spyro) / 16 + 224 clamped to 192..272 per two VBlanks, less its
// tiredness (header +14, up by the frame count each tick, capped at 1024, /16)
// : 5.6..8 units a second fresh, down to 3.75..6.1 tired; Spyro's charge is
// 7.15. A flame or charge (+18 & 30000) catches it (state 7). The pilot
// charges after it along the route's nodes (its own walkable path, never a
// straight line across water or off a ledge), straight at it once near and
// level, steering the charge as the pad does. One teleport near each thief
// it starts on (a counted fixture), only when it is far.
inline Step chase_route(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> done,unsigned per_target=1500) {
    struct S{unsigned at=0,ticks=0,stuck=0,hold=0,lost=0;Vec3 last;bool place=true;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return Status::Running;}
        while(s->at<targets.size() && done(g,targets[s->at])){if(s->ticks)p.photo="caught-"+std::to_string(s->at+1);++s->at;s->ticks=0;s->place=true;}
        if(s->at>=targets.size()) {
            if(std::all_of(targets.begin(),targets.end(),[&](unsigned k){return done(g,k);}))return Status::Done;
            s->at=0;s->ticks=0;s->place=true;return t<40000?Status::Running:Status::Failed;
        }
        const auto i=targets[s->at];const auto& o=g.retail_objects[i];
        const auto route=route_of(g,i);
        const auto state=ram(g,g.overlay_scripts.moby_address(i)+0x48,1);
        ++s->ticks;
        if(s->place) {
            // Behind the thief on its route, 3 units back toward the node it
            // came from (route +2 less the direction), facing it: Spyro's
            // nearest node is then behind it, so it keeps its direction and
            // runs on ahead (80038acc). A counted fixture; only when far.
            s->place=false;s->stuck=0;s->lost=0;
            if(flat(o.position,g.player.position)>6 || std::abs(o.position.y-g.player.position.y)>2) {
                Vec3 back=o.position-(o.position-o.previous_position)*20;
                if(route.nodes.size()>1) {
                    const unsigned n=static_cast<unsigned>(route.nodes.size());
                    back=route.nodes[(route.target+n-static_cast<unsigned>(route.direction))%n];
                    if(flat(back,o.position)<1)back=route.nodes[(route.target+2*n-2*static_cast<unsigned>(route.direction))%n];
                }
                Vec3 d=Vec3{back.x-o.position.x,0,back.z-o.position.z};
                d=length(d)>.1f?normalized(d):Vec3{0,0,1};
                bool placed=false;
                for(const float r:{3.0f,2.2f,4.0f})for(const float turn:{0.0f,.5f,-.5f,1.0f,-1.0f}) {
                    if(placed)break;
                    const Vec3 dir{d.x*std::cos(turn)-d.z*std::sin(turn),0,d.x*std::sin(turn)+d.z*std::cos(turn)};
                    const auto q=o.position+dir*r;
                    if(const auto hit=g.level.raycast(q+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-o.position.y)<1.2f) {
                        g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};placed=true;
                    }
                }
                if(placed) {
                    g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-g.player.position.x,o.position.z-g.player.position.z);
                    g.camera.reset(g.level,g.player.position,g.player.yaw);p.fixture("teleport behind thief "+std::to_string(i));
                }
            }
            s->last=g.player.position;
        }
        const auto me=g.player.position;
        const float gap=flat(o.position,me),dy=o.position.y-me.y;
        const auto aim=o.position+(o.position-o.previous_position)*std::min(10.0f,gap*2);
        const bool swimming=g.player.swim.mode!=WaterMode::Dry;
        if(route.nodes.size()==1) {
            // Mode 1 (header +28 = 1): a one-node route. Within 4 units of
            // its node the thief runs from Spyro; beyond, 80039de4 takes it
            // round the node (radius 0x1800) to the side away from Spyro and
            // it stops there. So: stand on the node, then charge at where it
            // will be (it moves round, not away), and back to the node.
            const auto home=route.nodes[0];
            if(!s->hold && s->ticks%240==0)s->stuck=0;
            if(s->lost==0 && flat(home,me)>1.5f && gap>3.5f)in=toward(g,home);
            else if(gap<7) {
                const float reach=gap/7.15f*30;   // script ticks to get there
                const auto lead=o.position+(o.position-o.previous_position)*reach;
                in=charging(g) || gap>2.4f?charge_toward(g,lead):toward(g,o.position,.4f);
                if(++s->lost>45){s->lost=0;}   // a charge lasts at most 45 ticks, then back to the node
            } else in=toward(g,o.position,.3f);
            if(gap<2.8f && std::abs(dy)<1.0f && std::abs(heading_error(g,o.position))<.45f && !charging(g))in.flame_pressed=s->ticks%6==0;
            if(p.trace && s->ticks%120==0)std::printf("  guard t %u thief %u at %.1f,%.1f,%.1f st %u spyro %.1f,%.1f,%.1f %s\n",t,i,o.position.x,o.position.y,o.position.z,state,me.x,me.y,me.z,motion_name(g.player.motion));
            if(s->ticks>per_target){++s->at;s->ticks=0;s->place=true;}
            return t<40000?Status::Running:Status::Failed;
        }
        if((state==0 && gap>7) || swimming)in=toward(g,aim);   // walk to a waiting thief; swim out
        else in=charge_toward(g,aim);
        if(gap<2.6f && std::abs(dy)<1.0f && std::abs(heading_error(g,o.position))<.4f && !charging(g))in.flame_pressed=s->ticks%8==0;
        if(s->ticks%20==19) {
            if(flat(g.player.position,s->last)<.4f)++s->stuck;else s->stuck=0;
            s->last=g.player.position;
            if(s->stuck)s->hold=14;   // a full jump: X held
        }
        if(s->hold){in.jump_pressed=s->hold==14;in.jump_held=true;--s->hold;if(charging(g))in.charge=true;}
        // Out-run, lost on a ledge or in the water: back behind it.
        if(gap>12 || std::abs(dy)>3 || swimming)++s->lost;else s->lost=0;
        if(s->stuck>=4 || s->lost>90)s->place=true;
        if(p.trace && s->ticks%120==0)std::printf("  chase t %u thief %u at %.1f,%.1f,%.1f st %u target %u dir %d spyro %.1f,%.1f,%.1f %s\n",t,i,o.position.x,o.position.y,o.position.z,
            state,route.target,route.direction,me.x,me.y,me.z,motion_name(g.player.motion));
        if(s->ticks>per_target){++s->at;s->ticks=0;s->place=true;}
        return t<40000?Status::Running:Status::Failed;
    };
}
// Run `body` over and over until `stop` holds (targets that come back).
inline Step until(std::function<Step()> make,std::function<bool(Game&)> stop,unsigned limit=40000) {
    auto body=std::make_shared<Step>();auto ticks=std::make_shared<unsigned>(0);
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*body=make();*ticks=0;}
        if(stop(g))return Status::Done;
        if((*body)(p,g,in,(*ticks)++)!=Status::Running){*body=make();*ticks=0;}
        return t<limit?Status::Running:Status::Failed;
    };
}
// attack_each over every authored object of a class (listed on the first tick).
inline Step attack_class(unsigned type,std::function<bool(Game&,unsigned)> done,bool charge_first=false) {
    auto inner=std::make_shared<Step>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0) {
            std::vector<unsigned> targets;
            for(unsigned i=0;i<g.level.objects.size() && i<g.retail_objects.size();++i)if(g.retail_objects[i].type==type)targets.push_back(i);
            *inner=attack_each(targets,done,charge_first);
        }
        return (*inner)(p,g,in,t);
    };
}
// Walk through a power-up gate: a teleport in front of it (fixture), then the
// stick straight through its plane.
inline Step cross_gate(unsigned gate,std::function<bool(Game&)> skip={}) {
    // The gate (powerups.cpp, 80075f6c) fires when Spyro's origin crosses its
    // plane within 0x380 units of its centre: start on whichever side has
    // floor, walk straight through the middle.
    auto side=std::make_shared<float>(-1.0f);
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(gate>=g.retail_objects.size())return Status::Failed;
        if(t==0 && skip && (skip(g) || g.dialogue.active()))return Status::Done;   // nothing left to power up for
        const auto& o=g.retail_objects[gate];
        const float yaw=retail::native_heading(o.rotation[2]*16);const Vec3 dir{std::sin(yaw),0,std::cos(yaw)};
        if(t==0) {
            *side=-1;bool placed=false;
            for(const float s:{-1.0f,1.0f})for(const float r:{2.2f,1.6f,3.0f}) {
                if(placed)break;
                const Vec3 start=o.position+dir*(s*r);
                const auto hit=g.level.raycast(start+Vec3{0,2.5f,0},{0,-1,0},5);
                if(!hit || hit->normal.y<.6f || std::abs(hit->point.y-o.position.y)>1.6f)continue;
                g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};*side=s;placed=true;
            }
            g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(-dir.x*(*side),-dir.z*(*side));g.camera.reset(g.level,g.player.position,g.player.yaw);
            p.fixture("teleport before the power-up gate "+std::to_string(gate));
        }
        if(g.dialogue.active()){if(t%30==29)in.dialogue.next=true;return t<1200?Status::Running:Status::Failed;}
        if(g.powers.flight.remaining>300 || g.powers.fire.remaining>300 || g.powers.ice.remaining>300) {   // a fresh power (a running one may be nearly spent)
            if(p.trace)std::printf("  gate %u kind %u duration %u: flight %u fire %u flying %d\n",gate,o.powerup_gate?o.powerup_gate->kind:99,o.powerup_gate?o.powerup_gate->duration:0,
                g.powers.flight.remaining,g.powers.fire.remaining,g.powers.flying?1:0);
            return Status::Done;
        }
        in=toward(g,o.position-dir*(*side*3));
        // Blocked short of the plane (a ledge or the gate frame): jump through.
        if(t>40 && t%30==0){in.jump_pressed=true;in.jump_held=true;}else if(t>40 && t%30<12)in.jump_held=true;
        if(t==20)p.photo="gate";
        return t<400?Status::Running:Status::Failed;
    };
}
// Fly (superfly) to each target and flame it: the stick turns (x) and pitches
// (z, + dives), X lifts. Done when `done` says so.
inline InputState fly_at(const Game& g,Vec3 target) {
    InputState in;const auto d=target-(g.player.position+retail::origin_above_feet);
    const float heading=std::atan2(d.x,d.z),pitch=std::atan2(d.y,std::hypot(d.x,d.z));
    in.move_x=std::clamp(std::remainder(heading-g.player.yaw,2*3.14159265f)*4,-1.0f,1.0f);
    in.move_z=std::clamp((g.player.pitch-pitch)*4,-1.0f,1.0f);
    return in;
}
inline Step flame_targets(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> lit,std::function<bool(Game&)> done,unsigned limit,float aim_height=.6f,float range=4.5f,float lead=0,bool until_power_out=false) {
    struct S{unsigned at=0,flames=0,evade=0,room=0,hits=0;Vec3 last;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*s={};s->at=static_cast<unsigned>(targets.size());}
        if(done(g))return Status::Done;
        if(g.dialogue.active())return Status::Done;
        // The power has run out: back to the gate (the next steps) for more.
        if(until_power_out && t>60 && !g.powers.flying && !g.powers.flight.remaining && !g.powers.fire.remaining)return Status::Done;
        // The nearest target still unlit, chosen again once the current one is lit.
        if(s->at>=targets.size() || lit(g,targets[s->at])) {
            if(s->at<targets.size() && ++s->hits<=3)p.photo="hit-"+std::to_string(s->hits);
            s->at=static_cast<unsigned>(targets.size());float best=1e9f;
            for(unsigned k=0;k<targets.size();++k)if(!lit(g,targets[k])) {
                const float d=length(g.retail_objects[targets[k]].position-g.player.position);
                if(d<best){best=d;s->at=k;}
            }
        }
        if(s->at>=targets.size())return t<limit?Status::Running:Status::Failed;
        const auto& o=g.retail_objects[targets[s->at]];
        // lead < 0: aim where a superflame shot (440 units a frame, 12.9 world
        // units a second) meets it; position deltas are per script tick (2 frames).
        const float range_now=length(o.position-(g.player.position+retail::origin_above_feet));
        const float lead_ticks=lead<0?range_now/12.9f*30:lead;
        const auto aim=o.position+Vec3{0,aim_height,0}+(o.position-o.previous_position)*lead_ticks;
        if(g.powers.flying) {
            in=fly_at(g,aim);
            // Pinned against a wall or pillar: turn away and climb for a while.
            if(t%20==0){if(length(g.player.position-s->last)<.3f && !s->evade)s->evade=36;s->last=g.player.position;}
            if(s->evade){--s->evade;in.move_x=1;in.move_z=s->evade>18?-1.0f:0.0f;}
            // Superfly turns about 105 degrees a second at 5.4 units a second: a
            // target inside that circle (radius about 3) is orbited, never faced.
            // Fly on straight to get room, then turn back in.
            {
                const float d=length(aim-(g.player.position+retail::origin_above_feet));
                const float err=std::remainder(std::atan2(aim.x-g.player.position.x,aim.z-g.player.position.z)-g.player.yaw,6.2831853f);
                if(!s->evade && !s->room && d<4.5f && std::abs(err)>.8f)s->room=50;
                if(s->room){--s->room;in.move_x=0;}
            }
            const float d=length(aim-(g.player.position+retail::origin_above_feet));
            if(d<range && std::abs(std::remainder(std::atan2(aim.x-g.player.position.x,aim.z-g.player.position.z)-g.player.yaw,6.2831853f))<.35f && t%6==0){in.flame_pressed=true;++s->flames;}
            if(d<1.2f)in.move_z=-1;   // pull up past it
        } else {
            // Superfly takes off with X pressed again in the air after a jump.
            in=toward(g,aim);
            if(t%20==0){in.jump_pressed=true;in.jump_held=true;}
            else if(t%20==8 && !g.player.grounded){in.jump_pressed=true;in.jump_held=true;}
        }
        if(p.trace && t%120==0)std::printf("  flame t %u rot %.0f,%.0f,%.0f target %u/%zu (%u) at %.1f,%.1f,%.1f spyro %.1f,%.1f,%.1f flying %d flight %u flames %u\n",t,o.rotation[0],o.rotation[1],o.rotation[2],s->at,targets.size(),targets[s->at],
            o.position.x,o.position.y,o.position.z,g.player.position.x,g.player.position.y,g.player.position.z,g.powers.flying?1:0,g.powers.flight.remaining,s->flames);
        return t<limit?Status::Running:Status::Failed;
    };
}

// Superfly sweep over ground targets (Crystal Glacier's draclets, Magma
// Cone's crashers). Superfly (powerups.cpp fly_move, resident table
// 80060180) turns 20 angle units a VBlank (1.84 rad/s) at 5.45 units a second
// level: a turning circle of radius about 3. A target inside that circle off
// the nose is orbited, never faced. So each tick the pilot picks the unlit
// target cheapest to reach counting the turn (distance + 3.2 x the heading
// change, a target close abeam costs a whole circle), commits to it until it
// is lit or passed, flies at its height plus `height` (pulling up over
// floors and away from walls) and breathes fire (`shoot`: Circle) whenever
// any unlit target is within `range` ahead. A grounded Spyro jumps and
// presses X again in the air to fly on. Done when `done` (or the power is
// spent, with until_power_out).
inline Step sweep_targets(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> lit,std::function<bool(Game&)> done,unsigned limit,
                          float height=.8f,float range=3.2f,bool until_power_out=false,float shot_speed=0,std::vector<unsigned> ring={}) {
    struct S{int at=-1;unsigned committed=0,flames=0,hits=0,evade=0,ring_at=~0u;Vec3 last;std::vector<bool> was;std::vector<unsigned> states;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*s={};s->was.assign(targets.size(),false);}
        if(done(g))return Status::Done;
        if(g.dialogue.active())return Status::Done;
        if(until_power_out && t>60 && !g.powers.flying && !g.powers.flight.remaining)return Status::Done;
        if(s->states.size()!=targets.size())s->states.assign(targets.size(),0);
        for(unsigned k=0;k<targets.size();++k){
            const bool now=lit(g,targets[k]);if(now && !s->was[k] && ++s->hits<=4)p.photo="hit-"+std::to_string(s->hits);s->was[k]=now;
            const auto st=ram(g,g.overlay_scripts.moby_address(targets[k])+0x48,1);
            if(st!=s->states[k] && p.trace && std::getenv("SWEEP_DEBUG"))std::printf("    t %u target %u state %x -> %x\n",t,targets[k],s->states[k],st);
            s->states[k]=st;
        }
        const auto eye=g.player.position+retail::origin_above_feet;
        static const float tune_h=std::getenv("SWEEP_H")?std::strtof(std::getenv("SWEEP_H"),nullptr):-99.0f;const float height_now=tune_h>-90?tune_h:height;
        static const float tune_c=std::getenv("SWEEP_C")?std::strtof(std::getenv("SWEEP_C"),nullptr):-99.0f;const float clearance=tune_c>-90?tune_c:std::max(.5f,height_now+.35f);
        const auto lead_of=[&](unsigned k){
            const auto& o=g.retail_objects[targets[k]];
            const float d=length(o.position-eye);
            const float ticks=shot_speed>0?d/shot_speed*30:std::min(20.0f,d/5.45f*30*.5f);
            return o.position+Vec3{0,height_now,0}+(o.position-o.previous_position)*ticks;
        };
        const auto cost_of=[&](unsigned k){
            const auto a=lead_of(k);const float d=flat(a,eye);
            const float err=std::abs(std::remainder(std::atan2(a.x-eye.x,a.z-eye.z)-g.player.yaw,6.2831853f));
            // Inside the turning circle and off the nose: a full loop first.
            const float circle=(d<6.5f && err>.6f)?20.0f:0.0f;
            return d+3.2f*err+circle+std::abs(a.y-eye.y)*.5f;
        };
        const auto clear_to=[&](Vec3 a,Vec3 b){const auto d=b-a;const auto hit=g.level.raycast(a,d,length(d));return !hit || hit->distance>=length(d)-.05f;};
        const auto index_of=[&](unsigned object){return static_cast<int>(std::find(targets.begin(),targets.end(),object)-targets.begin());};
        if(!ring.empty()) {
            // A tour (ring of target ids, each in sight of the next): fly it
            // node by node, every target a waypoint whether standing or not,
            // so the flight keeps to the tunnels; on to the next node once
            // this one is reached or passed, or is down with the next in sight.
            if(s->ring_at>=ring.size()) {
                float best=1e9f;
                for(unsigned k=0;k<ring.size();++k){const float d=length(g.retail_objects[ring[k]].position-eye);if(d<best && clear_to(eye,g.retail_objects[ring[k]].position+Vec3{0,.8f,0})){best=d;s->ring_at=k;}}
                if(s->ring_at>=ring.size())s->ring_at=0;
            }
            for(unsigned guard=0;guard<ring.size();++guard) {
                const unsigned k=static_cast<unsigned>(index_of(ring[s->ring_at]));
                const auto a=lead_of(k);
                const float err=std::abs(std::remainder(std::atan2(a.x-eye.x,a.z-eye.z)-g.player.yaw,6.2831853f));
                const auto next=g.retail_objects[ring[(s->ring_at+1)%ring.size()]].position+Vec3{0,.8f,0};
                const bool reached=flat(a,eye)<1.8f || (flat(a,eye)<4.0f && err>1.3f && g.powers.flying);
                if(reached || (lit(g,targets[k]) && clear_to(eye,next)) || ++s->committed>420){s->ring_at=(s->ring_at+1)%static_cast<unsigned>(ring.size());s->committed=0;}
                else break;
            }
            s->at=index_of(ring[s->ring_at]);
        } else {
        if(s->at>=0 && (lit(g,targets[static_cast<unsigned>(s->at)]) || ++s->committed>360))s->at=-1;
        if(s->at>=0 && g.powers.flying) {   // passed it: choose again
            const auto a=lead_of(static_cast<unsigned>(s->at));
            const float err=std::abs(std::remainder(std::atan2(a.x-eye.x,a.z-eye.z)-g.player.yaw,6.2831853f));
            if(flat(a,eye)<3.0f && err>1.2f)s->at=-1;
        }
        if(s->at<0) {
            float best=1e9f;s->committed=0;
            for(unsigned k=0;k<targets.size();++k)if(!lit(g,targets[k])){const float c=cost_of(k);if(c<best){best=c;s->at=static_cast<int>(k);}}
        }
        }
        if(t==0 && p.trace && std::getenv("SWEEP_LOS")) {   // line of sight between the targets (and from Spyro), at flight height
            const auto clear=[&](Vec3 a,Vec3 b){const auto d=b-a;const auto hit=g.level.raycast(a,d,length(d));return !hit || hit->distance>=length(d)-.05f;};
            for(unsigned a=0;a<targets.size();++a){std::printf("    los %u:",targets[a]);for(unsigned b=0;b<targets.size();++b)if(a!=b && clear(g.retail_objects[targets[a]].position+Vec3{0,.8f,0},g.retail_objects[targets[b]].position+Vec3{0,.8f,0}))std::printf(" %u(%.0f)",targets[b],length(g.retail_objects[targets[a]].position-g.retail_objects[targets[b]].position));std::printf("\n");}
            std::printf("    los spyro:");for(unsigned b=0;b<targets.size();++b)if(clear(eye,g.retail_objects[targets[b]].position+Vec3{0,.8f,0}))std::printf(" %u",targets[b]);std::printf("\n");
        }
        if(s->at<0)return t<limit?Status::Running:Status::Failed;
        const auto aim=lead_of(static_cast<unsigned>(s->at));
        if(g.powers.flying) {
            in=fly_at(g,aim);
            // Floors and walls: keep a metre over the floor, turn from a wall ahead.
            const Vec3 ahead{std::sin(g.player.yaw),0,std::cos(g.player.yaw)};
            if(const auto floor=g.level.raycast(eye,{0,-1,0},2.5f);floor && floor->distance<clearance)in.move_z=-1;
            if(const auto wall=g.level.raycast(eye,ahead,2.2f);wall && wall->normal.y<.5f && !s->evade)s->evade=24;
            if(t%20==0){if(length(g.player.position-s->last)<.3f && !s->evade)s->evade=36;s->last=g.player.position;}
            if(s->evade){--s->evade;in.move_x=1;in.move_z=-.6f;}
            // Breathe fire at anything unlit in front and in reach.
            for(unsigned k=0;k<targets.size();++k)if(!lit(g,targets[k])) {
                const auto a=lead_of(k);const auto d=a-eye;
                const float err=std::abs(std::remainder(std::atan2(d.x,d.z)-g.player.yaw,6.2831853f));
                if(length(d)<range && err<.45f && std::abs(d.y)<1.6f){in.flame_pressed=t%4==0;if(in.flame_pressed)++s->flames;if(p.trace && in.flame_pressed && std::getenv("SWEEP_DEBUG"))std::printf("    flame at %u d %.2f err %.2f dy %.2f active %d motion %s st %x\n",targets[k],length(d),err,d.y,g.interactions.flame_active()?1:0,motion_name(g.player.motion),ram(g,g.overlay_scripts.moby_address(targets[k])+0x48,1));break;}
            }
        } else {
            // Superfly takes off with X pressed again in the air after a jump.
            in=toward(g,aim);
            if(t%20==0){in.jump_pressed=true;in.jump_held=true;}
            else if(t%20==8 && !g.player.grounded){in.jump_pressed=true;in.jump_held=true;}
        }
        if(p.trace && t%120==0) {
            unsigned up=0;for(unsigned k=0;k<targets.size();++k)up+=!lit(g,targets[k]);
            const auto& o=g.retail_objects[targets[static_cast<unsigned>(s->at)]];
            std::printf("  sweep t %u up %u/%zu target %u at %.1f,%.1f,%.1f st %x spyro %.1f,%.1f,%.1f yaw %.2f flying %d flight %u flames %u\n",t,up,targets.size(),targets[static_cast<unsigned>(s->at)],
                o.position.x,o.position.y,o.position.z,ram(g,g.overlay_scripts.moby_address(targets[static_cast<unsigned>(s->at)])+0x48,1),
                g.player.position.x,g.player.position.y,g.player.position.z,g.player.yaw,g.powers.flying?1:0,g.powers.flight.remaining,s->flames);
        }
        return t<limit?Status::Running:Status::Failed;
    };
}
// Superfly steering toward a point, minding the turning circle. Superfly
// turns at most 20 angle units a VBlank (1.84 rad/s) at 5.45 units a second
// level: a circle of radius about 3. A point inside the circle on its own
// side of the nose is never reached by turning at it (pure pursuit orbits
// it), so the pilot flies straight on until the point has left that circle,
// then turns in: every approach ends on a straight line. Obstacles: the
// wanted direction is tried first, then the nearest free one (yaw and pitch
// fanned out around it, 3 units of clear air), so walls and rocks are flown
// round rather than into; within `clearance` of the floor it climbs.
inline InputState fly_to(const Game& g,Vec3 aim,unsigned& stuck,float radius=3.4f,float clearance=.9f) {
    const auto eye=g.player.position+retail::origin_above_feet;
    const float d=flat(aim,eye);
    float want_yaw=std::atan2(aim.x-eye.x,aim.z-eye.z);
    const float err=std::remainder(want_yaw-g.player.yaw,6.2831853f);
    const float a=g.player.yaw+(err>0?1:-1)*1.5707963f;
    const Vec3 centre{eye.x+std::sin(a)*radius,0,eye.z+std::cos(a)*radius};
    if(std::abs(err)>.12f && std::hypot(aim.x-centre.x,aim.z-centre.z)<radius)want_yaw=g.player.yaw;
    float want_pitch=std::clamp(std::atan2(aim.y-eye.y,std::max(d,.1f)),-.9f,.9f);
    if(d<std::abs(aim.y-eye.y)*1.2f && d<3.5f)want_pitch=0;   // too steep from here: level out and come round
    const auto dir=[](float y,float p){return Vec3{std::sin(y)*std::cos(p),std::sin(p),std::cos(y)*std::cos(p)};};
    const auto clear=[&](float y,float p,float len){return !g.level.raycast(eye,dir(y,p),len).has_value();};
    const float look=std::min(3.0f,std::max(1.2f,length(aim-eye)-.3f));
    if(!clear(want_yaw,want_pitch,look)) {
        bool found=false;
        for(const float dy:{.35f,-.35f,.7f,-.7f,1.05f,-1.05f,1.4f,-1.4f,1.9f,-1.9f,2.5f,-2.5f}) {
            for(const float dp:{0.0f,.35f,-.35f,.7f}) {
                const float y=want_yaw+(err>0?dy:-dy),pp=std::clamp(want_pitch+dp,-.9f,1.1f);
                if(clear(y,pp,3.0f)){want_yaw=y;want_pitch=pp;found=true;break;}
            }
            if(found)break;
        }
        if(!found)want_pitch=1.0f;   // boxed in: climb
    }
    if(const auto floor=g.level.raycast(eye,{0,-1,0},2.5f);floor && floor->distance<clearance)want_pitch=std::max(want_pitch,.35f);
    // Pressed against something (no headway): climb and turn for a moment.
    if(stuck){--stuck;want_pitch=.8f;want_yaw=g.player.yaw+1.2f;}
    else if(length(g.player.position-g.player.previous_position)<.02f)stuck=20;
    InputState in;
    in.move_x=std::clamp(std::remainder(want_yaw-g.player.yaw,6.2831853f)*4,-1.0f,1.0f);
    in.move_z=std::clamp((g.player.pitch-want_pitch)*4,-1.0f,1.0f);
    return in;
}
// Magma Cone's Party Crashers (overlay 48). The lava fountains (91 #25 ..
// #32, #45; state 0) blow a bubble every 4 .. 5 s (a 91 child: state 4
// rising, 5 floating two units up). A floating bubble within 512 units of
// Spyro and of his height (8001bf6c) asks to be picked up (8006a1fc =
// 10000001, itself in 8006a208) while his head is free (8006a15c = 0); he
// takes it in his mouth (head 3). Circle spits it (state 2): it picks the
// best lava monster (128 awake and solid (+8), |dx| + |dy| + |dz| under 24576
// units (24 world units), the heading to it within 12/256 of a turn of
// Spyro's 80069ffe and the pitch to 768 units over it within 16/256 of his
// 80069ffd), and flies at 1024 units over it on a ballistic arc (80038e6c,
// 600 units a tick, gravity 8); a monster it touches (state 3: 8004055c,
// radius 256, attack 10000000) dies (+18 & 10100000: state 5, 80067088 down
// by one). With none in that cone it flies straight on. A flame alone (10000)
// never hurts them. So: fly through a floating bubble, then line up on a
// monster from a few units off and spit. Before the power runs out the
// pilot flies back over `home` (the gate's floor) so the flight ends there.
// `keep_below` {x, z, half, height}: a square to stay under (Magma Cone's
// lid, 401 #12: Spyro over its box (header +20, 2.5 units a side) above its
// height less 768 with floor under him (8006a0b0 = 0) closes the volcano;
// Bippo (13B #44, kind 2) then ends the level with a checkpoint restart).
// `gate`: the power-up gate to fly back through for more power (as a player
// does: the gate refills the flight whenever Spyro crosses its plane within
// 0x380 units of its centre), so the flight never ends and no fixture is
// needed; without it the flight ends over `home`.
inline Step carry_shots(std::vector<unsigned> targets,std::function<bool(Game&,unsigned)> down,std::function<bool(Game&)> done,unsigned limit,Vec3 home,std::array<float,4> keep_below={0,0,0,0},int gate=-1) {
    struct S{int bubble=-1,target=-1;unsigned evade=0,shots=0,pickups=0,kills=0,committed=0,cooldown=0,away=0;std::vector<bool> was;std::vector<std::pair<int,unsigned>> banned;std::map<unsigned,unsigned> misses;std::pair<int,unsigned> last_shot{-1,0};unsigned through=0,refills=0,aiming=0,lining=0,bubble_away=0,tries=0;float side=1;Vec3 setup;bool setting_up=false;};auto s=std::make_shared<S>();
    static const float bubble_dy=std::getenv("BUBBLE_DY")?std::strtof(std::getenv("BUBBLE_DY"),nullptr):-.2f;
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0){*s={};s->was.assign(targets.size(),false);}
        const auto under_lid=[&](InputState& v) {   // dive out of (and never into) the square's air space
            if(keep_below[2]<=0)return;
            const auto e=g.player.position;const Vec3 ahead{std::sin(g.player.yaw)*2.5f,0,std::cos(g.player.yaw)*2.5f};
            for(const Vec3 q:{e,e+ahead})if(std::abs(q.x-keep_below[0])<keep_below[2]+1.5f && std::abs(q.z-keep_below[1])<keep_below[2]+1.5f && e.y>keep_below[3]-2.0f){v.move_z=1;break;}
        };
        if(done(g))return Status::Done;
        if(g.dialogue.active())return Status::Done;
        for(unsigned k=0;k<targets.size();++k){const bool now=down(g,targets[k]);if(now && !s->was[k] && ++s->kills<=4)p.photo="hit-"+std::to_string(s->kills);s->was[k]=now;}
        if(t>60 && !g.powers.flying && !g.powers.flight.remaining)return Status::Done;   // spent: back through the gate
        const auto eye=g.player.position+retail::origin_above_feet;
        const unsigned head=g.overlay_scripts.head_state();
        if(!g.powers.flying) {   // take off: jump, X again in the air
            in=toward(g,home);
            if(t%20==0){in.jump_pressed=true;in.jump_held=true;}
            else if(t%20==8 && !g.player.grounded){in.jump_pressed=true;in.jump_held=true;}
            return t<limit?Status::Running:Status::Failed;
        }
        if(gate>=0 && static_cast<std::size_t>(gate)<g.retail_objects.size() && g.retail_objects[gate].powerup_gate && (g.powers.flight.remaining<900 || s->through) && head!=2) {
            // Back through the gate: line up 4.5 units out on this side, then straight through its centre.
            const auto& o=g.retail_objects[gate];
            const auto centre=o.position-Vec3{0,o.powerup_gate->origin_offset,0};
            const float yaw=retail::native_heading(o.rotation[2]*16);const Vec3 n{std::sin(yaw),0,std::cos(yaw)};
            const float side_now=dot(eye-centre,n)>0?1.0f:-1.0f;
            if(!s->through){s->side=side_now;s->through=1;}
            const Vec3 start=centre+n*(s->side*7.0f),finish=centre-n*(s->side*4.0f);
            const float along=dot(eye-centre,n)*s->side;
            if(s->through==1) {
                // Out to 7 units on this side, then turn in along the centre
                // line: in once 3 .. 9 units out, within 2.5 of the line and
                // heading at the gate. The other side after 400 ticks.
                in=fly_to(g,start,s->evade);
                const Vec3 rel=eye-centre;const float lateral=length(rel-n*dot(rel,n));
                const float facing=-s->side*(std::sin(g.player.yaw)*n.x+std::cos(g.player.yaw)*n.z);
                if((flat(start,eye)<1.6f) || (along>3 && along<9 && lateral<2.5f && facing>.5f))s->through=2;
                else if(++s->lining>400){s->lining=0;s->side=-s->side;}
            } else {
                // Pure pursuit of the centre line, 2.5 units ahead: the
                // crossing must fall within 0x380 units of the centre.
                in=fly_to(g,centre+n*(s->side*std::max(-4.0f,along-2.5f)),s->evade,3.4f,.3f);
                if(along<-2.5f || flat(finish,eye)<1.0f || ++s->through>260)s->through=0;
            }
            if(g.powers.flight.remaining>3000 && s->through!=1){s->through=0;++s->refills;}
            under_lid(in);
            if(p.trace && std::getenv("CARRY_DEBUG") && t%5==0)std::printf("    gate t %u through %u side %.0f eye %.2f,%.2f,%.2f centre %.2f,%.2f,%.2f along %.2f flight %u\n",t,s->through,s->side,eye.x,eye.y,eye.z,centre.x,centre.y,centre.z,
                dot(eye-centre,n)*s->side,g.powers.flight.remaining);
            return t<limit?Status::Running:Status::Failed;
        }
        if(g.powers.flight.remaining<260 && head!=3) {   // land by the gate before the power runs out
            if(flat(home,eye)>2.5f)in=fly_to(g,home+Vec3{0,1.5f,0},s->evade);
            else {in=fly_at(g,home+Vec3{0,.5f,0});in.move_x=0;}
            return t<limit?Status::Running:Status::Failed;
        }
        // Spawned mobys sit in the script's free slots, not at moby_address: their mirrored state is +48.
        const auto ram_state=[&](unsigned i){return i<g.level.objects.size()?ram(g,g.overlay_scripts.moby_address(i)+0x48,1):static_cast<std::uint32_t>(std::max<int>(g.retail_objects[i].state,0));};
        if(head==0 || head==2) {
            // The nearest floating bubble, counting the turn toward it.
            if(s->bubble>=0 && s->committed>=420 && head==0)s->banned.push_back({s->bubble,t+900});   // not reached: leave it a while
            if(s->bubble>=0 && (static_cast<std::size_t>(s->bubble)>=g.retail_objects.size() || g.retail_objects[s->bubble].state<0 || g.retail_objects[s->bubble].type!=0x91 || ram_state(s->bubble)!=5 || ++s->committed>420))s->bubble=-1;
            std::erase_if(s->banned,[&](const std::pair<int,unsigned>& b){return b.second<=t;});
            if(s->bubble<0) {
                float best=1e9f;s->committed=0;
                for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i) {
                    const auto& o=g.retail_objects[i];if(o.type!=0x91 || o.state<0)continue;
                    if(std::any_of(s->banned.begin(),s->banned.end(),[&](const std::pair<int,unsigned>& b){return b.first==static_cast<int>(i);}))continue;
                    const auto st=ram_state(static_cast<unsigned>(i));if(st!=5 && st!=4)continue;
                    const float err=std::abs(std::remainder(std::atan2(o.position.x-eye.x,o.position.z-eye.z)-g.player.yaw,6.2831853f));
                    const float c=length(o.position-eye)+2.5f*err+(st==4?6:0);
                    if(c<best){best=c;s->bubble=static_cast<int>(i);}
                }
            }
            if(s->bubble>=0) {
                const auto at=g.retail_objects[s->bubble].position;
                if(p.trace && std::getenv("CARRY_DEBUG") && length(at-eye)<1.6f)std::printf("    near bubble %d t %u flat %.2f dy %.2f head %u\n",s->bubble,t,flat(at,eye),eye.y-at.y,head);
                // Bubbles float two units over their fountain, often a unit off the
                // rock: near one, let the floor come within 0.4 of the eye.
                // Close but off the line (it must pass within half a unit of
                // the head): fly on to 5.5 units and come again straight.
                const float bd=flat(at,eye),berr=std::abs(std::remainder(std::atan2(at.x-eye.x,at.z-eye.z)-g.player.yaw,6.2831853f));
                if(!s->bubble_away && bd<3.5f && bd>.6f && berr>.45f)s->bubble_away=1;
                if(s->bubble_away && (bd>5.5f || ++s->bubble_away>120))s->bubble_away=0;
                if(s->bubble_away)in=fly_to(g,eye+Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)}*4+Vec3{0,at.y+bubble_dy-eye.y,0},s->evade);
                else in=fly_to(g,at+Vec3{0,bubble_dy,0},s->evade,3.4f,length(at-eye)<4.0f?.4f:.9f);
            } else in=fly_to(g,home+Vec3{0,4,0},s->evade);
        } else {
            // Carrying: line up on the monster cheapest to face and spit.
            const auto alive=[&](unsigned i){return !down(g,i) && ram_state(i)>=1 && ram_state(i)<=4;};
            if(s->target>=0 && !alive(static_cast<unsigned>(s->target)))s->target=-1;
            // Not lined up on it within 15 s: another one first (a ledge or rock in the way from here).
            if(s->target>=0 && ++s->aiming>450){s->banned.push_back({s->target,t+900});s->target=-1;}
            std::erase_if(s->banned,[&](const std::pair<int,unsigned>& b){return b.second<=t;});
            const auto banned=[&](unsigned i){return std::any_of(s->banned.begin(),s->banned.end(),[&](const std::pair<int,unsigned>& b){return b.first==static_cast<int>(i);});};
            if(s->target<0) {
                float best=1e9f;s->aiming=0;
                // Every one left set aside: try them again rather than idle.
                if(std::none_of(targets.begin(),targets.end(),[&](unsigned i){return alive(i) && !banned(i);}))s->banned.clear();
                for(const auto i:targets)if(alive(i) && !banned(i)) {
                    const auto a=g.retail_objects[i].position;
                    const float err=std::abs(std::remainder(std::atan2(a.x-eye.x,a.z-eye.z)-g.player.yaw,6.2831853f));
                    const float c=length(a-eye)+4.0f*err+18.0f*static_cast<float>(s->misses[i]);   // one missed often: another first
                    if(c<best){best=c;s->target=static_cast<int>(i);}
                }
                // The run-in starts from open air: the clearest of eight
                // directions out of it (9 units, line of sight back to it),
                // a different one on each new try.
                if(s->target>=0) {
                    const auto a=g.retail_objects[s->target].position+Vec3{0,1.6f,0};
                    float widest=-1;++s->tries;
                    for(unsigned k=0;k<8;++k) {
                        const float ang=(static_cast<float>(k)+.5f*static_cast<float>(s->tries%2))*.785398f;const Vec3 dir{std::sin(ang),0,std::cos(ang)};
                        const auto hit=g.level.raycast(a,dir,9.0f);const float free=hit?hit->distance:9.0f;
                        const float score=free+(k==(s->tries*3)%8?.5f:0);
                        if(score>widest){widest=score;s->setup=a+dir*std::max(3.0f,free-1.5f);}
                    }
                    s->setting_up=true;
                }
            }
            if(s->target>=0 && s->setting_up && flat(s->setup,eye)<2.0f)s->setting_up=false;
            if(s->target>=0) {
                const auto a=g.retail_objects[s->target].position;
                const Vec3 aim=a+Vec3{0,.75f,0};
                const Vec3 approach=a+Vec3{0,1.6f,0};   // come in a little above: over the rock it stands on
                const float d=flat(a,eye);
                const float yaw_err=std::abs(std::remainder(std::atan2(a.x-eye.x,a.z-eye.z)-g.player.yaw,6.2831853f));
                // Extend and come again: close in but not lined up, fly on
                // straight (or away) to 9 units before turning back in, so the
                // last stretch is a straight run at it.
                if(!s->away && !s->setting_up && ((d<7.0f && yaw_err>.6f) || d<2.5f))s->away=1;
                if(s->away && (d>9.0f || ++s->away>150))s->away=0;
                if(s->setting_up)in=fly_to(g,s->setup,s->evade);
                else if(s->away) {
                    const Vec3 out=yaw_err<1.57f?Vec3{eye.x-a.x,0,eye.z-a.z}:Vec3{std::sin(g.player.yaw),0,std::cos(g.player.yaw)};
                    in=fly_to(g,eye+out*(6/std::max(.3f,length(out)))+Vec3{0,approach.y-eye.y,0},s->evade);
                }
                else in=fly_to(g,approach,s->evade);
                const float pitch_to=std::atan2(aim.y-eye.y,std::max(.1f,d));
                const float manhattan=std::abs(a.x-eye.x)+std::abs(a.y-eye.y)+std::abs(a.z-eye.z);
                if(s->cooldown)--s->cooldown;
                // A clear line to it: a bubble that meets the rock first is spent.
                const bool open=!g.level.raycast(eye,aim+Vec3{0,.25f,0}-eye,length(aim+Vec3{0,.25f,0}-eye)).has_value();
                if(p.trace && std::getenv("CARRY_DEBUG") && head==3 && d<12 && t%3==0)std::printf("    aim t %u at %d d %.1f yaw %.2f pitch %.2f/%.2f open %d man %.1f\n",t,s->target,d,yaw_err,pitch_to,g.player.pitch,open?1:0,manhattan);
                if(head==3 && !s->cooldown && open && d>=2.0f && d<12.0f && manhattan<22 && yaw_err<.22f && std::abs(pitch_to-g.player.pitch)<.32f) {
                    in.flame_pressed=true;++s->shots;s->cooldown=10;s->last_shot={s->target,t};
                    if(p.trace)std::printf("    spit t %u at %d d %.1f yaw %.2f pitch %.2f/%.2f\n",t,s->target,d,yaw_err,pitch_to,g.player.pitch);
                }
            } else in=fly_to(g,home+Vec3{0,4,0},s->evade);
        }
        under_lid(in);
        if(p.trace && std::getenv("CARRY_DEBUG") && t%3==0)for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i) {
            const auto& o=g.retail_objects[i];
            if(o.type==0x91 && (o.state==2 || o.state==3 || o.state==0x63))std::printf("    shot %zu t %u state %d at %.2f,%.2f,%.2f\n",i,t,o.state,o.position.x,o.position.y,o.position.z);
        }
        if(p.trace && std::getenv("CARRY_DEBUG") && t%10==0)std::printf("    in t %u x %.2f z %.2f evade %u head %u pos %.1f,%.1f,%.1f yaw %.2f pitch %.2f\n",t,in.move_x,in.move_z,s->evade,head,
            g.player.position.x,g.player.position.y,g.player.position.z,g.player.yaw,g.player.pitch);
        if(head==3 && s->bubble>=0){++s->pickups;s->bubble=-1;}
        // A shot that has not felled its monster within 3 s counts as a miss.
        if(s->last_shot.first>=0 && t>s->last_shot.second+90) {
            if(!down(g,static_cast<unsigned>(s->last_shot.first)))++s->misses[static_cast<unsigned>(s->last_shot.first)];
            s->last_shot={-1,0};s->target=-1;
        }
        if(p.trace && t%120==0) {
            unsigned up=0;for(const auto i:targets)up+=!down(g,i);
            std::printf("  carry t %u up %u/%zu head %u bubble %d target %d spyro %.1f,%.1f,%.1f yaw %.2f pitch %.2f flight %u pickups %u shots %u kills %u refills %u\n",t,up,targets.size(),head,s->bubble,s->target,
                g.player.position.x,g.player.position.y,g.player.position.z,g.player.yaw,g.player.pitch,g.powers.flight.remaining,s->pickups,s->shots,s->kills,s->refills);
            if(std::getenv("CARRY_DEBUG"))for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i)if(g.retail_objects[i].type==0x91)
                std::printf("    bubble %zu at %.1f,%.1f,%.1f state %d ram %x +38 %d\n",i,g.retail_objects[i].position.x,g.retail_objects[i].position.y,g.retail_objects[i].position.z,g.retail_objects[i].state,ram_state(static_cast<unsigned>(i)),static_cast<int>(static_cast<std::int16_t>(ram(g,g.overlay_scripts.moby_address(static_cast<unsigned>(i))+0x38,2))));
        }
        return t<limit?Status::Running:Status::Failed;
    };
}
// A round of Sunny Beach's turtle soup (see the catalogue): the turtle most
// in danger (nearest the pot) is approached from the side away from the water
// box and charged into it; never into the chef's water (+64) or pot (+98)
// boxes. Done when the round ends (phase steps on), the chef talks, or he
// rings the bell (state 10: a restart follows).
inline Step turtle_round(unsigned chef) {
    struct S{int target=-1;unsigned charge=0,phase=0,saved=0,swimming=0;bool started=false;std::vector<int> seen;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        const auto h=ram(g,g.overlay_scripts.moby_address(chef));
        if(t==0){*s={};s->phase=ram(g,h+0x94);}
        if(g.dialogue.active())return Status::Done;
        if(ram(g,h+0x94)!=s->phase && s->started)return Status::Done;
        if(ram(g,h+0x43,1))s->started=true;
        {   // a turtle saved: swimming away (state 3) in the water box
            unsigned now=0;for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i)if(g.retail_objects[i].type==0xb3 && g.retail_objects[i].state==3)++now;
            if(now>s->swimming && s->saved<3)p.photo="saved-"+std::to_string(++s->saved);
            s->swimming=now;
        }
        if(ram(g,g.overlay_scripts.moby_address(chef)+0x48,1)==10){p.photo="bell";if(p.trace)std::printf("  soup: the bell (saved %u)\n",s->saved);return Status::Done;}
        const auto water=box_centre(g,h+0x64),pot=g.retail_objects[ram(g,h+0x30)].position;
        const auto forbidden=[&](Vec3 q){return in_box(g,h+0x64,q) || in_box(g,h+0x98,q);};
        // Live turtles still walking (B3 the chef spawned: header +0E == 0).
        int best=-1;float best_d=1e9f;unsigned walking=0;
        for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i) {
            const auto& o=g.retail_objects[i];if(o.type!=0xb3 || o.state<0 || !o.retail_script)continue;
            const auto st=ram(g,g.overlay_scripts.moby_address(static_cast<unsigned>(i))+0x48,1);
            if(!(st<=2 || st==0xb || st==0xe || st==0xf))continue;
            ++walking;const float d=flat(o.position,pot);if(d<best_d){best_d=d;best=static_cast<int>(i);}
        }
        if(t%60==0 && p.trace)std::printf("  soup t %u phase %u walking %u target %d spyro %.1f,%.1f forbidden %d\n",t,ram(g,h+0x94),walking,best,g.player.position.x,g.player.position.z,forbidden(g.player.position)?1:0);
        if(s->charge) {   // charging the target: until it slides, or 40 ticks, or the water is near
            --s->charge;
            if(s->target<0 || s->target>=static_cast<int>(g.retail_objects.size())){s->charge=0;return Status::Running;}
            const auto& o=g.retail_objects[static_cast<unsigned>(s->target)];
            const auto st=ram(g,g.overlay_scripts.moby_address(static_cast<unsigned>(s->target))+0x48,1);
            const auto ahead=g.player.position+normalized(Vec3{o.position.x-g.player.position.x,0,o.position.z-g.player.position.z})*1.2f;
            if(st==4 || st==5 || st==3 || o.state<0 || forbidden(ahead)){s->charge=0;return Status::Running;}
            in=toward(g,o.position);in.charge=true;
            if(p.trace && std::getenv("SOUP_DEBUG"))std::printf("    charging %d: d %.2f dy %.2f motion %s st %u +18 %x\n",s->target,flat(o.position,g.player.position),o.position.y-g.player.position.y,motion_name(g.player.motion),st,ram(g,g.overlay_scripts.moby_address(static_cast<unsigned>(s->target))+0x18));
            return Status::Running;
        }
        if(best<0)return t<20000?Status::Running:Status::Failed;
        s->target=best;const auto& o=g.retail_objects[static_cast<unsigned>(best)];
        const auto step=o.position-o.previous_position;
        const auto lead=o.position+step*std::min(30.0f,flat(o.position,g.player.position)*6);
        // The push: a charged turtle slides roughly the way Spyro faces. Of the
        // headings round the water band's normal, the one nearest it whose
        // slide (to 2 units into the band) passes the pot by 1.6 or more.
        const float band=static_cast<float>(ram(g,h+0x64+0x14)&0xff)*6.2831853f/256;
        const Vec3 normal{std::sin(band),0,std::cos(band)};   // the band's short axis (b), toward its centre from the beach
        Vec3 d=normalized(Vec3{water.x-lead.x,0,water.z-lead.z});
        const Vec3 towards=dot(normal,d)>0?normal:normal*-1.0f;
        bool found=false;Vec3 stand=lead-towards*1.2f;
        for(const float turn:{0.0f,.25f,-.25f,.5f,-.5f,.75f,-.75f,1.0f,-1.0f}) {
            const Vec3 r{towards.x*std::cos(turn)-towards.z*std::sin(turn),0,towards.x*std::sin(turn)+towards.z*std::cos(turn)};
            float reach=0;for(float k=.5f;k<16;k+=.5f)if(in_box(g,h+0x64,lead+r*k)){reach=k+2;break;}
            if(!reach)continue;
            const auto rel=pot-lead;const float along=std::clamp(dot(Vec3{rel.x,0,rel.z},r),0.0f,reach);
            if(flat(lead+r*along,pot)<1.6f)continue;
            const auto q=lead-r*1.2f;if(forbidden(q))continue;
            stand=q;d=r;found=true;break;
        }
        if(!found)stand=lead-d*1.2f;
        const bool placed=flat(stand,g.player.position)<.5f;
        if(placed && std::abs(heading_error(g,lead))<.35f){
            s->charge=30;in=toward(g,lead);in.charge=true;
            if(p.trace)std::printf("  soup: charge turtle %d (state %u) from %.1f,%.1f push %.2f,%.2f\n",best,ram(g,g.overlay_scripts.moby_address(static_cast<unsigned>(best))+0x48,1),g.player.position.x,g.player.position.z,d.x,d.z);
            return Status::Running;
        }
        in=toward(g,placed?lead:stand,placed?.3f:1.0f);
        if(forbidden(g.player.position+Vec3{in.move_x,0,in.move_z}*.6f)){in.move_x=-in.move_x*.3f;in.move_z=-in.move_z*.3f;}
        return t<20000?Status::Running:Status::Failed;
    };
}
// Play `body` again (as a player retries: the giver offers another go and the
// talk is answered yes) until `won`, at most `tries` times.
inline Step retry(std::vector<Step> body,std::function<bool(Game&)> won,unsigned tries) {
    struct S{unsigned at=0,ticks=0,round=0;};auto s=std::make_shared<S>();
    return [=](Pilot& p,Game& g,InputState& in,unsigned t) {
        if(t==0)*s={};
        if(won(g))return Status::Done;
        if(s->at>=body.size()){if(++s->round>=tries)return Status::Failed;s->at=0;s->ticks=0;if(p.trace)std::printf("  retry %u\n",s->round);}
        const auto status=body[s->at](p,g,in,s->ticks++);
        if(status==Status::Done){++s->at;s->ticks=0;}
        else if(status==Status::Failed){++s->at;s->ticks=0;}   // a lost round: the next step talks to the giver again
        return Status::Running;
    };
}
inline std::vector<Pilot> catalogue() {
    std::vector<Pilot> all;
    const auto add=[&](const char* name,unsigned slot,unsigned giver,unsigned bit,const char* what,std::vector<std::pair<std::string,Step>> steps) {
        Pilot p;p.name=name;p.slot=slot;p.giver=giver;p.orb_bit=bit;p.what=what;p.steps=std::move(steps);all.push_back(std::move(p));
    };
    const Vec3 red_goal{121.37f,16.8f,101.01f};
    const auto orb=[](unsigned bit){return [bit](Game& g){return ((g.guide_awards.orbs>>bit)&1)!=0;};};
    const auto keeper_game=[=](unsigned tries){return retry({hockey(red_goal,22,3.4f,-1,9000),return_and_talk(19),read_dialogue(),wait(300)},orb(0),tries);};
    add("colossus-hockey-goalie",21,19,0,"Brother Kipp: 5 goals past the goal keeper in 2 minutes (another go if the time runs out)",{
        {"talk",approach_and_talk(19)},{"accept",read_dialogue()},{"warm-up",wait(400)},
        {"hockey",keeper_game(4)},{"reward",reward()}});
    add("colossus-hockey-1v1",21,19,1,"Brother Kipp: first to 5 goals against the red skater (after the goal keeper game, same run)",{
        {"talk",approach_and_talk(19)},{"accept",read_dialogue()},{"warm-up",wait(400)},
        {"hockey keeper",keeper_game(4)},{"one on one",until_word(19,0x80066f30,2)},{"warm-up",wait(300)},
        {"hockey 1v1",retry({hockey(red_goal,22,3.4f,21,16000),return_and_talk(19),read_dialogue(),wait(300)},orb(1),4)},{"reward",reward()}});
    // Colossus' statues: the Professor (193 #24) asks for the ten green statues
    // (1E4) to be flamed. A statue on a flame (+18 & 10000) while the moby at
    // its header +4 has +49 set (the challenge running) lights (+49 = 1,
    // persisted, state 3) and counts itself in that moby's header +3C.
    add("colossus-statues",21,24,2,"Professor: flame the ten green statues the evil spirit hides in",{
        {"talk",approach_and_talk(24)},{"accept",read_dialogue()},
        {"statues",attack_each({25,26,27,28,29,72,73,74,75,76},[](Game& g,unsigned i){return ram(g,g.overlay_scripts.moby_address(i)+0x49,1)==1;})},
        {"return",return_and_talk(24)},{"reward",reward()}});
    // Glimmer's gem lamps (overlay 16). Kanga (22A kind 8) / Roo (kind 9): once
    // their big lamp (40C, header +34) is lit (state 3) they set task bit 2 / 4
    // (80067148), place Spyro before them (8001f524) and talk by themselves
    // (+5 = 2); the orb line follows. A small lamp (21C) lights on a flame
    // (+18 & 10000) and sets its bit (header +0) in the big lamp's +49; the big
    // lamp lights once +49 = 3F (all six) before they go out again. The
    // superfly comes from the power-up gate before the giver (1CC #15 / #23),
    // which opens once 10 enemies' spirits have reached it.
    const auto lamp_lit=[](Game& g,unsigned i){return ram(g,g.overlay_scripts.moby_address(i)+0x48,1)>=1;};
    const auto big_lamp=[](unsigned big){return [big](Game& g){return ram(g,g.overlay_scripts.moby_address(big)+0x48,1)>=1 || g.dialogue.active();};};
    add("glimmer-lamps-outdoor",17,179,1,"Kanga: light the six gem lamps by the pyramid with the superfly power-up",{
        {"enemies for the power-up",defeat_for_gate(15)},{"talk",approach_and_talk(179)},{"accept",read_dialogue()},
        {"power-up gate",cross_gate(15)},{"lamps",flame_targets({16,17,18,19,20,21},lamp_lit,big_lamp(25),6000,3.2f)},{"reward",reward(1500)}});
    add("glimmer-lamps-cave",17,180,2,"Roo: light the six gem lamps in the cave with the superfly power-up",{
        {"enemies for the power-up",defeat_for_gate(23)},{"talk",approach_and_talk(180)},{"accept",read_dialogue()},
        {"power-up gate",cross_gate(23)},{"lamps",flame_targets({29,30,31,32,33,34},lamp_lit,big_lamp(28),6000,std::getenv("LAMP_AIM")?std::strtof(std::getenv("LAMP_AIM"),nullptr):-2.0f)},{"reward",reward(1500)}});
    // Metropolis' sheep saucers (overlay 66): the Inventor Droid (251 #29, script
    // owned) offers the combination power-up (superfly + superflame, gate 1CC
    // #26) once enough enemies are down.
    // Wave one is three saucers (252 #36, #44, #45, state 0 while flying;
    // 80067084 counts them down); wave two the five resting in state 16 (#19,
    // #25, #31, #34, #35). Superflame (Circle in flight) shoots them down.
    const auto saucer_down=[](Game& g,unsigned i){const auto st=ram(g,g.overlay_scripts.moby_address(i)+0x48,1);return st!=0 && st!=16;};
    const auto talk_or=[](std::uint32_t word,std::uint32_t value){return [=](Game& g){return g.dialogue.active() || ram(g,word)==value;};};
    add("metropolis-coup",67,29,0,"Inventor Droid: defeat the invaders until her power-up is ready, then talk to her",{
        {"enemies for the power-up",defeat_for_gate(26)},{"talk",approach_and_talk(29)},{"orb line",read_dialogue()},{"reward",reward(600)}});
    add("metropolis-saucers-1",67,29,1,"Inventor Droid: shoot down the first wave of sheep saucers with superfly + superflame",{
        {"enemies for the power-up",defeat_for_gate(26)},{"talk",approach_and_talk(29)},{"orb 0 and the offer",read_dialogue()},
        {"talks",reward(600)},{"power-up gate",cross_gate(26)},
        {"saucers",flame_targets({36,44,45},saucer_down,talk_or(0x80067084,0),6000,.4f,18.0f,-1.0f)},{"reward",reward(1500)}});
    {
        std::vector<std::pair<std::string,Step>> steps{
            {"enemies for the power-up",defeat_for_gate(26)},{"talk",approach_and_talk(29)},{"orb 0 and the offer",read_dialogue()},
            {"talks",reward(600)},{"power-up gate",cross_gate(26)},
            {"saucers",flame_targets({36,44,45},saucer_down,talk_or(0x80067084,0),6000,.4f,18.0f,-1.0f)},{"reward",reward(1500)},
            {"second wave offer",return_and_talk(29)},{"read",read_dialogue()},{"talks",reward(600)}};
        // The power lasts 3600 frames: back through the gate as often as needed.
        for(unsigned round=0;round<3;++round) {
            steps.push_back({"power-up gate "+std::to_string(round+2),cross_gate(26,talk_or(0x80067084,0))});
            steps.push_back({"second wave "+std::to_string(round+1),flame_targets({19,25,31,34,35},saucer_down,talk_or(0x80067084,0),4200,.4f,18.0f,-1.0f,true)});
        }
        steps.push_back({"reward",reward(1500)});
        add("metropolis-saucers-2",67,29,2,"Inventor Droid: the second wave of five sheep saucers (after the first wave, same run)",std::move(steps));
    }
    // Sunny Beach's turtle boxes (overlay 26). Advisor Gromit (B6 #41, kind 3)
    // counts the live mobys of group 1 (80039218(1): the seven boxes BA #9 #12
    // #13 #14 #34 #37 #38, into 80066fec for the HUD); at none left he sets task
    // bit 0 (80067148), places Spyro before him (8001f524) and talks by himself
    // (+5 = 2); the orb line follows. A box (BA) breaks only on attack bit
    // 100000 (superflame, 970000), spawns its baby turtles (class 123) and is
    // removed. The superflame comes from the gate 1CE #15 (11 enemies).
    const auto box_open=[](Game& g,unsigned i){return g.retail_objects[i].state<0 || ram(g,g.overlay_scripts.moby_address(i)+0x48,1)!=0;};
    add("sunny-beach-turtle-boxes",27,41,0,"Advisor Gromit: break the seven turtle boxes with the superflame power-up",{
        {"enemies for the power-up",defeat_for_gate(15)},{"talk",approach_and_talk(41)},{"accept",read_dialogue()},
        {"power-up gate",cross_gate(15)},{"boxes",attack_each({9,12,13,14,34,37,38},box_open)},{"reward",reward(1500)}});
    // Sunny Beach's turtle soup (Master Chef BB #31, header phase +94). Talking
    // moves phase 0 -> 1 (and 4 -> 5); Spyro in the start box (+4C) starts a
    // round (+43 = 1, 80066f30 = 1, phase 2 / 6). The chef spawns turtles (B3)
    // that walk to the pot (125 #39, header +30); one within 1500 units jumps
    // in (states F, 70) and sets the chef's +49: the bell, a checkpoint
    // restart (state 10, 8004c1ac) with the phase stepped back. So does Spyro
    // in the pot strip (+98) or the water (+64). A charged turtle (+18 &
    // 20000) slides (state 4); one that reaches the water box (+64) in the air
    // swims away (states 3, D) and is saved. With none walking left, the round
    // ends (phase 3 / 7: task bit 2 / 4, Spyro placed, the chef talks by
    // himself with orb 1 / 2). Round one is the first 3 turtles of the
    // schedule (+40), round two the rest (+41 = 13).
    const auto chef_phase=[](unsigned chef,unsigned phase){return [=](Game& g){return ram(g,ram(g,g.overlay_scripts.moby_address(chef))+0x94)>=phase;};};
    add("sunny-beach-soup-1",27,31,1,"Master Chef: keep the first turtles out of the cauldron (charge them into the sea)",{
        {"talk",approach_and_talk(31)},{"accept",read_dialogue()},
        {"round",retry({walk_to({86.18f,0,38.49f},.6f,900),turtle_round(31),reward(400),return_and_talk(31),read_dialogue()},chef_phase(31,3),5)},
        {"reward",reward(1200)}});
    add("sunny-beach-soup-2",27,31,2,"Master Chef: keep every turtle out of the cauldron, second round (after the first, same run)",{
        {"talk",approach_and_talk(31)},{"accept",read_dialogue()},
        {"round 1",retry({walk_to({86.18f,0,38.49f},.6f,900),turtle_round(31),reward(400),return_and_talk(31),read_dialogue()},chef_phase(31,3),5)},
        {"reward 1",reward(1200)},{"talk again",return_and_talk(31)},{"read",read_dialogue()},
        {"round 2",retry({walk_to({86.18f,0,38.49f},.6f,900),turtle_round(31),reward(400),return_and_talk(31),read_dialogue()},chef_phase(31,7),5)},
        {"reward 2",reward(1200)}});
    // Skelos Badlands' lava lizards (overlay 34; Lumpy 62 #52, kind 1). Lumpy
    // sets task bit 0 once group 4 is all gone (80039218(4): lizards 49 #165
    // .. #172, hatched from the eggs 39 #157 .. #164) and bit 1 for group 2
    // (#29 .. #44, eggs #30 .. #43), places Spyro and talks by himself (+5 =
    // 2): line 2 (orb 1), line 6 (orb 2).
    const auto gone=[](Game& g,unsigned i){return g.retail_objects[i].state<0 || ram(g,g.overlay_scripts.moby_address(i)+0x48,1)>=0x80;};
    add("skelos-lizards-1",35,52,0,"Lumpy: save the villagers from the first lava lizards",{
        {"talk",approach_and_talk(52)},{"accept",read_dialogue()},
        {"lizards",attack_each({165,166,167,168,169,170,171,172},gone)},{"reward",reward(1500)}});
    // Skelos Badlands' Dem bones (Ooga 62 #56, kind 2; overlay 34). Ooga waits
    // for 80066fec == 8 (the bones, `8007db68`), then sets task bit 2 and +5 = 1
    // (talks when Spyro comes back). The eight bone holders (class 59, case
    // 8007cec8: #8 #12 #14 #20 #21 #45 #46 #49) throw a bone (class 65) on any
    // hit (+18 != 0, state not 4) at their header +0C point (80038e6c) and go
    // (state 4); a bone (case 8007e7dc) is taken within 600 units of Spyro in
    // 2D and in height (80066fec + 1). The class 26 "lava toads" are ordinary
    // enemies: killing all four of group 9 drops a D3, not a bone.
    {
        const auto holder_gone=[](Game& g,unsigned i){const auto st=ram(g,g.overlay_scripts.moby_address(i)+0x48,1);return g.retail_objects[i].state<0 || st==4 || st>=0x80;};
        add("skelos-dem-bones",35,56,2,"Ooga: get the bones back from the lava toads",{
            {"talk",approach_and_talk(56)},{"accept",read_dialogue()},
            {"bones",hit_and_collect({21,49,8,46,14,12,45,20},holder_gone,0x65,0x80066fec,8)},
            {"return",return_and_talk(56)},{"reward",reward(1500)}});
    }
    // Robotica Farms' crow bugs (Farmer Applebee 10D #48, kind 2): see
    // ambush_perchers. Applebee places Spyro and talks by himself.
    add("robotica-crow-bugs",65,48,2,"Farmer Applebee: catch the four crow bugs on the scarecrows",{
        {"talk",approach_and_talk(48)},{"accept",read_dialogue()},
        {"bugs",ambush_perchers({4,5,6,7},[](Game& g){return g.dialogue.active() || (ram(g,0x80067148,1)&4)!=0;})},{"reward",reward(1500)}});
    // Cloud Temples' bells (overlay 60; Murgen 1C7 #32, kind 3, the only 1C7
    // handed to the interpreter). A charge (+18 & 20000) rings a bell (14F,
    // case 80073e98): its first ring sets state 1, 80066fec + 1 and adds 1 to
    // Murgen's +49 (the moby at the bell's header +00); at +49 == 5 Murgen
    // sets task bit 1, places Spyro and talks by himself (orb 1). Bells #23 and
    // #27 ring from a jump; #25 from the ice block of the troll under it
    // (freeze_and_climb), with the superfreeze of gate #68 (the trolls appear
    // once it is open).
    {
        const auto rung=[](Game& g,unsigned i){return ram(g,g.overlay_scripts.moby_address(i)+0x48,1)!=0;};
        add("cloud-temples-bells",61,32,1,"Murgen: ring the three bells on the temple towers (charge them; the third from a frozen troll)",{
            {"enemies for the power-up",defeat_for_gate(68)},{"talk",approach_and_talk(32)},{"accept",read_dialogue()},{"power-up gate",cross_gate(68)},
            {"bells",charge_jump_each({23,27},rung)},{"one bell left",reward(420)},
            {"whirlwind",ride_whirlwind(10,{99.1f,27.5f,32.0f})},
            {"into the bell room",freeze_climb({180,29,63,79},{99.0f,30.0f,37.5f},UINT32_MAX,rung)},{"third bell",freeze_climb({66,67},{99.05f,30.0f,38.09f},25,rung)},{"reward",reward(1500)}});
    }
    // Shady Oasis' thieves (Bruno 188 #12, kind 1): three lamp thieves (108,
    // group 5: #13 #34 #35).
    add("shady-oasis-thieves",47,12,0,"Bruno: get the three magic lamps back from the thieves",{
        {"talk",approach_and_talk(12)},{"accept",read_dialogue()},
        {"thieves",chase_each({13,34,35},gone)},{"return",return_and_talk(12)},{"reward",reward(1500)}});
    // Mystic Marsh's thieves (Basil 1FE #40, kind 3): the four spark plug
    // thieves (1A #29 #39 #43 #49) count themselves into 80066fec (overlay 58
    // 8007b16c); at 4 Basil sets his task bit and talks by himself.
    add("mystic-marsh-thieves",59,40,1,"Basil: catch the four thieves for the spark plugs",{
        {"talk",approach_and_talk(40)},{"accept",read_dialogue()},
        {"thieves",chase_route({39,43,29,49},gone)},{"reward",reward(1500)}});
    add("skelos-lizards-2",35,52,1,"Lumpy: the second lava lizards (after the first, same run)",{
        {"talk",approach_and_talk(52)},{"accept",read_dialogue()},
        {"lizards",attack_each({165,166,167,168,169,170,171,172},gone)},{"reward",reward(1500)},
        {"talk again",return_and_talk(52)},{"accept again",read_dialogue()},{"eggs drop",wait(240)},
        {"lizards again",attack_each({29,31,33,35,38,39,41,44},gone)},{"reward again",reward(1500)}});
    // Crystal Glacier's draclets (overlay 36). Shaman Tik (C6 #64, kind 7)
    // counts group 7 (the 18 draclets 50 #47 .. #63, #67) still standing
    // (8007e114: state below 80 and not 3, 32 or 33); with none standing and
    // none just flamed (state 3) he removes them, sets task bit 0 (80067148)
    // and talks by himself. A flame (+18 & 10000) knocks a draclet down
    // (state 3, then 32); a standing one near a downed one revives it (33
    // for a timer, then 34), so they must all go down quickly. The superfly
    // comes from the gate 1D0 #23 once 15 enemies are down.
    {
        const auto draclet_down=[](Game& g,unsigned i){const auto st=ram(g,g.overlay_scripts.moby_address(i)+0x48,1);return g.retail_objects[i].state<0 || st==3 || st==0x32 || st==0x33 || st>=0x80;};
        const auto tik_done=[](Game& g){return g.dialogue.active() || (ram(g,0x80067148,1)&1)!=0;};
        std::vector<std::pair<std::string,Step>> steps{{"enemies for the power-up",defeat_for_gate(23)},{"talk",approach_and_talk(64)},{"accept",read_dialogue()}};
        for(unsigned round=0;round<4;++round) {
            steps.push_back({"power-up gate "+std::to_string(round+1),cross_gate(23,tik_done)});
            steps.push_back({"draclets "+std::to_string(round+1),sweep_targets({47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,67},draclet_down,tik_done,3000,0.0f,3.2f,true,0,{62,61,58,59,60,57,56,55,54,53,52,51,50,49,47,48,63,67})});
        }
        steps.push_back({"reward",reward(1500)});
        add("crystal-glacier-draclets",37,64,0,"Shaman Tik: flame every draclet in the caves with the superfly power-up, before they revive each other",std::move(steps));
    }
    // Magma Cone's Party Crashers (overlay 48). Chedda (13B #43, kind 3)
    // offers it once the superfly gate (1CC #33) is open (two 13E spirits);
    // accepting runs her look shot and wakes the twelve lava monsters (128
    // #13 .. #24), counted down in 80067088. A monster dies only on attack
    // bit 10000000 or 100000 (the case's first test): the lava bubbles Spyro
    // carries and spits at them (carry_shots). At none left the last defeat
    // ends the shot, she places Spyro before her and talks (orb bit 2).
    {
        const auto crasher_down=[](Game& g,unsigned i){return g.retail_objects[i].state<0;};
        const auto none_left=[](Game& g){return g.dialogue.active() || ram(g,0x80067088)==0;};
        std::vector<std::pair<std::string,Step>> steps{{"enemies for the power-up",defeat_for_gate(33,0x13e)},{"talk",approach_and_talk(43)},{"accept",read_dialogue()},{"look shot",wait(240)}};
        for(unsigned round=0;round<4;++round) {
            steps.push_back({"power-up gate "+std::to_string(round+1),cross_gate(33,none_left)});
            steps.push_back({"crashers "+std::to_string(round+1),carry_shots({13,14,15,16,17,18,19,20,21,22,23,24},crasher_down,none_left,16000u,{62.0f,27.0f,36.2f},{55.69f,32.28f,2.5f,32.25f},33)});
        }
        steps.push_back({"reward",reward(1500)});
        add("magma-cone-party-crashers",49,43,2,"Chedda: shoot down the twelve lava monsters with the superfly power-up",std::move(steps));
    }
    return all;
}
inline std::optional<Pilot> make(const std::string& name) {
    for(auto& p:catalogue())if(p.name==name)return p;
    return std::nullopt;
}
} // namespace challenge_pilot

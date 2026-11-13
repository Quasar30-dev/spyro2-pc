#pragma once
// Validation replay of Hurricos' diode quest (docs/RETAIL_SCRIPTS.md, "Hurricos'
// diodes"). Robots (class 2F, moby group 1) juggle the diodes (class 34) and
// pull them back out of their sockets (class 35) until they are defeated; the
// power bolt (8B) fires once every socket holds a diode and group 1 is gone.
// Like the level audit, it teleports between places and drives Spyro with
// ordinary inputs where it matters: walking up to a diode facing it (the
// diode posts the pickup request), carrying it and spitting it (Circle).
#include "spyro/coordinates.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace carry_quest {
using namespace spyro;
struct Report {unsigned sockets{},powered{},robots_alive{},rounds{},deliveries{};};
using Step=std::function<void(InputState)>;

inline void park(Game& g,Vec3 p,Vec3 face) {
    if(const auto f=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8))p=f->point+Vec3{0,.02f,0};
    g.player.position=g.player.previous_position=p;g.player.velocity={};
    g.player.yaw=g.player.previous_yaw=std::atan2(face.x-p.x,face.z-p.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
}
inline void place(Game& g,Vec3 floor,Vec3 face) {
    g.player.position=g.player.previous_position=floor;g.player.velocity={};
    g.player.yaw=g.player.previous_yaw=std::atan2(face.x-floor.x,face.z-floor.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
}
inline bool visible(const Game& g,unsigned i){return static_cast<std::int8_t>(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x4c,1))>0;}
inline bool alive(const Game& g,unsigned i){return g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1)<0x80;}

// Moby group n's members (800670e0 lists, bit 15 ends a list).
inline std::vector<unsigned> group(const Game& g,unsigned n) {
    const auto& s=g.overlay_scripts;std::vector<unsigned> out;
    const auto list=s.peek(s.peek(0x800670e0)+n*4);
    if(list<0x80000000u || list>=0x80800000u)return out;
    for(unsigned k=0;k<512;++k) {
        const auto e=s.peek(list+k*2,2);
        if((e&0x7fff)>=g.level.objects.size())return {};
        out.push_back(e&0x7fff);if(e&0x8000)return out;
    }
    return {};
}
// Defeat a robot: superflame it and stay beside it until it is gone. A robot
// the scripts keep hidden (+4C = 0) does not animate, and a knocked-down robot
// only finishes (and explodes) while it is active, so only visible ones count.
inline void hunt(Game& g,const Step& step,unsigned h) {
    if(g.retail_objects[h].state<0 || !visible(g,h))return;
    for(unsigned t=0;t<300 && g.retail_objects[h].state>=0;++t) {
        if(t%20==0 && std::hypot(g.player.position.x-g.retail_objects[h].position.x,g.player.position.z-g.retail_objects[h].position.z)>4.5f) {
            const auto at=g.retail_objects[h].position;bool placed=false;
            for(unsigned k=0;k<8 && !placed;++k) {
                const float a=k*.785f;const Vec3 c=at+Vec3{std::sin(a)*3,0,std::cos(a)*3};
                if(const auto f=g.level.raycast(c+Vec3{0,3,0},{0,-1,0},8);f && std::abs(f->point.y-at.y)<1.5f){park(g,c,at);placed=true;}
            }
        }
        if(t>=20 && t%10==0)g.retail_objects[h].attack_flags|=superflame_attack;
        step({});
    }
}
// Walk up to diode d facing it until Spyro takes it, carry it to `spit`
// units from socket gi (teleporting when far or blocked) and spit it.
inline bool deliver(Game& g,const Step& step,unsigned d,unsigned gi,unsigned attempts=3,float spit=2.0f,bool verbose=false) {
    const auto& s=g.overlay_scripts;
    const auto socket=g.retail_objects[gi].position;const auto gp=s.peek(s.moby_address(gi));
    for(unsigned round=0;round<attempts && !s.peek(gp+0x10);++round) {
        for(unsigned t=0;t<30;++t)step({});
        const auto at=g.retail_objects[d].position;auto away=at-socket;away.y=0;away=length(away)>.05f?normalized(away):Vec3{0,0,1};
        {   // On floor level with the diode, preferring the far side from the socket.
            std::optional<Vec3> from;float best=1e9f;
            for(unsigned k=0;k<16;++k) {
                const float a=k*.3927f;const Vec3 c=at+Vec3{std::sin(a)*1.6f,0,std::cos(a)*1.6f};
                const auto f=g.level.raycast(c+Vec3{0,1.2f,0},{0,-1,0},3);
                if(!f || std::abs(f->point.y-(at.y-.1f))>.45f)continue;
                const float score=length(c-(at+away*1.6f));
                if(score<best){best=score;from=f->point+Vec3{0,.02f,0};}
            }
            if(from)place(g,*from,at);else park(g,at+away*1.6f,at);
        }
        const auto picked=s.pickups;
        for(unsigned t=0;t<120 && s.pickups==picked;++t) {
            InputState in;const auto v=g.retail_objects[d].position-g.player.position;const float n=std::hypot(v.x,v.z);
            if(n>.75f){in.move_x=v.x/n*.4f;in.move_z=v.z/n*.4f;}
            step(in);
        }
        if(verbose)std::printf("  diode %u -> socket %u, try %u: %s\n",d,gi,round,s.pickups>picked?"picked up":"not picked up");
        if(s.pickups==picked)continue;
        {auto v=g.player.position-socket;v.y=0;if(length(v)>spit+4)park(g,socket+normalized(v)*(spit+2.5f),socket);}
        for(unsigned k=0;k<120;++k) {
            InputState in;auto v=socket-g.player.position;v.y=0;const float n=length(v);
            if(n<=spit)break;
            in.move_x=v.x/n*.6f;in.move_z=v.z/n*.6f;step(in);
        }
        if(auto v=socket-g.player.position;std::hypot(v.x,v.z)>spit+.5f) {
            // Blocked: a level spot `spit` from the socket with a clear line to it.
            std::optional<Vec3> spot;float best=1e9f;
            for(unsigned k=0;k<24;++k) {
                const float a=k*.2618f;const Vec3 c=socket+Vec3{std::sin(a)*spit,0,std::cos(a)*spit};
                const auto f=g.level.raycast(c+Vec3{0,1.5f,0},{0,-1,0},4);
                if(!f || std::abs(f->point.y-socket.y)>.8f)continue;
                const auto eye=f->point+Vec3{0,.5f,0},line=socket+Vec3{0,.5f,0}-eye;
                if(const auto hit=g.level.raycast(eye,line,length(line));hit && hit->distance<length(line)-.2f)continue;
                const float n=length(f->point-g.player.position);if(n<best){best=n;spot=f->point+Vec3{0,.02f,0};}
            }
            if(spot)place(g,*spot,socket);
        }
        {auto v=socket-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(v.x,v.z);}
        for(unsigned k=0;k<4;++k)step({});
        {InputState in;in.flame_pressed=true;step(in);}
        for(unsigned k=0;k<150;++k)step({});
        if(verbose)std::printf("   spat: socket %u %s\n",gi,s.peek(gp+0x10)?"took it":"missed");
    }
    if(s.carrying()){InputState in;in.flame_pressed=true;step(in);}
    return s.peek(gp+0x10)!=0;
}
// The whole quest. `keep` names sockets, `spare` diodes and `spare_robots`
// robots to leave for a filmed finish; rounds repeat until every other socket
// holds a diode and no other group 1 robot is left.
inline Report power_sockets(Game& g,const Step& step,std::set<unsigned> keep={},std::set<unsigned> spare={},bool verbose=false,
                            std::set<unsigned> spare_robots={}) {
    const auto& s=g.overlay_scripts;Report r;
    for(unsigned t=0;t<4 && !s.active();++t)step({});   // the scripts boot on the first tick
    if(!s.active())return r;
    std::vector<unsigned> diodes,sockets;
    for(const auto& o:g.retail_objects)if(o.id<g.level.objects.size() && !o.runtime_spawned) {
        if(o.type==0x34)diodes.push_back(o.id);
        if(o.type==0x35)sockets.push_back(o.id);
    }
    if(sockets.empty())return r;
    const auto robots=group(g,1);
    const auto hunt_all=[&]{for(unsigned pass=0;pass<3;++pass)for(const auto h:robots)if(!spare_robots.contains(h))hunt(g,step,h);};
    for(;r.rounds<12;++r.rounds) {
        hunt_all();
        std::set<std::uint32_t> socketed;std::vector<unsigned> empty;
        for(const auto gi:sockets){const auto v=s.peek(s.peek(s.moby_address(gi))+0x10);if(v)socketed.insert(v);else if(!keep.contains(gi))empty.push_back(gi);}
        r.robots_alive=0;for(const auto h:robots)r.robots_alive+=alive(g,h) && !spare_robots.contains(h);
        if(verbose) {
            std::printf("  round %u: %zu/%zu sockets powered, %u robots alive:",r.rounds,socketed.size(),sockets.size(),r.robots_alive);
            for(const auto h:robots)if(alive(g,h))std::printf(" %u (+4C %u, %.1f %.1f %.1f)",h,s.peek(s.moby_address(h)+0x4c,1),g.retail_objects[h].position.x,g.retail_objects[h].position.y,g.retail_objects[h].position.z);
            std::printf("\n");
        }
        if(empty.empty() && !r.robots_alive)break;
        for(const auto gi:empty) {
            unsigned best=UINT32_MAX;float nearest=1e9f;
            for(const auto d:diodes) {
                const auto m=s.moby_address(d);
                if(spare.contains(d) || socketed.contains(m) || !alive(g,d) || s.peek(s.peek(m)+0x20))continue;
                const float n=length(g.retail_objects[d].position-g.retail_objects[gi].position);
                if(n<nearest){nearest=n;best=d;}
            }
            if(best==UINT32_MAX)continue;
            if(deliver(g,step,best,gi,3,2.0f,verbose)){++r.deliveries;socketed.insert(s.moby_address(best));}
            hunt_all();
        }
    }
    r.sockets=static_cast<unsigned>(sockets.size());r.powered=0;
    for(const auto gi:sockets)r.powered+=s.peek(s.peek(s.moby_address(gi))+0x10)!=0;
    r.robots_alive=0;for(const auto h:robots)r.robots_alive+=alive(g,h);
    return r;
}
} // namespace carry_quest

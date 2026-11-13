// End-to-end completion audit of every campaign level on the real runtime
// (native systems + original overlay scripts). For each level it:
//   1. defeats every spirit-carrying enemy (native or script owned), parking
//      Spyro beside it and escalating the retail attack masks;
//   2. breaks every container;
//   3. collects every gem in the level, including the ones just released;
//   4. checks every powerup gate opened.
// Hurricos' diode quest (class 34 diodes, 35 sockets) is played through as
// well: Spyro carries each diode to a socket and defeats the robots.
// Spyro is kept alive throughout; teleports stand in for the walking route.
// Usage: spyro_level_completion <retail data> [slot ...]
#include "spyro/assets.hpp"
#include "spyro/object_interaction.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include "carry_quest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <numbers>
#include <set>

using namespace spyro;
namespace {
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);}
// Scripted terrain groups (doors, gates, bridges, lifts) that left their
// authored timeline state at any point of the run.
std::map<unsigned,std::pair<unsigned,unsigned>> terrain_start;   // morph index -> (node, flags)
std::set<unsigned> terrain_moved;                                 // groups
const Level* authored=nullptr;   // the decoded level: each group's authored timeline state
void watch_terrain(const Game& g) {
    for(unsigned k=0;k<g.level.terrain_morphs.size();++k) {
        const auto& m=g.level.terrain_morphs[k];
        if(!m.scripted || m.color)continue;   // colour tracks (lights) are not doors
        const auto& a=authored && k<authored->terrain_morphs.size()?authored->terrain_morphs[k]:m;
        terrain_start.try_emplace(k,a.node,a.flags);
        if(a.node!=m.node || (a.flags&2)!=(m.flags&2))terrain_moved.insert(m.group);
    }
}
// Conversations started by walking past a speaker or Zoe are read through
// (next, or "yes" to a question) as a player would; the scripts pause while one is open.
void ticks(Game& g,unsigned n) {
    static unsigned clock=0;
    for(unsigned i=0;i<n;++i) {
        keep_alive(g);InputState in;
        if(g.dialogue.active() && ++clock%15==0) {
            const auto* line=g.dialogue.line();
            if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;
        }
        g.advance(FixedClock::step,in);
        watch_terrain(g);
    }
}
// Spyro on real floor a few units from the object, facing it.
void park(Game& g,Vec3 at,float distance) {
    for(unsigned k=0;k<16;++k) {
        const float a=k*std::numbers::pi_v<float>/8,r=k<8?distance:distance*1.6f;
        const Vec3 p=at+Vec3{std::sin(a)*r,0,std::cos(a)*r};
        if(const auto hit=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8);hit && hit->normal.y>.6f && std::abs(hit->point.y-at.y)<3) {
            g.player.position=g.player.previous_position=hit->point+Vec3{0,.01f,0};g.player.velocity={};
            g.player.yaw=g.player.previous_yaw=std::atan2(at.x-p.x,at.z-p.z);
            g.camera.reset(g.level,g.player.position,g.player.yaw);   // a teleport: the camera comes too (scripts only run on screen)
            return;
        }
    }
    g.player.position=g.player.previous_position=at+Vec3{0,.1f,0};g.player.velocity={};
}
bool gone(const Game& g,unsigned i){const auto& o=g.retail_objects[i];return o.collected || o.state<0;}
// Escalating retail attack masks: flame, charge, headbash, superflame,
// supercharge, green bomb (what the game's own powers deliver).
bool defeat(Game& g,unsigned i) {
    for(const std::uint32_t mask:{flame_attack,charge_attack,headbash_attack,0x970000u,0x560000u,0x10570000u}) {
        for(unsigned t=0;t<90 && !gone(g,i);++t){if(t%10==0)g.retail_objects[i].attack_flags|=mask;ticks(g,1);}
        if(gone(g,i))return true;
    }
    return false;
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_level_completion <retail data> [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        for(const auto slot:slots) {
            if(!retail_level_id(slot))continue;
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models,nullptr);
            // A completing player has paid every Moneybags service and holds
            // the orbs the homeworld doors ask for: their doors open at load.
            if(!std::getenv("UNPAID")){g.purchases=(1u<<19)-1;g.player.orbs=64;g.player.talismans=(1u<<29)-1;}
            authored=&level;
            ticks(g,2);
            const bool speedway=g.systems.speedway();
            if(std::getenv("HOST_TRACE"))for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==0x80)
                std::cout<<"    rider "<<i<<" held_gem "<<unsigned(g.retail_objects[i].held_gem)<<" ram+50 "<<g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x50,1)
                    <<" record[0x50] "<<unsigned(level.objects[i].record.size()>0x50?level.objects[i].record[0x50]:0)<<" state "<<g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1)<<"\n";
            // 1. Enemies.
            unsigned enemies=0,defeated=0;std::map<unsigned,unsigned> survivors;
            for(unsigned i=0;i<level.objects.size();++i) {
                // Copies: ticking can spawn objects and reallocate the vector.
                const auto o=g.retail_objects[i];
                if(std::getenv("SKIP_ENEMIES") || !(level.objects[i].record[83]&127) || !(o.enemy || o.retail_script) || o.boss_body || gone(g,i))continue;
                ++enemies;park(g,o.position,2.6f);ticks(g,30);
                bool down=defeat(g,i);
                // Some only wake for a player approaching from the floor below
                // (Aquaria Towers' ledge crabs 9B check the camera eye against an
                // anchor under them): try lower floors around it, camera behind.
                for(unsigned k=0;k<24 && !down;++k) {
                    const float a=(k%8)*std::numbers::pi_v<float>/4,r=1.0f+(k/8)*1.2f;
                    const Vec3 p=o.position+Vec3{std::sin(a)*r,-1.5f,std::cos(a)*r};
                    const auto floor=g.level.raycast(p,{0,-1,0},8);
                    if(!floor || floor->normal.y<.6f || o.position.y-floor->point.y<2)continue;
                    g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(o.position.x-floor->point.x,o.position.z-floor->point.z);
                    g.camera.reset(g.level,g.player.position,g.player.yaw);ticks(g,20);
                    down=defeat(g,i);
                }
                if(down)++defeated;else {
                    ++survivors[o.type];
                    if(std::getenv("HOST_TRACE"))std::cout<<"    undefeated "<<i<<" class "<<std::hex<<o.type<<" at moby "<<g.overlay_scripts.moby_address(i)<<std::dec
                        <<" pos "<<o.position.x<<","<<o.position.y<<","<<o.position.z<<" ram state "<<g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1)<<"\n";
                }
            }
            // 1b. Groups defeated together (Crystal Glacier 50: a group controller
            // awards every member's spirit once none of them is awake): strike all
            // survivors of a class in the same tick, escalating the masks.
            // The controller (Crystal Glacier c6) only runs while active near
            // Spyro: cluster the members and stay at each cluster while striking.
            for(auto& [type,count]:survivors) {
                std::vector<unsigned> members;
                for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==type && !gone(g,i) && (level.objects[i].record[83]&127))members.push_back(i);
                std::vector<std::vector<unsigned>> clusters;
                for(const auto i:members) {
                    auto c=std::find_if(clusters.begin(),clusters.end(),[&](const auto& k){return length(g.retail_objects[k.front()].position-g.retail_objects[i].position)<16;});
                    if(c==clusters.end())clusters.push_back({i});else c->push_back(i);
                }
                // Crystal Glacier's challenge only counts while Spyro flies with
                // the level's flight gate power (movement mode 6): fly if it has one.
                const bool flight_gate=std::any_of(g.retail_objects.begin(),g.retail_objects.end(),[](const auto& o){return o.powerup_gate && o.powerup_gate->kind==2 && o.powerup_gate->unlocked;});
                const auto fly=[&]{if(!flight_gate)return;g.cheats.superfly=true;g.powers.flight={2400,2400};
                    if(!g.powers.flying){g.player.position.y+=1.5f;g.player.previous_position=g.player.position;g.player.grounded=false;g.powers.flying=true;}};
                // One quick pass over every cluster per attack mask (members
                // recover and carry each other off when left too long), then wait.
                for(const std::uint32_t mask:{flame_attack,charge_attack,headbash_attack,0x970000u}) {
                    for(const auto& cluster:clusters) {
                        Vec3 centre{};for(const auto i:cluster)centre+=g.retail_objects[i].position;centre=centre/static_cast<float>(cluster.size());
                        park(g,centre,1.0f);
                        for(unsigned t=0;t<90;t+=6) {
                            if(t%12==0)for(const auto i:cluster)if(!gone(g,i))g.retail_objects[i].attack_flags|=mask;
                            fly();ticks(g,6);
                        }
                    }
                    for(unsigned t=0;t<300;t+=10){fly();ticks(g,10);}
                    if(std::all_of(members.begin(),members.end(),[&](unsigned i){return gone(g,i);}))break;
                }
                if(flight_gate){g.powers.end_flight();g.cheats.superfly=false;}
                unsigned left=0;for(const auto i:members)left+=!gone(g,i);
                defeated+=count-std::min(count,left);count=left;
            }
            std::erase_if(survivors,[](const auto& s){return s.second==0;});
            // 2. Containers.
            unsigned containers=0,broken=0;std::map<unsigned,unsigned> unbroken;
            for(unsigned i=0;i<level.objects.size();++i) {
                const auto o=g.retail_objects[i];
                if(!(o.container || is_breakable(o.type) || is_heavy_container(o.type)) || gone(g,i))continue;
                ++containers;park(g,o.position,1.6f);ticks(g,10);
                if(defeat(g,i))++broken;else ++unbroken[o.type];
            }
            // 2b. Script-owned gem holders: mobys holding a gem themselves, and
            // the hosts that class 80 gem riders follow (param[0]).
            unsigned holders_hit=0,holders_open=0;
            {
                std::vector<unsigned> hosts;
                for(unsigned i=0;i<level.objects.size();++i) {
                    if(!g.retail_objects[i].retail_script || gone(g,i))continue;
                    const unsigned held=g.retail_objects[i].held_gem;
                    if(g.retail_objects[i].type==0x80) {
                        const auto host=g.overlay_scripts.peek(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)));
                        if(host<level.objects.size() && g.retail_objects[host].retail_script)hosts.push_back(host);
                    } else if(held==1 || held==2 || held==5 || held==10 || held==25)hosts.push_back(i);
                }
                std::sort(hosts.begin(),hosts.end());hosts.erase(std::unique(hosts.begin(),hosts.end()),hosts.end());
                for(const auto i:hosts) {
                    if(gone(g,i))continue;
                    // Some holders take several hits, moving between them (Zephyr's
                    // ce hops along its path, then bursts): keep at it, following it.
                    ++holders_hit;bool open=false;
                    const auto sub=[&]{return std::to_string(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x48,1))+"/"+std::to_string(g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x49,1));};
                    const auto first=sub();
                    if(std::getenv("HOST_TRACE")) {
                        g.retail_objects[i].attack_flags|=headbash_attack;const auto set=g.retail_objects[i].attack_flags;ticks(g,1);
                        std::cout<<"    hit probe "<<i<<": set "<<std::hex<<set<<" after tick engine "<<g.retail_objects[i].attack_flags<<" ram+18 "<<g.overlay_scripts.peek(g.overlay_scripts.moby_address(i)+0x18)
                            <<std::dec<<" script "<<g.retail_objects[i].retail_script<<" frozen "<<g.retail_objects[i].frozen_ticks<<" enemy "<<g.retail_objects[i].enemy.has_value()<<" power "<<g.retail_objects[i].power_target.has_value()<<"\n";
                    }
                    // Some only break to a headbash right beside them (Winter Tundra 2a8 within 0x351): close in on later rounds.
                    for(unsigned round=0;round<8 && !open;++round){park(g,g.retail_objects[i].position,round<2?1.8f:round<5?.6f:.25f);ticks(g,10);open=defeat(g,i);}
                    // A headbash comes down from a jump: drop Spyro from just above it
                    // (beside a kerb or a door frame, walking stops him short).
                    for(unsigned round=0;round<2 && !open;++round) {
                        const auto at=g.retail_objects[i].position;
                        g.player.position=g.player.previous_position=at+Vec3{round?.3f:0,1.2f,round?0:.3f};g.player.velocity={};
                        ticks(g,20);open=defeat(g,i);
                    }
                    if(std::getenv("HOST_TRACE"))std::cout<<"    free moby slots "<<static_cast<int>(g.overlay_scripts.peek(0x80066f40)-g.overlay_scripts.peek(0x80066f3c))<<"\n";
                    if(std::getenv("HOST_TRACE")) {
                        const auto m=g.overlay_scripts.moby_address(i);
                        std::cout<<"    holder "<<i<<" class "<<std::hex<<g.retail_objects[i].type<<std::dec<<" at "<<std::hex<<m<<std::dec<<" state/sub "<<first<<" -> "<<sub()<<" open "<<open
                            <<" | spyro origin "<<int(g.overlay_scripts.peek(0x80069ff0))<<","<<int(g.overlay_scripts.peek(0x80069ff4))<<","<<int(g.overlay_scripts.peek(0x80069ff8))
                            <<" moby "<<int(g.overlay_scripts.peek(m+0xc))<<","<<int(g.overlay_scripts.peek(m+0x10))<<","<<int(g.overlay_scripts.peek(m+0x14))<<"\n";
                    }
                    holders_open+=open;
                }
            }
            ticks(g,120);   // released gems settle
            // 2c. Interactables (INTERACT=1): stand facing every other script
            // moby and flame, charge and headbash it, as a player trying every
            // switch would; report which ones move terrain groups (doors).
            if(std::getenv("INTERACT")) {
                std::map<unsigned,Vec3> solids;for(const auto& c:g.level.object_colliders)solids[c.object]=c.bounds.center();
                for(unsigned i=0;i<level.objects.size();++i) {
                    const auto o=g.retail_objects[i];
                    if(!o.retail_script || gone(g,i) || (level.objects[i].record[83]&127))continue;
                    const auto before=terrain_moved;const auto faults=g.overlay_scripts.faults;
                    park(g,o.position,1.6f);ticks(g,6);
                    for(const std::uint32_t mask:{flame_attack,charge_attack,headbash_attack}) {
                        if(gone(g,i))break;
                        g.retail_objects[i].attack_flags|=mask;ticks(g,24);
                    }
                    // Solid ones may be plates: stand on top (8006a134), then headbash it.
                    if(const auto c=std::find_if(g.level.object_colliders.begin(),g.level.object_colliders.end(),[&](const auto& x){return x.object==i;});
                       c!=g.level.object_colliders.end() && !gone(g,i)) {
                        const Vec3 top{c->bounds.center().x,c->bounds.max.y+.05f,c->bounds.center().z};
                        g.player.position=g.player.previous_position=top;g.player.velocity={};ticks(g,40);
                        g.retail_objects[i].attack_flags|=headbash_attack;ticks(g,20);
                    }
                    if(g.overlay_scripts.faults!=faults)
                        std::cout<<"    fault while trying "<<i<<" class "<<std::hex<<o.type<<std::dec<<": "<<g.overlay_scripts.fault<<"\n";
                    if(terrain_moved!=before) {
                        std::cout<<"    switch "<<i<<" class "<<std::hex<<o.type<<std::dec<<" moved groups";
                        for(const auto group:terrain_moved)if(!before.contains(group))std::cout<<" g"<<group;
                        std::cout<<"\n";
                    }
                }
                ticks(g,120);
                unsigned moved=0;std::map<unsigned,unsigned> still;
                for(const auto& [object,centre]:solids) {
                    const auto c=std::find_if(g.level.object_colliders.begin(),g.level.object_colliders.end(),[&](const auto& x){return x.object==object;});
                    if(c==g.level.object_colliders.end() || length(c->bounds.center()-centre)>.3f)++moved;else ++still[g.retail_objects[object].type];
                }
                std::cout<<"    solid mobys "<<solids.size()<<", moved or opened "<<moved<<"; unchanged:";
                for(const auto& [type,n]:still)std::cout<<" "<<std::hex<<type<<std::dec<<" x"<<n;
                std::cout<<"\n";
            }
            // 2d. Carried diodes (Hurricos): every socket powered, every robot down.
            std::string quest;
            {
                const auto shots=g.overlay_scripts.camera_shots;
                const auto report=carry_quest::power_sockets(g,[&](InputState in){keep_alive(g);g.advance(FixedClock::step,in);watch_terrain(g);});
                if(report.sockets) {
                    ticks(g,240);
                    quest="  diode sockets "+std::to_string(report.powered)+"/"+std::to_string(report.sockets)+" (robots left "+std::to_string(report.robots_alive)+
                        ", pickups "+std::to_string(g.overlay_scripts.pickups)+", camera shots "+std::to_string(g.overlay_scripts.camera_shots-shots)+")";
                }
            }
            // 3. Gems (repeat: collecting can release more).
            unsigned gems_placed=0,gem_value=0;
            for(const auto& o:level.objects)if(o.type==1){++gems_placed;gem_value+=o.gem_value;}
            unsigned missed=0;std::vector<Vec3> missing;
            for(unsigned pass=0;pass<3;++pass) {
                missing.clear();missed=0;
                for(std::size_t i=0;i<g.retail_objects.size();++i) {
                    if(g.retail_objects[i].type!=1 || g.retail_objects[i].collected || g.retail_objects[i].state<0)continue;
                    const auto at=g.retail_objects[i].position;
                    g.player.position=g.player.previous_position=at-retail::origin_above_feet*.5f;g.player.velocity={};
                    for(unsigned t=0;t<8 && !g.retail_objects[i].collected;++t)ticks(g,1);
                    if(!g.retail_objects[i].collected){++missed;missing.push_back(at);}
                }
                if(!missed)break;
                ticks(g,60);
            }
            // Gems still held by mobys that are not gone (not containers or
            // spirit enemies, or never broken), by class.
            std::map<unsigned,std::pair<unsigned,unsigned>> holders;
            for(std::size_t i=0;i<g.retail_objects.size();++i) {
                const auto& o=g.retail_objects[i];
                if(o.collected || o.state<0 || o.type==1)continue;
                const unsigned held=o.held_gem;
                if(held==1 || held==2 || held==5 || held==10 || held==25){auto& h=holders[o.type];++h.first;h.second+=held;
                    if(o.type==0x80 && std::getenv("HOST_TRACE") && i<level.objects.size()) {   // class 80 rides its host (param[0])
                        const auto host=g.overlay_scripts.peek(g.overlay_scripts.peek(g.overlay_scripts.moby_address(unsigned(i))));
                        if(host<g.retail_objects.size())std::cout<<"    gem rider "<<i<<" host "<<host<<" class "<<std::hex<<g.retail_objects[host].type<<std::dec
                            <<" engine-gone "<<gone(g,host)<<" script "<<g.retail_objects[host].retail_script<<" ram-state "<<g.overlay_scripts.peek(g.overlay_scripts.moby_address(host)+0x48,1)<<"\n";
                    }}
            }
            if(std::getenv("HOST_TRACE")) {
                unsigned held_at_boot=0,lost=0;
                for(const auto& o:level.objects)if(o.type!=1 && (o.held_gem==1 || o.held_gem==2 || o.held_gem==5 || o.held_gem==10 || o.held_gem==25))held_at_boot+=o.held_gem;
                for(const auto& o:g.retail_objects)if(o.runtime_spawned && o.type==1 && !o.collected && o.state<0)lost+=o.gem_value;
                std::map<unsigned,std::pair<unsigned,unsigned>> by_class;
                for(unsigned i=0;i<level.objects.size();++i){const unsigned h=level.objects[i].held_gem;if(level.objects[i].type!=1 && (h==1 || h==2 || h==5 || h==10 || h==25)){auto& c=by_class[level.objects[i].type];++c.first;c.second+=h;}}
                for(const auto& [type,c]:by_class)std::cout<<"    held at boot: class "<<std::hex<<type<<std::dec<<" x"<<c.first<<" worth "<<c.second<<(g.retail_objects.empty()?"":"")<<"\n";
                std::cout<<"    held by mobys at boot "<<held_at_boot<<" + placed "<<gem_value<<" = "<<held_at_boot+gem_value<<"; released gems removed uncollected "<<lost<<"\n";
            }
            if(std::getenv("HOST_TRACE"))for(const auto& o:g.retail_objects)if(o.script_gem)
                std::cout<<"    script gem value "<<o.gem_value<<" collected "<<o.collected<<" state "<<int(o.state)<<" at "<<o.position.x<<","<<o.position.y<<","<<o.position.z<<"\n";
            // 4. Gates.
            unsigned gates=0,open=0;
            for(const auto& o:g.retail_objects)if(o.powerup_gate){++gates;open+=o.powerup_gate->unlocked;}
            std::cout<<"slot "<<slot<<(speedway?" (speedway)":"")<<": enemies "<<defeated<<"/"<<enemies<<"  containers "<<broken<<"/"<<containers<<"  script gem holders "<<holders_open<<"/"<<holders_hit
                <<"  gems "<<g.level_gems()<<" (Spyro's counter "<<g.player.gems<<") value collected (placed "<<gems_placed<<" worth "<<gem_value<<", uncollected "<<missed<<")"
                <<"  gates "<<open<<"/"<<gates<<"  spirits "<<g.spirits()<<"  faults "<<g.overlay_scripts.faults<<"  script gems "<<g.overlay_scripts.gems<<quest<<"\n";
            if(g.overlay_scripts.faults)std::cout<<"    last fault: "<<g.overlay_scripts.fault<<"\n";
            if(g.overlay_scripts.grid_repairs)std::cout<<"    grid cell repairs (80041538 guard): "<<g.overlay_scripts.grid_repairs<<"\n";
            for(const auto& [type,n]:survivors)std::cout<<"    undefeated enemy class "<<std::hex<<type<<std::dec<<" x"<<n<<"\n";
            for(const auto& [type,n]:unbroken)std::cout<<"    unbroken container class "<<std::hex<<type<<std::dec<<" x"<<n<<"\n";
            for(const auto& [type,h]:holders)std::cout<<"    gems still held: class "<<std::hex<<type<<std::dec<<" x"<<h.first<<" worth "<<h.second<<(g.retail_objects.empty()?"":"")<<"\n";
            for(std::size_t k=0;k<missing.size() && k<6;++k)std::cout<<"    uncollected gem at "<<missing[k].x<<","<<missing[k].y<<","<<missing[k].z<<"\n";
            {
                std::set<unsigned> groups;for(const auto& m:g.level.terrain_morphs)if(m.scripted && !m.color)groups.insert(m.group);
                std::cout<<"    terrain groups "<<groups.size()<<", moved "<<terrain_moved.size()<<" (door commands "<<g.overlay_scripts.door_commands<<"); never:";
                for(const auto group:groups)if(!terrain_moved.contains(group)) {
                    Bounds b;
                    for(const auto& m:g.level.terrain_morphs)if(m.group==group && m.collision)
                        for(unsigned i=0;i<m.frames.front().size()/3;++i){const auto& t=g.level.triangles[m.first_triangle+i];b.add(t.a);b.add(t.b);b.add(t.c);}
                    std::cout<<" g"<<group;
                    if(b.min.x<=b.max.x)std::cout<<"@"<<int(b.center().x)<<","<<int(b.center().y)<<","<<int(b.center().z);
                }
                std::cout<<"\n";
                terrain_start.clear();terrain_moved.clear();
            }
            std::cout<<std::flush;
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}

// Census of solid script mobys: every script-owned moby whose collision bank
// (+08) carries triangles. 8003dc64 (Spyro's terrain test) and the floor
// rays 8003ce38/8003ee84 collide with these triangles as well as the scene's;
// doors, gates, lifts and switch plates are solid this way.
// Usage: spyro_solid_census <retail data> [slot ...]
#include "spyro/assets.hpp"
#include "spyro/elements.hpp"
#include "spyro/object_interaction.hpp"
#include "spyro/overlay_vm.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include "spyro/coordinates.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <numbers>
#include <optional>

using namespace spyro;
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_solid_census <retail data> [slot ...]\n";return 2;}
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
            if(const char* p=std::getenv("PURCHASES"))g.purchases=static_cast<std::uint32_t>(std::stoul(p,nullptr,16));
            if(const char* o=std::getenv("ORBS"))g.player.orbs=static_cast<unsigned>(std::stoul(o));
            if(const char* t=std::getenv("TALISMANS"))g.player.talismans=static_cast<std::uint32_t>(std::stoul(t,nullptr,16));
            for(unsigned t=0;t<(std::getenv("TICKS")?std::stoul(std::getenv("TICKS")):4u);++t)g.advance(FixedClock::step,{});
            auto& s=g.overlay_scripts;
            std::map<unsigned,unsigned> per_class;unsigned solid=0;
            for(unsigned i=0;i<g.retail_objects.size() && i<level.objects.size();++i) {
                if(!g.retail_objects[i].retail_script)continue;
                const auto m=s.moby_address(i);const auto bank=s.peek(m+8);
                if(bank<0x80000000u || bank>=0x80800000u)continue;
                const auto header=s.peek(bank);
                const unsigned vertices=(header>>16)&0x7f;
                if(!(header>>16))continue;
                unsigned triangles=0;
                for(auto p=bank+4;triangles<512;p+=4){const auto w=s.peek(p);if(w&0x80000000u)break;++triangles;}
                ++solid;++per_class[g.retail_objects[i].type];
                const auto p=g.retail_objects[i].position;
                std::printf("slot %u obj %u class %x state %d bank %08x header %08x verts %u tris %u at %.1f %.1f %.1f\n",slot,i,
                    g.retail_objects[i].type,int(static_cast<std::int8_t>(s.peek(m+0x48,1))),bank,header,vertices,triangles,p.x,p.y,p.z);
                if(std::getenv("MATRIX"))std::printf("  matrix %08x %08x %08x %08x %08x rot %02x %02x %02x anim %08x blend %02x\n",s.peek(m+0x20),s.peek(m+0x24),s.peek(m+0x28),
                    s.peek(m+0x2c),s.peek(m+0x30),s.peek(m+0x44,1),s.peek(m+0x45,1),s.peek(m+0x46,1),s.peek(m+0x3c),s.peek(m+0x40,1));
            }
            std::printf("slot %u solid %u classes:",slot,solid);
            for(const auto& [c,n]:per_class)std::printf(" %x x%u",c,n);
            std::printf(" | colliders %zu triangles %zu\n",g.level.object_colliders.size(),g.level.object_triangles.size());
            if(const char* wanted=std::getenv("CLASS")) {
                const auto type=std::stoul(wanted,nullptr,16);
                if(const auto* models=g.object_models()) {
                    const auto bank=models->models.find(static_cast<unsigned>(type));
                    if(bank==models->models.end())std::printf("  class %lx: no model in the object bank\n",type);
                    else {
                        std::printf("  class %lx model: animated %d meshes %zu clips %zu\n",type,int(bank->second.animated),bank->second.meshes.size(),bank->second.animations.size());
                        for(std::size_t c=0;c<bank->second.animations.size();++c) {
                            std::printf("   clip %zu frames %zu radius %.2f\n",c,bank->second.animations[c].frames.size(),bank->second.animations[c].radius);
                            for(std::size_t f=0;f<bank->second.animations[c].frames.size();f+=7) {
                                ObjectAnimationState st{};st.clip=st.next_clip=static_cast<std::uint8_t>(c);st.frame=st.next_frame=static_cast<std::uint8_t>(f);
                                const auto pose=sample_object_animation(bank->second,st,models->animation_blends);Bounds b;for(const auto v:pose.vertices)b.add(v);
                                std::printf("    frame %zu model-space x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n",f,b.min.x,b.max.x,b.min.y,b.max.y,b.min.z,b.max.z);
                            }
                        }
                        for(std::size_t m=0;m<bank->second.meshes.size();++m) {
                            const auto& mesh=bank->second.meshes[m];unsigned translucent=0,textured=0;
                            for(const auto& f:mesh.faces){translucent+=f.translucent;textured+=f.textured;}
                            std::printf("   mesh %zu vertices %zu faces %zu textured %u translucent %u radius %.2f\n",m,mesh.vertices.size(),mesh.faces.size(),textured,translucent,mesh.radius);
                        }
                    }
                    const auto table=s.peek(0x80068c94+static_cast<std::uint32_t>(type)*4);
                    std::printf("  class table entry %08x\n",table);
                }
                for(const auto& o:g.retail_objects)if(o.type==type && o.powerup_gate)
                    std::printf("  gate obj %u kind %u duration %u requirement %u\n",o.id,o.powerup_gate->kind,o.powerup_gate->duration,o.powerup_gate->requirement);
                for(const auto& o:g.retail_objects)if(o.type==type)
                    std::printf("  class %x obj %u script %d container %d enemy %d npc-ish %d state %d collected %d at %.1f %.1f %.1f params %08x\n",o.type,o.id,int(o.retail_script),
                        int(o.container.has_value()),int(o.enemy.has_value()),int(o.scripted_contact),int(o.state),int(o.collected),o.position.x,o.position.y,o.position.z,
                        o.id<level.objects.size()?s.peek(s.moby_address(o.id)):0u);
                if(const char* off=std::getenv("PARAM_PEEK"))for(const auto& o:g.retail_objects)if(o.type==type && o.id<level.objects.size()) {
                    const auto params=s.peek(s.moby_address(o.id));const auto v=s.peek(params+std::stoul(off,nullptr,16));
                    std::printf("    obj %u params+%s = %08x",o.id,off,v);
                    if(v>=0x80000000u && v<0x80800000u)std::printf(" -> class %x at %d %d %d (moby %08x)",s.peek(v+0x36,2),int(s.peek(v+0xc)),int(s.peek(v+0x10)),int(s.peek(v+0x14)),v);
                    std::printf(" | subtype %u portal %d",s.peek(params,1),int(std::count_if(g.level.portals.begin(),g.level.portals.end(),[&](const auto& p){return p.gate_npc==o.id;})));
                    std::printf(" | self at %d %d %d +47 %02x\n",int(s.peek(s.moby_address(o.id)+0xc)),int(s.peek(s.moby_address(o.id)+0x10)),int(s.peek(s.moby_address(o.id)+0x14)),s.peek(s.moby_address(o.id)+0x47,1));
                }
                for(const auto& o:g.retail_objects)if(o.type==type && o.id<level.objects.size()) {
                    const auto m=s.moby_address(o.id);const auto bank=s.peek(m+8);
                    std::printf("    obj %u bank %08x header %08x cell %d +18 %08x +47 %02x +48 %02x +4a %02x +4c %02x +4d %02x\n",o.id,bank,bank>=0x80000000u && bank<0x80800000u?s.peek(bank):0u,
                        int(static_cast<std::int16_t>(s.peek(m+0x34,2))),s.peek(m+0x18),s.peek(m+0x47,1),s.peek(m+0x48,1),s.peek(m+0x4a,1),s.peek(m+0x4c,1),s.peek(m+0x4d,1));
                    std::printf("      +4e %02x +4f %02x +50 %02x +51 %02x +52 %02x +53 %02x +54 %08x engine size %u colour %08x\n",s.peek(m+0x4e,1),s.peek(m+0x4f,1),s.peek(m+0x50,1),s.peek(m+0x51,1),
                        s.peek(m+0x52,1),s.peek(m+0x53,1),s.peek(m+0x54),unsigned(o.size),o.material_color);
                }
            }
            if(const char* at=std::getenv("PROBE_AT")) {
                // Distances to collision around a point: 16 horizontal rays and one up.
                Vec3 p{};std::sscanf(at,"%f,%f,%f",&p.x,&p.y,&p.z);
                std::printf("  probe %.1f %.1f %.1f:",p.x,p.y,p.z);
                for(unsigned k=0;k<16;++k) {
                    const float a=k*std::numbers::pi_v<float>/8;const auto hit=g.level.raycast(p,{std::sin(a),0,std::cos(a)},40);
                    std::printf(" %u:%.1f%s",k*45/2,hit?hit->distance:99.f,hit && (hit->triangle&Level::object_triangle)?"m":"");
                }
                const auto up=g.level.raycast(p,{0,1,0},60);std::printf(" up:%.1f",up?up->distance:99.f);
                const auto down=g.level.raycast(p,{0,-1,0},60);std::printf(" down:%.1f\n",down?down->distance:99.f);
            }
            if(const char* from=std::getenv("WALK_FROM")) {
                Vec3 p{},d{};std::sscanf(from,"%f,%f,%f,%f,%f",&p.x,&p.y,&p.z,&d.x,&d.z);d=normalized(d);
                g.player.position=g.player.previous_position=p;g.player.velocity={};g.player.yaw=std::atan2(d.x,d.z);
                for(unsigned t=0;t<90;++t){InputState in;in.move_x=d.x;in.move_z=d.z;g.player.invulnerable_ticks=600;g.advance(FixedClock::step,in);}
                const auto c=g.player.position+retail::origin_above_feet;
                std::printf("  walked to %.2f %.2f %.2f (centre y %.2f radius %.2f)\n",g.player.position.x,g.player.position.y,g.player.position.z,c.y,g.tuning.radius);
                for(const auto i:g.level.collision_candidates(c.x-1,c.z-1,c.x+1,c.z+1)) {
                    const auto& t=g.level.triangles[i];const auto q=closest_point(c,t);
                    if(!t.disabled && !t.water && length(q-c)<g.tuning.radius+.05f)
                        std::printf("   touching triangle %u at %.2f %.2f %.2f normal-ish %.2f %.2f %.2f\n",i,q.x,q.y,q.z,(c-q).x,(c-q).y,(c-q).z);
                }
            }
            if(const char* crab=std::getenv("CRAB")) {
                // Aquaria Towers 9B with an anchor (param+34): try floor spots
                // around the anchor, facing the crab, camera behind; report when
                // the crab wakes (+47 != F8) and the camera eye's distance.
                const auto i=static_cast<unsigned>(std::stoul(crab));const auto m=s.moby_address(i);
                const auto anchor=s.peek(s.peek(m)+0x34);
                const auto a=retail::to_world({static_cast<std::int32_t>(s.peek(anchor+0xc)),static_cast<std::int32_t>(s.peek(anchor+0x10)),static_cast<std::int32_t>(s.peek(anchor+0x14))});
                const auto c=g.retail_objects[i].position;
                std::printf("  crab %u at %.1f %.1f %.1f anchor %.1f %.1f %.1f\n",i,c.x,c.y,c.z,a.x,a.y,a.z);
                for(const float r:{0.0f,1.5f,3.0f,4.5f})for(unsigned k=0;k<(r?8u:1u);++k) {
                    const float ang=k*std::numbers::pi_v<float>/4;const Vec3 p=a+Vec3{std::sin(ang)*r,0,std::cos(ang)*r};
                    const auto floor=g.level.raycast(p+Vec3{0,2.5f,0},{0,-1,0},6);
                    if(!floor || floor->normal.y<.6f)continue;
                    g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(c.x-floor->point.x,c.z-floor->point.z);
                    g.camera.reset(g.level,g.player.position,g.player.yaw);
                    unsigned awake=0;float best=1e9f;
                    for(unsigned t=0;t<40;++t) {
                        g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});
                        const auto eye=g.camera.sample(1).eye;best=std::min(best,std::max(std::abs(eye.x-a.x),std::abs(eye.z-a.z)));
                        awake+=s.peek(m+0x47,1)!=0xf8;
                    }
                    std::printf("   spyro %.1f %.1f %.1f: eye-anchor %.2f awake ticks %u state %d\n",floor->point.x,floor->point.y,floor->point.z,best,awake,int(static_cast<std::int8_t>(s.peek(m+0x48,1))));
                }
            }
            if(std::getenv("LIGHTS_OUT")) {
                // Idol Springs' 1E5 tiles: standing on one toggles its lit bit
                // (param+0) and two linked tiles' (param+4/+8 moby indices). The
                // ring is solved once every tile is lit (+49 reaches 99).
                std::vector<unsigned> tiles;
                for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==0x1e5)tiles.push_back(i);
                const auto params=[&](unsigned i){return s.peek(s.moby_address(i));};
                const auto index_of=[&](unsigned object){return static_cast<unsigned>(std::find(tiles.begin(),tiles.end(),object)-tiles.begin());};
                unsigned start{},goal{};std::vector<unsigned> move(tiles.size());
                for(unsigned k=0;k<tiles.size();++k) {
                    const auto p=params(tiles[k]);
                    if(s.peek(p,2)&1)start|=1u<<k;
                    goal|=1u<<k;
                    move[k]=1u<<k;
                    for(const auto link:{s.peek(p+4),s.peek(p+8)})if(const auto j=index_of(link);j<tiles.size())move[k]|=1u<<j;
                    std::printf("  tile %u: lit %u target %u links %u %u\n",tiles[k],s.peek(p,2),s.peek(p+2,2),s.peek(p+4),s.peek(p+8));
                }
                // Breadth-first search over lit patterns.
                std::map<unsigned,std::pair<unsigned,unsigned>> seen{{start,{UINT32_MAX,UINT32_MAX}}};std::vector<unsigned> queue{start};
                for(std::size_t q=0;q<queue.size() && !seen.contains(goal);++q)
                    for(unsigned k=0;k<tiles.size();++k){const auto next=queue[q]^move[k];if(seen.try_emplace(next,queue[q],k).second)queue.push_back(next);}
                std::vector<unsigned> plan;
                for(auto at=goal;seen.contains(at) && seen[at].first!=UINT32_MAX;at=seen[at].first)plan.insert(plan.begin(),seen[at].second);
                std::printf("  solution (%zu steps):",plan.size());for(const auto k:plan)std::printf(" %u",tiles[k]);std::printf("\n");
                const auto off=g.player.position;
                std::map<unsigned,unsigned> nodes;for(const auto& m:g.level.terrain_morphs)if(m.collision)nodes[m.group]=m.node;
                const auto rest=[&](unsigned n){for(unsigned t=0;t<n;++t){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}};
                if(std::getenv("UNSOLVED"))plan.clear();
                for(const auto k:plan) {
                    const auto c=std::find_if(g.level.object_colliders.begin(),g.level.object_colliders.end(),[&](const auto& x){return x.object==tiles[k];});
                    if(c==g.level.object_colliders.end())continue;
                    g.player.position=g.player.previous_position={c->bounds.center().x,c->bounds.max.y+.05f,c->bounds.center().z};g.player.velocity={};rest(30);
                    g.player.position=g.player.previous_position=off;
                    g.player.velocity={};rest(40);
                }
                std::vector<unsigned> states(level.objects.size());
                for(unsigned i=0;i<states.size();++i)states[i]=s.peek(s.moby_address(i)+0x48,1);
                const auto orbs=g.player.orbs;rest(300);
                for(unsigned i=0;i<states.size();++i)if(g.retail_objects[i].type!=0x1e5 && states[i]!=s.peek(s.moby_address(i)+0x48,1))
                    std::printf("  moby %u class %x state %u -> %u\n",i,g.retail_objects[i].type,states[i],s.peek(s.moby_address(i)+0x48,1));
                std::printf("  lit/solved after:");for(const auto i:tiles)std::printf(" %u/%u",s.peek(params(i),2),s.peek(s.moby_address(i)+0x49,1));std::printf("\n");
                std::printf("  orbs %u -> %u, awarded %u\n",orbs,g.player.orbs,s.orbs_awarded);
                for(const auto& m:g.level.terrain_morphs)if(m.collision && nodes.contains(m.group) && nodes[m.group]!=m.node)std::printf("  group %u node %u -> %u\n",m.group,nodes[m.group],m.node);
                // Then press triangle beside every other moby near the puzzle: its
                // prize comes from the foreman's script conversation.
                std::vector<unsigned> near;
                const auto centre=g.retail_objects[tiles.front()].position;
                for(unsigned i=0;i<level.objects.size();++i)
                    if(g.retail_objects[i].type!=0x1e5 && (length(g.retail_objects[i].position-centre)<40 || g.retail_objects[i].type==0x418 || g.retail_objects[i].type==0x41b || g.retail_objects[i].type==0x124))near.push_back(i);
                std::sort(near.begin(),near.end(),[&](unsigned a,unsigned b){return length(g.retail_objects[a].position-centre)<length(g.retail_objects[b].position-centre);});
                for(unsigned pass=0;pass<2;++pass)for(const auto object:near) {
                    const auto at=g.retail_objects[object].position;
                    std::optional<RayHit> floor;
                    for(const float r:{1.6f,2.6f})for(const Vec3 d:{Vec3{r,0,0},Vec3{-r,0,0},Vec3{0,0,r},Vec3{0,0,-r}})if(!floor)floor=g.level.raycast(at+d+Vec3{0,2,0},{0,-1,0},6);
                    if(!floor){if(g.retail_objects[object].type==0x124)std::printf("  moby %u: no floor\n",object);continue;}
                    g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(at.x-floor->point.x,at.z-floor->point.z);
                    g.camera.reset(g.level,g.player.position,g.player.yaw);rest(20);
                    const auto before=g.player.orbs;unsigned lines{};
                    if(object==197 || object==267)std::printf("  at moby %u (%.1f %.1f %.1f): spyro (%.1f %.1f %.1f) grounded %d water %d\n",object,at.x,at.y,at.z,
                        g.player.position.x,g.player.position.y,g.player.position.z,int(g.player.grounded),int(g.player.swim.mode!=WaterMode::Dry));
                    if(object==197 || object==267) {
                        const auto m=s.moby_address(object),p=s.peek(m);
                        std::printf("    state %u sub %u drawn %u flag %02x | params %08x %08x %08x %08x %08x %08x\n",s.peek(m+0x48,1),s.peek(m+0x49,1),s.peek(m+0x4d,1),s.peek(m+0x47,1),
                            s.peek(p),s.peek(p+4),s.peek(p+8),s.peek(p+12),s.peek(p+16),s.peek(p+20));
                    }
                    for(unsigned t=0;t<1500;++t) {
                        InputState in{};in.dialogue.interact=!lines && t%30==2 && !g.dialogue.active();
                        if(g.dialogue.active() && t%15==0){const auto* line=g.dialogue.line();++lines;if(line)std::printf("    [%u] %.110s\n",object,line->text.c_str());if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;}
                        g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);
                        if((lines && !g.dialogue.active()) || (!lines && t>300))break;
                    }
                    if(lines)std::printf("  talk moby %u class %x: %u lines, orbs %u -> %u (awarded %u), flags 8006abf0: %08x %08x\n",object,g.retail_objects[object].type,lines,before,g.player.orbs,s.orbs_awarded,s.peek(0x8006abf0),s.peek(0x8006abf4+4));
                    if(lines) {
                        const auto from=g.retail_objects[object].position;rest(600);
                        const auto to=g.retail_objects[object].position;
                        std::printf("  moby %u moved (%.1f %.1f %.1f) -> (%.1f %.1f %.1f), state %u\n",object,from.x,from.y,from.z,to.x,to.y,to.z,s.peek(s.moby_address(object)+0x48,1));
                    }
                }
            }
            if(std::getenv("ELEMENT_RATES")) {
                // Measured on the live game: energy refill, passive Fury gain and
                // how long each element's Fury runs (pose + staged effects).
                g.upgrades.enabled=true;
                for(auto& t:g.upgrades.elements.tiers)t=3;
                const auto settle=[&]{for(unsigned t=0;t<30;++t){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}};
                g.upgrades.elements.equipped=Element::Fire;settle();
                g.upgrades.elements.energy=0;unsigned refill=0;
                while(g.upgrades.elements.energy<99.9f && refill<600){g.advance(FixedClock::step,{});++refill;}
                std::printf("  energy 0->100: %u ticks (%.2f s)\n",refill,refill/60.0f);
                g.upgrades.elements.fury=0;for(unsigned t=0;t<60;++t)g.advance(FixedClock::step,{});
                std::printf("  passive Fury after 1 s: %.2f\n",g.upgrades.elements.fury);
                for(const auto e:{Element::Ice,Element::Fire,Element::Water,Element::Ice,Element::Electric,Element::Convexity}) {
                    g.upgrades.elements.equipped=e;g.upgrades.elements.fury=100;settle();g.upgrades.elements.fury=100;
                    InputState in;in.fury_pressed=true;g.advance(FixedClock::step,in);
                    unsigned ticks=1;bool started=false;
                    while(ticks<3600) {
                        g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});++ticks;
                        const bool live=g.elements.fury_pose() || g.elements.fury().stage || g.elements.dark_fury();
                        if(live)started=true;else if(started)break;
                    }
                    std::printf("  %s Fury: %s, %u ticks (%.1f s)\n",element_name(e),started?"ran":"did not start",ticks,ticks/60.0f);
                    for(unsigned t=0;t<240;++t){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}
                }
            }
            if(const char* sweep=std::getenv("GROUP_SWEEP")) {
                // Knock every member of `class` cluster by cluster, as a player
                // sweeping the level; log all members after each cluster.
                const auto type=static_cast<unsigned>(std::stoul(sweep,nullptr,16));
                std::vector<unsigned> members;
                for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==type)members.push_back(i);
                std::vector<std::vector<unsigned>> clusters;
                for(const auto i:members) {
                    auto c=std::find_if(clusters.begin(),clusters.end(),[&](const auto& k){return length(g.retail_objects[k.front()].position-g.retail_objects[i].position)<16;});
                    if(c==clusters.end())clusters.push_back({i});else c->push_back(i);
                }
                // FLY=gate: unlock that power gate and keep Spyro flying (mode 6).
                const unsigned gate=std::getenv("FLY")?static_cast<unsigned>(std::stoul(std::getenv("FLY"))):UINT32_MAX;
                const auto fly=[&]{if(gate==UINT32_MAX)return;if(g.retail_objects[gate].powerup_gate){g.retail_objects[gate].powerup_gate->requirement=0;g.retail_objects[gate].powerup_gate->unlocked=true;}
                    g.cheats.superfly=true;g.powers.flight={2400,2400};
                    if(!g.powers.flying){g.player.position.y+=1.5f;g.player.previous_position=g.player.position;g.player.grounded=false;g.powers.flying=true;}};
                const auto log=[&](const char* when){std::printf("  %s:",when);
                    for(const auto i:members)std::printf(" %u:%d",i,int(static_cast<std::int8_t>(s.peek(s.moby_address(i)+0x48,1))));std::printf("\n");};
                for(const auto& cluster:clusters) {
                    Vec3 centre{};for(const auto i:cluster)centre+=g.retail_objects[i].position;centre=centre/static_cast<float>(cluster.size());
                    if(const auto floor=g.level.raycast(centre+Vec3{0,3,0},{0,-1,0},10))g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};
                    g.camera.reset(g.level,g.player.position,g.player.yaw);
                    for(unsigned t=1;t<=90;++t) {
                        g.player.health=std::max(g.player.health,3u);fly();
                        if(t%12==1)for(const auto i:cluster)if(g.retail_objects[i].state>=0)g.retail_objects[i].attack_flags|=flame_attack;
                        g.advance(FixedClock::step,{});
                    }
                    log("cluster done");
                }
                for(unsigned t=0;t<300;++t){g.player.health=std::max(g.player.health,3u);fly();g.advance(FixedClock::step,{});}
                log("after wait");
                std::printf("  motion %d mode-6 flight, c6 flags: ",int(g.player.motion));
                for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==0xc6)std::printf(" %u:+38=%u",i,s.peek(s.peek(s.moby_address(i))+0x38));
                std::printf("\n");
                std::printf("  spirits %u\n",g.spirits());
            }
            if(const char* group=std::getenv("GROUP_STRIKE")) {
                // GROUP_STRIKE=class,controller: flame every member of `class` near
                // the controller object at once; log their states over time.
                unsigned type{},ctl{};std::sscanf(group,"%x,%u",&type,&ctl);
                const auto at=g.retail_objects[ctl].position;
                std::vector<unsigned> members;
                for(unsigned i=0;i<level.objects.size();++i)if(g.retail_objects[i].type==type && length(g.retail_objects[i].position-at)<20)members.push_back(i);
                if(const auto floor=g.level.raycast(at+Vec3{1.5f,3,0},{0,-1,0},8))g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};
                g.camera.reset(g.level,g.player.position,g.player.yaw);
                const auto states=[&](unsigned t){std::printf("  t%u ctl %d:",t,int(static_cast<std::int8_t>(s.peek(s.moby_address(ctl)+0x48,1))));
                    for(const auto i:members)std::printf(" %u:%d",i,int(static_cast<std::int8_t>(s.peek(s.moby_address(i)+0x48,1))));std::printf("\n");};
                for(unsigned t=0;t<20;++t)g.advance(FixedClock::step,{});
                states(0);
                for(unsigned t=1;t<=600;++t) {
                    g.player.health=std::max(g.player.health,3u);
                    if(t<=60 && t%12==1)for(const auto i:members)g.retail_objects[i].attack_flags|=flame_attack;
                    g.advance(FixedClock::step,{});
                    if(t%30==0)states(t);
                }
                // Group lists (800670e0 -> 32 lists of halfword moby indices, high bit ends).
                const auto lists=s.peek(0x800670e0);
                for(unsigned gl=0;gl<32 && lists;++gl) {
                    auto entry=s.peek(lists+gl*4);if(!entry)continue;
                    std::printf("  group %u:",gl);
                    for(unsigned guard=0;guard<256;++guard) {
                        const auto v=static_cast<std::int16_t>(s.peek(entry,2));entry+=2;
                        const auto idx=static_cast<unsigned>(v&0x7fff);
                        std::printf(" %u(%x:%d)",idx,s.peek(s.moby_address(idx)+0x36,2),int(static_cast<std::int8_t>(s.peek(s.moby_address(idx)+0x48,1))));
                        if(v<0)break;
                    }
                    std::printf("\n");
                }
            }
            if(const char* dump=std::getenv("DUMP")) {
                const auto at=static_cast<std::uint32_t>(std::stoul(dump,nullptr,16));
                for(unsigned k=0;k<0x60;k+=4)std::printf("  %08x: %08x (%d)\n",at+k,s.peek(at+k),int(s.peek(at+k)));
            }
            if(const char* find=std::getenv("FIND_PARAMS")) {
                // Which moby's parameter block (moby+0) is, or contains, this address?
                const auto want=static_cast<std::uint32_t>(std::stoul(find,nullptr,16));
                for(unsigned i=0;i<level.objects.size();++i) {
                    const auto p=s.peek(s.moby_address(i));
                    if(p<=want && want<p+0x80)std::printf("  params of obj %u class %x at %08x (+%x)\n",i,g.retail_objects[i].type,p,want-p);
                }
            }
            if(const char* undrawn=std::getenv("UNDRAWN")) {
                // UNDRAWN=ticks: script mobys the retail renderer skips (+4C <= 0) after
                // the given ticks, by class, against those it draws.
                for(unsigned t=0,n=static_cast<unsigned>(std::stoul(undrawn));t<n;++t){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}
                std::map<unsigned,std::pair<unsigned,unsigned>> classes;
                for(const auto& o:g.retail_objects)if(o.retail_script && o.state>=0 && !o.collected)(o.script_undrawn?classes[o.type].first:classes[o.type].second)++;
                const auto* models=g.object_models();
                std::printf("  undrawn:");for(const auto& [type,n]:classes)if(n.first)std::printf(" %x%s %u/%u",type,models && models->models.contains(type)?"*":"",n.first,n.first+n.second);std::printf("\n");
            }
            if(const char* near=std::getenv("BIG_NEAR")) {
                // BIG_NEAR=x,y,z[,radius,ticks]: objects whose posed model spans more than
                // two units, within the radius of the point, after the given ticks.
                Vec3 p{};float radius=30;unsigned ticks=0;std::sscanf(near,"%f,%f,%f,%f,%u",&p.x,&p.y,&p.z,&radius,&ticks);
                for(unsigned t=0;t<ticks;++t)g.advance(FixedClock::step,{});
                const auto* models=g.object_models();
                for(const auto& o:g.retail_objects) {
                    if(o.state<0 || o.collected || o.hidden || length(o.position-p)>radius)continue;
                    const auto bank=models->models.find(o.type);if(bank==models->models.end())continue;
                    Bounds b;
                    if(bank->second.animated){const auto pose=sample_object_animation(bank->second,o.animation,models->animation_blends);for(const auto v:pose.vertices)b.add(v*object_model_scale(o));}
                    else if(o.model<bank->second.meshes.size())for(const auto v:bank->second.meshes[o.model].vertices)b.add(v);
                    const auto e=b.max-b.min;
                    if(std::max({e.x,e.y,e.z})>2)std::printf("  big obj %u class %x at %.1f %.1f %.1f extent %.1f %.1f %.1f clip %u frame %u rot %.0f %.0f %.0f script %d +4C %u\n",o.id,o.type,o.position.x,o.position.y,o.position.z,
                        e.x,e.y,e.z,unsigned(o.animation.clip),unsigned(o.animation.frame),o.rotation[0],o.rotation[1],o.rotation[2],int(o.retail_script),o.id<level.objects.size()?s.peek(s.moby_address(o.id)+0x4c,1):0u);
                }
            }
            if(const char* which=std::getenv("REGION")) {
                // REGION=n[,ticks]: the scene region's drawn triangles (material,
                // blend, corners) at load and after the given ticks.
                unsigned region=0,ticks=0;std::sscanf(which,"%u,%u",&region,&ticks);
                const auto list=[&](const char* label) {
                    std::printf("  region %u %s:\n",region,label);
                    for(std::size_t i=0;i+2<g.level.visual_vertices.size();i+=3) {
                        const auto& a=g.level.visual_vertices[i];if(a.source_region!=region)continue;
                        const auto& b=g.level.visual_vertices[i+1];const auto& c=g.level.visual_vertices[i+2];
                        const auto& m=g.level.terrain_materials.at(a.material);
                        std::printf("   tri %zu mat %u%s blend %u v%u,%u,%u (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f) rgb %.2f %.2f %.2f\n",i/3,a.material,m.translucent?" translucent":"",m.blend_mode,
                            a.source_vertex,b.source_vertex,c.source_vertex,a.position.x,a.position.y,a.position.z,b.position.x,b.position.y,b.position.z,c.position.x,c.position.y,c.position.z,a.color.x,a.color.y,a.color.z);
                    }
                };
                list("at load");
                for(unsigned t=0;t<ticks;++t)g.advance(FixedClock::step,{});
                if(ticks)list("later");
            }
            if(const char* grid=std::getenv("HEIGHTS")) {
                // Floor height grid around x,z (radius, step); '~' marks water above the floor.
                float cx{},cz{},radius=6,stepping=1;std::sscanf(grid,"%f,%f,%f,%f",&cx,&cz,&radius,&stepping);
                for(float z=cz+radius;z>=cz-radius-.01f;z-=stepping) {
                    std::printf("  z%6.1f:",z);
                    for(float x=cx-radius;x<=cx+radius+.01f;x+=stepping) {
                        const auto f=g.level.raycast({x,40,z},{0,-1,0},80);
                        if(!f){std::printf("   ---");continue;}
                        const bool wet=g.level.water_at(f->point+Vec3{0,.2f,0}).has_value();
                        std::printf(" %5.1f%c",f->point.y,wet?'~':' ');
                    }
                    std::printf("\n");
                }
            }
            if(const char* zoe=std::getenv("ZOE_APPROACH")) {
                // Walk up to a Zoe (class 110) from dry ground ~10 units away,
                // camera following, then press talk; report her checkpoint.
                const auto i=static_cast<unsigned>(std::stoul(zoe));const auto at=g.retail_objects[i].position;
                bool placed=false;
                const int only=std::getenv("ZOE_ANGLE")?std::atoi(std::getenv("ZOE_ANGLE")):-1;
                for(float r:{4.f,6.f,8.f,10.f,12.f})for(unsigned k=0;k<16 && !placed;++k) {
                    if(only>=0 && int(k)!=only)continue;
                    const float a=k*std::numbers::pi_v<float>/8;
                    const auto floor=g.level.raycast(at+Vec3{std::sin(a)*r,4,std::cos(a)*r},{0,-1,0},10);
                    if(!floor || floor->normal.y<.75f || std::abs(floor->point.y-at.y)>1.5f || g.level.water_at(floor->point+Vec3{0,.3f,0}))continue;
                    g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(at.x-floor->point.x,at.z-floor->point.z);g.camera.reset(g.level,g.player.position,g.player.yaw);placed=true;
                }
                if(const char* from=std::getenv("ZOE_START")) {
                    Vec3 p{};std::sscanf(from,"%f,%f,%f",&p.x,&p.y,&p.z);
                    g.player.position=g.player.previous_position=p;g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(at.x-p.x,at.z-p.z);g.camera.reset(g.level,g.player.position,g.player.yaw);placed=true;
                }
                if(!placed)std::printf("  no dry floor near Zoe %u\n",i);
                const auto before=s.checkpoints;unsigned walked=0;const auto start=g.player.position;
                std::printf("  Zoe %u at %.2f %.2f %.2f; start %.2f %.2f %.2f\n",i,at.x,at.y,at.z,start.x,start.y,start.z);
                for(unsigned t=0;t<600;++t) {
                    InputState in;const auto d=at-g.player.position;const float n=std::hypot(d.x,d.z);
                    if(n>1.4f){in.move_x=d.x/n;in.move_z=d.z/n;++walked;}else in.dialogue.interact=t%20==0;
                    if(g.dialogue.active())in.dialogue.next=t%15==0;
                    const auto contacts=s.solid_contacts;
                    g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);
                    if(t%100==99)std::printf("   t%u at %.2f %.2f %.2f motion %d solid contacts +%u\n",t,g.player.position.x,g.player.position.y,g.player.position.z,
                        int(g.player.motion),s.solid_contacts-contacts);
                }
                const auto m=s.moby_address(i);
                std::printf("  Zoe %u: walked %u ticks, distance %.2f, checkpoints %u -> %u, state %u fade %02x draw %02x drawn %u dialogues %u\n",i,walked,
                    length(at-g.player.position),before,s.checkpoints,s.peek(m+0x48,1),s.peek(m+0x4f,1),s.peek(m+0x4c,1),s.peek(m+0x4d,1),s.dialogues);
            }
            if(const char* list=std::getenv("TRACE_OBJ")) {
                // States and positions of the listed records every 30 ticks, Spyro left alone.
                std::vector<unsigned> ids;{std::string text(list);std::size_t p=0;while(p<text.size()){ids.push_back(static_cast<unsigned>(std::stoul(text.substr(p))));p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                const unsigned ticks=std::getenv("TRACE_TICKS")?static_cast<unsigned>(std::stoul(std::getenv("TRACE_TICKS"))):600;
                if(const char* at=std::getenv("TRACE_SPYRO")) {   // park Spyro here (camera behind), standing still
                    Vec3 p{};std::sscanf(at,"%f,%f,%f",&p.x,&p.y,&p.z);
                    if(const auto f=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8))p=f->point+Vec3{0,.02f,0};
                    g.player.position=g.player.previous_position=p;g.player.velocity={};
                    const auto target=g.retail_objects[ids.front()].position;
                    g.player.yaw=g.player.previous_yaw=std::atan2(target.x-p.x,target.z-p.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                }
                std::vector<unsigned> targets;   // TRACE_HIT: records struck every 20 ticks from tick 60
                if(const char* hits=std::getenv("TRACE_HIT")){std::string text(hits);std::size_t p=0;while(p<text.size()){targets.push_back(static_cast<unsigned>(std::stoul(text.substr(p))));p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                for(unsigned t=0;t<=ticks;++t) {
                    if(t>=60 && t%20==0)for(const auto h:targets)if(g.retail_objects[h].state>=0)g.retail_objects[h].attack_flags|=superflame_attack;
                    if(t%30==0) {
                        std::printf("  t%u",t);
                        for(const auto id:ids){const auto m=s.moby_address(id);const auto& o=g.retail_objects[id];
                            std::printf(" | %u:%x st%u (%.1f %.1f %.1f) p28 %x p2c %08x 4c %02x anim %08x",id,o.type,s.peek(m+0x48,1),o.position.x,o.position.y,o.position.z,s.peek(s.peek(m)+0x28),s.peek(s.peek(m)+0x2c),s.peek(m+0x4c,1),s.peek(m+0x3c));}
                        std::printf("\n");
                    }
                    g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});
                }
            }
            if(const char* push=std::getenv("PUSH")) {
                // PUSH=diode GATE=socket ROBOTS=a,b: defeat the robots playing keep-away
                // with the diode, then walk it into its socket from behind.
                const auto d=static_cast<unsigned>(std::stoul(push));const auto dm=s.moby_address(d);
                const auto gi=static_cast<unsigned>(std::stoul(std::getenv("GATE")?std::getenv("GATE"):"0"));
                const auto socket=g.retail_objects[gi].position;
                std::vector<unsigned> robots;if(const char* r=std::getenv("ROBOTS")){std::string text(r);std::size_t p=0;while(p<text.size()){robots.push_back(static_cast<unsigned>(std::stoul(text.substr(p))));p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                const auto park=[&](Vec3 p,Vec3 face) {
                    if(const auto f=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8))p=f->point+Vec3{0,.02f,0};
                    g.player.position=g.player.previous_position=p;g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(face.x-p.x,face.z-p.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                };
                park(g.retail_objects[d].position+Vec3{0,0,-6},g.retail_objects[d].position);
                for(unsigned t=0;t<240;++t){if(t>=40 && t%20==0)for(const auto h:robots)if(g.retail_objects[h].state>=0)g.retail_objects[h].attack_flags|=superflame_attack;
                    g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}
                std::printf("  robots down; diode at %.2f %.2f %.2f state %u, socket %.2f %.2f %.2f\n",g.retail_objects[d].position.x,g.retail_objects[d].position.y,g.retail_objects[d].position.z,
                    s.peek(dm+0x48,1),socket.x,socket.y,socket.z);
                const auto gp=s.peek(s.moby_address(gi));
                for(unsigned round=0;round<10 && !s.peek(gp+0x10);++round) {
                    const auto at=g.retail_objects[d].position;auto dir=socket-at;dir.y=0;
                    if(length(dir)<.05f)break;dir=normalized(dir);
                    park(at-dir*1.6f,at);
                    for(unsigned t=0;t<90;++t){InputState in;if(t<45){in.move_x=dir.x*.5f;in.move_z=dir.z*.5f;in.charge=std::getenv("PUSH_CHARGE")!=nullptr && t<30;}g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);}
                    std::printf("  push %u: diode %.2f %.2f %.2f state %u, %.2f from socket, socket params+10 %08x\n",round,g.retail_objects[d].position.x,g.retail_objects[d].position.y,
                        g.retail_objects[d].position.z,s.peek(dm+0x48,1),std::hypot(g.retail_objects[d].position.x-socket.x,g.retail_objects[d].position.z-socket.z),s.peek(gp+0x10));
                }
                for(unsigned t=0;t<200;++t){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});}
                std::printf("  socket %u params+10 %08x group %u\n",gi,s.peek(gp+0x10),s.peek(gp+0x30));
                for(const auto& mm:g.level.terrain_morphs)if(mm.group==s.peek(gp+0x30))std::printf("   group %u kind %u node %u\n",mm.group,mm.kind,mm.node);
            }
            if(const char* fetch=std::getenv("FETCH")) {
                // FETCH=diode:socket,... [ROBOTS=a,b] [SPIT=distance]: defeat the
                // robots, then for each pair walk up to the diode facing it until
                // Spyro picks it up, carry it (teleporting when far) to SPIT units
                // short of the socket and spit it (Circle).
                std::vector<std::pair<unsigned,unsigned>> pairs;
                if(std::string(fetch)!="auto"){std::string text(fetch);std::size_t p=0;while(p<text.size()){const auto a=static_cast<unsigned>(std::stoul(text.substr(p)));const auto colon=text.find(':',p);
                    const auto b=colon!=std::string::npos && colon<text.find(',',p)?static_cast<unsigned>(std::stoul(text.substr(colon+1))):static_cast<unsigned>(std::stoul(std::getenv("GATE")?std::getenv("GATE"):"0"));
                    pairs.push_back({a,b});p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                std::vector<unsigned> robots;if(const char* r=std::getenv("ROBOTS")){std::string text(r);std::size_t p=0;while(p<text.size()){robots.push_back(static_cast<unsigned>(std::stoul(text.substr(p))));p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                const float spit=std::getenv("SPIT")?std::stof(std::getenv("SPIT")):2.0f;
                const auto park=[&](Vec3 p,Vec3 face) {
                    if(const auto f=g.level.raycast(p+Vec3{0,3,0},{0,-1,0},8))p=f->point+Vec3{0,.02f,0};
                    g.player.position=g.player.previous_position=p;g.player.velocity={};
                    g.player.yaw=g.player.previous_yaw=std::atan2(face.x-p.x,face.z-p.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                };
                const auto step=[&](InputState in){g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);};
                // Robots the scripts keep hidden (+4C = 0) do not animate, so a
                // knocked-down hidden robot never finishes: only hunt visible ones.
                const auto hunt=[&](unsigned h) {
                    if(g.retail_objects[h].state<0 || !static_cast<std::int8_t>(s.peek(s.moby_address(h)+0x4c,1)))return;
                    park(g.retail_objects[h].position+Vec3{0,0,-6},g.retail_objects[h].position);
                    // Stay until it is gone: a knocked-down robot only finishes (and
                    // explodes) while it is active, i.e. near Spyro.
                    unsigned t=0;
                    for(;t<300 && g.retail_objects[h].state>=0;++t){if(t>=20 && t%10==0)g.retail_objects[h].attack_flags|=superflame_attack;
                        if(t%20==0 && std::hypot(g.player.position.x-g.retail_objects[h].position.x,g.player.position.z-g.retail_objects[h].position.z)>4.5f) {
                            // Follow it: stand on floor about three units away, facing it.
                            const auto at=g.retail_objects[h].position;
                            for(unsigned k=0;k<8;++k){const float a=k*.785f;const Vec3 c=at+Vec3{std::sin(a)*3,0,std::cos(a)*3};
                                if(const auto f=g.level.raycast(c+Vec3{0,3,0},{0,-1,0},8);f && std::abs(f->point.y-at.y)<1.5f){park(c,at);break;}}
                        }
                        step({});
                        if(t%30==29) {
                            const auto view=g.camera.sample(1);
                            std::printf("   robot %u t%u state %02x anim %08x blend %02x at %.1f %.1f %.1f, spyro %.1f %.1f %.1f +4D %u +4C %u eye %.1f %.1f %.1f fwd %.2f %.2f %.2f\n",h,t,s.peek(s.moby_address(h)+0x48,1),
                                s.peek(s.moby_address(h)+0x3c),s.peek(s.moby_address(h)+0x40,1),
                                g.retail_objects[h].position.x,g.retail_objects[h].position.y,g.retail_objects[h].position.z,g.player.position.x,g.player.position.y,g.player.position.z,
                                s.peek(s.moby_address(h)+0x4d,1),s.peek(s.moby_address(h)+0x4c,1),view.eye.x,view.eye.y,view.eye.z,view.forward.x,view.forward.y,view.forward.z);
                        }}
                };
                const auto hunt_all=[&]{for(unsigned pass=0;pass<3;++pass)for(const auto h:robots)hunt(h);};
                hunt_all();
                for(unsigned t=0;t<90;++t)step({});
                // Deliver diode d into socket gi: walk up facing it until Spyro takes
                // it, carry it (teleporting when far) to SPIT short and spit it.
                const auto deliver=[&](unsigned d,unsigned gi,unsigned attempts) {
                    const auto dm=s.moby_address(d);const auto socket=g.retail_objects[gi].position;const auto gp=s.peek(s.moby_address(gi));
                    for(unsigned round=0;round<attempts && !s.peek(gp+0x10);++round) {
                        for(unsigned t=0;t<30;++t)step({});
                        const auto at=g.retail_objects[d].position;auto away=at-socket;away.y=0;away=length(away)>.05f?normalized(away):Vec3{0,0,1};
                        {   // Approach on floor level with the diode, preferring the far side from the socket.
                            std::optional<Vec3> from;float best=1e9f;
                            for(unsigned k=0;k<16;++k) {
                                const float a=k*.3927f;const Vec3 c=at+Vec3{std::sin(a)*1.6f,0,std::cos(a)*1.6f};
                                const auto f=g.level.raycast(c+Vec3{0,1.2f,0},{0,-1,0},3);
                                if(!f || std::abs(f->point.y-(at.y-.1f))>.45f)continue;
                                const float score=length(c-(at+away*1.6f));
                                if(score<best){best=score;from=f->point+Vec3{0,.02f,0};}
                            }
                            if(from) {
                                g.player.position=g.player.previous_position=*from;g.player.velocity={};
                                g.player.yaw=g.player.previous_yaw=std::atan2(at.x-from->x,at.z-from->z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                            } else park(at+away*1.6f,at);
                        }
                        const auto picked=s.pickups;unsigned t=0;
                        for(;t<120 && s.pickups==picked;++t) {
                            InputState in;const auto v=g.retail_objects[d].position-g.player.position;const float n=std::hypot(v.x,v.z);
                            if(n>.75f){in.move_x=v.x/n*.4f;in.move_z=v.z/n*.4f;}
                            step(in);
                        }
                        std::printf("  %u->%u round %u: diode state %u at %.2f %.2f %.2f, picked up %s after %u ticks (head %u)\n",d,gi,round,s.peek(dm+0x48,1),at.x,at.y,at.z,
                            s.pickups>picked?"yes":"no",t,s.peek(0x8006a15c));
                        if(s.pickups==picked) {
                            std::printf("   not picked: spyro %.2f %.2f %.2f yaw %.2f, diode params+20 %08x, robots holding:",g.player.position.x,g.player.position.y,g.player.position.z,
                                g.player.yaw,s.peek(s.peek(dm)+0x20));
                            for(const auto h:robots)std::printf(" %u:st%d",h,int(g.retail_objects[h].state));
                            std::printf("%c",10);
                            continue;
                        }
                        {auto v=g.player.position-socket;v.y=0;if(length(v)>spit+4)park(socket+normalized(v)*(spit+2.5f),socket);}
                        for(unsigned k=0;k<120;++k) {
                            InputState in;auto v=socket-g.player.position;v.y=0;const float n=length(v);
                            if(n<=spit)break;
                            in.move_x=v.x/n*.6f;in.move_z=v.z/n*.6f;step(in);
                        }
                        if(auto v=socket-g.player.position;std::hypot(v.x,v.z)>spit+.5f) {
                            // Blocked: stand at a level spot SPIT from the socket with a clear line to it.
                            std::optional<Vec3> spot;float best=1e9f;
                            for(unsigned k=0;k<24;++k) {
                                const float a=k*.2618f;const Vec3 c=socket+Vec3{std::sin(a)*spit,0,std::cos(a)*spit};
                                const auto f=g.level.raycast(c+Vec3{0,1.5f,0},{0,-1,0},4);
                                if(!f || std::abs(f->point.y-socket.y)>.8f)continue;
                                const auto eye=f->point+Vec3{0,.5f,0},line=socket+Vec3{0,.5f,0}-eye;
                                if(const auto hit=g.level.raycast(eye,line,length(line));hit && hit->distance<length(line)-.2f)continue;
                                const float n=length(f->point-g.player.position);if(n<best){best=n;spot=f->point+Vec3{0,.02f,0};}
                            }
                            if(spot){g.player.position=g.player.previous_position=*spot;g.player.velocity={};g.camera.reset(g.level,g.player.position,g.player.yaw);}
                        }
                        {auto v=socket-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(v.x,v.z);}
                        for(unsigned k=0;k<4;++k)step({});
                        {InputState in;in.flame_pressed=true;step(in);}
                        for(unsigned k=0;k<150;++k)step({});
                        std::printf("   spat: diode state %u at %.2f %.2f %.2f, %.2f from socket, socket params+10 %08x\n",s.peek(dm+0x48,1),g.retail_objects[d].position.x,
                            g.retail_objects[d].position.y,g.retail_objects[d].position.z,length(g.retail_objects[d].position-socket),s.peek(gp+0x10));
                    }
                    if(s.carrying()){InputState in;in.flame_pressed=true;step(in);}
                };
                if(std::string(fetch)=="auto") {
                    // The whole quest: every class 35 socket, every class 34 diode; robots
                    // (ROBOTS, or group 1) pull diodes back out until they are defeated.
                    std::vector<unsigned> diodes,sockets;
                    for(const auto& o:g.retail_objects){if(o.type==0x34 && o.id<level.objects.size())diodes.push_back(o.id);if(o.type==0x35 && o.id<level.objects.size())sockets.push_back(o.id);}
                    if(robots.empty()) {
                        const auto list=s.peek(s.peek(0x800670e0)+4);
                        for(unsigned k=0;k<512;++k){const auto e=s.peek(list+k*2,2);robots.push_back(e&0x7fff);if(e&0x8000)break;}
                    }
                    for(unsigned round=0;round<12;++round) {
                        hunt_all();
                        std::set<std::uint32_t> socketed;std::vector<unsigned> empty;
                        for(const auto gi:sockets){const auto v=s.peek(s.peek(s.moby_address(gi))+0x10);if(v)socketed.insert(v);else empty.push_back(gi);}
                        unsigned alive=0;for(const auto h:robots)alive+=s.peek(s.moby_address(h)+0x48,1)<0x80;
                        std::printf("  auto round %u: %zu/%zu sockets powered, %u robots alive\n",round,sockets.size()-empty.size(),sockets.size(),alive);
                        if(empty.empty() && !alive)break;
                        for(const auto gi:empty) {
                            unsigned best=UINT32_MAX;float nearest=1e9f;
                            for(const auto d:diodes) {
                                const auto m=s.moby_address(d);
                                if(socketed.contains(m) || s.peek(m+0x48,1)>=0x80 || s.peek(s.peek(m)+0x20))continue;
                                const float n=length(g.retail_objects[d].position-g.retail_objects[gi].position);
                                if(n<nearest){nearest=n;best=d;}
                            }
                            if(best==UINT32_MAX){std::printf("   no free diode for socket %u\n",gi);continue;}
                            deliver(best,gi,3);socketed.insert(s.moby_address(best));
                            hunt_all();
                        }
                    }
                    for(unsigned t=0;t<120;++t)step({});
                }
                for(const auto [d,gi]:pairs) {
                    const auto dm=s.moby_address(d);const auto socket=g.retail_objects[gi].position;const auto gp=s.peek(s.moby_address(gi));
                    std::vector<std::uint32_t> reach_before;for(unsigned i=0;i<level.objects.size();++i)reach_before.push_back(s.peek(s.moby_address(i)+0x4c,1));
                    deliver(d,gi,6);
                    hunt_all();
                    for(unsigned t=0;t<120;++t)step({});
                    std::vector<unsigned> revealed;
                    for(unsigned i=0;i<level.objects.size();++i)if(s.peek(s.moby_address(i)+0x4c,1)!=reach_before[i]) {
                        revealed.push_back(i);
                        std::printf("   revealed obj %u class %x +4C %u -> %u at %.1f %.1f %.1f state %d\n",i,g.retail_objects[i].type,reach_before[i],s.peek(s.moby_address(i)+0x4c,1),
                            g.retail_objects[i].position.x,g.retail_objects[i].position.y,g.retail_objects[i].position.z,int(g.retail_objects[i].state));
                    }
                    for(unsigned t=0;t<8;++t) {   // their animation words (+3C clip/next/frame/next frame, +40 blend) and rotation
                        std::printf("   t%u:",t*4);
                        for(const auto i:revealed){const auto m=s.moby_address(i);std::printf(" %u:%08x/%02x r%02x%02x%02x",i,s.peek(m+0x3c),s.peek(m+0x40,1),s.peek(m+0x44,1),s.peek(m+0x45,1),s.peek(m+0x46,1));}
                        std::printf("\n");for(unsigned k=0;k<4;++k)step({});
                    }
                    std::printf("  socket %u params+10 %08x group %u:",gi,s.peek(gp+0x10),s.peek(gp+0x30));
                    for(const auto& mm:g.level.terrain_morphs)if(mm.group==s.peek(gp+0x30)) {
                        std::printf(" kind %u node %u%s",mm.kind,mm.node,mm.color?" colour":mm.collision?" collision":" vertices");
                        if(mm.collision && !mm.frames.empty()) {
                            Bounds b;for(std::size_t k=0;k<mm.frames.front().size()/3;++k){const auto& tri=g.level.triangles.at(mm.first_triangle+k);b.add(tri.a);b.add(tri.b);b.add(tri.c);}
                            std::printf(" [collision now %.1f..%.1f %.1f..%.1f %.1f..%.1f]",b.min.x,b.max.x,b.min.y,b.max.y,b.min.z,b.max.z);
                        }
                    }
                    std::printf("\n");
                }
            }
            if(const char* group=std::getenv("GROUP_LIST")) {
                // GROUP_LIST=n: members of moby group n (800670e0 lists; 80039218 counts
                // those whose state is below 80).
                const auto list=s.peek(s.peek(0x800670e0)+static_cast<std::uint32_t>(std::stoul(group))*4);unsigned alive=0;
                for(unsigned k=0;k<512;++k) {
                    const auto entry=s.peek(list+k*2,2);const auto index=entry&0x7fff;const auto m=s.moby_address(index);
                    const auto state=s.peek(m+0x48,1);alive+=state<0x80;
                    const auto& o=g.retail_objects[index];
                    std::printf("  group %s member %u class %x state %02x at %.1f %.1f %.1f%s\n",group,index,s.peek(m+0x36,2),state,o.position.x,o.position.y,o.position.z,o.enemy?" (native enemy)":"");
                    if(entry&0x8000)break;
                }
                std::printf("  group %s alive %u\n",group,alive);
            }
            if(const char* after=std::getenv("WATCH_AFTER")) {
                // WATCH_AFTER=a,b,...: after the other probes, trace these records (state,
                // +4C, +49) and every terrain group's node for 600 more ticks.
                std::vector<unsigned> ids;{std::string text(after);std::size_t p=0;while(p<text.size()){ids.push_back(static_cast<unsigned>(std::stoul(text.substr(p))));p=text.find(',',p);if(p==std::string::npos)break;++p;}}
                std::map<unsigned,unsigned> nodes;for(const auto& mm:g.level.terrain_morphs)nodes[mm.group]=mm.node;
                for(unsigned t=0;t<600;++t) {
                    g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,{});
                    if(t%60==0) {
                        std::printf("  after t%u:",t);
                        for(const auto id:ids){const auto m=s.moby_address(id);std::printf(" %u st%u 4c%02x 49%02x p14 %u",id,s.peek(m+0x48,1),s.peek(m+0x4c,1),s.peek(m+0x49,1),s.peek(s.peek(m)+0x14));}
                        std::printf(" camera shots %u\n",s.camera_shots);
                    }
                    for(const auto& mm:g.level.terrain_morphs)if(nodes[mm.group]!=mm.node){std::printf("   t%u group %u node %u -> %u\n",t,mm.group,nodes[mm.group],mm.node);nodes[mm.group]=mm.node;}
                }
            }
            if(const char* carry=std::getenv("CARRY")) {
                // Hurricos diodes (class 34): walk up facing one so it is picked
                // up (state 3), then walk it to a socket (class 35, GATE=index).
                const auto d=static_cast<unsigned>(std::stoul(carry));const auto dm=s.moby_address(d);
                const auto gi=static_cast<unsigned>(std::stoul(std::getenv("GATE")?std::getenv("GATE"):"0"));
                const auto socket=g.retail_objects[gi].position;
                const auto walk_to=[&](Vec3 target,unsigned ticks,float stop,const char* label) {
                    for(unsigned t=0;t<ticks;++t) {
                        InputState in;const auto v=target-g.player.position;const float n=std::hypot(v.x,v.z);
                        if(n>stop){in.move_x=v.x/n*.6f;in.move_z=v.z/n*.6f;}
                        g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);
                        if(t%20==19)std::printf("  %s t%u spyro %.2f %.2f %.2f diode %.2f %.2f %.2f state %u a1fc %08x a208 %08x\n",label,t,g.player.position.x,g.player.position.y,g.player.position.z,
                            g.retail_objects[d].position.x,g.retail_objects[d].position.y,g.retail_objects[d].position.z,s.peek(dm+0x48,1),s.peek(0x8006a1fc),s.peek(0x8006a208));
                    }
                };
                const auto at=g.retail_objects[d].position;auto away=at-socket;away.y=0;away=length(away)>.01f?normalized(away):Vec3{0,0,1};
                auto start=at+away*2.2f;
                if(const auto f=g.level.raycast(start+Vec3{0,3,0},{0,-1,0},8))start=f->point+Vec3{0,.02f,0};
                g.player.position=g.player.previous_position=start;g.player.velocity={};
                g.player.yaw=g.player.previous_yaw=std::atan2(at.x-start.x,at.z-start.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
                walk_to(at,120,.7f,"approach");
                walk_to(socket,360,.3f,"carry");
                const auto gp=s.peek(s.moby_address(gi));
                std::printf("  socket %u params+10 %08x group %u\n",gi,s.peek(gp+0x10),s.peek(gp+0x30));
                for(const auto& mm:g.level.terrain_morphs)if(mm.group==s.peek(gp+0x30))std::printf("   group %u kind %u node %u\n",mm.group,mm.kind,mm.node);
            }
            if(const char* refs=std::getenv("REF_SEARCH")) {
                // Which mobys' parameter blocks hold a word naming this object:
                // its index, its moby address or its parameter address.
                const auto object=static_cast<unsigned>(std::stoul(refs));
                const auto moby=s.moby_address(object),own=s.peek(moby);
                for(unsigned i=0;i<level.objects.size();++i) {
                    if(i==object)continue;
                    const auto p=s.peek(s.moby_address(i));
                    if(p<0x80000000u || p>=0x80800000u)continue;
                    for(unsigned k=0;k<0x100;k+=4) {
                        const auto w=s.peek(p+k);
                        if(w==moby || w==own || (k%4==0 && w==object && k>=4))
                            std::printf("  obj %u class %x params+%x = %08x (%s)\n",i,g.retail_objects[i].type,k,w,w==moby?"moby":w==own?"params":"index");
                    }
                }
            }
            if(const char* target=std::getenv("STAND")) {
                // Stand Spyro on a solid moby; report its state and Spyro's support.
                const auto i=static_cast<unsigned>(std::stoul(target));const auto m=s.moby_address(i);
                const auto c=std::find_if(g.level.object_colliders.begin(),g.level.object_colliders.end(),[&](const auto& x){return x.object==i;});
                if(c!=g.level.object_colliders.end()) {
                    const Vec3 top{c->bounds.center().x,c->bounds.max.y+.05f,c->bounds.center().z};
                    g.player.position=g.player.previous_position=top;g.player.velocity={};
                    for(unsigned t=0;t<180;++t) {
                        g.advance(FixedClock::step,{});
                        if(t%20==19)std::printf("  t%u spyro %.2f %.2f %.2f grounded %d moby state %d at %.2f %.2f %.2f a134 %08x\n",t,g.player.position.x,g.player.position.y,g.player.position.z,
                            int(g.player.grounded),int(static_cast<std::int8_t>(s.peek(m+0x48,1))),g.retail_objects[i].position.x,g.retail_objects[i].position.y,g.retail_objects[i].position.z,s.peek(0x8006a134));
                        if(t%20==19)std::printf("     params+0 %04x +2 %04x\n",s.peek(s.peek(m),2),s.peek(s.peek(m)+2,2));
                    }
                } else std::printf("  object %u is not solid\n",i);
            }
            if(const char* target=std::getenv("CHARGE")) {
                // Charge into one object from 2.5 units away, facing it.
                const auto i=static_cast<unsigned>(std::stoul(target));const auto m=s.moby_address(i);std::printf("  charge target moby %08x\n",m);
                const float yaw=std::stof(std::getenv("YAW")?std::getenv("YAW"):"0");
                const Vec3 dir{std::sin(yaw),0,std::cos(yaw)};
                // CHARGE_REPEAT: charge again from behind the object's new position.
                const unsigned repeats=std::getenv("CHARGE_REPEAT")?static_cast<unsigned>(std::stoul(std::getenv("CHARGE_REPEAT"))):1;
                for(unsigned round=0;round<repeats;++round) {
                    if(std::getenv("CLEAR_ATTACK"))s.poke(m+0x18,0);   // experiment: does a stale +18 block later hits?
                    const auto at=g.retail_objects[i].position;
                    auto from=at-dir*2.5f;
                    if(const auto hit=g.level.raycast(from+Vec3{0,3,0},{0,-1,0},8))from=hit->point;
                    g.player.position=g.player.previous_position=from;g.player.velocity={};g.player.yaw=g.player.previous_yaw=yaw;
                    g.camera.reset(g.level,g.player.position,g.player.yaw);
                    for(unsigned t=0;t<150;++t) {
                        InputState in;in.move_x=dir.x;in.move_z=dir.z;in.charge=t<90;g.player.health=std::max(g.player.health,3u);g.advance(FixedClock::step,in);
                        if(t%30==29)std::printf("  r%u t%u spyro %.2f %.2f %.2f object %.2f %.2f %.2f motion %d state %d +18 %08x\n",round,t,g.player.position.x,g.player.position.y,g.player.position.z,
                            g.retail_objects[i].position.x,g.retail_objects[i].position.y,g.retail_objects[i].position.z,int(g.player.motion),int(static_cast<std::int8_t>(s.peek(m+0x48,1))),s.peek(m+0x18));
                    }
                }
                if(const char* gate=std::getenv("GATE")) {
                    const auto gi=static_cast<unsigned>(std::stoul(gate));const auto gp=s.peek(s.moby_address(gi));
                    std::printf("  gate %u params+10 %08x group %u\n",gi,s.peek(gp+0x10),s.peek(gp+0x30));
                    for(const auto& mm:g.level.terrain_morphs)if(mm.group==s.peek(gp+0x30))std::printf("   group %u kind %u node %u\n",mm.group,mm.kind,mm.node);
                }
            }
            if(std::getenv("MORPHS")) {
                // Terrain animation timelines (8001c734's seven directories at
                // 800681d4): record +0 group, +1 flags (2 = stopped), +2 node.
                for(unsigned kind=0;kind<7;++kind) {
                    const auto count=s.peek(0x800681d4+kind*8),list=s.peek(0x800681d8+kind*8);
                    std::printf("  kind %u: %u records",kind,count);
                    for(unsigned i=0;i<count && i<64;++i) {
                        const auto r=s.peek(list+i*4);
                        std::printf(" [g%u f%u n%u]",s.peek(r,1),s.peek(r+1,1),s.peek(r+2,1));
                    }
                    std::printf("\n");
                }
                std::printf("  native morphs %zu skipped %u terrain changes %u door commands %u\n",g.level.terrain_morphs.size(),g.level.skipped_morphs,g.overlay_scripts.terrain_changes,
                    g.overlay_scripts.door_commands);
                for(const auto& m:g.level.terrain_morphs) {
                    constexpr unsigned dir[7]{0,1,4,2,6,5,3};const auto r=s.peek(s.peek(0x800681d8+dir[m.kind]*8)+m.record*4);
                    Bounds b;
                    if(m.collision)for(unsigned i=0;i<m.frames.front().size()/3;++i){const auto& t=g.level.triangles[m.first_triangle+i];b.add(t.a);b.add(t.b);b.add(t.c);}
                    else for(const auto& [out,v]:m.targets){(void)v;b.add(g.level.visual_vertices[out].position);}
                    std::printf("   morph kind %u record %u group %u node %u flags %u steps %zu%s | ram f%u n%u r%u | %.1f %.1f %.1f .. %.1f %.1f %.1f\n",m.kind,m.record,m.group,m.node,m.flags,m.steps.size(),
                        m.steps.size() && m.node<m.steps.size() && m.steps[m.node].hidden?" hidden":"",s.peek(r+1,1),s.peek(r+2,1),s.peek(r+3,1),
                        b.min.x,b.min.y,b.min.z,b.max.x,b.max.y,b.max.z);
                    if(m.collision)std::printf("     triangles %u..%zu\n",m.first_triangle,m.first_triangle+m.frames.front().size()/3-1);
                }
                std::printf("  faults %u %s ticks %u\n",s.faults,s.fault.c_str(),s.script_ticks);
            }
            if(std::getenv("WALK")) {
                // Walk Spyro into every solid moby from the nearest open ground
                // and report whether its triangles stop him.
                const auto colliders=g.level.object_colliders;
                for(const auto& c:colliders) {
                    const auto centre=c.bounds.center();const auto& o=g.retail_objects[c.object];
                    std::printf("  collider obj %u class %x object at %.2f %.2f %.2f bounds %.2f %.2f %.2f .. %.2f %.2f %.2f\n",c.object,o.type,o.position.x,o.position.y,o.position.z,
                        c.bounds.min.x,c.bounds.min.y,c.bounds.min.z,c.bounds.max.x,c.bounds.max.y,c.bounds.max.z);
                    const float reach=std::max(c.bounds.max.x-c.bounds.min.x,c.bounds.max.z-c.bounds.min.z)*.5f+1.6f;
                    std::optional<Vec3> stand;Vec3 dir;
                    for(unsigned k=0;k<16 && !stand;++k) {
                        const float a=k*std::numbers::pi_v<float>/8;const Vec3 at=centre+Vec3{std::sin(a)*reach,0,std::cos(a)*reach};
                        const auto hit=g.level.raycast(at+Vec3{0,3,0},{0,-1,0},6);
                        if(!hit || hit->normal.y<.7f || (hit->triangle&Level::object_triangle) || hit->point.y>c.bounds.max.y-.3f || hit->point.y<c.bounds.min.y-1.5f)continue;
                        stand=hit->point;dir=normalized(Vec3{centre.x-at.x,0,centre.z-at.z});
                    }
                    if(!stand){std::printf("  walk obj %u class %x: no ground beside it\n",c.object,o.type);continue;}
                    g.player.position=g.player.previous_position=*stand+Vec3{0,.05f,0};g.player.velocity={};g.player.yaw=std::atan2(dir.x,dir.z);
                    const auto before=g.overlay_scripts.solid_contacts;float deepest=-1e9f;
                    for(unsigned t=0;t<90;++t) {
                        g.player.health=std::max(g.player.health,3u);
                        InputState in;in.move_x=dir.x;in.move_z=dir.z;g.advance(FixedClock::step,in);
                        deepest=std::max(deepest,dot(g.player.position-centre,dir));
                    }
                    std::printf("  walk obj %u class %x state %d: start %.2f deepest %.2f end %.2f (half %.2f) y %.2f->%.2f contacts %u\n",c.object,o.type,int(o.state),
                        dot(*stand-centre,dir),deepest,dot(g.player.position-centre,dir),reach-1.6f,stand->y,g.player.position.y,g.overlay_scripts.solid_contacts-before);
                }
            }
            // Mobys the engine keeps native lose their bank (+08) at boot. A raw
            // boot shows which of them are solid in the original.
            OverlayVm raw;const auto boot=boot_retail_level(raw,*level.script_image);
            if(!boot.fault.empty())continue;
            for(unsigned i=0;i<boot.count && i<g.retail_objects.size();++i) {
                if(g.retail_objects[i].retail_script)continue;
                const auto m=boot.mobys+i*88;const auto bank=raw.read32(m+8);
                if(bank<0x80000000u || bank>=0x80800000u || !(raw.read32(bank)>>16))continue;
                const auto& o=g.retail_objects[i];
                std::printf("slot %u native obj %u class %x header %08x enemy %d gate %d container %d at %.1f %.1f %.1f\n",slot,i,o.type,raw.read32(bank),
                    int(o.enemy.has_value()),int(o.powerup_gate.has_value()),int(o.container.has_value()),o.position.x,o.position.y,o.position.z);
            }
        }
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}

#include "spyro/travel.hpp"
#include "spyro/savegame.hpp"
#include "spyro/campaign.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(why);}
void ticks(Game& g,unsigned n){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,{});}
Level ground(){Level l;l.wad_slot=15;l.campaign_scene=10;l.bounds={{-20,0,-20},{20,30,20}};l.triangles={{{-20,0,-20},{20,0,20},{20,0,-20}},{{-20,0,-20},{-20,0,20},{20,0,20}}};return l;}
CampaignPortal door(){CampaignPortal p;p.destination=17;p.enabled=true;p.aperture={Vec3{1,0,0},{1,1.7f,0},{0,2,0},{-1,1.7f,0},{-1,0,0}};p.picture={{{0,0,0},{1,0,0}},{{0.5f,1,0},{0,1,0}},{{1,0,0},{0,0,1}}};return p;}
void synthetic() {
    auto p=door();check(portal_crossed(p,{0,0.4f,-8},{0,0.4f,8}),"Fast portal crossing tunneled past trigger");
    check(!portal_crossed(p,{2,0.4f,-1},{2,0.4f,1}),"Outside-arch movement triggered travel");
    check(!portal_crossed(p,{0,3,-1},{0,3,1}),"Above-arch movement triggered travel");
    check(!portal_crossed(p,{0,0.4f,1},{0,0.4f,2}),"Walking parallel to portal triggered travel");
    p.enabled=false;check(!portal_crossed(p,{0,0.4f,-1},{0,0.4f,1}),"Disabled portal allowed travel");p.enabled=true;
    {
        auto labels=ground();auto named=p;named.label="Glimmer";named.label_origin={0,1,0};labels.portals={named};
        const auto text=portal_letters(labels,{5,1,0},0);
        check(text.size()==7 && text.front().model==6 && text.back().model==17,"Portal did not use the original alphabet meshes");
        for(const auto& letter:text)check(letter.type==0x229 && letter.material_color==2 && finite(letter.position),"Portal lettering has an empty material or invalid transform");
        check(portal_letters(labels,{-5,1,0},0).empty(),"One-sided label appeared behind the portal");
        labels.portals[0].label_two_sided=true;check(portal_letters(labels,{-5,1,0},0).size()==7,"Two-sided portal lost its reverse label");
        labels.portals[0].enabled=false;check(portal_letters(labels,{5,1,0},0).empty(),"Closed portal displayed a live label");
    }
    auto l=ground();l.portals={p};Scene s;s.wad_slot=15;Game g(l,s);g.portal_travel.cooldown=0;
    g.player.previous_position={0,0,-1};g.player.position={0,0,1};g.player.gems=23;g.player.health=1;detect_portal_entry(g);
    check(g.portal_travel.phase==PortalTravel::Phase::Departing && g.portal_travel.request.destination==17,"Campaign trigger failed to queue authored destination");
    ticks(g,32);check(g.portal_travel.phase==PortalTravel::Phase::Loading && g.portal_travel.fade==1,"Departure did not complete before loading");
    GameSave checkpoint;checkpoint.capture(g);check(length(checkpoint.levels.at(15).position-g.portal_travel.start)<1e-6f,"Save retained an unsafe mid-transition position");
    check(g.player.gems==23 && g.player.health==1,"Travel modified inventory/health");
    const auto stopped=g.player.position;ticks(g,10);check(length(g.player.position-stopped)<1e-6f,"Player advanced while waiting for loader");
    begin_portal_arrival(g,{15,11});ticks(g,40);check(!g.portal_travel.active() && g.portal_travel.cooldown==90,"Arrival did not restore control/cooldown");
    Game guest(l,s);guest.portal_travel.cooldown=0;guest.portal_travel.allowed=false;guest.player.previous_position={0,0,-1};guest.player.position={0,0,1};detect_portal_entry(guest);
    check(!guest.portal_travel.active(),"Online guest overrode destination ownership");
    auto w=ground();RetailObject o;o.type=0x400;o.rotation[2]=64;w.objects={o};w.whirlwinds={{0,0,0,0,0,6,1,true}};
    {
        auto wind=w.whirlwinds[0];wind.ticks=320;
        const auto before=whirlwind_particles(wind,0),after=whirlwind_particles(wind,1);
        check(before.size()>20 && before.size()<60,"Whirlwind ignored its retail emission rate/lifetime");
        unsigned moving=0;float smallest=10,largest=-10;
        for(const auto& a:before) {
            check(finite(a.offset) && a.offset.y>=0 && a.offset.y<wind.height && std::hypot(a.offset.x,a.offset.z)<=wind.radius+0.01f,"Whirlwind star escaped its authored column");
            if(a.offset.y>0.2f) {
                const float phase=std::remainder(std::atan2(a.offset.z,a.offset.x)-(a.offset.y*retail::units_per_world-64)*retail::angle_to_radians,6.2831853f);
                smallest=std::min(smallest,phase);largest=std::max(largest,phase);
            }
            const auto b=std::find_if(after.begin(),after.end(),[&](const auto& p){return p.birth==a.birth;});
            if(b!=after.end()){check(std::abs(b->offset.y-a.offset.y-32/retail::units_per_world)<0.00001f,"Whirlwind star did not rise at the retail rate");++moving;}
        }
        check(moving>20 && largest-smallest>0.7f,"Whirlwind stars aligned into a shared height-dependent spiral");
        ++wind.ticks;const auto next=whirlwind_particles(wind,0);
        check(next.size()==after.size(),"Whirlwind interpolation changed particle births");
        for(unsigned i=0;i<next.size();++i)check(length(next[i].offset-after[i].offset)<0.00001f,"Whirlwind particles pop between rendered and fixed ticks");
        for(const auto& streak:whirlwind_particles(wind,0,true)) {
            const auto radius=std::hypot(streak.offset.x,streak.offset.z);
            check(radius>=wind.radius*0.5f-0.001f && radius<=wind.radius+0.001f && streak.length<=0.375f && streak.offset.y+streak.length<=wind.height+0.001f,"Whirlwind streak lost its retail radius/ceiling clipping");
        }
        wind.active=false;check(whirlwind_particles(wind,0).empty() && whirlwind_particles(wind,0,true).empty(),"Inactive lift retained particles");
    }
    Game lift(w,s);lift.player.position={0,0,0};float highest=0;bool lifted=false,exited=false;
    for(unsigned i=0;i<700;++i){ticks(lift,1);highest=std::max(highest,lift.player.position.y);lifted|=lift.player.motion==Motion::Whirlwind;if(lifted && lift.whirlwind.index==UINT32_MAX){exited=true;break;}}
    check(lifted && highest>5 && exited,"Whirlwind failed to lift and release Spyro");
    check(lift.player.air.gliding && lift.whirlwind.cooldown>0,"Whirlwind exit did not latch glide/re-entry cooldown");
    auto closed=w;closed.whirlwinds[0].kind=2;closed.whirlwinds[0].active=false;Game gated(closed,s);ticks(gated,30);check(gated.whirlwind.index==UINT32_MAX,"Quest-gated lift activated without its script flag");
    gated.retail_objects[0].state=1;ticks(gated,1);check(gated.whirlwind.index==0,"Quest script cannot enable its lift");
    auto proximity=w;proximity.whirlwinds[0].kind=1;proximity.whirlwinds[0].active=false;Game delayed(proximity,s);ticks(delayed,59);
    check(delayed.whirlwind.index==UINT32_MAX,"Delayed lift started before its authored timer");ticks(delayed,3);check(delayed.whirlwind.index==0,"Proximity lift never became active");
    for(double hz:{30.0,60.0,144.0}){Game rate(w,s);for(unsigned i=0;i<unsigned(hz*2);++i)rate.advance(1/hz,{});check(rate.clock.ticks()==120 && rate.player.position.y>5,"Lift depends on presentation frequency");}
}
void purchase_check(Level level,const ObjectModels& models,unsigned purchase) {
    const auto npc=std::find_if(level.npcs.begin(),level.npcs.end(),[&](const auto& n){return std::any_of(n.lines.begin(),n.lines.end(),[&](const auto& line){return line.purchase==int(purchase) && line.supported;});});
    check(npc!=level.npcs.end(),"Portal service has no supported retail offer");
    const auto speaker=*npc;level.npcs={speaker};level.triangles.clear();auto objects=level.objects;
    const auto& actor=objects[speaker.object];const float feet=actor.position.y-static_cast<std::int16_t>(read_u16(actor.record,56))/retail::units_per_world;
    const Vec3 player{actor.position.x,feet+0.2f,actor.position.z+1};
    unsigned gems=2000;std::uint32_t purchases=0;NpcInteractions talk;talk.reset(level);
    const auto step=[&](DialogueInput in={}){talk.tick(level,objects,&models,player,0,true,false,in,{&gems,&purchases});};
    DialogueInput enter;enter.interact=true;step(enter);check(talk.active(),"Cannot begin portal purchase conversation");
    for(unsigned i=0;i<12;++i)step();DialogueInput yes;yes.choice=0;step(yes);
    check(purchases==(1u<<purchase) && gems==2000-models.purchase_prices[purchase],"Portal payment did not use its authored price exactly once");
    talk.dismiss(objects);step(enter);for(unsigned i=0;i<12;++i)step();step(yes);
    check(gems==2000-models.purchase_prices[purchase],"Revisiting Moneybags charged for an already-open portal");
}
void retail_checks(const char* source) {
    AssetStore store(source);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
    unsigned portals=0,lifts=0,transports=0,pictures=0;std::map<unsigned,Level> worlds;
    for(unsigned slot=15;slot<=71;slot+=2){const auto b=wad.read(slot);auto l=decode_level(b,slot);decode_travel(b,exe,l);portals+=unsigned(l.portals.size());lifts+=unsigned(l.whirlwinds.size());transports+=unsigned(l.home_transports.size());
        if(slot==33 || slot==57){const auto models=decode_object_models(b,exe);if(slot==33){purchase_check(l,models,10);purchase_check(l,models,12);}else purchase_check(l,models,18);}
        Scene gs;gs.wad_slot=slot;gs.spawn=l.spawn;Game progress(l,gs);update_travel(progress);
        for(unsigned i=0;i<l.portals.size();++i) {
            const auto& p=l.portals[i];
            if(p.purchase!=UINT32_MAX){check(!progress.level.portals[i].enabled,"Unpaid portal is open");progress.purchases|=1u<<p.purchase;update_travel(progress);check(progress.level.portals[i].enabled,"Paid portal remained locked");}
            if(p.required_orbs){progress.player.orbs=p.required_orbs-1;update_travel(progress);check(!progress.level.portals[i].enabled,"Orb portal unlocked too early");++progress.player.orbs;update_travel(progress);check(progress.level.portals[i].enabled,"Orb portal did not activate at its threshold");}
            if(p.return_home){progress.guide_awards.orbs=2;update_travel(progress);check(!progress.level.portals[i].enabled,"Unrelated challenge orb unlocked the exit");progress.portal_travel.unlock=true;update_travel(progress);check(progress.level.portals[i].enabled,"Session override cannot open the exit");progress.portal_travel.unlock=false;}
        }
        if(!l.whirlwinds.empty())check(!decode_powerup_assets(b).whirlwind_particle.texture_rgba.empty(),"Whirlwind bank has no decoded retail particle");
        for(auto p:l.portals){check(p.destination>=15 && p.destination<=71,"Retail portal destination not a campaign scene");p.enabled=true;p.opening=1;
            if(!p.return_home && (slot==15 || slot==33 || slot==51))check(!p.label.empty(),"Home-world portal lost its authored level-name controller");
            Vec3 c{};for(auto a:p.aperture)c+=a;c=c/5;const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
            check(length(n)>0.9f && portal_contains(p,c),"Retail aperture is degenerate");
            check(portal_crossed(p,c-n*3,c+n*3),"Retail doorway crossing missed");
            for(float side:{-1.0f,1.0f}){const auto image=portal_picture(p,c+n*side*5);++pictures;
                if(side<0 && !p.two_sided){check(image.empty(),"One-sided panorama rendered from behind");continue;}
                check(!image.empty() && image.size()%3==0,"Retail panorama disappeared");
                for(const auto& v:image)check(finite(v.position) && portal_contains(p,v.position,0.01f),"Panorama escaped the original aperture");}
        }
        for(const auto& w:l.whirlwinds){check(w.object<l.objects.size() && l.objects[w.object].type==0x400 && w.height>3 && w.radius>0,"Retail lift lost its authored dimensions");
            auto test=l;test.whirlwinds={w};test.whirlwinds[0].kind=0;test.whirlwinds[0].active=true;test.portals.clear();test.home_transports.clear();test.npcs.clear();
            Scene s;s.wad_slot=slot;s.spawn=l.objects[w.object].position;
            // Particle emitters may start below the walkable floor (Zephyr).
            // Approach from the floor within the retail activation volume.
            if(const auto floor=l.raycast(s.spawn+Vec3{0,w.height-3,0},{0,-1,0},w.height-2))s.spawn=floor->point+Vec3{0,0.01f,0};
            Game g(test,s);bool entered=false,exited=false;
            for(unsigned t=0;t<1800;++t){ticks(g,1);entered|=g.whirlwind.index!=UINT32_MAX;if(entered && g.whirlwind.index==UINT32_MAX){exited=true;break;}}
            if(!entered || !exited)std::cerr<<"Lift slot "<<slot<<" object "<<w.object<<" at "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<" base "<<l.objects[w.object].position.x<<','<<l.objects[w.object].position.y<<','<<l.objects[w.object].position.z<<" velocity "<<g.player.velocity.x<<','<<g.player.velocity.y<<','<<g.player.velocity.z<<" ride "<<g.whirlwind.index<<" height "<<w.height<<'\n';
            check(entered && exited,"Retail lift cannot complete its ascent in actual level collision");
        }
        worlds.emplace(slot,std::move(l));
    }
    for(const auto& [slot,l]:worlds)for(const auto& h:l.home_transports){const auto& target=worlds.at(h.destination);
        check(std::any_of(target.home_transports.begin(),target.home_transports.end(),[&](auto a){return a.arrival==h.arrival;}),"Transporter arrival index is unmapped");
        Scene s;s.wad_slot=slot;Game g(l,s);g.visited_levels=0x1fffffff;g.portal_travel.cooldown=0;
        g.player.position=l.objects[h.object].position+Vec3{3,0.8f,0};update_travel(g);
        g.player.position=g.player.previous_position=l.objects[h.object].position+Vec3{0,0.8f,0};detect_portal_entry(g);
        check(g.portal_travel.request.transporter && g.portal_travel.request.destination==h.destination,"Home transporter failed to trigger");
        Game locked(l,s);locked.portal_travel.cooldown=0;locked.player.position=l.objects[h.object].position+Vec3{3,0.8f,0};update_travel(locked);
        locked.player.position=locked.player.previous_position=l.objects[h.object].position+Vec3{0,0.8f,0};detect_portal_entry(locked);
        check(!locked.portal_travel.active(),"Transporter bypassed the unvisited-home gate");
        Scene ts;ts.wad_slot=h.destination;ts.spawn=target.spawn;Game arrived(target,ts);begin_portal_arrival(arrived,g.portal_travel.request);ticks(arrived,40);check(!arrived.portal_travel.active() && finite(arrived.player.position),"Home transporter arrival failed");
    }
    check(portals==42 && lifts==26 && transports==12,"Portal/lift inventory incomplete");
    std::cout<<portals<<" portals, "<<lifts<<" whirlwinds, "<<transports<<" home transporters, "<<pictures<<" panorama sides\n";
}
}
int main(int argc,char** argv){try{synthetic();if(argc>1)retail_checks(argv[1]);std::cout<<checks<<" portal checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

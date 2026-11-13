// In-game orb challenges on the real runtime (native systems + the original
// overlay scripts), headless. Each challenge is played by its autopilot
// (tools/challenge_pilot.hpp, shared with the workbench's --challenge-check):
// stand before its giver (a teleport: the counted test fixture), talk with
// Triangle and accept, then play it with pad-equivalent inputs only. It passes
// when the giver's own code awards the orb (8003a0d8 / 80016328) and the PC
// records it.
// Usage: spyro_challenges <retail data> <challenge|list|all> [--trace]
//        spyro_challenges <retail data> hooks              (the levels' HUD hooks)
//        spyro_challenges <retail data> lines SLOT OBJECT  (a speaker's conversation)
// Env: OBJECTS=a,b,c (trace these objects), PEEK=addr[:size],... (trace RAM),
//      SPAWNED=1 (trace script-spawned objects), TRACE_EVERY=N.
#include "challenge_pilot.hpp"
#include "spyro/assets.hpp"
#include "spyro/camera.hpp"
#include "spyro/travel.hpp"
#include <cstdlib>
#include <iostream>
#include <set>

using namespace spyro;
namespace {
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);}
void trace_line(Game& g,const char* tag,unsigned tick,const std::vector<unsigned>& watch) {
    auto& s=g.overlay_scripts;
    std::printf("%s t %u spyro %.2f %.2f %.2f motion %s cam %u/%u dlg %d gs %u orbs %02x hud %zu",tag,tick,g.player.position.x,g.player.position.y,g.player.position.z,
        motion_name(g.player.motion),s.peek(0x80067ed0),s.peek(0x80067ed4),g.dialogue.active()?1:0,s.peek(0x800681c8),g.guide_awards.orbs,s.hud_draws().size());
    for(const auto o:watch)if(o<g.retail_objects.size()) {
        const auto& r=g.retail_objects[o];const auto m=s.moby_address(o);
        std::printf(" | %u:%x %.1f,%.1f,%.1f s%u/%u",o,r.type,r.position.x,r.position.y,r.position.z,s.peek(m+0x48,1),s.peek(m+0x49,1));
    }
    if(const char* list=std::getenv("PEEK"))for(const char* p=list;*p;) {   // PEEK=addr[:size],... (hex)
        char* e=nullptr;const auto a=static_cast<std::uint32_t>(std::strtoul(p,&e,16));if(e==p)break;unsigned size=4;
        if(*e==':')size=static_cast<unsigned>(std::strtoul(e+1,&e,10));
        std::printf(" [%08x]=%x",a,s.peek(a,size));p=*e==','?e+1:e;
    }
    std::printf("\n");
    if(std::getenv("SPAWNED"))for(std::size_t i=g.level.objects.size();i<g.retail_objects.size();++i) {
        const auto& o=g.retail_objects[i];if(o.state<0)continue;
        std::printf("      spawned %zu type %x at %.2f %.2f %.2f state %d script %d\n",i,o.type,o.position.x,o.position.y,o.position.z,o.state,o.retail_script?1:0);
    }
    if(std::getenv("HUD"))for(const auto& d:s.hud_draws())std::printf("      hud uv %u,%u clut %04x %ux%u at %d,%d color %06x\n",d.u,d.v,d.clut,d.width,d.height,d.x,d.y,d.color);
}
std::unique_ptr<Game> boot(WadArchive& wad,std::span<const std::uint8_t> exe,unsigned slot,std::unique_ptr<Level>& level,std::unique_ptr<ObjectModels>& models) {
    const auto archive=wad.read(slot);
    level=std::make_unique<Level>(decode_level(archive,slot));decode_travel(archive,exe,*level);
    models=std::make_unique<ObjectModels>(decode_object_models(archive,exe));
    initialize_retail_objects(*level,*models);level->script_image=make_retail_script_image(wad,exe,slot);
    Scene scene;scene.wad_slot=slot;scene.spawn=level->spawn;
    return std::make_unique<Game>(*level,scene,decode_camera_settings(exe),models.get(),nullptr);
}
int play(WadArchive& wad,std::span<const std::uint8_t> exe,challenge_pilot::Pilot pilot,bool trace) {
    std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;
    auto game=boot(wad,exe,pilot.slot,level,models);auto& g=*game;
    unsigned every=600;if(const char* e=std::getenv("TRACE_EVERY"))every=std::max(1u,static_cast<unsigned>(std::strtoul(e,nullptr,10)));
    std::vector<unsigned> watch{pilot.giver};
    if(const char* list=std::getenv("OBJECTS"))for(const char* p=list;*p;){char* e=nullptr;const auto o=std::strtoul(p,&e,10);if(e==p)break;watch.push_back(static_cast<unsigned>(o));p=*e==','?e+1:e;}
    for(unsigned t=0;t<4;++t){keep_alive(g);g.advance(FixedClock::step,{});}
    pilot.trace=trace;
    std::printf("challenge %s slot %u giver %u type %x: %s\n",pilot.name.c_str(),pilot.slot,pilot.giver,g.retail_objects[pilot.giver].type,pilot.what.c_str());
    unsigned tick=0,hud_seen=0,last_step=UINT32_MAX;
    for(;tick<60000 && !pilot.finished();++tick) {
        keep_alive(g);
        const auto in=pilot.next(g);
        g.advance(FixedClock::step,in);
        if(!g.overlay_scripts.hud_draws().empty())++hud_seen;
        if(trace && (tick%every==0 || pilot.index!=last_step)){trace_line(g,pilot.index<pilot.steps.size()?pilot.steps[pilot.index].first.c_str():"done",tick,watch);last_step=pilot.index;}
    }
    trace_line(g,"end",tick,watch);
    const bool awarded=pilot.awarded(g);
    std::printf("fixtures %u:",pilot.fixtures);for(const auto& f:pilot.fixture_notes)std::printf(" [%s]",f.c_str());std::printf("\n");
    std::printf("%s %s orb bit %u awarded %d orbs %02x steps %u/%zu failed '%s' ticks %u hud-ticks %u faults %u fault '%s'\n",awarded?"PASS":"FAIL",pilot.name.c_str(),pilot.orb_bit,
        awarded?1:0,g.guide_awards.orbs,pilot.index,pilot.steps.size(),pilot.failure.c_str(),tick,hud_seen,g.overlay_scripts.faults,g.overlay_scripts.fault.c_str());
    return awarded?0:1;
}
}
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_challenges <retail data> <challenge|list|all|hooks|lines SLOT OBJECT> [--trace]\n";return 2;}
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const std::string which=argv[2];
    bool trace=false;for(int i=3;i<argc;++i)if(std::string(argv[i])=="--trace")trace=true;
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        if(which=="list"){for(const auto& c:challenge_pilot::catalogue())std::printf("%s slot %u giver %u orb %u: %s\n",c.name.c_str(),c.slot,c.giver,c.orb_bit,c.what.c_str());return 0;}
        if(which=="hooks") {   // the level overlays' HUD hooks (80067154 update, 80067158 draw)
            for(unsigned slot=15;slot<=69;slot+=2) {
                if(!retail_level_id(slot))continue;
                std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;auto g=boot(wad,exe,slot,level,models);
                for(unsigned t=0;t<4;++t){keep_alive(*g);g->advance(FixedClock::step,{});}
                std::printf("slot %u update %08x draw %08x particles %08x spawn %08x\n",slot,g->overlay_scripts.peek(0x80067154),g->overlay_scripts.peek(0x80067158),
                    g->overlay_scripts.peek(0x800670fc),g->overlay_scripts.peek(0x800670b4));
            }
            return 0;
        }
        if(which=="lines" && argc>=5) {   // lines SLOT OBJECT: a speaker's decoded conversation
            const unsigned slot=std::stoul(argv[3]),object=std::stoul(argv[4]);
            const auto npc=decode_npc_record(wad.read(slot),slot,object);
            if(!npc){std::printf("no conversation\n");return 1;}
            std::printf("%s type %x subtype %u\n",npc->name.c_str(),npc->type,npc->subtype);
            for(std::size_t k=0;k<npc->lines.size();++k)std::printf("  %zu orb %x talisman %d: %s\n",k,npc->lines[k].orb_bits,npc->lines[k].talisman,npc->lines[k].text.c_str());
            return 0;
        }
        if(which=="objects" && argc>=4) {   // objects SLOT [CLASS,...]: every object (or those classes) after boot, and the moby groups (800670e0)
            const unsigned slot=std::stoul(argv[3]);std::set<unsigned> only;
            if(argc>=5 && argv[4][0]!='-')for(const char* p=argv[4];*p;){char* e=nullptr;const auto v=std::strtoul(p,&e,16);if(e==p)break;only.insert(static_cast<unsigned>(v));p=*e==','?e+1:e;}
            std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;auto g=boot(wad,exe,slot,level,models);
            for(unsigned t=0;t<4;++t){keep_alive(*g);g->advance(FixedClock::step,{});}
            auto& s=g->overlay_scripts;
            std::printf("slot %u objects %zu (initial %zu) owned %u spawn %.2f %.2f %.2f\n",slot,g->retail_objects.size(),g->level.objects.size(),s.owned(),g->player.position.x,g->player.position.y,g->player.position.z);
            for(std::size_t i=0;i<g->retail_objects.size();++i) {
                const auto& o=g->retail_objects[i];if(!only.empty() && !only.contains(o.type))continue;
                const auto m=s.moby_address(static_cast<unsigned>(i));const auto h=s.peek(m);
                std::printf("%3zu %3x at %7.2f %7.2f %7.2f st %02x/%02x script %d enemy %d gate %d spirit %u yaw %.0f",i,o.type,o.position.x,o.position.y,o.position.z,s.peek(m+0x48,1),s.peek(m+0x49,1),
                    o.retail_script?1:0,o.enemy?1:0,o.powerup_gate?1:0,o.record[83]&127u,o.rotation[2]);
                if(h>=0x80000000u && h<0x80800000u){std::printf(" hdr");const unsigned hdr_len=std::getenv("HDR")?static_cast<unsigned>(std::strtoul(std::getenv("HDR"),nullptr,16)):0x40;for(unsigned b=0;b<hdr_len;b+=4)std::printf(" %08x",s.peek(h+b));}
                std::printf("\n");
            }
            const auto groups=s.peek(0x80066f4c),table=s.peek(0x800670e0);
            for(unsigned k=0;k<groups && k<64;++k) {
                std::printf("group %u:",k);auto at=s.peek(table+k*4);
                for(unsigned guard=0;guard<512;++guard,at+=2){const auto v=s.peek(at,2);std::printf(" %u",v&0x7fff);if(v&0x8000)break;}
                std::printf("\n");
            }
            return 0;
        }
        if(which=="talk" && argc>=4) {   // talk SLOT|all [OBJECT,...] [--no]: talk to every speaker, read to the end, check Spyro moves again
            std::vector<unsigned> slots;
            if(std::string(argv[3])=="all"){for(unsigned s=15;s<=71;s+=2)if(retail_level_id(s))slots.push_back(s);}
            else slots.push_back(std::stoul(argv[3]));
            std::set<unsigned> only;bool answer_no=false,triangle=false,revisit=std::getenv("TALK_REWARDED")!=nullptr;
            for(int k=4;k<argc;++k) {
                const std::string a=argv[k];
                if(a=="--no")answer_no=true;
                else if(a=="--triangle")triangle=true;   // advance with Triangle (E), the talk button, not Cross
                else if(a=="--revisit")revisit=true;
                else if(a[0]!='-')for(const char* p=argv[k];*p;){char* e=nullptr;const auto v=std::strtoul(p,&e,10);if(e==p)break;only.insert(static_cast<unsigned>(v));p=*e==','?e+1:e;}
            }
            unsigned speakers=0,talked=0,locks=0;
            for(const auto slot:slots) {
                const auto archive=wad.read(slot);
                std::vector<unsigned> records;
                {
                    std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;auto g=boot(wad,exe,slot,level,models);
                    for(unsigned i=0;i<g->level.objects.size();++i) {
                        if(!only.empty() && !only.contains(i))continue;
                        std::optional<NpcDefinition> npc;try{npc=decode_npc_record(archive,slot,i);}catch(const std::exception&){}
                        if(!npc || npc->lines.empty() || npc->lines.size()>32)continue;
                        if(std::none_of(npc->lines.begin(),npc->lines.end(),[](const DialogueLine& l){return !l.text.empty();}))continue;
                        records.push_back(i);
                    }
                }
                for(const auto i:records) {
                    ++speakers;
                    std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;auto game=boot(wad,exe,slot,level,models);auto& g=*game;
                    for(unsigned t=0;t<4;++t){keep_alive(g);g.advance(FixedClock::step,{});}
                    auto& s=g.overlay_scripts;
                    const auto& o=g.retail_objects[i];
                    if(o.state<0 || o.collected){std::printf("slot %u #%u %x: gone at boot\n",slot,i,o.type);continue;}
                    if(!challenge_pilot::stand_before(g,i)){std::printf("slot %u #%u %x: no floor before it\n",slot,i,o.type);continue;}
                    if(revisit){g.award_talisman();g.guide_awards.orbs=0xff;g.reward_this_visit=false;}   // fixture: saved rewards, a new visit
                    unsigned t=0;bool opened=false;
                    for(;t<400 && !opened;++t){keep_alive(g);InputState in;in.dialogue.interact=(t%8)==1;g.advance(FixedClock::step,in);opened=g.dialogue.active();}
                    if(!opened){std::printf("slot %u #%u %x: no talk (script %d)\n",slot,i,o.type,o.retail_script?1:0);continue;}
                    ++talked;
                    // Read it through: Cross on a line, the answer on a question.
                    unsigned lines=0,read=0;bool closed=false;
                    for(read=0;read<4000;++read) {
                        keep_alive(g);InputState in;
                        if(read%24==23) {
                            const auto* l=g.dialogue.line();
                            if(l && l->yes>=0 && l->no>=0)in.dialogue.choice=answer_no?1:0;else if(triangle)in.dialogue.interact=true;else in.dialogue.next=true;
                            ++lines;
                        }
                        g.advance(FixedClock::step,in);
                        if(!g.dialogue.active() && !g.reward_show.active()){closed=true;break;}
                    }
                    // Control back: wait, then hold the stick away from the speaker.
                    for(unsigned w=0;w<90;++w){keep_alive(g);g.advance(FixedClock::step,{});}
                    const auto before=g.player.position;bool reopened=false;
                    auto away=g.player.position-g.retail_objects[i].position;away.y=0;const float al=length(away);
                    for(unsigned w=0;w<90;++w){keep_alive(g);InputState in;if(al>.01f){in.move_x=away.x/al;in.move_z=away.z/al;}else in.move_z=1;g.advance(FixedClock::step,in);reopened|=g.dialogue.active();}
                    float moved=length(g.player.position-before);
                    // A wall is not a dialogue lock (e.g. Icy Speedway's
                    // Hunter). Try the other stick directions from the
                    // current position, without moving Spyro by fixture.
                    for(const auto direction:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,0,1},Vec3{0,0,-1}}) {
                        if(moved>=.3f || g.dialogue.active() || g.reward_show.active())break;
                        const auto start=g.player.position;InputState in;in.move_x=direction.x;in.move_z=direction.z;
                        for(unsigned w=0;w<90;++w){keep_alive(g);g.advance(FixedClock::step,in);reopened|=g.dialogue.active();}
                        moved=std::max(moved,length(g.player.position-start));
                    }
                    const bool lock=!closed || moved<.3f || g.dialogue.active() || g.reward_show.active() || s.faults>0;
                    if(lock)++locks;
                    const auto* l=g.dialogue.line();
                    std::printf("slot %u #%u %x '%s' script %d: %s lines %u ticks %u moved %.2f dlg %d retail %d line '%.40s' gs %u 80067100 %u cam %u/%u scene_locked %d reward %d restarting %d motion %s%s\n",
                        slot,i,o.type,g.dialogue.active()?g.dialogue.name():"",o.retail_script?1:0,lock?"LOCK":"ok",lines,read,moved,g.dialogue.active()?1:0,g.dialogue.retail_driven()?1:0,
                        l?l->text.c_str():"",s.peek(0x800681c8),s.peek(0x80067100),s.peek(0x80067ed0),s.peek(0x80067ed4),g.scene_locked?1:0,g.reward_show.active()?1:0,s.restarting()?1:0,
                        motion_name(g.player.motion),reopened?" (talked again)":"");
                    {   // the reward state the speaker's own code reads
                        const auto h=s.peek(s.moby_address(i));const unsigned index=(slot-15)/2;
                        std::printf("      talisman %d ram-talisman %u 80066f44 %u orbs %02x ram-orbs %02x +5 %u state %u/%u conversations %u reward-show %d\n",g.guide_awards.talisman?1:0,
                            s.peek(0x8006b264+index,1),s.peek(0x80066f44,1),g.guide_awards.orbs,s.peek(0x80069fd0+index,1),h>=0x80000000u?s.peek(h+5,1):255u,
                            s.peek(s.moby_address(i)+0x48,1),s.peek(s.moby_address(i)+0x49,1),g.dialogue.conversations(),g.reward_show.active()?1:0);
                    }
                }
            }
            std::printf("talk sweep: %u speakers, %u talked, %u locks\n",speakers,talked,locks);
            return locks?1:0;
        }
        if(which=="heights" && argc>=9) {   // heights SLOT X0 Z0 X1 Z1 STEP: the floor layers (top down) along a line
            const unsigned slot=std::stoul(argv[3]);const float x0=std::stof(argv[4]),z0=std::stof(argv[5]),x1=std::stof(argv[6]),z1=std::stof(argv[7]),step=std::max(.05f,std::stof(argv[8]));
            std::unique_ptr<Level> level;std::unique_ptr<ObjectModels> models;auto g=boot(wad,exe,slot,level,models);
            const float span=std::hypot(x1-x0,z1-z0);
            for(float d=0;d<=span+1e-3f;d+=step) {
                const float f=span>0?d/span:0;Vec3 q{x0+(x1-x0)*f,200,z0+(z1-z0)*f};
                std::printf("%7.2f %7.2f:",q.x,q.z);
                for(int n=0;n<6;++n){const auto hit=g->level.raycast(q,{0,-1,0},400);if(!hit)break;std::printf(" %.2f(n%.2f)",hit->point.y,hit->normal.y);q=hit->point-Vec3{0,.02f,0};}
                std::printf("\n");
            }
            return 0;
        }
        if(which=="all") {
            unsigned failed=0;
            for(const auto& c:challenge_pilot::catalogue())failed+=play(wad,exe,c,trace)!=0;
            std::printf("%u failed\n",failed);return failed?1:0;
        }
        auto pilot=challenge_pilot::make(which);
        if(!pilot){std::cerr<<"unknown challenge (list shows them)\n";return 2;}
        return play(wad,exe,*pilot,trace);
    } catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<"\n";return 3;}
}

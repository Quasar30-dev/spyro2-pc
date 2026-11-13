// Engagement audit of every spirit-bearing enemy that runs its original
// overlay script. For each one, four fresh games:
//   far    Spyro idles 40 units away (control),
//   near   Spyro idles 2.5 units away (does the enemy behave differently?
//          does it hurt him?),
//   flame  Spyro breathes real flame at it from 1.6 units,
//   charge Spyro charges into it from 5 units.
// "Engaged" = the near run differs from the control (position, state or
// animation clip sequence). Output: one JSON line per enemy plus a summary
// per level.
// Usage: spyro_enemy_audit <retail data> [slot ...]
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include "spyro/spyro_model.hpp"
#include <iostream>
#include <map>

using namespace spyro;
namespace {
struct Track {std::vector<Vec3> pos;std::vector<int> state;std::vector<unsigned> clip;};
bool place(Game& g,Vec3 target,float distance,float& yaw_out) {
    for(unsigned i=0;i<16;++i) {
        const float a=i*std::numbers::pi_v<float>/8;const Vec3 d{std::sin(a),0,std::cos(a)};
        const auto floor=g.level.raycast(target-d*distance+Vec3{0,1.5f,0},{0,-1,0},5);
        if(!floor || floor->normal.y<.7f || std::abs(floor->point.y-target.y)>2.5f)continue;
        // a clear line from Spyro's chest to the target
        const auto from=floor->point+Vec3{0,.4f,0},to=target+Vec3{0,.3f,0};
        const auto hit=g.level.raycast(from,normalized(to-from),length(to-from));
        if(hit && hit->distance<length(to-from)-.6f)continue;
        g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};
        yaw_out=std::atan2(d.x,d.z);g.player.yaw=g.player.previous_yaw=yaw_out;return true;
    }
    return false;
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: spyro_enemy_audit <retail data> [slot ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        std::vector<unsigned> slots;for(int i=2;i<argc;++i)slots.push_back(std::stoul(argv[i]));
        if(slots.empty())for(unsigned s=15;s<=69;s+=2)slots.push_back(s);
        for(const auto slot:slots) {
            const auto archive=wad.read(slot);
            Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
            initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
            const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;
            // which records the runtime owns (one probe game)
            std::vector<unsigned> enemies;
            {
                Game g(level,scene,{},&models,&spyro);g.advance(FixedClock::step,{});g.advance(FixedClock::step,{});
                for(unsigned i=0;i<level.objects.size();++i) {
                    const auto& o=g.retail_objects[i];
                    if(o.retail_script && level.objects[i].state>=0 && (level.objects[i].record[83]&127))enemies.push_back(i);
                }
            }
            std::map<unsigned,std::array<unsigned,6>> by_class;   // count, engaged, hurt, flame, charge, placed
            for(const auto index:enemies) {
                const auto type=level.objects[index].type;auto& c=by_class[type];++c[0];
                auto run=[&](float distance,int mode,Track& track,unsigned& hits,int& health_lost,bool& killed,bool& placed) {
                    Game g(level,scene,{},&models,&spyro);g.advance(FixedClock::step,{});g.advance(FixedClock::step,{});
                    const auto target=g.retail_objects[index].position;float yaw=0;
                    placed=distance>20?true:place(g,target,distance,yaw);
                    if(distance>20) {
                        // park far away on any floor
                        bool ok=false;
                        for(unsigned k=0;k<24 && !ok;++k){const float a=k*.2618f;const Vec3 d{std::sin(a),0,std::cos(a)};
                            const auto f=g.level.raycast(target+d*40+Vec3{0,30,0},{0,-1,0},80);
                            if(f && f->normal.y>.7f){g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};ok=true;}}
                        placed=ok;
                    }
                    if(!placed)return;
                    const auto spirits=g.spirits();const auto health=g.player.health;const auto lives=g.player.lives;
                    const auto hits0=g.overlay_scripts.hits;
                    for(unsigned t=0;t<300;++t) {
                        InputState in;
                        const auto& o=g.retail_objects[index];
                        const auto d=o.position-g.player.position;
                        if(mode==1){g.player.yaw=std::atan2(d.x,d.z);in.flame_pressed=(t%20)==0 && t<200;}
                        if(mode==2 && t<150){const auto dir=normalized(Vec3{d.x,0,d.z});in.move_x=dir.x;in.move_z=dir.z;in.charge=true;}
                        if(mode!=0)g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);
                        g.advance(FixedClock::step,in);
                        if(t%10==0){track.pos.push_back(o.position);track.state.push_back(o.state);track.clip.push_back(o.animation.clip);}
                    }
                    hits=g.overlay_scripts.hits-hits0;
                    health_lost=int(health)-int(g.player.health)+3*(int(lives)-int(g.player.lives));
                    killed=g.retail_objects[index].state<0 || g.spirits()>spirits;
                };
                Track far,near,flame,charge;unsigned hf{},hn{},hx{},hc{};int lf{},ln{},lx{},lc{};bool kf{},kn{},kx{},kc{},pf{},pn{},px{},pc{};
                run(40,0,far,hf,lf,kf,pf);run(2.5f,0,near,hn,ln,kn,pn);run(1.6f,1,flame,hx,lx,kx,px);run(5,2,charge,hc,lc,kc,pc);
                float moved=0;bool state_diff=false,clip_diff=false;
                for(std::size_t k=0;k<std::min(far.pos.size(),near.pos.size());++k) {
                    moved=std::max(moved,length(near.pos[k]-far.pos[k]));
                    state_diff|=near.state[k]!=far.state[k];clip_diff|=near.clip[k]!=far.clip[k];
                }
                const bool engaged=pn && pf && (moved>.25f || state_diff || clip_diff);
                const bool hurt=hn>0 || ln>0;
                if(engaged)++c[1];if(hurt)++c[2];if(kx)++c[3];if(kc)++c[4];if(pn)++c[5];
                const auto& o=level.objects[index];
                std::cout<<"{\"slot\":"<<slot<<",\"record\":"<<index<<",\"class\":\""<<std::hex<<type<<std::dec<<"\",\"pos\":["<<o.position.x<<","<<o.position.y<<","<<o.position.z
                    <<"],\"placed\":"<<pn<<",\"engaged\":"<<engaged<<",\"moved\":"<<moved<<",\"state_diff\":"<<state_diff<<",\"clip_diff\":"<<clip_diff
                    <<",\"hurt\":"<<hurt<<",\"flame_kill\":"<<kx<<",\"charge_kill\":"<<kc<<"}\n"<<std::flush;
            }
            std::cout<<"# slot "<<slot<<" enemies "<<enemies.size()<<" :";
            for(const auto& [type,c]:by_class)std::cout<<" "<<std::hex<<type<<std::dec<<"[n"<<c[0]<<" eng"<<c[1]<<" hurt"<<c[2]<<" fl"<<c[3]<<" ch"<<c[4]<<" placed"<<c[5]<<"]";
            std::cout<<"\n"<<std::flush;
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}

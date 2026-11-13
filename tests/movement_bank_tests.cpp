#include "spyro/travel.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(why);}
void plane(Level& l,float y,bool water=false) {
    const unsigned id=unsigned(l.triangles.size());
    l.triangles.push_back({{-100,y,-100},{100,y,100},{100,y,-100},id,water});
    l.triangles.push_back({{-100,y,-100},{-100,y,100},{100,y,100},id+1,water});
    l.bounds.add({-100,y,-100});l.bounds.add({100,y,100});
}
void ticks(Game& g,unsigned count,InputState input={}) {
    for(unsigned i=0;i<count;++i) {
        g.advance(FixedClock::step,input);
        const auto pose=g.animation.sample(.37f,SuperchargeAppearance::MainModelChargeWings);
        for(auto v:pose.vertices)check(finite(v),"Movement produced an invalid Spyro vertex");
    }
}
void gameplay(const SpyroModel& model) {
    Level pool;pool.surface_swimming=pool.underwater_swimming=true;plane(pool,-15);plane(pool,0,true);
    Scene scene;scene.spawn={0,.1f,0};Game swim(pool,scene,{},nullptr,&model);
    ticks(swim,100);check(swim.player.motion==Motion::SurfaceIdle,"Missing surface idle");
    InputState move;move.move_z=1;ticks(swim,30,move);check(swim.player.motion==Motion::SurfaceSwim,"Missing paddling");
    InputState dive;dive.charge=true;ticks(swim,1,dive);ticks(swim,185);
    check(swim.player.motion==Motion::UnderwaterIdle,"Missing underwater idle");
    move={};move.jump_held=true;ticks(swim,60,move);check(swim.player.motion==Motion::UnderwaterSwim,"Missing swimming");
    ticks(swim,30,dive);check(swim.player.motion==Motion::UnderwaterCharge,"Missing swim charge");
    check(swim.hurt(swim.player.position+Vec3{1,0,0}),"Underwater damage was rejected");
    ticks(swim,1);check(swim.player.motion==Motion::UnderwaterHurt,"Missing underwater hurt motion");
    ticks(swim,30);check(swim.player.motion==Motion::UnderwaterIdle,"Hurt did not return to swimming");
    Level lift;plane(lift,0);RetailObject o;o.type=0x400;o.rotation[2]=64;lift.objects={o};
    lift.whirlwinds={{0,0,0,0,0,6,1,true}};scene.spawn={0,0,0};Game ride(lift,scene,{},nullptr,&model);
    bool raised=false,released=false;
    for(unsigned i=0;i<700;++i){ticks(ride,1);raised|=ride.player.motion==Motion::Whirlwind;
        if(raised && ride.whirlwind.index==UINT32_MAX){released=true;break;}}
    check(raised && released && ride.player.air.gliding,"Missing whirlwind animation or exit");
}
}
int main(int argc,char** argv) {try {
    if(argc!=2)throw FormatError("Usage: spyro_movement_bank_tests retail-path");
    AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
    const auto global=wad.read(0),power=wad.read(29),swim=wad.read(15),slide=wad.read(45),hurt=wad.read(59);
    const unsigned motions[]{0,1,2,5,6,7,12,16,17,18,21,24,25,33,34,35,40,41,42,43,44,46,49,57,61};
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);const auto model=decode_spyro(global,archive,exe,power,29,swim,slide,hurt);
        for(auto id:motions)check(model.animations.contains(id),"A mounted scene lacks a selectable movement animation");
        for(auto id:motions) {
            // A charge may be interrupted by any of these environments/abilities.
            SpyroAnimator animation;animation.reset(&model,61);SpyroSecondaryAnimator head;head.reset(&model,animation.state());
            for(auto requested:{61u,id,0u,61u})for(unsigned tick=0;tick<90;++tick) {
                animation.tick(requested);head.tick(0,animation.state(),true);
                for(auto appearance:{SuperchargeAppearance::Retail,SuperchargeAppearance::MainModel,SuperchargeAppearance::MainModelChargeWings}) {
                    const auto p=model.secondary_track(animation.state().clip,animation.sample(.37f,appearance),head.sample(.37f,appearance),appearance);
                    for(auto v:p.vertices)check(finite(v),"Movement transition produced non-finite geometry");
                    for(const auto& face:model.clip(animation.state().clip,appearance).faces)for(auto i:face.vertices)
                        check(i<p.vertices.size(),"Movement transition selected incompatible topology");
                }
            }
        }
        if(slot==17 || slot==19 || slot==29 || slot==95)gameplay(model);
        std::cout<<"Slot "<<slot<<": all movement clips and charge transitions passed\n";
    }
    // Reproduce the original Glimmer-bank omission and verify the import source.
    const auto native=decode_spyro(global,wad.read(17),exe,power,29);
    check(!native.animations.contains(40),"Glimmer fixture no longer demonstrates the missing-swim crash");
    bool reproduced=false;
    try {
        Level pool;pool.surface_swimming=pool.underwater_swimming=true;plane(pool,-15);plane(pool,0,true);
        Scene start;start.spawn={0,.1f,0};Game missing(pool,start,{},nullptr,&native);ticks(missing,100);
    } catch(const FormatError& e) {
        reproduced=std::string(e.what()).find("missing retail Spyro animation 44")!=std::string::npos;
    }
    check(reproduced,"Native-only bank no longer reproduces the logged fatal animation path");
    const auto complete=decode_spyro(global,wad.read(17),exe,power,29,swim,slide,hurt);
    check(complete.animations.at(57).imported_from_slot==59,"Underwater hurt donor provenance was lost");
    check(complete.animations.at(40).imported_from_slot==15 && complete.animations.at(49).imported_from_slot==45,
        "Supplemental animation provenance was lost");
    std::cout<<checks<<" animation-bank checks passed across 41 banks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// Bounded real resident/loader/adapter execution. Explicit RAM fixtures.
// This is an automated implementation check, not a normal-input playthrough.
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/overlay_vm.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
using namespace spyro;
Bytes bytes(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);if(!f)throw FormatError("Cannot read "+p.string());
    return Bytes(std::istreambuf_iterator<char>(f),{});
}
void need(bool b,const std::string& s){if(!b)throw FormatError(s);}
int main(int argc,char**argv){try{
    need(argc==5,"usage: soi_actor_vm_check ARCHIVE OVERLAY TICK_HEX EXPECTED_COUNT");
    AssetStore store("data/disc");WadArchive wad(store);RetailScriptImage image;
    image.executable=store.read_all(find_game_executable(store));image.loader=wad.read(1);
    image.archive=bytes(argv[1]);image.overlay=bytes(argv[2]);image.slot=17;image.level_id=11;
    const unsigned tick=std::stoul(argv[3],nullptr,16),expected=std::stoul(argv[4]);
    OverlayVm vm;const auto boot=boot_retail_level(vm,image);need(boot.fault.empty(),"BOOT "+boot.fault);
    need(boot.count==expected,"Count "+std::to_string(boot.count));vm.write32(0x80067018,2);
    std::cout<<"ORIGINAL LOADER count "<<boot.count<<" mobys "<<std::hex<<boot.mobys<<std::dec<<"\n";
    auto call=[&](unsigned pc,std::initializer_list<std::uint32_t> args){vm.r[29]=retail_stack_top;vm.call(pc,std::span(args.begin(),args.size()),800000);need(vm.fault.empty(),"PC "+std::to_string(pc)+" "+vm.fault);};
    unsigned enemies=0,greeters=0,critters=0,controller=0,portal=0;std::array<unsigned,3>bells{};
    for(unsigned i=225;i<boot.count;++i){
        const auto m=boot.mobys+88*i,p=vm.read32(m),kind=vm.read32(p+4);
        need(vm.read32(p)==0x534f4933,"Private parameter relocation");
        need(!native_retail_class(vm.read16(m+54)),"Actor unexpectedly native");
        need(vm.read8(m+67)==255 && vm.read8(m+74)!=0,"Independent active-list eligibility");
        if(kind==4){need(vm.read32(p+44)>=boot.mobys,"Fan parent relocation");continue;}
        if(kind==5){bells.at(vm.read32(p+36))=m;continue;}
        if(kind==6){controller=m;continue;}
        if(kind==7){portal=m;continue;}
        for(unsigned k=0;k<3;++k)vm.write32(0x80069ff0+4*k,vm.read32(m+12+4*k)+(k==0?1800:k==2?512:0));
        call(tick,{m});need(vm.read32(m+8)==(kind?p+64:0),"Passive body pointer");
        if(kind==0){++greeters;need(vm.read8(m+72)==0 && vm.read8(m+61)==0,"Ambient greeter");continue;}
        if(kind==3){
            ++critters;vm.write32(m+24,0x10000);call(tick,{m});
            need(vm.read8(m+72)==7 && vm.read8(m+76)==0 && vm.read32(m+8)==0,"Critter hidden cooldown");
            bool food=false;const auto pool=vm.read32(0x800670bc);
            for(auto at=pool;at<pool+88*32;at+=88){if(vm.read8(at+72)==255)break;if(vm.read16(at+54)==0x10 && vm.read8(at+72)<128)food=true;}
            if(!food){std::cerr<<"FOOD DIAGNOSTIC pool "<<std::hex<<pool<<" end "<<vm.read32(0x80066fb4)<<" free "<<vm.read32(0x80066fd4)<<std::dec<<'\n';
                for(auto at=pool;at<pool+88*48;at+=88)std::cerr<<"POOL "<<(at-pool)/88<<" class "<<std::hex<<vm.read16(at+54)<<" state "<<unsigned(vm.read8(at+72))<<std::dec<<'\n';}
            need(food,"Critter did not release actual class10 butterfly");
            for(unsigned k=0;k<3;++k)vm.write32(0x80069ff0+4*k,vm.read32(m+12+4*k)+(k==0?12000:0));
            vm.write32(p+24,1200);call(tick,{m});
            need(vm.read8(m+72)==0 && vm.read8(m+76)==32 && vm.read32(m+8)==p+64,"Source critter distant respawn");
            std::cout<<"CRITTER source "<<vm.read32(p+40)<<" index "<<i<<" class10 release, no gem; hidden cooldown; explicit timer1200/away fixture respawn\n";continue;
        }
        ++enemies;std::set<unsigned> states{vm.read8(m+72)},clips{vm.read8(m+61)};
        const auto x0=vm.read32(m+12);bool damage=false;
        // Follow this actor with a nearby player-origin fixture. The adapter
        // still executes its original grid update and generated state machine.
        for(unsigned t=0;t<160;++t){
            vm.write32(0x80069ff0,vm.read32(m+12)+800);vm.write32(0x80069ff4,vm.read32(m+16));vm.write32(0x80069ff8,vm.read32(m+20)+512);
            vm.write32(0x8006a014,0);call(tick,{m});states.insert(vm.read8(m+72));clips.insert(vm.read8(m+61));damage|=vm.read32(0x8006a014)!=0;
        }
        need(states.contains(2)&&states.contains(3)&&states.contains(4),"Windup/swing/recovery missing source "+std::to_string(vm.read32(p+40)));
        need(damage,"Swing damage request missing");
        if(kind==1)need(vm.read32(m+12)!=x0,"Spear source lunge missing");
        // The named post-collision mailbox is a RAM fixture, not physical input.
        vm.write32(m+24,0x10000);call(tick,{m});need(vm.read8(m+72)==6,"Flame death state");
        for(unsigned t=0;t<55 && vm.read8(m+72)<128;++t)call(tick,{m});
        need(vm.read8(m+72)==0xfd,"Initial source actor not retired");
        const auto n=vm.read32(0x8006718c+0x240);bool persisted=false;
        for(unsigned k=0;k<n;++k)if((vm.read32(0x8006718c+0x40+8*k)&0xffffff)==((m+72)&0xffffff))persisted=true;
        need(persisted,"Removed state not registered in resident save list");
        std::cout<<"ACTOR source "<<vm.read32(p+40)<<" index "<<i<<" kind "<<kind<<" states";for(auto s:states)std::cout<<' '<<s;
        std::cout<<" clips";for(auto c:clips)std::cout<<' '<<c;
        std::cout<<" swing-damage 1 death fd resident-persist 1 drop-value "<<unsigned(vm.read8(m+80))<<"\n";
    }
    need(enemies==21 && greeters==1,"Source enemy/greeter count");
    if(controller){
        const auto q=vm.read32(controller);call(tick,{portal});need(vm.read8(portal+76)==0,"Source return portal initially visible");
        const auto ring=[&](unsigned n){
            // Approach/quiet time lets the source contact cooldown expire;
            // a named direct-call fixture does not tick offscreen actors.
            for(unsigned i=0;i<20;++i)call(tick,{bells[n]});
            vm.write32(bells[n]+24,0x10000);call(tick,{bells[n]});for(unsigned i=0;i<16;++i)call(tick,{bells[n]});};
        ring(0);need(vm.read32(q+28)==0 && vm.read32(q+32)==0,"Incorrect first bell accepted");
        ring(2);ring(1);ring(2);need(vm.read32(q+28)==0 && vm.read32(q+32)==0,"Wrong middle bell did not reset");
        for(unsigned n:{2u,1u,0u,2u,1u,0u})ring(n);
        need(vm.read32(q+28)==6 && vm.read32(q+32)==1,"Original six-bell sequence did not complete");
        call(tick,{portal});need(vm.read8(portal+76)==32 && vm.read8(portal+61)==1,"Source return portal gate did not open");
        bool persisted=false;for(unsigned k=0;k<vm.read32(0x8006718c+0x240);++k)if((vm.read32(0x8006718c+0x40+8*k)&0xffffff)==((q+32)&0xffffff))persisted=true;
        need(persisted,"Quest completion was not registered with resident save API");
        std::cout<<"SOURCE QUEST CHECK wrong first/middle resets; 2,1,0,2,1,0 opens index "<<(portal-boot.mobys)/88<<"; resident completion persistence; parent travel binding remains separate\n";
    }
    std::cout<<"BOUNDED ACTOR ADAPTER CHECK COMPLETE enemies "<<enemies<<" critters "<<critters<<" greeters "<<greeters<<"; named RAM fixtures; not normal-flow gameplay\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

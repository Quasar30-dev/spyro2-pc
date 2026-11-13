// Boots a level's original runtime in the overlay interpreter (the same
// boot_retail_level the game uses), then runs the moby dispatcher with Spyro
// parked next to a chosen moby class, logging guarded / resident calls.
// Usage: spyro_vm_probe <retail data> <level slot> [class hex ...]
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/overlay_vm.hpp"
#include "spyro/retail_scripts.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace spyro;
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_vm_probe <retail data> <level slot> [class hex ...]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const unsigned slot=std::stoul(argv[2]);
        // Class arguments; "attack=MASK@TICK" ORs MASK into the first chosen moby's +18 word at TICK.
        std::set<unsigned> classes;std::uint32_t attack_mask=0;unsigned attack_tick=UINT32_MAX;std::vector<std::pair<unsigned,std::uint32_t>> attacks;unsigned total_ticks=120;   // repeatable attack=MASK@TICK; ticks=N
        for(int i=3;i<argc;++i) {
            const std::string a=argv[i];
            if(a.rfind("ticks=",0)==0){total_ticks=std::stoul(a.substr(6));continue;}
            if(a.rfind("attack=",0)==0){const auto at=a.find('@');attack_mask=std::stoul(a.substr(7,at-7),nullptr,16);attack_tick=std::min(attack_tick,static_cast<unsigned>(std::stoul(a.substr(at+1))));attacks.push_back({static_cast<unsigned>(std::stoul(a.substr(at+1))),attack_mask});}
            else classes.insert(std::stoul(a,nullptr,16));
        }
        const auto exe=assets.read_all("SCUS_944.25");
        const auto image=make_retail_script_image(wad,exe,slot);
        if(!image){std::cerr<<"slot "<<slot<<" has no retail level runtime\n";return 1;}
        OverlayVm vm;
        const auto boot=boot_retail_level(vm,*image);
        std::cout<<"camera mode "<<vm.read32(0x80067ed0)<<" substate "<<vm.read32(0x80067ed4)<<" request "<<vm.read32(0x80067fec)<<"\n";
        std::cout<<"boot: "<<(boot.fault.empty()?"ok":boot.fault)<<" mobys 0x"<<std::hex<<boot.mobys<<" dispatcher 0x"<<boot.dispatcher<<std::dec<<" count "<<boot.count<<"\n";
        if(!boot.fault.empty())return 1;
        const auto cases=find_dispatcher(image->overlay).cases;
        for(const auto c:classes)if(cases.contains(c))std::cout<<"case "<<std::hex<<c<<" @ "<<cases.at(c)<<std::dec<<"\n";
        std::map<unsigned,unsigned> census;std::vector<std::uint32_t> chosen;
        for(unsigned i=0;i<boot.count;++i) {
            const auto m=boot.mobys+i*88;const auto type=vm.read16(m+0x36);
            if(static_cast<std::int8_t>(vm.read8(m+0x48))<0)continue;
            ++census[type];if(classes.contains(type))chosen.push_back(m);
        }
        std::cout<<"classes (count, *=script case, n=native):";
        for(const auto& [type,n]:census)std::cout<<" "<<std::hex<<type<<std::dec<<"x"<<n<<(cases.contains(type)?"*":"")<<(native_retail_class(type)?"n":"");
        std::cout<<"\nchosen "<<chosen.size()<<", class table entries:";
        for(const auto& [type,n]:census)if(vm.read32(0x80068c94+type*4))std::cout<<" "<<std::hex<<type<<"@"<<vm.read32(0x80068c94+type*4)<<std::dec;
        std::cout<<"\n";
        for(const auto& [a,n]:vm.skipped)std::cout<<"  guarded call 0x"<<std::hex<<a<<std::dec<<" x"<<n<<"\n";
        vm.skipped.clear();
        vm.hle[0x80040e94]=[&](OverlayVm& v){for(std::size_t i=0;i<chosen.size();++i)v.write32(0x800685f0+std::uint32_t(i)*4,chosen[i]);v.write32(0x800685f0+std::uint32_t(chosen.size())*4,0);};
        if(!chosen.empty()){vm.write32(0x80069ff0,vm.read32(chosen[0]+12)+1500);vm.write32(0x80069ff4,vm.read32(chosen[0]+16));vm.write32(0x80069ff8,vm.read32(chosen[0]+20));}
        vm.write32(0x80067018,2);
        std::unordered_map<std::uint32_t,std::uint64_t> trace;vm.trace=&trace;
        std::unordered_map<std::uint32_t,std::uint64_t> pcs;vm.executed_pcs=&pcs;
        for(unsigned tick=0;tick<total_ticks;++tick) {
            const auto start=vm.executed;
            for(const auto& [when,mask]:attacks)if(tick==when && !chosen.empty())vm.write32(chosen[0]+0x18,vm.read32(chosen[0]+0x18)|mask);
            vm.r[29]=retail_stack_top;vm.call(boot.dispatcher,{},5'000'000);
            if(!vm.fault.empty()){std::cout<<"tick "<<tick<<" fault "<<vm.fault<<" pc 0x"<<std::hex<<vm.pc;
                for(unsigned k=0;k<16;++k)std::cout<<" "<<vm.history[(vm.history_at+k)&15];
                std::cout<<std::dec<<"\n";break;}
            if((tick%30==0 || (tick>attack_tick && tick<attack_tick+120 && tick%5==0)) && !chosen.empty())std::cout<<"tick "<<tick<<" executed "<<vm.executed-start<<" moby0 state "<<int(vm.read8(chosen[0]+72))
                <<" pos "<<static_cast<std::int32_t>(vm.read32(chosen[0]+12))<<','<<static_cast<std::int32_t>(vm.read32(chosen[0]+16))
                <<" clip "<<int(vm.read8(chosen[0]+60))<<" frame "<<int(vm.read8(chosen[0]+62))<<" bank 0x"<<std::hex<<vm.read32(chosen[0]+8)<<std::dec<<"\n";
        }
        for(std::uint32_t m=vm.read32(0x800670bc);m<vm.read32(0x80066fd4);m+=88)
            std::cout<<"  spawned 0x"<<std::hex<<m<<" type "<<vm.read16(m+0x36)<<std::dec<<" state "<<int(static_cast<std::int8_t>(vm.read8(m+0x48)))
                <<" anim "<<int(vm.read8(m+0x3c))<<'/'<<int(vm.read8(m+0x3d))<<'/'<<int(vm.read8(m+0x3e))<<'/'<<int(vm.read8(m+0x3f))<<" blend "<<int(vm.read8(m+0x40))<<" on "<<int(vm.read8(m+0x41))<<"\n";
        for(const auto& [a,n]:vm.skipped)std::cout<<"  guarded call 0x"<<std::hex<<a<<std::dec<<" x"<<n<<"\n";
        std::vector<std::pair<std::uint32_t,std::uint64_t>> calls(trace.begin(),trace.end());std::sort(calls.begin(),calls.end());
        std::cout<<"resident calls:";for(const auto& [a,n]:calls)if(a<0x8006d264)std::cout<<" "<<std::hex<<a<<std::dec<<"x"<<n;std::cout<<"\n";
        {   // executed overlay code as contiguous ranges (the chosen classes' handlers)
            std::vector<std::uint32_t> at;for(const auto& [a,n]:pcs)if(a>=0x8006d264 && a<0x80200000)at.push_back(a);
            std::sort(at.begin(),at.end());std::cout<<"overlay code executed:";
            for(std::size_t i=0;i<at.size();) {std::size_t j=i;while(j+1<at.size() && at[j+1]-at[j]<=16)++j;
                std::cout<<" "<<std::hex<<at[i]<<"-"<<at[j]+4<<std::dec;i=j+1;}
            std::cout<<'\n';
        }
        std::cout<<"overlay calls:";for(const auto& [a,n]:calls)if(a>=0x8006d264 && a<0x80200000)std::cout<<" "<<std::hex<<a<<std::dec<<"x"<<n;std::cout<<"\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}

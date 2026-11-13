// Execute the exact generated Glimmer dispatch extension on the retained VM.
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/overlay_vm.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw FormatError("usage: soi_container_gate_check PATCHED_OVERLAY16");
        std::ifstream in(argv[1],std::ios::binary);Bytes overlay(std::istreambuf_iterator<char>(in),{});
        AssetStore assets("data/disc");const auto exe=assets.read_all(find_game_executable(assets));
        unsigned cases=0;
        for(unsigned type:{0xc8u,0xc9u})for(unsigned flags:{0u,0x10000u,0x20000u,0x40000u,0x80000u,0x100000u,0x90000u}) {
            OverlayVm vm;vm.load(read_u32(exe,0x18),std::span(exe).subspan(0x800,read_u32(exe,0x1c)));
            vm.load(0x8006d264,overlay);constexpr unsigned moby=0x800a0000;
            vm.r[3]=type;vm.r[20]=moby;vm.write32(moby+24,flags);
            int gate=-1;
            vm.hle[0x800738f4]=[&](OverlayVm&){gate=1;};
            vm.hle[0x8007e1ac]=[&](OverlayVm&){gate=0;};
            vm.call(0x80070540,{},1000);
            if(!vm.fault.empty())throw FormatError(vm.fault);
            const bool expected=type==0xc8 || (flags&0x160000)!=0;
            if(gate!=int(expected) || (!expected && vm.read32(moby+24)!=0))throw FormatError("Gate mismatch");
            std::cout << "ORIGINAL DISPATCH + EXTENSION class " << std::hex << type << " hit " << flags
                      << " continue " << (gate?0x800738f4:0x8007e1ac) << std::dec << " instructions " << vm.executed << '\n';
            ++cases;
        }
        std::cout << "BOUNDED CONTAINER GATE EQUALITY " << cases << " C8/C9 cases; existing C8 continuation retained; no original/native interaction claim\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}

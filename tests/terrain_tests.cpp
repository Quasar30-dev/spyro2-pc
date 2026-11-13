#include "spyro/level.hpp"
#include <iostream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
void put(Bytes& bytes,std::size_t at,std::uint32_t value,unsigned size=4) {
    for(unsigned i=0;i<size;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));
}
Bytes scene(bool translucent,unsigned mode) {
    // One independently authored PSX terrain quad with a four-color CLUT:
    // transparent, black STP, red STP, opaque green.
    Bytes bytes(0x80400);put(bytes,0,0x400);put(bytes,4,0x80000);
    put(bytes,8,64);put(bytes,12,140);put(bytes,64,56);put(bytes,68,1);
    for(unsigned q=0;q<6;++q) {
        const auto d=72+q*8;put(bytes,d+2,0x1020,2);
        bytes[d+4]=bytes[d+5]=31;bytes[d+6]=static_cast<std::uint8_t>(8|(mode<<5));
        bytes[d+7]=translucent?0x80:0;
    }
    constexpr unsigned region=120;
    put(bytes,region,84);put(bytes,region+4,4);put(bytes,region+8,1);put(bytes,region+12,12);
    bytes[region+36]=4;bytes[region+37]=1;bytes[region+38]=1;
    put(bytes,region+44,0);put(bytes,region+48,64u<<21);
    put(bytes,region+52,64u<<10);put(bytes,region+56,(64u<<21)|(64u<<10));
    put(bytes,region+60,0x808080);put(bytes,region+64,0x808080);
    put(bytes,region+68,0x03020100);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<8;++x)put(bytes,0x400+(y*512+x)*2,0x3210,2);
    const unsigned palette=0x400+64*512*2;
    put(bytes,palette+2,0x8000,2);put(bytes,palette+4,0x801f,2);put(bytes,palette+6,0x03e0,2);
    return bytes;
}
}
int main() {
    try {
        for(unsigned mode=0;mode<4;++mode) {
            Level level;decode_terrain(scene(true,mode),level);
            check(level.terrain_materials.size()==1 && level.terrain_materials[0].translucent &&
                level.terrain_materials[0].blend_mode==mode,"Terrain primitive flag / TPAGE blend was lost");
            check(level.visual_vertices.size()==6 && level.visual_vertices.back().material==0,"Terrain material not attached to the quad");
            check(level.texture_rgba[3]==0 && level.texture_rgba[7]==128 &&
                level.texture_rgba[11]==128 && level.texture_rgba[15]==255,"Zero / STP / opaque CLUT entries were conflated");
            check(level.texture_rgba[4]==0 && level.texture_rgba[8]==255 && level.texture_rgba[13]==255,
                "Water palette RGB changed during transparency decode");
            check(level.visual_vertices[0].color.x==1,"Retail neutral vertex modulation changed");
        }
        Level opaque;decode_terrain(scene(false,1),opaque);
        check(!opaque.terrain_materials[0].translucent && opaque.texture_rgba[7]==128,
            "STP incorrectly enables transparency on an opaque primitive");
        auto bad=scene(true,1);bad[72+3*8+7]=0;
        bool rejected=false;try {Level level;decode_terrain(bad,level);}catch(const FormatError&){rejected=true;}
        check(rejected,"Unmapped quadrant blend disagreement was silently flattened");
        std::cout<<checks<<" terrain checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "spyro/level.hpp"
#include <iostream>
#include <fstream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(why);}
void put(Bytes& b,unsigned at,unsigned word,unsigned n=4){for(unsigned i=0;i<n;++i)b.at(at+i)=static_cast<std::uint8_t>(word>>(8*i));}
template<class F>void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const FormatError&){rejected=true;}check(rejected,why);}
Bytes fixture() {
    Bytes b(70);put(b,0,0x204080);put(b,4,1);put(b,8,12);
    put(b,20,512,2);b[24]=4;b[25]=3;put(b,26,static_cast<unsigned>(-512),2);put(b,28,6,2);put(b,30,4,2);
    put(b,32,0);put(b,36,1024u<<21);put(b,40,1024u<<10);put(b,44,512);
    put(b,48,255);put(b,52,255<<8);put(b,56,255<<16);
    put(b,60,2|(1<<10)|(2<<17));put(b,64,1|(2<<8),2);put(b,66,3|(1<<8),2);put(b,68,0x8000|2|(2<<8),2);return b;
}
void basic() {
    auto data=fixture();Level level;decode_sky(data,level);
    check(level.sky_regions.size()==1 && level.sky_vertices.size()==9,"Sky fan/strip triangle count");
    check(length(level.sky_vertices[0].direction-Vec3{-0.5f,0,0.5f})<1e-6f,"Sky packed axes/offset");
    check(level.sky_vertices[0].color.z==1 && level.sky_vertices[2].color.x==1,"Sky vertex palette associations");
    check(level.sky_vertices[3].color.x==1 && level.sky_vertices[4].color.y==1 && level.sky_vertices[5].direction.y==0.5f,"Sky fan continuation");
    check(level.sky_vertices[6].color.y==1 && level.sky_vertices[7].direction.y==0.5f,"Sky strip flip did not exchange the shared edge");
    auto broken=data;put(broken,8,2);rejects([&]{decode_sky(broken,level);},"Sky accepted directory overlap");
    broken=data;put(broken,64,7,2);rejects([&]{decode_sky(broken,level);},"Sky accepted missing vertex");
    broken=data;put(broken,66,3|(4<<8),2);rejects([&]{decode_sky(broken,level);},"Sky accepted missing colour");
    broken=data;broken.resize(69);rejects([&]{decode_sky(broken,level);},"Sky accepted truncated fan");
    level.sky_regions.resize(2);Bytes pvs{7,0x81,7,0,255,0,1,3,0,0,3,0,1};decode_sky_visibility(pvs,level);
    auto visible=visible_sky_regions(level,{0,0,0});check(!visible[0] && visible[1],"Sky octree low child/remove delta");
    visible=visible_sky_regions(level,{200,200,200});check(visible[0] && !visible[1],"Sky octree high child/sibling skip");
    visible=visible_sky_regions(level,{200,0,0});check(visible[0] && visible[1],"Missing octant must discard parent deltas and draw the complete sky");
    pvs[10]=0;rejects([&]{decode_sky_visibility(pvs,level);},"Sky accepted zero-size child");
    pvs={3,0,2};rejects([&]{decode_sky_visibility(pvs,level);},"Sky accepted missing visibility region");
    decode_sky_visibility({},level);check(visible_sky_regions(level,{-30,80,400})[0],"Absent PVS must show all regions");
}
void retail_checks(const std::filesystem::path& source,const std::filesystem::path& masks) {
    std::ofstream output;if(!masks.empty())output.open(masks);
    AssetStore assets(source);WadArchive wad(assets);unsigned triangles=0,regions=0,trees=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto level=decode_level(wad.read(slot),slot);
        check(!level.sky_regions.empty() && !level.sky_vertices.empty(),"Retail scene has no sky");
        unsigned end=0;
        for(const auto& r:level.sky_regions){check(r.first==end && r.count%3==0,"Sky region range discontinuity");end+=r.count;}
        check(end==level.sky_vertices.size(),"Sky region ranges omit triangles");
        for(const auto& v:level.sky_vertices)check(finite(v.direction) && finite(v.color) && length(v.direction)<2 && v.color.x>=0 && v.color.x<=1,"Invalid retail sky vertex");
        for(unsigned x=0;x<8;++x)for(unsigned y=0;y<8;++y)for(unsigned z=0;z<8;++z) {
            const auto visible=visible_sky_regions(level,{x*32.0f,y*32.0f,z*32.0f});
            std::uint64_t mask=0;for(unsigned bit=0;bit<64;++bit)if(visible[bit])mask|=std::uint64_t(1)<<bit;
            if(output.is_open())output<<slot<<' '<<x*32768<<' '<<z*32768<<' '<<y*32768<<' '<<std::hex<<mask<<std::dec<<'\n';
        }
        regions+=static_cast<unsigned>(level.sky_regions.size());triangles+=static_cast<unsigned>(level.sky_vertices.size()/3);trees+=!level.sky_visibility.empty();
        std::cout<<slot<<": "<<level.sky_regions.size()<<" sky regions, "<<level.sky_vertices.size()/3<<" triangles, "<<level.sky_visibility.size()<<" PVS bytes\n";
    }
    check(trees==17,"Retail sky PVS coverage changed");
    std::cout<<"41 skies: "<<regions<<" regions, "<<triangles<<" triangles, "<<trees<<" visibility trees\n";
}
}
int main(int argc,char** argv){try{basic();if(argc>1)retail_checks(std::filesystem::u8path(argv[1]),argc>2?std::filesystem::u8path(argv[2]):std::filesystem::path{});std::cout<<checks<<" sky checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// Writes every resident interface sprite (power crystal, caps, cells, ...) of
// a retail scene as PPM files plus a contact sheet, for art reference.
#include "spyro/assets.hpp"
#include "spyro/interface.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace spyro;
namespace {
void write_ppm(const std::filesystem::path& path,const Bytes& rgba,unsigned atlas_width,const HudSprite& s,unsigned scale) {
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<s.width*scale<<' '<<s.height*scale<<"\n255\n";
    for(unsigned y=0;y<s.height*scale;++y)for(unsigned x=0;x<s.width*scale;++x) {
        const auto* p=&rgba[(std::size_t(s.y+y/scale)*atlas_width+s.x+x/scale)*4];
        // Transparent texels on magenta so the silhouette is obvious.
        const unsigned char c[3]{p[3]?p[0]:std::uint8_t(255),p[3]?p[1]:std::uint8_t(0),p[3]?p[2]:std::uint8_t(255)};
        out.write(reinterpret_cast<const char*>(c),3);
    }
}
}
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_interface_probe <retail data> <output dir> [slot]\n";return 2;}
    AssetStore assets(argv[1]);WadArchive wad(assets);const unsigned slot=argc>3?std::stoul(argv[3]):17;
    const auto exe=assets.read_all("SCUS_944.25");
    const auto ui=decode_interface(wad.read(slot),exe,wad.read(4));
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    for(std::size_t i=0;i<ui.resident.size();++i) {
        const auto& s=ui.resident[i];if(!s.width || !s.height)continue;
        write_ppm(out/("resident-"+std::to_string(i)+".ppm"),ui.rgba,ui.width,s,4);
        std::cout<<"resident "<<i<<' '<<s.x<<','<<s.y<<' '<<s.width<<'x'<<s.height<<'\n';
    }
    return 0;
}

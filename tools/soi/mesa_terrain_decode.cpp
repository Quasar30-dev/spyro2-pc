// Offline probe of the existing native terrain decoder. No application/game
// session and no changes to the core library or main executable.
#include "spyro/level.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: mesa_terrain_decode ARCHIVE OUT_RGBA");
        std::ifstream in(argv[1],std::ios::binary);
        if(!in)throw std::runtime_error("archive open failed");
        std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in),{});
        spyro::Level level;spyro::decode_terrain(bytes,level);
        std::ofstream out(argv[2],std::ios::binary);
        out.write(reinterpret_cast<const char*>(level.texture_rgba.data()),level.texture_rgba.size());
        if(!out)throw std::runtime_error("atlas write failed");
        std::cout<<"NATIVE ATLAS "<<level.texture_width<<' '<<level.texture_height
                 <<" bytes "<<level.texture_rgba.size()<<" faces "<<level.terrain_faces<<'\n';
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

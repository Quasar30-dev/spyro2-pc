#include "spyro/level.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace spyro;
int main(int argc,char** argv){try{
    if(argc!=3 && argc!=4)throw FormatError("Usage: spyro_ripto_probe retail-copy output-directory [slot]");
    const unsigned slot=argc==4?unsigned(std::stoul(argv[3])):71;
    AssetStore store(argv[1]);WadArchive wad(store);const auto bytes=wad.read(slot),exe=store.read_all("SCUS_944.25");
    auto models=decode_object_models(bytes,exe);auto level=decode_level(bytes,slot);initialize_retail_objects(level,models);
    const std::filesystem::path dir(argv[2]);std::filesystem::create_directories(dir);
    std::ofstream out(dir/"retail-banks.json");out<<std::setprecision(8)<<"{\"models\":[";bool comma=false;
    for(const auto& [type,m]:models.models) {
        if(slot==71 && type<0x288)continue;if(comma)out<<',';comma=true;
        out<<"{\"type\":"<<type<<",\"animated\":"<<(m.animated?"true":"false")<<",\"meshes\":"<<m.meshes.size()<<",\"cylinder\":";
        if(m.cylinder){const auto& c=*m.cylinder;out<<'['<<c.broad_radius<<','<<c.radius<<','<<c.cap_radius<<','<<c.bottom<<','<<c.top<<']';}else out<<"null";
        out<<",\"clips\":[";
        for(unsigned c=0;c<m.animations.size();++c){if(c)out<<',';out<<"{\"id\":"<<c<<",\"frames\":[";
            const auto& a=m.animations[c];for(unsigned f=0;f<a.frames.size();++f){if(f)out<<',';const auto& v=a.frames[f];
                out<<"{\"meta\":"<<v.metadata<<",\"ticks\":"<<v.ticks<<",\"mesh\":"<<v.mesh<<",\"vertices\":[";
                for(unsigned k=0;k<v.vertices.size();++k){if(k)out<<',';auto p=v.vertices[k];out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
                out<<"]}";}out<<"]}";}
        out<<"]}";
    }
    out<<"],\"objects\":[";
    for(unsigned i=0;i<level.objects.size();++i){if(i)out<<',';const auto& o=level.objects[i];out<<"{\"id\":"<<o.id<<",\"type\":"<<o.type<<",\"size\":"<<unsigned(o.size)<<",\"state\":"<<int(o.state)<<",\"position\":["<<o.position.x<<','<<o.position.y<<','<<o.position.z<<"]}";}
    out<<"]}\n";if(!out)throw FormatError("Cannot write Ripto probe");
    if(slot!=71) {
        std::ofstream tex(dir/"objects.rgba",std::ios::binary);tex.write(reinterpret_cast<const char*>(models.texture_rgba.data()),models.texture_rgba.size());
        std::ofstream faces(dir/"model-faces.json");faces<<"{\"width\":"<<models.texture_width<<",\"height\":"<<models.texture_height<<",\"models\":[";bool more=false;
        for(const auto& [type,m]:models.models)if(m.animated){if(more)faces<<',';more=true;faces<<"{\"type\":"<<type<<",\"faces\":[";bool fm=false;
            for(const auto& mesh:m.meshes)for(const auto& f:mesh.faces){if(fm)faces<<',';fm=true;faces<<"{\"textured\":"<<(f.textured?"true":"false")<<",\"quad\":"<<(f.quad?"true":"false")<<",\"indices\":[";
                for(unsigned i=0;i<4;++i){if(i)faces<<',';faces<<f.vertices[i];}faces<<"],\"uv\":[";
                for(unsigned i=0;i<4;++i){if(i)faces<<',';faces<<'['<<f.uv[i][0]<<','<<f.uv[i][1]<<']';}faces<<"],\"colors\":[";
                for(unsigned i=0;i<4;++i){if(i)faces<<',';faces<<'['<<f.colors[i].x<<','<<f.colors[i].y<<','<<f.colors[i].z<<']';}faces<<"]}";}
            faces<<"]}";}faces<<"]}";
    }
    std::cout<<"Decoded retail boss actors, collision, textures and animation metadata\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

// Export a retail scene's terrain for Blender authoring: the visual mesh
// (vertex colour + atlas UV, opaque / translucent material groups), the
// collision mesh (ground / water / special-surface groups), the texture atlas
// as raw RGBA plus its size, and a JSON summary. Coordinates are Blender Z-up
// in retail units / 1024, the custom-level convention (engine = (x, z, y)).
// Usage: spyro_scene_export <retail data> <wad slot> <output dir>
#include "spyro/assets.hpp"
#include "spyro/level.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace spyro;
namespace {
void write_vec(std::ostream& o,Vec3 p){o<<p.x<<' '<<p.z<<' '<<p.y;}
void json_vec(std::ostream& o,Vec3 p){o<<'['<<p.x<<','<<p.z<<','<<p.y<<']';}
}
int main(int argc,char** argv) {
    if(argc!=4){std::cerr<<"Usage: spyro_scene_export <retail data> <wad slot> <output dir>\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const unsigned slot=std::stoul(argv[2]);
        const std::filesystem::path dir(argv[3]);std::filesystem::create_directories(dir);
        const auto level=decode_level(wad.read(slot),slot);
        {
            std::ofstream out(dir/"atlas.rgba",std::ios::binary);
            out.write(reinterpret_cast<const char*>(level.texture_rgba.data()),static_cast<std::streamsize>(level.texture_rgba.size()));
        }
        std::ofstream obj(dir/"visual.obj");obj<<std::setprecision(7)<<"# WAD "<<slot<<" visual terrain, retail/1024, Z up\nmtllib visual.mtl\n";
        const auto& v=level.visual_vertices;
        for(const auto& p:v){obj<<"v ";write_vec(obj,p.position);obj<<' '<<p.color.x<<' '<<p.color.y<<' '<<p.color.z<<'\n';}
        for(const auto& p:v)obj<<"vt "<<p.u<<' '<<1-p.v<<'\n';
        for(int pass=0;pass<2;++pass) {
            obj<<"usemtl "<<(pass?"translucent":"opaque")<<"\n";
            for(std::size_t i=0;i+2<v.size();i+=3) {
                const bool translucent=level.terrain_materials.at(v[i].material).translucent;
                if(translucent!=(pass==1))continue;
                obj<<"f "<<i+1<<'/'<<i+1<<' '<<i+2<<'/'<<i+2<<' '<<i+3<<'/'<<i+3<<'\n';
            }
        }
        std::ofstream mtl(dir/"visual.mtl");mtl<<"newmtl opaque\nmap_Kd atlas.png\nnewmtl translucent\nmap_Kd atlas.png\nd 0.5\n";
        std::ofstream col(dir/"collision.obj");col<<std::setprecision(7)<<"# WAD "<<slot<<" collision\n";
        for(const auto& t:level.triangles){col<<"v ";write_vec(col,t.a);col<<"\nv ";write_vec(col,t.b);col<<"\nv ";write_vec(col,t.c);col<<'\n';}
        for(int group=0;group<3;++group) {
            col<<"g "<<(group==0?"ground":group==1?"water":"special")<<"\n";
            for(std::size_t i=0;i<level.triangles.size();++i) {
                const auto& t=level.triangles[i];
                const int g=t.water?1:(t.surface_type?2:0);
                if(g==group)col<<"f "<<i*3+1<<' '<<i*3+2<<' '<<i*3+3<<'\n';
            }
        }
        // The scene's sky: vertex-coloured triangles per region. The game draws
        // them around the camera with its translation cleared (80029170), so
        // these coordinates are relative to the eye, not placed in the world.
        std::ofstream sky(dir/"sky.obj");sky<<std::setprecision(7)<<"# WAD "<<slot<<" sky (camera-centred), retail/1024, Z up\n";
        for(const auto& s:level.sky_vertices){sky<<"v ";write_vec(sky,s.direction);sky<<' '<<s.color.x<<' '<<s.color.y<<' '<<s.color.z<<'\n';}
        for(std::size_t r=0;r<level.sky_regions.size();++r) {
            const auto& region=level.sky_regions[r];sky<<"g region"<<r<<"\n";
            for(unsigned i=region.first;i+2<region.first+region.count;i+=3)sky<<"f "<<i+1<<' '<<i+2<<' '<<i+3<<'\n';
        }
        std::ofstream json(dir/"scene.json");
        json<<std::setprecision(7)<<"{\"slot\":"<<slot<<",\"atlas\":{\"width\":"<<level.texture_width<<",\"height\":"<<level.texture_height
            <<"},\"visual_triangles\":"<<v.size()/3<<",\"collision_triangles\":"<<level.triangles.size()<<",\"water_triangles\":"<<level.water_triangles
            <<",\"spawn\":";json_vec(json,level.spawn);json<<",\"bounds\":[";json_vec(json,level.bounds.min);json<<',';json_vec(json,level.bounds.max);
        json<<"],\"sky\":["<<level.sky_color.x<<','<<level.sky_color.y<<','<<level.sky_color.z<<"]}\n";
        std::cout<<"WAD "<<slot<<": "<<v.size()/3<<" visual triangles, "<<level.triangles.size()<<" collision triangles, atlas "
            <<level.texture_width<<"x"<<level.texture_height<<"\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}

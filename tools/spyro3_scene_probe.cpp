// Read-only structural/ground-support census of every YOTD scene and subarea.
// The report contains derived metadata, never archive payloads.
#include "spyro/level.hpp"
#include "spyro/coordinates.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace spyro;
namespace {
void vector(std::ostream& out,Vec3 p){out<<p.x<<'\t'<<p.y<<'\t'<<p.z;}
std::string clean(std::string text){for(auto& c:text)if(c=='\t' || c=='\n' || c=='\r')c=' ';return text;}
void hit(std::ostream& out,const std::optional<RayHit>& h) {
    if(h)out<<h->distance<<'\t'<<h->point.y<<'\t'<<h->normal.y;
    else out<<"none\tnone\tnone";
}
}
int main(int argc,char** argv)try {
    if(argc<2 || argc>3)throw std::runtime_error("Usage: spyro3_scene_probe <game source> [report.tsv]");
    AssetStore source(std::filesystem::u8path(argv[1]));
    const auto exe=source.read_all(find_game_executable(source));const auto& profile=game_profile(exe);
    if(!profile.yotd())throw std::runtime_error("The scene census requires the explicit YOTD profile");
    WadArchive wad(source,&profile);
    std::ofstream file;if(argc==3){file.open(std::filesystem::u8path(argv[2]));if(!file)throw std::runtime_error("Cannot create scene report");}
    std::ostream& out=argc==3?file:std::cout;
    out<<std::setprecision(9)<<"slot\tarea\tstatus\tterrain_regions\tterrain_faces\tvisual_triangles\tcollision_triangles\twater_triangles\tcollision_from_terrain\ttexture_width\ttexture_height\tsky_regions\tsky_triangles\tcollision_offset\tcollision_bytes\tspawn_x\tspawn_y\tspawn_z\tauthored_spawn_x\tauthored_spawn_y\tauthored_spawn_z\tspawn_projected\tdown_distance\tdown_floor_y\tdown_normal_y\tup_distance\tup_floor_y\tup_normal_y\tnear_support_drop\tnear_support_y\tnear_support_normal_y\tbounds_min_x\tbounds_min_y\tbounds_min_z\tbounds_max_x\tbounds_max_y\tbounds_max_z\terror\n";
    unsigned areas=0,failures=0,fallbacks=0,projected=0;std::uint64_t visuals=0,collisions=0;
    for(const auto slot:profile.levels()) {
        Bytes archive;
        unsigned count=0;
        try{archive=wad.read(slot);count=level_area_count(archive,profile);}
        catch(const std::exception& e){++failures;out<<slot<<"\t0\tERROR\t"<<clean(e.what())<<'\n';out.flush();continue;}
        for(unsigned area=0;area<count;++area) {
            ++areas;
            try {
                const auto level=decode_level(archive,slot,profile,area);
                if(level.visual_vertices.empty() || level.triangles.empty() || level.sky_vertices.empty())throw std::runtime_error("Empty decoded geometry");
                if(level.source_game!=profile.build || level.source_area!=area)throw std::runtime_error("Scene source identity was lost");
                if(!finite(level.spawn) || !finite(level.bounds.min) || !finite(level.bounds.max))throw std::runtime_error("Non-finite scene coordinates");
                for(const auto& v:level.visual_vertices)if(!finite(v.position) || !finite(v.color) || !std::isfinite(v.u) || !std::isfinite(v.v) || v.material>=level.terrain_materials.size())throw std::runtime_error("Invalid rendered terrain vertex");
                for(const auto& t:level.triangles)if(!finite(t.a) || !finite(t.b) || !finite(t.c))throw std::runtime_error("Non-finite collision triangle");
                const auto down=level.raycast(level.spawn+Vec3{0,.75f,0},{0,-1,0},256);
                const auto up=level.raycast(level.spawn+Vec3{0,.01f,0},{0,1,0},256);
                const auto support=level.ground_support(level.spawn+retail::origin_above_feet,retail::spyro_origin_height,2);
                out<<slot<<'\t'<<area<<"\tOK\t"<<level.terrain_regions<<'\t'<<level.terrain_faces<<'\t'<<level.visual_vertices.size()/3<<'\t'<<level.triangles.size()<<'\t'<<level.water_triangles<<'\t'<<level.collision_from_terrain<<'\t'<<level.texture_width<<'\t'<<level.texture_height<<'\t'<<level.sky_regions.size()<<'\t'<<level.sky_vertices.size()/3<<'\t'<<level.collision_offset<<'\t'<<level.collision_bytes<<'\t';
                vector(out,level.spawn);out<<'\t';vector(out,level.authored_spawn);out<<'\t'<<level.spawn_projected<<'\t';
                hit(out,down);out<<'\t';hit(out,up);out<<'\t';hit(out,support);out<<'\t';
                vector(out,level.bounds.min);out<<'\t';vector(out,level.bounds.max);out<<"\t\n";
                fallbacks+=level.collision_from_terrain;projected+=level.spawn_projected;visuals+=level.visual_vertices.size()/3;collisions+=level.triangles.size();
            } catch(const std::exception& e) {
                ++failures;out<<slot<<'\t'<<area<<"\tERROR";for(unsigned column=3;column<37;++column)out<<'\t';out<<'\t'<<clean(e.what())<<'\n';
            }
            out.flush();
        }
    }
    std::cerr<<areas<<" areas, "<<failures<<" errors, "<<fallbacks<<" terrain collision fallbacks, "<<projected<<" projected spawns, "<<visuals<<" visual triangles, "<<collisions<<" collision triangles\n";
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

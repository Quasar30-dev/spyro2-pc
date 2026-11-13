// Ranks a level's objects by how open the ground around them is, for filming
// effects that need room on every side (Furies, blasts). For each object
// standing on level floor it casts 32 horizontal rays at three heights and
// reports the nearest terrain, ignoring objects within that radius.
// Usage: spyro_open_spots <retail data> <slot> [count]
//        spyro_open_spots <retail data> <slot> --headings <record>
//        spyro_open_spots <retail data> <slot> --enemies
#include "spyro/assets.hpp"
#include "spyro/runtime.hpp"
#include "spyro/travel.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

using namespace spyro;
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_open_spots <retail data> <slot> [count]\n";return 2;}
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        const bool headings=argc>4 && std::string(argv[3])=="--headings";
        const bool enemies=argc>3 && std::string(argv[3])=="--enemies";
        const unsigned slot=std::stoul(argv[2]),count=argc>3 && !headings && !enemies?std::stoul(argv[3]):12;
        const auto archive=wad.read(slot);Level level=decode_level(archive,slot);auto models=decode_object_models(archive,exe);
        initialize_retail_objects(level,models);
        if(argc>3 && std::string(argv[3])=="--enemies") {
            // Spirit-carrying records (the enemies a level counts): record, class, spirits, position.
            for(unsigned i=0;i<level.objects.size();++i)if(const auto spirit=level.objects[i].record[83]&127)
                std::cout<<"enemy "<<i<<" class "<<std::hex<<level.objects[i].type<<std::dec<<" spirit "<<spirit<<" at "
                         <<level.objects[i].position.x<<" "<<level.objects[i].position.y<<" "<<level.objects[i].position.z<<"\n";
            return 0;
        }
        if(headings) {
            // Clear flying distance from one record, per heading, 2 and 5 above its floor.
            const auto& o=level.objects.at(std::stoul(argv[4]));
            const auto under=level.raycast(o.position+Vec3{0,.5f,0},{0,-1,0},4);const auto base=under?under->point:o.position;
            for(unsigned k=0;k<16;++k) {
                const float a=k*std::numbers::pi_v<float>/8;const Vec3 d{std::sin(a),0,std::cos(a)};
                std::cout<<"heading "<<a<<" dir "<<d.x<<" "<<d.z;
                for(const float h:{2.0f,5.0f}){const auto hit=level.raycast(base+Vec3{0,h,0},d,60);std::cout<<" clear@"<<h<<" "<<(hit?hit->distance:60.0f);}
                std::cout<<"\n";
            }
            return 0;
        }
        struct Spot {unsigned record{};float open{};Vec3 floor;};std::vector<Spot> spots;
        for(unsigned i=0;i<level.objects.size();++i) {
            const auto& o=level.objects[i];
            const auto under=level.raycast(o.position+Vec3{0,.5f,0},{0,-1,0},4);
            if(!under || under->normal.y<.9f)continue;
            float open=1e9f;
            for(const float h:{.6f,2.5f,5.0f})for(unsigned k=0;k<32;++k) {
                const float a=k*std::numbers::pi_v<float>/16;
                const auto hit=level.raycast(under->point+Vec3{0,h,0},{std::sin(a),0,std::cos(a)},20);
                open=std::min(open,hit?hit->distance:20.0f);
            }
            float crowd=20;
            for(const auto& other:level.objects)if(&other!=&o && models.models.contains(other.type) && !models.models.at(other.type).meshes.empty()) {
                const auto& mesh=models.models.at(other.type).meshes.front();
                if(mesh.radius*object_model_scale(other)>.6f)crowd=std::min(crowd,length(other.position-under->point));
            }
            spots.push_back({i,std::min(open,crowd),under->point});
        }
        std::sort(spots.begin(),spots.end(),[](const auto& a,const auto& b){return a.open>b.open;});
        for(unsigned k=0;k<std::min<std::size_t>(count,spots.size());++k)
            std::cout<<"record "<<spots[k].record<<" class "<<std::hex<<level.objects[spots[k].record].type<<std::dec<<" open "<<spots[k].open
                     <<" at "<<spots[k].floor.x<<" "<<spots[k].floor.y<<" "<<spots[k].floor.z<<"\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}

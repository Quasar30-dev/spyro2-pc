#include "spyro/level.hpp"
#include <iostream>
#include <set>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool ok,const char* what){++checks;if(!ok)throw FormatError(what);}
void basic() {
    ObjectFace basket;basket.quad=true;basket.textured=true;
    const std::array<Vec3,4> wall{{{-1,-1,1},{1,-1,1},{-1,1,1},{1,1,1}}};
    auto facing=object_face_view(basket,wall,{0,0,0});
    check(facing.count==4 && !facing.backface,"Plain basket quad front was inverted");
    check(object_face_view(basket,wall,{0,0,2}).count==0,"Plain basket quad exposed its interior");
    basket.flags=1;check(object_face_view(basket,wall,{0,0,2}).count==0,"Plain quad incorrectly inherited triangle two-sided flag");
    ObjectFace face;face.animated=true;face.flags=0x20;face.shiny_normal={0,0,-100};face.palette_valid=false;
    RetailObject metal;metal.material_color=0x2a505050;
    const ObjectBasis identity{{1,0,0},{0,1,0},{0,0,1}};
    auto color=normal_object_color(metal,face,identity,{1,0,0},{0,1,0},{0,0,1});
    // Independently evaluated GTE MVMVA -> GPF -> CC: RGB 156 (+/-1).
    check(object_normal_material(metal,face) && std::abs(color.x*255-156)<1,"Normal-lit metal equation");
    face.shiny_normal.z=100;
    color=normal_object_color(metal,face,identity,{1,0,0},{0,1,0},{0,0,1});
    check(color.x*255<4,"Normal direction did not affect the metal shading");
    face.shiny_normal={0,0,-100};face.textured=true;
    color=normal_object_color(metal,face,identity,{1,0,0},{0,1,0},{0,0,1});
    check(color.x>1.2f,"Textured material lost PSX 128-neutral modulation");
    face.quad=true;check(!object_normal_material(metal,face),"Palette quad interpreted as a normal");
    face.quad=false;face.textured=false;metal.material_color=0x90000000;
    check(object_environment_material(metal,face),"High-bit reflection material ignored");
    color=animated_object_color(metal,face,{128/255.0f,128/255.0f,128/255.0f});
    check(std::abs(color.x-1)<0.001f,"Reflection packet did not convert palette units to textured modulation");
}
void retail_audit(const std::filesystem::path& path) {
    AssetStore assets(path);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
    unsigned banks=0,instances=0,faces=0,normals=0,reflections=0;std::set<unsigned> normal_types,reflection_types;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);const auto models=decode_object_models(archive,exe);
        auto level=decode_level(archive,slot);initialize_retail_objects(level,models);++banks;
        for(const auto& object:level.objects) {
            const auto found=models.models.find(object.type);if(found==models.models.end())continue;++instances;
            const auto basis=object_basis(object);
            for(const auto& mesh:found->second.meshes)for(const auto& face:mesh.faces) {
                ++faces;
                if(object_normal_material(object,face)) {
                    ++normals;normal_types.insert(object.type);
                    for(const auto forward:std::array<Vec3,4>{{{0,0,1},{1,0,0},{0,0,-1},{-1,0,0}}}) {
                        const auto color=normal_object_color(object,face,basis,cross(Vec3{0,1,0},forward),{0,1,0},forward);
                        check(finite(color) && color.x>=0 && color.y>=0 && color.z>=0,"Invalid metal shade");
                    }
                } else if(!face.palette_valid) {
                    std::cerr<<"Missing material slot "<<slot<<" record "<<object.id<<" type "<<object.type<<" word "<<std::hex<<object.material_color<<std::dec<<'\n';
                    check(false,"Instance cannot render its mesh material");
                }
                if(object_environment_material(object,face)) {
                    ++reflections;reflection_types.insert(object.type);
                    for(unsigned k=0;k<(face.quad?4u:3u);++k) {
                        const auto uv=object_environment_uv(models,basis*mesh.vertices[face.vertices[k]],5,120);
                        check(std::isfinite(uv[0]) && std::isfinite(uv[1]) && uv[0]>0 && uv[0]<1 && uv[1]>0 && uv[1]<1,"Reflection outside retail atlas");
                    }
                }
            }
        }
        std::cout<<"WAD "<<slot<<": "<<models.models.size()<<" object banks checked\n";
    }
    check(normals>1000 && reflections>0,"Retail audit did not exercise the special materials");
    std::cout<<banks<<" levels, "<<instances<<" instances, "<<faces<<" face/instance combinations; "<<normals<<" normal-lit, "<<reflections<<" reflective\nNormal classes:";
    for(auto type:normal_types)std::cout<<" "<<std::hex<<type;
    std::cout<<"\nReflective classes:";for(auto type:reflection_types)std::cout<<" "<<std::hex<<type;std::cout<<std::dec<<'\n';
}
}
int main(int argc,char** argv){try{basic();if(argc>1)retail_audit(argv[1]);std::cout<<checks<<" object material checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "spyro/level.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/travel.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
using namespace spyro;
void vec(std::ostream& o,Vec3 p){o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
void pixels(const std::filesystem::path& dir,const std::string& name,const Bytes& rgba,unsigned w,unsigned h){
    std::ofstream f(dir/(name+".rgba"),std::ios::binary);f.write(reinterpret_cast<const char*>(rgba.data()),rgba.size());
    std::ofstream meta(dir/(name+".json"));meta<<"{\"width\":"<<w<<",\"height\":"<<h<<"}";
}
int main(int argc,char** argv){try{
    if(argc!=3 && argc!=4)throw FormatError("Usage: level_asset_export retail-source output-directory [actor-bank-slot]");
    const unsigned slot=argc==4?static_cast<unsigned>(std::stoul(argv[3])):17;
    if(slot<15 || slot>71 || !(slot&1))throw FormatError("Asset export requires a retail level actor bank");
    AssetStore store(argv[1]);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");std::filesystem::path dir(argv[2]);std::filesystem::create_directories(dir);
    std::set<unsigned> texture_slots{15u,17u,33u,59u,slot};
    for(unsigned texture_slot:texture_slots){auto l=decode_level(wad.read(texture_slot),texture_slot);pixels(dir,"terrain-"+std::to_string(texture_slot),l.texture_rgba,l.texture_width,l.texture_height);}
    auto a=wad.read(slot);auto l=decode_level(a,slot);decode_travel(a,exe,l);auto models=decode_object_models(a,exe);initialize_retail_objects(l,models);
    pixels(dir,"objects-"+std::to_string(slot),models.texture_rgba,models.texture_width,models.texture_height);
    auto spyro=decode_spyro(wad.read(0),a,exe,wad.read(29),29);pixels(dir,"spyro",spyro.texture_rgba,spyro.texture_width,spyro.texture_height);
    std::ofstream out(dir/"retail-meshes.json");out<<std::setprecision(8)<<"{\"profile\":\"SCUS_944.25 CDC0917C\",\"source_slot\":"<<slot<<",\"models\":[";bool comma=false;
    std::vector<unsigned> types;
    for(const auto& o:l.objects)if(std::find(types.begin(),types.end(),o.type)==types.end())types.push_back(o.type);
    for(unsigned type:types){
        const auto obj=std::find_if(l.objects.begin(),l.objects.end(),[&](const auto& o){return o.type==type && o.state>=0;});
        if(obj==l.objects.end() || !models.models.contains(type))continue;
        const auto& bank=models.models.at(type);const auto pose=bank.animated?sample_object_animation(bank,obj->animation,models.animation_blends):ObjectPose{};
        const auto& mesh=bank.meshes.at(bank.animated?pose.mesh:obj->model);const auto& vertices=bank.animated?pose.vertices:mesh.vertices;
        const float scale=bank.animated?object_model_scale(*obj):1;
        if(comma)out<<',';comma=true;out<<"{\"type\":"<<type<<",\"record\":"<<obj->id<<",\"animated\":"<<(bank.animated?"true":"false")<<",\"position\":";vec(out,obj->position);out<<",\"vertices\":[";
        for(unsigned i=0;i<vertices.size();++i){if(i)out<<',';vec(out,vertices[i]*scale);}out<<"],\"faces\":[";
        bool fc=false;for(const auto& f:mesh.faces){if(fc)out<<',';fc=true;out<<"{\"indices\":[";const unsigned k[]{0,1,3,2};const unsigned n=f.quad?4:3;
            for(unsigned j=0;j<n;++j){if(j)out<<',';out<<f.vertices[f.quad?k[j]:j];}out<<"],\"uv\":[";
            for(unsigned j=0;j<n;++j){if(j)out<<',';const auto uv=f.uv[f.quad?k[j]:j];out<<'['<<uv[0]<<','<<uv[1]<<']';}out<<"],\"colors\":[";
            for(unsigned j=0;j<n;++j){if(j)out<<',';auto c=f.colors[f.quad?k[j]:j];if(f.animated)c=animated_object_color(*obj,f,c);vec(out,c);}out<<"],\"textured\":"<<(f.textured?"true":"false")<<'}';
        }out<<"]}";
    }
    out<<"],\"spyro\":{\"vertices\":[";const auto& clip=spyro.animations.at(0);
    for(unsigned i=0;i<clip.frames[0].vertices.size();++i){if(i)out<<',';vec(out,clip.frames[0].vertices[i]);}out<<"],\"faces\":[";
    for(unsigned i=0;i<clip.faces.size();++i){if(i)out<<',';const auto& f=clip.faces[i];const unsigned k[]{0,1,3,2},n=f.quad?4:3;out<<"{\"indices\":[";
        for(unsigned j=0;j<n;++j){if(j)out<<',';out<<f.vertices[f.quad?k[j]:j];}out<<"],\"uv\":[";
        for(unsigned j=0;j<n;++j){if(j)out<<',';auto uv=f.uv[f.quad?k[j]:j];out<<'['<<uv[0]<<','<<uv[1]<<']';}out<<"],\"colors\":[";
        for(unsigned j=0;j<n;++j){if(j)out<<',';vec(out,f.colors[f.quad?k[j]:j]);}out<<"],\"textured\":true}";
    }out<<"]}}";
    std::ofstream world(dir/("reference-"+std::to_string(slot)+".json"));world<<std::setprecision(8)<<"{\"slot\":"<<slot<<",\"spawn\":";vec(world,l.spawn);
    world<<",\"vertices\":[";for(unsigned i=0;i<l.visual_vertices.size();++i){if(i)world<<',';const auto& v=l.visual_vertices[i];world<<'['<<v.position.x<<','<<v.position.y<<','<<v.position.z<<','<<v.color.x<<','<<v.color.y<<','<<v.color.z<<','<<v.u<<','<<v.v<<','<<v.material<<','<<v.source_region<<']';}
    world<<"],\"objects\":[";for(unsigned i=0;i<l.objects.size();++i){if(i)world<<',';const auto& o=l.objects[i];world<<"{\"record\":"<<o.id<<",\"type\":"<<o.type<<",\"model\":"<<static_cast<unsigned>(o.model)<<",\"state\":"<<static_cast<int>(o.state)<<",\"position\":";vec(world,o.position);world<<'}';}
    world<<"],\"npcs\":[";for(unsigned i=0;i<l.npcs.size();++i){if(i)world<<',';const auto& n=l.npcs[i];world<<"{\"record\":"<<n.object<<",\"type\":"<<n.type<<",\"name\":"<<std::quoted(n.name)<<'}';}
    world<<"],\"portals\":[";for(unsigned i=0;i<l.portals.size();++i){if(i)world<<',';const auto& p=l.portals[i];world<<"{\"index\":"<<p.index<<",\"object\":"<<p.object<<",\"destination\":"<<p.destination<<",\"scene\":"<<p.scene_id<<",\"aperture\":[";for(unsigned j=0;j<5;++j){if(j)world<<',';vec(world,p.aperture[j]);}world<<"]}";}
    world<<"],\"materials\":[";for(unsigned i=0;i<l.terrain_materials.size();++i){if(i)world<<',';world<<"{\"translucent\":"<<(l.terrain_materials[i].translucent?"true":"false")<<",\"blend\":"<<l.terrain_materials[i].blend_mode<<'}';}world<<"]}";
    std::cout<<"Exported retail terrain, reference layout and actor meshes from slot "<<slot<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

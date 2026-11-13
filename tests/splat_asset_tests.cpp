#include "spyro/splats.hpp"
#include "spyro/assets.hpp"
#include <fstream>
#include <iostream>
#include <limits>

int main(){
    using namespace spyro;unsigned assertions=0;
    const auto path=std::filesystem::temp_directory_path()/"spyro-splat-parser-validation.s2gs";
    const auto check=[&](bool ok){++assertions;if(!ok)throw std::runtime_error("Scenery parser check failed");};
    const auto write=[&](GaussianSplat value,unsigned version=1,unsigned nv=3){
        std::ofstream f(path,std::ios::binary);f.write(version==1?"S2GS0001":version==2?"S2GS0002":"S2GS0003",8);unsigned count=version==1?1:0;f.write(reinterpret_cast<char*>(&count),4);
        if(version==1)f.write(reinterpret_cast<char*>(&value),sizeof(value));
        else{unsigned dim=1;f.write(reinterpret_cast<char*>(&nv),4);f.write(reinterpret_cast<char*>(&dim),4);f.write(reinterpret_cast<char*>(&dim),4);SplatSurfaceVertex triangle[3]={{{0,0,0},0,0},{{1,0,0},1,0},{{0,1,0},0,1}};if(version==3)triangle[0].color=value.color;for(auto& v:triangle)f.write(reinterpret_cast<char*>(&v),version==3?32:20);unsigned rgba=0xffffffff;f.write(reinterpret_cast<char*>(&rgba),4);}
    };
    const auto reject=[&](){bool failed=false;try{load_splats(path);}catch(const FormatError&){failed=true;}check(failed);};
    try{
        GaussianSplat good{{1,2,3},.8f,{.1f,.2f,.3f},0,{.03f,.02f,.01f},0,{.001f,.002f,.001f},0};
        write(good);auto scene=load_splats(path);check(scene.size()==1 && scene[0].position.z==3 && scene[0].reserved0>0);
        auto bad=good;bad.opacity=std::numeric_limits<float>::quiet_NaN();write(bad);reject();
        bad=good;bad.diagonal.x=-1;write(bad);reject();
        bad=good;bad.off_diagonal.x=10;write(bad);reject();
        bad=good;bad.color.x=2;write(bad);reject();
        write(good);std::filesystem::resize_file(path,20);reject();
        write(good);{std::ofstream f(path,std::ios::binary|std::ios::app);f.put('x');}reject();
        write(good,2);SplatSurface mesh;scene=load_splats(path,&mesh);check(scene.empty() && mesh.vertices.size()==3 && mesh.width==1 && mesh.rgba.size()==4);
        write(good,2,4);reject();
        write(good,3);load_splats(path,&mesh);check(mesh.vertices[0].color.y==good.color.y && mesh.vertices[1].color.x==1);
        bad=good;bad.color.z=std::numeric_limits<float>::quiet_NaN();write(bad,3);reject();
        bad=good;bad.color.x=1.5f;write(bad,3);reject();
        std::filesystem::remove(path);std::cout<<assertions<<" scenery parser checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove(path);return 1;}
}

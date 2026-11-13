// Lists Spyro's textured faces with their vertex indices, UV boxes and mean
// texel colour, and writes the decoded atlas with each face's UV box marked.
// Used to fix the Dark Spyro eye landmarks against the mounted retail model.
#include "spyro/assets.hpp"
#include "spyro/level.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/spyro_dark.hpp"
#include <fstream>
#include <iostream>
#include <set>

using namespace spyro;
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_eye_probe <retail data> <output.ppm> [slot]\n";return 2;}
    AssetStore assets(argv[1]);WadArchive wad(assets);const unsigned slot=argc>3?std::stoul(argv[3]):15;
    const auto exe=assets.read_all("SCUS_944.25");const auto archive=wad.read(slot);
    auto model=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
    const auto& clip=model.animations.at(argc>4?std::stoul(argv[4]):0u);
    const auto& source=model.texture_rgba;auto rgba=source;const unsigned w=model.texture_width,h=model.texture_height;
    std::cout<<"atlas "<<w<<"x"<<h<<" faces "<<clip.faces.size()<<" vertices "<<clip.frames.front().vertices.size()<<"\n";
    for(std::size_t i=0;i<clip.faces.size();++i) {
        const auto& f=clip.faces[i];
        float u0=1,v0=1,u1=0,v1=0;
        for(unsigned k=0;k<(f.quad?4u:3u);++k){u0=std::min(u0,f.uv[k][0]);u1=std::max(u1,f.uv[k][0]);v0=std::min(v0,f.uv[k][1]);v1=std::max(v1,f.uv[k][1]);}
        double r=0,g=0,b=0,n=0;
        const unsigned x0=unsigned(u0*w),x1=unsigned(u1*w),y0=unsigned(v0*h),y1=unsigned(v1*h);
        for(unsigned y=y0;y<=y1 && y<h;++y)for(unsigned x=x0;x<=x1 && x<w;++x){const auto* p=&source[(y*w+x)*4];if(p[3]){r+=p[0];g+=p[1];b+=p[2];++n;}}
        if(n<1)n=1;
        Vec3 color{};for(unsigned k=0;k<(f.quad?4u:3u);++k)color+=f.colors[k];
        Vec3 centroid{};for(unsigned k=0;k<(f.quad?4u:3u);++k)centroid+=clip.frames.front().vertices[f.vertices[k]];centroid=centroid/float(f.quad?4:3);
        std::cout<<"face "<<i<<(f.quad?" quad":" tri ")<<" c "<<centroid.x<<','<<centroid.y<<','<<centroid.z<<" v";
        for(unsigned k=0;k<(f.quad?4u:3u);++k)std::cout<<' '<<f.vertices[k];
        std::cout<<" tile "<<f.atlas_tile<<" uv "<<x0<<','<<y0<<'-'<<x1<<','<<y1<<" mean "<<int(r/n)<<','<<int(g/n)<<','<<int(b/n)
            <<" vcol "<<color.x/(f.quad?4:3)<<','<<color.y/(f.quad?4:3)<<','<<color.z/(f.quad?4:3)<<(f.translucent?" translucent":"")<<"\n";
        for(unsigned x=x0;x<=x1 && x<w;++x){for(unsigned y:{y0,y1})if(y<h){auto* p=&rgba[(y*w+x)*4];p[0]=255;p[1]=0;p[2]=255;p[3]=255;}}
        for(unsigned y=y0;y<=y1 && y<h;++y){for(unsigned x:{x0,x1})if(x<w){auto* p=&rgba[(y*w+x)*4];p[0]=255;p[1]=0;p[2]=255;p[3]=255;}}
    }
    if(argc>5 && std::string(argv[5])=="dark")rgba=dark_spyro_atlas(model);   // write the Dark Spyro atlas instead
    std::ofstream out(argv[2],std::ios::binary);out<<"P6\n"<<w<<' '<<h<<"\n255\n";
    for(unsigned i=0;i<w*h;++i)out.write(reinterpret_cast<const char*>(&rgba[i*4]),3);
    return 0;
}

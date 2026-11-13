#include "spyro/splats.hpp"
#include "spyro/assets.hpp"
#include <fstream>
#include <cstring>

namespace spyro {
std::vector<GaussianSplat> load_splats(const std::filesystem::path& path,SplatSurface* surface) {
    std::ifstream in(path,std::ios::binary);char magic[8]{};std::uint32_t count{};
    if(surface)*surface={};
    if(!in.read(magic,8) || (std::memcmp(magic,"S2GS0001",8) && std::memcmp(magic,"S2GS0002",8) && std::memcmp(magic,"S2GS0003",8)) || !in.read(reinterpret_cast<char*>(&count),4)
       || (count==0 && magic[7]=='1') || count>3000000)
        throw FormatError("Invalid Gaussian scene header or size");
    std::uint32_t nv=0,w=0,h=0;
    if(magic[7]!='1') {
        if(!in.read(reinterpret_cast<char*>(&nv),4)||!in.read(reinterpret_cast<char*>(&w),4)||!in.read(reinterpret_cast<char*>(&h),4)
           ||nv==0||nv>1500000||nv%3||w==0||h==0||w>8192||h>8192||std::uint64_t(w)*h>32*1024*1024)
            throw FormatError("Invalid Gaussian coverage surface header");
    }
    const unsigned stride=magic[7]=='3'?32:20;
    if(std::filesystem::file_size(path)!=(magic[7]!='1'?24ull:12ull)+64ull*count+std::uint64_t(stride)*nv+4ull*w*h)throw FormatError("Invalid Gaussian scene size");
    std::vector<GaussianSplat> splats(count);
    if(!in.read(reinterpret_cast<char*>(splats.data()),std::streamsize(count)*sizeof(GaussianSplat)))throw FormatError("Truncated Gaussian scene");
    for(auto& s:splats) {
        const auto finite=[](Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
        const double a=s.diagonal.x,b=s.diagonal.y,c=s.diagonal.z,d=s.off_diagonal.x,e=s.off_diagonal.y,f=s.off_diagonal.z;
        if(!finite(s.position)||!finite(s.color)||!finite(s.diagonal)||!finite(s.off_diagonal)||!std::isfinite(s.opacity)
           ||s.opacity<=0||s.opacity>1||s.color.x<0||s.color.y<0||s.color.z<0||s.color.x>1||s.color.y>1||s.color.z>1
           ||length(s.position)>8192 ||a<=0||b<=0||c<=0||a>10000||b>10000||c>10000
           ||a*b-d*d<=0 ||a*b*c+2*d*e*f-a*f*f-b*e*e-c*d*d < -1e-10)
            throw FormatError("Invalid Gaussian scene values or covariance");
        s.reserved0=3*std::sqrt(std::max({s.diagonal.x,s.diagonal.y,s.diagonal.z}));
    }
    if(nv) {
        SplatSurface coverage;coverage.width=w;coverage.height=h;coverage.vertices.resize(nv);coverage.rgba.resize(std::size_t(w)*h*4);
        if(magic[7]=='3') {
            if(!in.read(reinterpret_cast<char*>(coverage.vertices.data()),32ull*nv))throw FormatError("Truncated scenery surface");
        } else {
            struct LegacyVertex {Vec3 position;float u,v;};static_assert(sizeof(LegacyVertex)==20);
            std::vector<LegacyVertex> legacy(nv);
            if(!in.read(reinterpret_cast<char*>(legacy.data()),20ull*nv))throw FormatError("Truncated scenery surface");
            for(unsigned i=0;i<nv;++i)coverage.vertices[i]={legacy[i].position,legacy[i].u,legacy[i].v};
        }
        if(!in.read(reinterpret_cast<char*>(coverage.rgba.data()),coverage.rgba.size()))throw FormatError("Truncated scenery texture");
        for(const auto& v:coverage.vertices)if(!finite(v.position)||length(v.position)>8192||!std::isfinite(v.u)||!std::isfinite(v.v)||v.u<0||v.u>1||v.v<0||v.v>1 || !finite(v.color) || v.color.x<0 || v.color.y<0 || v.color.z<0 || v.color.x>1 || v.color.y>1 || v.color.z>1)throw FormatError("Invalid scenery vertex");
        if(surface)*surface=std::move(coverage);
    }
    return splats;
}
}

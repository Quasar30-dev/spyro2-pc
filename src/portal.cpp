#include "spyro/universe.hpp"
#include "spyro/coordinates.hpp"
#include <bit>
namespace spyro {
ReturnPortalAssets decode_return_portal(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe) {
    ReturnPortalAssets result;
    // Glimmer's 1AA case at 80075318 reads these five original aperture
    // points. The white/gold arch itself remains its ordinary animated Moby.
    for(unsigned i=0;i<5;++i) {
        const auto at=0x800+0x531f0+i*12;
        result.aperture[i]=retail::model_position({float(std::bit_cast<std::int32_t>(read_u32(exe,at))),
            float(std::bit_cast<std::int32_t>(read_u32(exe,at+4))),float(std::bit_cast<std::int32_t>(read_u32(exe,at+8)))});
    }
    // 8006FA98: collision, sky, then 0x44-byte portal records. +3C is a
    // section-relative pointer to two screen-space colour meshes. 800296D0
    // draws the background and destination panorama clipped to the aperture.
    std::size_t at=read_u32(archive,8),end=at+read_u32(archive,12);
    const auto skip=[&](std::size_t p){const auto n=read_u32(archive,p);if(n<4 || p>end || n>end-p)throw FormatError("Invalid portal section");return p+n;};
    for(unsigned i=0;i<6;++i)at=skip(at);at=skip(at+12);at=skip(at);
    const auto limit=skip(at);if(read_u32(archive,at+4)<1 || limit-at<76)throw FormatError("Missing return portal picture");
    const auto picture=at+read_u32(archive,at+8+60);
    const auto read=[&](std::size_t p){if(p<at || p+4>limit)throw FormatError("Portal picture outside scene section");return read_u32(archive,p);};
    for(unsigned layer=0;layer<2;++layer) {
        const auto base=picture+layer*60;const auto nv=read(base),nc=read(base+8),nf=read(base+16);
        if(nv>1024 || nc>1024 || nf>10000 || !nv || !nc)throw FormatError("Invalid return portal picture counts");
        const auto vertices=base+read(base+4),colors=base+read(base+12),faces=base+read(base+20);
        for(unsigned face=0;face<nf;++face) {
            const auto indices=read(faces+face*8),palette=read(faces+face*8+4);
            std::vector<PortalVertex> polygon;
            for(unsigned k=0;k<3;++k) {
                const auto vi=(indices>>(20-k*10))&1023;
                const auto ci=(palette&0x80000000)?((palette>>20)&1023):((palette>>(20-k*10))&1023);
                if(vi>=nv || ci>=nc)throw FormatError("Portal picture has an invalid face index");
                const auto packed=read(vertices+vi*4),rgb=read(colors+ci*4);
                const auto xy=std::bit_cast<std::int32_t>(packed)>>5;
                // The retail artwork spans the 512 x 216 gameplay rectangle.
                // Keep its colours and geometry, fitted behind the original arch.
                const float x=float(static_cast<std::int16_t>(xy)),y=float(xy>>16);
                polygon.push_back({{(x/512-0.5f)*1.5625f,(228-y)/216*1.66015625f,layer?0.0f:0.002f},
                    {float(rgb&255)/255,float(rgb>>8&255)/255,float(rgb>>16&255)/255}});
            }
            for(unsigned edge=0;edge<5 && !polygon.empty();++edge) {
                const auto a=result.aperture[edge],b=result.aperture[(edge+1)%5];
                const auto side=[&](Vec3 p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);};
                std::vector<PortalVertex> clipped;
                for(std::size_t i=0;i<polygon.size();++i) {
                    const auto p=polygon[i],q=polygon[(i+1)%polygon.size()];const auto sp=side(p.position),sq=side(q.position);
                    if(sp<=0)clipped.push_back(p);
                    if((sp<=0)!=(sq<=0)) {const float t=sp/(sp-sq);clipped.push_back({lerp(p.position,q.position,t),lerp(p.color,q.color,t)});}
                }
                polygon=std::move(clipped);
            }
            for(std::size_t i=1;i+1<polygon.size();++i)for(const auto k:{std::size_t(0),i,i+1})result.picture.push_back(polygon[k]);
        }
    }
    return result;
}
} // namespace spyro

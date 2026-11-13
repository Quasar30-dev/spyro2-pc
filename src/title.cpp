#include "spyro/title.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void check(bool value,const char* why){if(!value)throw FormatError(why);}
}
TitleAssets decode_title(View archive,View movie,View exe) {
    TitleAssets result;static_cast<CinematicAssets&>(result)=decode_cinematic(archive,movie,exe,95);
    const auto children=parse_wad(archive.first(std::min<std::size_t>(archive.size(),0x800)),archive.size());
    check(!children.empty() && children[0].size>=0x80000,"Missing title VRAM");
    const auto vram=archive.subspan(children[0].offset,0x80000);
    const auto word=[&](unsigned x,unsigned y){check(x>=512 && x<1024 && y<512,"Title sprite outside VRAM");return read_u16(vram,(y*512+x-512)*2);};
    result.rgba.resize(result.width*result.height*4);
    for(unsigned i=0;i<75;++i) {
        if(i>=28 && i<32)continue; // Different-language table entries, not US sprites.
        const unsigned at=(i<32?0x54744:0x5472c)+i*6,a=exe[at];
        const unsigned page=(a>>6)+(i<32?0x98:0x1c),u=(a&63)*(i<32?2:4),v=exe[at+1];
        const unsigned clut=(exe[at+2]&127)+exe[at+3]*64+0x4020+(i<32?0:16),depth=(page>>7)&3;
        auto& sprite=result.sprites[i];sprite={i%4*256,i/4*128,exe[at+4],exe[at+5]};
        // Logo/panel sprites need 128-pixel rows; text lives in a compact bank below.
        if(i>=28)sprite.y=7*128+(i-32)/4*12;
        check(sprite.y+sprite.height<=result.height && u+sprite.width<=256 && v+sprite.height<=256,"Invalid title sprite extent");
        for(unsigned y=0;y<sprite.height;++y)for(unsigned x=0;x<sprite.width;++x) {
            const unsigned packed=word((page&15)*64+(u+x)/(depth?2:4),((page>>4)&1)*256+v+y);
            const unsigned index=(packed>>(((u+x)%(depth?2:4))*(depth?8:4)))&(depth?255:15);
            const unsigned color=word((clut&63)*16+index,clut>>6),p=((sprite.y+y)*result.width+sprite.x+x)*4;
            for(unsigned c=0;c<3;++c){const auto n=(color>>(c*5))&31;result.rgba[p+c]=static_cast<std::uint8_t>((n<<3)|(n>>2));}
            result.rgba[p+3]=color?255:0;
        }
    }
    return result;
}
TitleFrame TitleAssets::sample(float ticks) const {
    check(std::isfinite(ticks) && ticks>=0,"Invalid title time");
    if(ticks>=1620)ticks=1032+std::fmod(ticks-1620,588.0f);
    return CinematicAssets::sample(ticks,true);
}
} // namespace spyro

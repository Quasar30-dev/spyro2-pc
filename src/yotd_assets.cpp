#include "spyro/yotd_assets.hpp"
#include <algorithm>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View data,std::size_t at,std::size_t bytes) {
    if(at>data.size() || bytes>data.size()-at)throw FormatError("YOTD VRAM data outside its extent");
}
std::size_t next_chunk(View data,std::size_t at) {
    require(data,at,4);const auto size=read_u32(data,at);
    if(size<4 || size%4)throw FormatError("Invalid YOTD area VRAM section chain");
    require(data,at,size);return at+size;
}
}
YotdVramImage decode_yotd_vram(View archive,const GameProfile& profile,unsigned area) {
    if(!profile.yotd())throw FormatError("Native YOTD VRAM applied to another profile");
    const auto children=parse_level_archive(archive,profile);
    if(children.size()!=4 || children[0].size<0x80000)throw FormatError("Missing YOTD VRAM image");
    require(archive,children[0].offset,0x80000);YotdVramImage image;
    image.pixels.assign(archive.begin()+children[0].offset,archive.begin()+children[0].offset+0x80000);
    if(!area)return image;
    if(area>4)throw FormatError("Invalid YOTD VRAM area");
    const auto header=0x20+(area-1)*16;require(archive,header,8);
    const auto blob_offset=read_u32(archive,header),blob_size=read_u32(archive,header+4);
    if(!blob_offset || !blob_size)throw FormatError("Missing YOTD VRAM area");
    require(archive,blob_offset,blob_size);const auto blob=archive.subspan(blob_offset,blob_size);
    require(blob,0,4);const auto bank_bytes=read_u32(blob,0);
    if(bank_bytes<4 || bank_bytes>blob.size() || blob.size()-bank_bytes<0x180)
        throw FormatError("Invalid YOTD area VRAM model directory");
    std::size_t at=std::size_t(bank_bytes)+0x180;
    for(unsigned i=0;i<4;++i)at=next_chunk(blob,at);
    at=next_chunk(blob,at); // Collision; extras omit the main scene's grid words.
    require(blob,at,16);
    const auto x=read_u32(blob,at),y=read_u32(blob,at+4),width=read_u32(blob,at+8),height=read_u32(blob,at+12);
    if(!(x|y|width|height))return image;
    if(x<512 || x>=1024 || y>=512 || !width || width>1024-x || !height || height>512-y)
        throw FormatError("Invalid YOTD area VRAM rectangle");
    require(blob,at+16,4);const auto gap=read_u32(blob,at+16);
    if(gap<4 || gap>0x800)throw FormatError("Invalid YOTD area VRAM alignment gap");
    const auto source=at+16+gap,bytes=std::size_t(width)*height*2;
    require(blob,source,bytes);
    if(source+bytes!=blob.size() || (std::size_t(blob_offset)+source)%0x800)
        throw FormatError("Invalid YOTD area VRAM image placement");
    for(unsigned row=0;row<height;++row) {
        const auto from=source+std::size_t(row)*width*2,to=(std::size_t(y+row)*512+x-512)*2;
        std::copy_n(blob.begin()+from,std::size_t(width)*2,image.pixels.begin()+to);
    }
    image.patch=YotdVramPatch{x,y,width,height,static_cast<unsigned>(blob_offset+at),static_cast<unsigned>(blob_offset+source)};
    return image;
}
} // namespace spyro

#include "spyro/retail_math.hpp"
#include <algorithm>
#include <bit>

namespace spyro::retail {
MathTables::MathTables() {
    // Disc-free test scenes. Mounted games overwrite these with 80061e5c / 80066478.
    for(unsigned i=0;i<atan.size();++i)atan[i]=static_cast<std::uint16_t>(std::lround(std::atan(i/128.0)*8192/std::numbers::pi));
    for(unsigned i=0;i<square_root.size();++i)square_root[i]=static_cast<std::uint16_t>(std::sqrt((i+64)/64.0)*4096);
}
unsigned MathTables::angle(int x,int y,unsigned mode) const {
    // 8001b4b8: octant lookup, 7-bit interpolation; byte or 12-bit angle.
    const auto ax=std::abs(static_cast<std::int64_t>(x)),ay=std::abs(static_cast<std::int64_t>(y));
    const auto ratio=static_cast<unsigned>((std::min(ax,ay)*16384)/std::max<std::int64_t>(1,std::max(ax,ay)));
    int base=0;bool negative=false;
    if(x<0) {
        if(y<0) {negative=ay>ax;base=negative?0x3000:0x2000;}
        else {negative=ax>=ay;base=negative?0x2000:0x1000;}
    } else if(y<0) {negative=ax>=ay;base=negative?0:0x3000;}
    else {negative=ay>ax;base=negative?0x1000:0;}
    int value=atan[ratio>>7];
    if(ratio&127)value+=((atan[(ratio>>7)+1]-value)*static_cast<int>(ratio&127))>>7;
    const auto result=base+(negative?-value:value);
    if(mode==1)return static_cast<unsigned>((result+2)>>2)&4095;
    return static_cast<unsigned>((result+32)>>6)&255;
}
int MathTables::length(IVec3 v,bool include_z) const {
    // 8001ba20: normalize for GTE SQR, table seed and one integer refinement.
    if(!include_z)v.z=0;
    const auto magnitude=[](int n){return static_cast<std::uint32_t>(std::abs(static_cast<std::int64_t>(n)));};
    const auto bits=magnitude(v.x)|magnitude(v.y)|magnitude(v.z);
    const int shift=std::max(0,18-std::countl_zero(bits));
    v={v.x>>shift,v.y>>shift,v.z>>shift};
    const auto sum=static_cast<unsigned>(v.x*v.x+v.y*v.y+v.z*v.z);
    if(!sum)return 0;
    const int even_zeros=std::countl_zero(sum)&~1,normalization=even_zeros-24;
    const auto index=(normalization<0?sum>>-normalization:sum<<normalization)-64;
    const int seed=(static_cast<unsigned>(square_root.at(index))<<((31-even_zeros)>>1))>>12;
    const int refined=seed?seed+((static_cast<int>(sum)-seed*seed)/(seed+1)>>1):0;
    return refined*(1<<shift);
}
int horizontal_distance(IVec3 v) {const int x=std::abs(v.x),y=std::abs(v.y);return std::max(x,y)+(3*std::min(x,y)>>3);}
IVec3 rescale(IVec3 v,int distance,int wanted) {
    if(!distance)return v;
    const auto factor=(static_cast<std::int64_t>(wanted)*32768)/distance;
    return {static_cast<int>(v.x*factor>>15),static_cast<int>(v.y*factor>>15),static_cast<int>(v.z*factor>>15)};
}
unsigned approach_angle(unsigned current,unsigned target,unsigned step) {
    // 80037da8 -> 8001bf24, at the stock two-VBlank script interval.
    int delta=static_cast<int>((target-current)&255);if(delta>128)delta-=256;
    const auto amount=std::min(step,static_cast<unsigned>(std::abs(delta)));
    return (current+(delta<0?-static_cast<int>(amount):static_cast<int>(amount)))&255;
}
} // namespace spyro::retail

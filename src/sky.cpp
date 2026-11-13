#include "spyro/level.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View data,std::size_t at,std::size_t bytes) {
    if(at>data.size() || bytes>data.size()-at)throw FormatError("Sky data outside its section");
}
Vec3 rgb(unsigned c){return {float(c&255)/255,float((c>>8)&255)/255,float((c>>16)&255)/255};}
int s16(View b,std::size_t at){return std::bit_cast<std::int16_t>(read_u16(b,at));}
// 80048d18: compact octree nodes contain visibility deltas followed by a
// relative child-block pointer. Sibling node lengths include their headers.
void validate_node(View data,std::size_t at,unsigned depth,unsigned regions,std::vector<bool>& visited) {
    require(data,at,depth?2:1);
    if(visited[at])throw FormatError("Overlapping sky visibility nodes");
    const unsigned bytes=data[at],mask=depth?data[at+1]:0,header=depth?(mask?4u:2u):1u;
    if(bytes<header)throw FormatError("Invalid sky visibility node size");
    require(data,at,bytes);
    for(std::size_t i=at;i<at+bytes;++i) {
        if(visited[i])throw FormatError("Overlapping sky visibility nodes");
        visited[i]=true;
    }
    for(std::size_t i=at+header;i<at+bytes;++i)
        if(data[i]!=255 && data[i]>=regions)throw FormatError("Sky visibility references a missing region");
    if(mask) {
        const auto offset=read_u16(data,at+2);
        if(offset<bytes)throw FormatError("Invalid sky visibility child offset");
        auto child=at+offset;
        for(unsigned bit=0;bit<8;++bit)if(mask&(1u<<bit)) {
            validate_node(data,child,depth-1,regions,visited);child+=data[child];
        }
    }
}
}
void decode_sky(View data,Level& level) {
    require(data,0,8);const auto count=read_u32(data,4);
    if(count>64)throw FormatError("Too many retail sky regions");
    require(data,8,count*4);level.sky_color=rgb(read_u32(data,0));
    level.sky_vertices.clear();level.sky_regions.clear();
    for(unsigned region=0;region<count;++region) {
        const auto at=read_u32(data,8+region*4);
        if(at<8+count*4)throw FormatError("Sky region overlaps its directory");
        require(data,at,20);
        const unsigned vertices=data[at+12],colors=data[at+13];
        if(!vertices || !colors || colors>128)throw FormatError("Invalid sky palette or vertex count");
        const std::size_t vertex_at=at+20,color_at=vertex_at+vertices*4,header_at=color_at+colors*4;
        require(data,vertex_at,(vertices+colors)*4);
        const auto scan_bytes=read_u16(data,at+16),header_bytes=read_u16(data,at+18);
        if(scan_bytes%2 || header_bytes%4)throw FormatError("Unaligned sky triangle streams");
        require(data,header_at,std::size_t(header_bytes)+scan_bytes);
        const auto end=header_at+header_bytes+scan_bytes;
        auto header=header_at,scan=header_at+header_bytes;
        SkyRegion range{static_cast<unsigned>(level.sky_vertices.size()),0};
        const auto emit=[&](unsigned v,unsigned c) {
            if(v>=vertices || c>=colors)throw FormatError("Sky triangle index outside its region");
            const auto packed=read_u32(data,vertex_at+v*4);
            // 80029170 loads GTE coordinates (offsetY-Y,offsetZ-Z,X+offsetX).
            // Its camera basis maps these back to original X/Y/Z-up here.
            const Vec3 original{float(s16(data,at+14)+int(packed>>21)),
                float(int((packed>>10)&2047)-s16(data,at+8)),float(int(packed&1023)-s16(data,at+10))};
            level.sky_vertices.push_back({retail::world_position(original),rgb(read_u32(data,color_at+c*4))});
        };
        while(scan<end) {
            if(header+4>header_at+header_bytes)throw FormatError("Sky triangle headers exhausted");
            const auto packet=read_u32(data,header);header+=4;
            const auto pair=read_u16(data,scan);scan+=2;
            unsigned a=pair&255,b=packet>>24,ca=(packet>>10)&127,cb=(packet>>3)&127;
            emit(pair>>8,(packet>>17)&127);emit(a,ca);emit(b,cb);
            for(unsigned extra=0;extra<(packet&7);++extra) {
                if(scan+2>end)throw FormatError("Truncated sky triangle fan");
                const auto next=read_u16(data,scan);scan+=2;
                if(next&0x8000){std::swap(a,b);std::swap(ca,cb);}
                emit(b,cb);emit(a,ca);b=next&255;cb=(next>>8)&127;emit(b,cb);
            }
        }
        if(header!=header_at+header_bytes)throw FormatError("Unused sky triangle headers");
        range.count=static_cast<unsigned>(level.sky_vertices.size())-range.first;level.sky_regions.push_back(range);
    }
}
void decode_sky_visibility(View data,Level& level) {
    level.sky_visibility.clear();if(data.empty())return;
    std::vector<bool> visited(data.size());validate_node(data,0,7,static_cast<unsigned>(level.sky_regions.size()),visited);
    level.sky_visibility.assign(data.begin(),data.end());
}
std::array<bool,64> visible_sky_regions(const Level& level,Vec3 eye) {
    std::array<bool,64> visible{};
    if(level.sky_visibility.empty()){visible.fill(true);return visible;}
    const auto& data=level.sky_visibility;
    const auto cell=[](float x){return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(x/2))) ;};
    const std::array<std::uint32_t,3> position{cell(eye.x),cell(eye.z),cell(eye.y)};
    std::array<unsigned,3> cursor{};std::size_t at=0;
    for(unsigned depth=7;;--depth) {
        const auto end=at+data[at];const unsigned mask=depth?data[at+1]:0;
        auto scan=at+(depth?(mask?4:2):1);
        while(scan<end && data[scan]!=255) {
            const auto index=data[scan++];visible[index]=false;
            if(scan==end || data[scan]<=index)break;
        }
        while(scan<end){const auto index=data[scan++];if(index!=255)visible[index]=true;}
        if(!depth || !mask)break;
        unsigned octant=0;
        for(unsigned axis=0;axis<3;++axis) {
            cursor[axis]*=2;
            if(position[axis]>=((cursor[axis]+1)<<depth)/2){++cursor[axis];octant|=4u>>axis;}
        }
        // Outside the authored octree, retail takes its all-visible fallback.
        // Retaining parent deltas here hides sky in editor/free-flight views.
        if(!(mask&(1u<<octant))){visible.fill(true);return visible;}
        at+=read_u16(data,at+2);
        for(unsigned i=0;i<octant;++i)if(mask&(1u<<i))at+=data[at];
    }
    return visible;
}
} // namespace spyro

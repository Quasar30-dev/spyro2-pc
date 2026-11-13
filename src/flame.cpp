#include "spyro/game_build.hpp"
#include "spyro/flame.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
FlamePoint read_point(View exe,std::size_t at) {
    return {retail::model_position({static_cast<float>(std::bit_cast<std::int16_t>(read_u16(exe,at))),
        static_cast<float>(std::bit_cast<std::int16_t>(read_u16(exe,at+2))),
        static_cast<float>(std::bit_cast<std::int16_t>(read_u16(exe,at+4)))}),static_cast<float>(read_u16(exe,at+6)&255)};
}
FlamePoint point(const FlameCurve& curve,float index) {
    if(!std::isfinite(index) || index< -1 || index>static_cast<float>(curve.points.size())-1)
        throw FormatError("Flame envelope references an invalid trajectory sample");
    const auto lo=static_cast<int>(std::floor(index)),hi=static_cast<int>(std::ceil(index));
    const auto& a=lo<0?curve.prefix:curve.points.at(lo);
    const auto& b=hi<0?curve.prefix:curve.points.at(hi);
    const float fraction=index-lo;
    return {lerp(a.position,b.position,fraction),a.twist+std::remainder(b.twist-a.twist,256.0f)*fraction};
}
float trig(std::span<const std::int16_t,256> table,float angle) {
    const auto lo=static_cast<int>(std::floor(angle));const auto fraction=angle-lo;
    return table[lo&255]+(table[(lo+1)&255]-table[lo&255])*fraction;
}
}
FlameAssets decode_flame_assets(View archive,View exe) {
    const ExeTables tables(exe);
    FlameAssets result;
    for(unsigned i=0;i<result.allowed.size();++i)result.allowed[i]=exe[tables(0x80061370)+i*4];
    for(unsigned i=0;i<8;++i) {
        const unsigned count=tables.profile.prerelease()?(i<4?24:32):(i<4?22:28);
        const auto at=tables(i<4?0x80062ad0:0x80062d90)+(i%4)*count*8;
        auto& curve=result.curves[i];curve.prefix=read_point(exe,at-8);
        for(unsigned j=0;j<count;++j)curve.points.push_back(read_point(exe,at+j*8));
        const auto offset=tables(0x80062a70)+(tables.profile.prerelease()?std::min(i,4u):i)*12;
        result.offsets[i]=retail::model_position({static_cast<float>(std::bit_cast<std::int32_t>(read_u32(exe,offset))),
            static_cast<float>(std::bit_cast<std::int32_t>(read_u32(exe,offset+4))),
            static_cast<float>(std::bit_cast<std::int32_t>(read_u32(exe,offset+8)))});
    }
    for(unsigned kind=0;kind<2;++kind)for(unsigned age=0;age<(tables.profile.prerelease()?(kind?40u:32u):(kind?36u:30u));++age) {
        const auto packed=read_u32(exe,tables(kind?0x800629e0:0x80062968)+age*4);
        const unsigned head=packed>>24,span=(packed>>16)&255,fade=(packed>>8)&255;
        if((packed&255) || head<2 || head>result.curves[kind*4].points.size()+1 || static_cast<int>(head)-2-static_cast<int>(span)< -1 || (span+fade)*5>128)
            throw FormatError("Invalid retail flame envelope");
        result.envelopes[kind].push_back({static_cast<float>(head),static_cast<float>(span),static_cast<float>(fade)});
    }
    for(unsigned i=0;i<16;++i) {
        result.order[i]=exe[tables(0x80063130)+i];
        if(result.order[i]>=8)throw FormatError("Invalid retail flame emission order");
    }
    for(unsigned i=0;i<4;++i) {
        const auto color=read_u32(exe,tables(0x80063110)+i*4);
        if((color>>24)!=0x30)throw FormatError("Unmapped flame tip material");
        result.tip_colors[i]={static_cast<float>(color&255)/255,static_cast<float>((color>>8)&255)/255,static_cast<float>((color>>16)&255)/255};
    }
    const auto entries=parse_level_archive(archive);
    if(entries.size()!=4 || entries[0].size<0x80000 || entries[3].size<0x1c)throw FormatError("Missing flame material sections");
    const auto header=entries[3].offset;
    const auto left=read_u32(archive,header+0x14),right=read_u32(archive,header+0x18);
    // Slot 95 has the separately mapped, shorter special-scene header.
    if(left==8 && right==0)return result;
    if((left&0xffff)!=0x8000 || right!=0x8039ff1f)throw FormatError("Unmapped flame texture coordinates / blend mode");
    const auto vram=archive.subspan(entries[0].offset,0x80000);
    const auto word=[&](unsigned x,unsigned y) {
        if(x<512 || x>=1024 || y>=512)throw FormatError("Flame material references unmapped VRAM");
        return read_u16(vram,(static_cast<std::size_t>(y)*512+x-512)*2);
    };
    const auto clut=left>>16,page=right>>16;
    result.texture_rgba.resize(FlameAssets::texture_width*FlameAssets::texture_height*4);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<32;++x) {
        const auto packed=word((page&15)*64+x/4,((page>>4)&1)*256+128+y);
        const auto index=(packed>>((x&3)*4))&15;
        const auto color=word((clut&63)*16+index,(clut>>6)&511);
        // All 40 mapped palettes mark every nontransparent texel additive.
        if(color && !(color&0x8000))throw FormatError("Unmapped opaque texel in flame material");
        const auto p=(y*32+x)*4;
        for(unsigned c=0;c<3;++c) {const auto v=(color>>(c*5))&31;result.texture_rgba[p+c]=static_cast<std::uint8_t>((v<<3)|(v>>2));}
        result.texture_rgba[p+3]=color?255:0;
    }
    return result;
}
FlameEvent FlameAction::tick(bool pressed,bool allowed,bool charging,bool effect_active) {
    force_follow_=false;
    if(!active_) {
        following_=false;
        if(pressed && allowed && !effect_active) {
            active_=following_=true;age_=1;return FlameEvent::Start;
        }
        return FlameEvent::None;
    }
    // 8002fc20 tests the old age, then increments it at the end of the VBlank.
    // Entering charge preserves an existing flame but cannot start a new one.
    if((!allowed && !charging) || age_>47) {
        active_=following_=false;age_=1;force_follow_=!allowed;return FlameEvent::Cancel;
    }
    if(age_==20)following_=false;
    ++age_;return FlameEvent::None;
}
FlameGeometry flame_geometry(const FlameAssets& assets,const FlamePose& pose,
                             std::span<const std::int16_t,256> sine,std::span<const std::int16_t,256> cosine) {
    FlameGeometry result;result.tips.reserve(96);result.trails.reserve(8*13*6);
    const auto world=[&](Vec3 local) {return pose.base+retail::rotate_model(local,pose.yaw);};
    // Original 80048f04 emits streams in reverse order.
    for(int i=7;i>=0;--i) {
        const auto& stream=pose.streams[i];const auto& table=assets.envelopes[i<4?0:1];
        if(!std::isfinite(stream.age) || stream.age<1 || stream.age>=table.size())continue;
        const auto lo=static_cast<unsigned>(stream.age)-1;const float fraction=stream.age-1-lo;
        const auto a=table.at(lo),b=table.at(lo+1);
        const FlameEnvelope e{a.head+(b.head-a.head)*fraction,a.span+(b.span-a.span)*fraction,a.fade+(b.fade-a.fade)*fraction};
        const auto& curve=assets.curves[i];
        if(curve.points.empty())throw FormatError("Missing flame trajectory");
        const bool tip=e.head<=stream.cutoff && e.head<=curve.points.size()-1;
        const auto start_tip=result.tips.size(),start_trail=result.trails.size();
        if(tip) {
            const auto base=point(curve,e.head-2);
            const float c=std::floor(trig(cosine,base.twist)*40/2048)/retail::units_per_world;
            const float s=std::floor(trig(sine,base.twist)*40/2048)/retail::units_per_world;
            const std::array<Vec3,4> vertices{point(curve,e.head).position,base.position+Vec3{s,c,0},
                base.position+Vec3{c,-s,0},base.position+Vec3{-c,s,0}};
            for(const unsigned index:{0,2,1,0,1,3,0,3,2,1,2,3})result.tips.push_back({world(vertices[index]),assets.tip_colors[index]});
        }
        float index=e.head-2,span=e.span,fade=e.fade;
        if(index>stream.cutoff) {const auto clipped=index-stream.cutoff;index-=clipped;span-=clipped;fade+=clipped;}
        if(span>0) {
            std::array<FlameVertex,2> previous;bool first=true;
            for(unsigned section=0;section<32;++section) {
                const auto p=point(curve,index);
                const float width=(span<=0 || (first && !tip))?8.0f:47.0f;
                const float c=std::floor(trig(cosine,p.twist)*width/2048)/retail::units_per_world;
                const float s=std::floor(trig(sine,p.twist)*width/2048)/retail::units_per_world;
                const float brightness=std::clamp((128-fade*5)/128,0.0f,1.0f),v=(fade*5+0.5f)/128;
                const Vec3 color{brightness,brightness,brightness};
                const std::array<FlameVertex,2> current{{{world(p.position+Vec3{c,-s,0}),color,0.5f/32,v},
                    {world(p.position-Vec3{c,-s,0}),color,31.5f/32,v}}};
                if(!first) {
                    const std::array<FlameVertex,4> quad{previous[0],previous[1],current[0],current[1]};
                    for(const unsigned k:{0,1,2,1,3,2})result.trails.push_back(quad[k]);
                }
                if(span<=0)break;
                previous=current;first=false;
                const auto step=std::min(span,2.0f);index-=step;span-=step;fade+=step;
            }
        }
        if(result.tips.size()!=start_tip || result.trails.size()!=start_trail)++result.streams;
    }
    return result;
}
} // namespace spyro

#include "spyro/game_build.hpp"
#include "spyro/powerup_effects.hpp"

namespace spyro {
ChargeSmokeAssets decode_charge_smoke(std::span<const std::uint8_t> archive) {
    const auto sections=parse_level_archive(archive);
    if(sections.size()!=4 || sections[0].size<0x80000)throw FormatError("Missing supercharge smoke sections");
    const auto& gameplay=sections[3];
    if(read_u32(archive,gameplay.offset+0x14)==8 && read_u32(archive,gameplay.offset+0x18)==0)return {};
    const auto data=archive.subspan(sections[1].offset,sections[1].size);
    const auto extent=[&](std::size_t at,std::size_t n){if(at>data.size() || n>data.size()-at)throw FormatError("Smoke material outside scene section");};
    const auto skip=[&](std::size_t at){const auto n=read_u32(data,at);if(n<4 || n%4)throw FormatError("Invalid smoke section chain");extent(at,n);return at+n;};
    // Loader 8006fa98: six visual sections, collision prefix/record, volumes,
    // object definitions, then typed particle texture records.
    std::size_t at=0;for(unsigned i=0;i<6;++i)at=skip(at);
    extent(at,16);at=skip(at+12);at=skip(at);if(!compact_demo_layout(archive))at=skip(at);
    const auto end=skip(at);const auto count=read_u32(data,at+4);at+=8;
    if(count>256)throw FormatError("Invalid particle texture count");
    std::uint32_t left=0,right=0;
    for(unsigned i=0;i<count;++i) {
        const unsigned type=read_u16(data,at),size=read_u16(data,at+2);
        if(size<8 || size%8 || at+4>end || size>end-at-4)throw FormatError("Invalid particle texture record");
        if(type==9){left=read_u32(data,at+4);right=read_u32(data,at+8);}
        at+=4+size;
    }
    if(!left && !right)return {};
    const unsigned clut=left>>16,page=right>>16,u=left&255,v=(left>>8)&255;
    if((right&255)<u || ((right>>8)&255)<v || ((page>>7)&3)!=0)throw FormatError("Unmapped smoke texture coordinates");
    ChargeSmokeAssets result;result.width=(right&255)-u+1;result.height=((right>>8)&255)-v+1;result.blend_mode=(page>>5)&3;
    if(result.width!=32 || result.height!=32 || result.blend_mode!=1)throw FormatError("Unmapped supercharge smoke material");
    const auto vram=archive.subspan(sections[0].offset,0x80000);
    const auto word=[&](unsigned x,unsigned y){if(x<512 || x>=1024 || y>=512)throw FormatError("Smoke texture outside retail VRAM");return read_u16(vram,((std::size_t(y)*512)+x-512)*2);};
    result.texture_rgba.reserve(result.width*result.height*4);
    for(unsigned y=0;y<result.height;++y)for(unsigned x=0;x<result.width;++x) {
        const auto packed=word((page&15)*64+(u+x)/4,((page>>4)&1)*256+v+y);
        const auto color=word((clut&63)*16+((packed>>(((u+x)%4)*4))&15),(clut>>6)&511);
        for(unsigned c=0;c<3;++c){const auto value=(color>>(c*5))&31;result.texture_rgba.push_back(static_cast<std::uint8_t>((value<<3)|(value>>2)));}
        result.texture_rgba.push_back(color?255:0);
    }
    return result;
}
void ChargeSmoke::tick(bool emitting,Vec3 origin,Vec3 velocity) {
    phase_=(phase_+1)&1;if(phase_)return;
    if(emitting) {
        // Overlay 18 800860f8, type 9. Positions/velocities are stored at
        // quarter resolution by 8001c13c; inherited velocity first shifts Q6.
        auto p=retail::from_world(origin);p.x+=-63+static_cast<int>(random_.next()&126);p.y+=-63+static_cast<int>(random_.next()&126);
        const auto quarter=[](int value){return static_cast<int>(std::floor(value/4.0));};
        const auto v=retail::from_world(velocity*(1.0f/60));
        auto& smoke=particles_[next_];next_=(next_+1)%static_cast<unsigned>(particles_.size());
        smoke={};smoke.position=smoke.previous={quarter(p.x),quarter(p.y),quarter(p.z)};
        smoke.velocity={quarter(v.x),quarter(v.y),quarter(v.z)};
        smoke.angle=random_.next()&255;smoke.active=true;++emitted_;
    }
    for(auto& p:particles_)if(p.active) {
        p.previous=p.position;p.previous_age=p.age;p.position=p.position+p.velocity;
        if(p.rise)++p.velocity.z;p.rise=!p.rise;
        p.age+=2;if(p.age>=32)p.active=false;
    }
}
void ChargeSmoke::reset_interpolation() {for(auto& p:particles_){p.previous=p.position;p.previous_age=p.age;}}
std::vector<ChargeSmokePose> ChargeSmoke::sample(float alpha) const {
    std::vector<ChargeSmokePose> out;const float blend=(phase_+std::clamp(alpha,0.0f,1.0f))*0.5f;
    for(const auto& p:particles_)if(p.active) {
        const auto age=p.previous_age+(p.age-p.previous_age)*blend;
        const auto position=lerp(retail::to_world(p.previous),retail::to_world(p.position),blend)*4;
        out.push_back({position,(24+age*2)*4/retail::units_per_world,p.angle*2*std::numbers::pi_v<float>/256});
    }
    return out;
}
} // namespace spyro

#include "spyro/cinematic.hpp"
#include "spyro/game_build.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void check(bool value,const char* why){if(!value)throw FormatError(why);}
void put(Bytes& bytes,unsigned at,unsigned value) {
    check(at<=bytes.size() && bytes.size()-at>=4,"Title relocation outside archive");
    for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(8*i));
}
Vec3 position(unsigned word) {
    return retail::world_position({float((word>>20)&1023)*256,float((word>>10)&1023)*256,float(word&1023)*256});
}
}
std::optional<unsigned> cinematic_slot(View exe,unsigned scene,bool outro) {
    if(game_profile(exe).prerelease())return {};
    if(scene<15 || scene>95 || !(scene&1))return {};
    if(scene==95)return outro?std::nullopt:std::optional<unsigned>(96);
    const auto index=exe[0x55114+(scene-15)/2];
    if(index==255 || (index==0 && scene!=17) || (scene==17 && !outro) || (scene>=73 && outro))return {};
    return 96+index+(outro?1:0);
}
CinematicAssets decode_cinematic(View archive,View movie,View exe,unsigned scene) {
    check(inspect_exe(exe).crc==0xcdc0917c,"Unmapped retail title profile");
    CinematicAssets result;result.level=decode_level(archive,scene);
    result.models=decode_object_models(archive,exe);result.models.models.clear();
    result.cinematic.assign(movie.begin(),movie.end());
    result.duration=read_u32(movie,0)*2;
    const unsigned chunks=read_u32(movie,4),actors=read_u32(movie,8);
    check(chunks>0 && chunks<=255 && actors<=128 && result.duration>0 && result.duration<=chunks*120 && result.duration>(chunks-1)*120,"Invalid cinematic header");
    for(unsigned i=0;i<chunks;++i) {
        const auto at=read_u32(movie,20+i*4),bytes=read_u32(movie,20+(chunks+i)*4);
        check(at<=movie.size() && bytes<=movie.size()-at,"Truncated title cinematic chunk");
        result.chunks.push_back(at);
    }
    std::array<unsigned,125> codebook;
    for(unsigned i=0;i<codebook.size();++i)codebook[i]=read_u32(exe,0x52e74+i*4);
    // 800739e0 / 80073c64: cinematic clips mix chunk-relative vertex
    // streams with model-relative faces/palettes. Relocate into one bounded
    // byte view for the existing retail Moby decoder; never retain PS1 pointers.
    for(unsigned i=0;i<actors;++i) {
        const auto base=read_u32(movie,24+chunks*8+i*4);
        const int count=std::bit_cast<std::int32_t>(read_u32(movie,base));
        if(count<0){result.models.models.emplace(i,decode_static_object_model(movie.subspan(base)));continue;}
        check(count>0 && count<=255,"Invalid cinematic actor segment count");
        const unsigned segments=std::min(chunks,static_cast<unsigned>(count));
        Bytes bank(movie.begin(),movie.end());
        put(bank,52,read_u32(movie,base+52)?base+read_u32(movie,base+52):0);put(bank,56,0);
        for(unsigned c=0;c<segments;++c) {
            const auto chunk=result.chunks[c],clip=chunk+read_u32(movie,base+60+c*4);
            put(bank,60+c*4,clip);
            for(const auto p:{12u,32u})put(bank,clip+p,chunk+read_u32(movie,clip+p));
            for(const auto p:{16u,20u})put(bank,clip+p,base+read_u32(movie,clip+p));
        }
        ObjectModel model;model.animated=true;model.animation_count=segments;
        try{decode_object_animations(model,bank,codebook);}
        catch(const FormatError& e){throw FormatError("Cinematic actor "+std::to_string(i)+": "+e.what());}
        result.models.models.emplace(i,std::move(model));
    }
    decode_object_textures(result.models,archive);
    if(scene!=95)result.sound=decode_cinematic_sound(movie,result.chunks,result.duration);
    return result;
}
CinematicFrame CinematicAssets::sample(float ticks,bool title_camera) const {
    check(std::isfinite(ticks) && ticks>=0,"Invalid title time");
    ticks=std::min(ticks,float(duration-1));
    const unsigned segment=std::min(static_cast<unsigned>(ticks)/120,static_cast<unsigned>(chunks.size()-1));
    const float time=ticks-segment*120;const auto chunk=chunks.at(segment);
    CinematicFrame result;
    for(unsigned i=0;i<models.models.size();++i) {
        const auto& model=models.models.at(i);
        if(!model.animated || segment>=model.animations.size())continue;
        const auto& clip=model.animations.at(segment);
        unsigned f=0;float start=0;
        while(f+1<clip.frames.size() && time>=start+clip.frames[f].ticks){start+=clip.frames[f].ticks;++f;}
        float blend=f+1<clip.frames.size()?std::clamp((time-start)/clip.frames[f].ticks,0.0f,1.0f):0;
        const auto positions=chunk+read_u32(cinematic,chunk+12+i*4);
        auto a=position(read_u32(cinematic,positions+f*4));
        const auto b=position(read_u32(cinematic,positions+std::min(f+1,static_cast<unsigned>(clip.frames.size()-1))*4));
        if(length(b-a)>2){if(blend>0.5f){++f;a=b;}blend=0;} // Retail discontinuity guard.
        RetailObject actor;actor.id=actor.type=static_cast<std::uint16_t>(i);actor.held_gem=255;actor.draw_flags=32;
        actor.position=actor.previous_position=lerp(a,b,blend);
        actor.animation.clip=actor.animation.next_clip=static_cast<std::uint8_t>(segment);actor.previous_animation=actor.animation;
        result.actors.push_back(actor);result.poses.push_back(object_animation_frame(model,segment,f+blend));
    }
    const auto camera=chunk+read_u32(cinematic,chunk+8);
    const unsigned f=std::min(static_cast<unsigned>(time)/2,59u),next=std::min(f+1,59u);
    float blend=next==f?0:(time-f*2)/2;
    const auto eye=[&](unsigned frame){const auto at=camera+frame*12;return retail::world_position({float(read_u16(cinematic,at))*4,float(read_u16(cinematic,at+2))*4,float(read_u16(cinematic,at+4))*4});};
    // 8007490c refuses interpolation across an authored camera cut.
    const int yaw_delta=std::bit_cast<std::int16_t>(read_u16(cinematic,camera+next*12+10))-std::bit_cast<std::int16_t>(read_u16(cinematic,camera+f*12+10));
    if(length(eye(next)-eye(f))>=1 || std::abs(std::remainder(float(yaw_delta),4096.0f))>=255)blend=0;
    result.eye=lerp(eye(f),eye(next),blend);
    float angles[3]{};
    for(unsigned i=0;i<3;++i) {
        const float a=std::bit_cast<std::int16_t>(read_u16(cinematic,camera+f*12+6+i*2));
        const float b=std::bit_cast<std::int16_t>(read_u16(cinematic,camera+next*12+6+i*2));
        angles[i]=a+std::remainder(b-a,4096.0f)*blend;
    }
    const float correction=title_camera?std::clamp((ticks-340)/32,0.0f,1.0f):0;angles[1]-=48*correction;angles[2]+=40*correction;
    const float pitch=angles[1]*retail::angle_to_radians,yaw=angles[2]*retail::angle_to_radians;
    // 8001e270 / 8001a2fc: Z is the original X/Y heading; view pitch
    // has the opposite sign to elevation. Convert Z-up to the PC's Y-up.
    result.forward={std::cos(yaw)*std::cos(pitch),-std::sin(pitch),std::sin(yaw)*std::cos(pitch)};
    const auto right=normalized(cross(Vec3{0,1,0},result.forward));
    const auto up=cross(result.forward,right);const float roll=angles[0]*retail::angle_to_radians;
    result.up=up*std::cos(roll)+right*std::sin(roll);
    return result;
}
} // namespace spyro

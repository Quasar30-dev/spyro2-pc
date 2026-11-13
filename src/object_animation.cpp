#include "spyro/objects.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void extent(View bytes,std::size_t at,std::size_t size) {
    if(at>bytes.size() || size>bytes.size()-at)throw FormatError("Animated object data outside its model bank");
}
int sx(unsigned value,unsigned bits) {
    const auto sign=1u<<(bits-1);
    return static_cast<int>((value&((1u<<bits)-1))^sign)-static_cast<int>(sign);
}
std::array<int,3> delta(std::uint32_t word) {
    return {sx(word>>21,11),sx(word>>10,11),sx(word,10)*2};
}
std::vector<Vec3> vertices(View bank,std::size_t base,std::size_t stream,unsigned count,
                           std::uint32_t control,unsigned shift,std::span<const std::uint32_t> codebook) {
    // 80044684..80044870: two interleaved streams. The halfword stream is
    // addressed in halfwords, unlike Spyro's byte-addressed frame stream.
    auto words=stream+(control&0xffff)*2;
    const auto words_end=words+((control>>16)&0x1ff)*2;
    auto codes=words_end;extent(bank,words,words_end-words);
    bool wide=(control&(1u<<25))!=0;
    std::array<int,3> position{};
    std::vector<Vec3> result;result.reserve(count);
    for(unsigned v=0;v<count;++v) {
        const auto packed=read_u32(bank,base+v*4);
        if(!wide) {
            extent(bank,codes,1);const auto code=bank[codes++];wide=(code&1)!=0;
            if((code>>1)>=codebook.size())throw FormatError("Invalid animated object codebook index");
            const auto d=delta(packed+codebook[code>>1]);
            for(unsigned k=0;k<3;++k)position[k]+=d[k];
        } else {
            if(words>words_end || words_end-words<2)throw FormatError("Animated object halfword stream exhausted");
            const auto code=read_u16(bank,words);words+=2;wide=(code&1)!=0;
            if(code&2) {
                if(words_end-words<2)throw FormatError("Truncated animated object absolute vertex");
                const auto next=read_u16(bank,words);words+=2;
                // 8004481c: Z is a signed 12-bit field with two control bits
                // cleared. Spyro's corresponding absolute field is half-sized.
                position={sx(next,16)>>5,-(sx(next,6)*32|((code&0xf000)>>11)),
                          -sx(code&0xffc,12)};
            } else {
                auto d=delta(packed);d[0]+=sx(code,16)>>9;
                d[1]+=sx(code>>4,7);d[2]+=sx(code>>1,5)*4;
                for(unsigned k=0;k<3;++k)position[k]+=d[k];
            }
        }
        // GTE packs VX with ADD, so a negative horizontal component borrows
        // one unit from VY. Preserve that same fixed-point packing correction.
        const int vx=-position[1],vy=-position[2]-(vx<0?1:0);
        const float scale=static_cast<float>(1u<<shift)/retail::units_per_world;
        result.push_back({vx*scale,-vy*scale,position[0]*scale});
    }
    return result;
}
Bytes face_stream(View bank,std::size_t at,std::size_t patches,std::uint32_t control) {
    const auto bytes=read_u32(bank,at);extent(bank,at,static_cast<std::size_t>(bytes)+4);
    Bytes result(bank.begin()+at,bank.begin()+at+bytes+4);
    if(control&0x80000000)return result;
    // 8004408c + 80044618: a frame can replace runs of face words. The table
    // and runs are relative to the model's +0x34 patch bank, never host pointers.
    const auto entry=patches+((control>>24)&0x7c);
    auto p=patches+read_u32(bank,entry);
    for(unsigned runs=0;;++runs) {
        if(runs>=4096)throw FormatError("Unterminated animated object face patches");
        const auto command=read_u32(bank,p);p+=4;
        const unsigned words=(command>>24)&127,offset=command&0xffff;
        if(!words || offset%4 || offset<4)throw FormatError("Invalid animated object face patch");
        extent(result,offset,words*4);extent(bank,p,words*4);
        std::copy_n(bank.begin()+p,words*4,result.begin()+offset);p+=words*4;
        if(command&0x80000000)break;
    }
    return result;
}
ObjectMesh mesh(View faces,View palette,unsigned count) {
    ObjectMesh result;
    for(std::size_t p=4;p<faces.size();) {
        extent(faces,p,8);ObjectFace f;f.animated=true;
        const auto indices=read_u32(faces,p);f.flags=read_u32(faces,p+4);
        f.textured=(f.flags&0x80000000)!=0;
        const unsigned packet=f.textured?20:8;extent(faces,p,packet);
        // 80044e14: animated polygon indices occupy full bytes (static: 7 bits).
        for(unsigned k=0;k<4;++k)f.vertices[k]=(indices>>(24-k*8))&255;
        f.quad=f.vertices[2]!=f.vertices[3];
        if(!f.quad && (f.flags&0x30)==0x20)
            f.shiny_normal={static_cast<float>(sx(f.flags>>15,8)),static_cast<float>(-sx(f.flags>>7,8)),
                            static_cast<float>(sx(f.flags>>23,8))};
        for(unsigned k=0;k<(f.quad?4u:3u);++k) {
            if(f.vertices[k]>=count)throw FormatError("Animated object polygon references an invalid vertex");
            const auto color=(f.flags>>(24-k*7))&127;
            if(color>=palette.size()/4) {
                // 80045110..80045824: bit 5 routes through the instance's
                // material mode. Mode 2 interprets these bits as a face normal,
                // not palette indices (for example class 12's metal surface).
                if(!(f.flags&0x20))throw FormatError("Animated object face references an invalid palette color");
                f.palette_valid=false;continue;
            }
            const auto rgb=read_u32(palette,color*4);const float divisor=f.textured?128.0f:255.0f;
            f.colors[k]={static_cast<float>(rgb&255)/divisor,static_cast<float>((rgb>>8)&255)/divisor,
                         static_cast<float>((rgb>>16)&255)/divisor};
        }
        if(f.textured) {
            f.clut=read_u16(faces,p+10);f.page=read_u16(faces,p+14);
            // 80045264 / 80044f90 adds GPU semi-transparency from TPAGE bit
            // 15. The palette's STP bit then selects which texels blend.
            f.translucent=(f.page&0x8000)!=0;f.blend_mode=(f.page>>5)&3;
            for(unsigned k=0;k<4;++k) {
                const auto uv=p+(k<2?8+k*4:16+(k-2)*2);
                f.uv[k]={static_cast<float>(faces[uv]),static_cast<float>(faces[uv+1])};
            }
        }
        result.faces.push_back(f);p+=packet;
    }
    return result;
}
const ObjectFrame& frame_at(const ObjectModel& model,unsigned clip,unsigned frame) {
    if(clip>=model.animations.size() || frame>=model.animations[clip].frames.size())
        throw FormatError("Object animation state outside its clip / frame bank");
    return model.animations[clip].frames[frame];
}
ObjectPose blend_frames(const ObjectFrame& a,const ObjectFrame& b,float blend) {
    ObjectPose pose;pose.mesh=a.mesh;pose.vertices=a.vertices;
    if(blend<=0)return pose;
    // A transition between clips of different meshes (Fracture Hills' encased
    // satyr 8A breaking out of its rock) cannot blend vertex by vertex: show
    // the frame the transition is heading for. (It used to throw and end the
    // game when such a satyr was freed.)
    if(a.vertices.size()!=b.vertices.size()){pose.mesh=b.mesh;pose.vertices=b.vertices;return pose;}
    for(unsigned v=0;v<pose.vertices.size();++v)pose.vertices[v]=lerp(a.vertices[v],b.vertices[v],blend);
    return pose;
}
}
void decode_object_animations(ObjectModel& model,View bank,std::span<const std::uint32_t> codebook) {
    if(!model.animation_count || model.animation_count>255)throw FormatError("Invalid animated object clip count");
    extent(bank,60,model.animation_count*4);
    const auto common=read_u32(bank,56),patches=read_u32(bank,52);extent(bank,common,0);extent(bank,patches,0);
    for(unsigned clip=0;clip<model.animation_count;++clip) {
        const auto at=read_u32(bank,60+clip*4);extent(bank,at,36);
        const unsigned count=bank[at],colors=bank[at+1],shift=bank[at+2],total=bank[at+4];
        if(!count || !colors || !total || shift>8)throw FormatError("Invalid animated object dimensions");
        extent(bank,at+36,count*8);
        const auto base=common+read_u32(bank,at+12),faces=common+read_u32(bank,at+16);
        const auto palette=common+read_u32(bank,at+20),stream=common+read_u32(bank,at+32);
        extent(bank,base,total*4);extent(bank,palette,colors*4);extent(bank,stream,0);
        ObjectAnimation animation;animation.vertex_shift=shift;
        std::map<unsigned,unsigned> variants;
        for(unsigned frame=0;frame<count;++frame) {
            ObjectFrame f;f.control=read_u32(bank,at+36+frame*8);f.metadata=read_u32(bank,at+40+frame*8);
            f.ticks=((f.metadata>>9)&15)+1;
            unsigned collision_count=model.attachment_vertices;for(const auto& shape:model.damage_banks)if(shape)collision_count=std::max(collision_count,shape->vertex_count);
            const auto collision_at=stream+(f.control&0xffff)*2;extent(bank,collision_at,collision_count*4);
            for(unsigned v=0;v<collision_count;++v) {
                // 80040768: the collision prefix contains absolute packed
                // coordinates, not the render mesh's differential vertices.
                const auto packed=read_u32(bank,collision_at+v*4);const float scale=(1u<<shift)/retail::units_per_world;
                f.collision_vertices.push_back(Vec3{static_cast<float>(sx(packed>>11,11)),
                    static_cast<float>(-sx(packed,12)),static_cast<float>(sx(packed>>21,11))}*scale);
            }
            f.vertices=vertices(bank,base,stream,total,f.control,shift,codebook);
            const auto variant=(f.control&0x80000000)?32:((f.control>>26)&31);
            const auto found=variants.find(variant);
            if(found==variants.end()) {
                const auto packet=face_stream(bank,faces,patches,f.control);
                auto geometry=mesh(packet,bank.subspan(palette,colors*4),total);
                geometry.vertices=f.vertices;f.mesh=static_cast<unsigned>(model.meshes.size());
                variants.emplace(variant,f.mesh);model.meshes.push_back(std::move(geometry));
            } else f.mesh=found->second;
            for(const auto p:f.vertices) {
                if(!finite(p) || length(p)>2048)throw FormatError("Implausible animated object vertex");
                animation.radius=std::max(animation.radius,length(p));
                model.meshes[f.mesh].radius=std::max(model.meshes[f.mesh].radius,length(p));
            }
            animation.frames.push_back(std::move(f));
        }
        model.animations.push_back(std::move(animation));
    }
}
ObjectAnimationState start_object_animation(const ObjectModel& model,unsigned clip) {
    frame_at(model,clip,0);
    ObjectAnimationState state;state.clip=state.next_clip=static_cast<std::uint8_t>(clip);
    state.active=true;state.blend=model.animations[clip].frames.size()>1?0x72:0;
    return state;
}
void tick_object_animation(const ObjectModel& model,ObjectAnimationState& state) {
    // 80041068 advances a VBlank counter. Call once at 60 Hz; the PS1 caller
    // usually supplies two VBlanks per update. The upper nibble encodes N-1.
    const unsigned span=state.blend>>4;
    if(!state.active || !span)return;
    state.flags=0;
    const int remainder=(state.blend&15)+1-static_cast<int>(span);
    if(remainder<=0){++state.blend;return;}
    const auto& next=frame_at(model,state.next_clip,state.next_frame);
    const bool transition=state.clip!=state.next_clip;
    state.clip=state.next_clip;state.frame=state.next_frame;
    const auto duration=(next.metadata>>9)&15;
    state.blend=static_cast<std::uint8_t>((duration<<4)+(transition?0:remainder-1));
    ++state.next_frame;state.flags=1;
    if(transition && !duration)state.next_frame=state.frame;
    else if(state.next_frame>=model.animations[state.clip].frames.size()) {state.next_frame=0;state.flags|=2;}
}
ObjectPose sample_object_animation(const ObjectModel& model,const ObjectAnimationState& state,
                                  std::span<const std::uint8_t> blends) {
    if(state.blend>=blends.size())throw FormatError("Missing retail object interpolation table");
    return blend_frames(frame_at(model,state.clip,state.frame),frame_at(model,state.next_clip,state.next_frame),blends[state.blend]/256.0f);
}
ObjectPose object_animation_frame(const ObjectModel& model,unsigned clip,float frame) {
    if(!std::isfinite(frame))throw FormatError("Invalid object animation frame");
    frame_at(model,clip,0);
    const auto& animation=model.animations.at(clip);
    frame=std::clamp(frame,0.0f,static_cast<float>(animation.frames.size()-1));
    const auto a=static_cast<unsigned>(frame),b=std::min(a+1,static_cast<unsigned>(animation.frames.size()-1));
    return blend_frames(animation.frames[a],animation.frames[b],frame-a);
}
Vec3 sample_object_attachment(const ObjectModel& model,const ObjectAnimationState& state,
                              std::span<const std::uint8_t> blends,unsigned vertex) {
    const auto& a=frame_at(model,state.clip,state.frame).collision_vertices;
    const auto& b=frame_at(model,state.next_clip,state.next_frame).collision_vertices;
    if(vertex>=a.size() || vertex>=b.size() || state.blend>=blends.size())throw FormatError("Missing object attachment vertex");
    return lerp(a[vertex],b[vertex],blends[state.blend]/256.0f);
}
float object_model_scale(const RetailObject& object) {
    // 80044034..8004406c multiplies camera translation by size/32 before
    // projection. Its geometric equivalent is reciprocal scaling of vertices.
    return object.size?32.0f/object.size:1.0f;
}
Vec3 animated_object_color(const RetailObject& object,const ObjectFace& face,Vec3 color) {
    // The ordinary DPCS tint path at 80045620 / 80045110 affects the polygons
    // tagged with bit 5. Sparx's body uses it; his wings retain their palette.
    const auto word=object.material_color,mode=(word>>28)&7;
    if((mode==0 && (word&0x80000000)) || mode==2 || mode>3)return color;
    if(mode!=3 && (face.quad || (face.flags&0x30)!=0x20))return color;
    const bool environment=object_environment_material(object,face);
    if(environment && !face.textured)color=color*(255.0f/128);
    const float amount=static_cast<float>((word>>24)&15)/16,divisor=(face.textured || environment)?128.0f:255.0f;
    return lerp(color,{static_cast<float>(word&255)/divisor,static_cast<float>((word>>8)&255)/divisor,
                       static_cast<float>((word>>16)&255)/divisor},amount);
}
bool object_normal_material(const RetailObject& object,const ObjectFace& face) {
    return face.animated && !face.quad && (face.flags&0x30)==0x20 && ((object.material_color>>28)&7)==2;
}
bool object_environment_material(const RetailObject& object,const ObjectFace& face) {
    return face.animated && !face.quad && (face.flags&0x30)==0x20 && ((object.material_color>>28)&7)==1;
}
Vec3 normal_object_color(const RetailObject& object,const ObjectFace& face,const ObjectBasis& basis,
                         Vec3 right,Vec3 up,Vec3 forward) {
    // 800455b0 + 80045ccc: MVMVA rotates the signed packed normal, GPF
    // multiplies it by 2*material strength, and CC uses three identical
    // [-2848,-2458,-2517] rows plus the material RGB background (<<4).
    // Camera Y carries the same 320/512 correction as the static shiny path.
    const auto n=basis*face.shiny_normal;const auto word=object.material_color;
    const float strength=static_cast<float>((word>>23)&30);
    const float lighting=(-2848*dot(n,right)+2458*dot(n,up)*0.625f-2517*dot(n,forward))*strength/65536;
    const auto channel=[&](unsigned shift) {
        const float value=std::clamp((static_cast<float>((word>>shift)&255)+lighting)*(255.0f/256),0.0f,255.0f);
        return value/(face.textured?128:255);
    };
    return {channel(0),channel(8),channel(16)};
}
std::array<float,2> object_environment_uv(const ObjectModels& models,Vec3 world_vertex,float depth,float ticks) {
    if(models.environment_tile==UINT32_MAX || !models.texture_width || !models.texture_height)
        throw FormatError("Reflective object has no retail environment texture");
    // 80045a60..80045cc4: spherical lookup over the world-rotated local
    // vertex, using the executable's azimuth, radial and sine tables.
    const float x=world_vertex.z,y=-world_vertex.x;
    const float ax=std::abs(x),ay=std::abs(y);
    const auto ratio=static_cast<unsigned>(std::min(ax,ay)*64/std::max(0.000001f,std::max(ax,ay)));
    const unsigned arc=models.environment_angles[std::min(64u,ratio)];
    int angle=ay>ax?64-static_cast<int>(arc):static_cast<int>(arc);
    if(x<0)angle=128-angle;if(y<0)angle=-angle;
    const float magnitude=length(world_vertex);
    const auto elevation=static_cast<unsigned>(std::clamp(magnitude>0?world_vertex.y*128/magnitude:0.0f,0.0f,128.0f));
    const float radius=static_cast<float>(models.environment_radii[elevation]>>1);
    const int phase=static_cast<int>(depth*retail::units_per_world/64)+static_cast<int>(ticks)/8;
    const auto heading=static_cast<unsigned>(angle+phase)&255;
    const float u=64+radius*models.cosine[heading]/4096,v=64+radius*models.sine[heading]/4096;
    return {(models.environment_tile%16*256+u+0.5f)/models.texture_width,
            (models.environment_tile/16*256+v+0.5f)/models.texture_height};
}
void tick_object_animations(std::span<RetailObject> objects,const ObjectModels& models) {
    for(auto& object:objects) {
        object.previous_animation=object.animation;
        if(object.collected || object.state<0 || object.frozen_ticks || object.stun_ticks || object.retail_script)continue;
        const auto found=models.models.find(object.type);
        if(found!=models.models.end() && found->second.animated)tick_object_animation(found->second,object.animation);
    }
}
} // namespace spyro

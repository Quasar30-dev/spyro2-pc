#include "spyro/pc_actors.hpp"
#include "spyro/demons.hpp"
#include <algorithm>
#include <cmath>

namespace spyro {
namespace {
#include "generated/pc_actor_data.inc"
void require(bool ok,const char* message){if(!ok)throw FormatError(std::string("Invalid .s2actor: ")+message);}
struct Reader {
    std::span<const std::uint8_t> data;std::size_t at{};
    std::uint32_t u32(){require(at+4<=data.size(),"truncated");const auto v=read_u32(data,at);at+=4;return v;}
    std::uint16_t u16(){require(at+2<=data.size(),"truncated");const auto v=read_u16(data,at);at+=2;return v;}
    std::int16_t i16(){return static_cast<std::int16_t>(u16());}
    std::uint8_t u8(){require(at<data.size(),"truncated");return data[at++];}
};
bool pow2(unsigned v){return v && !(v&(v-1));}
}
Bytes encode_s2actor_checksum(std::span<const std::uint8_t> body) {
    Bytes out(body.begin(),body.end());const auto crc=crc32(body);
    for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(crc>>(8*i)));
    return out;
}
PcActor decode_s2actor(std::span<const std::uint8_t> bytes) {
    require(bytes.size()>=52 && std::equal(bytes.begin(),bytes.begin()+8,"S2ACTOR1"),"bad magic");
    require(crc32(bytes.first(bytes.size()-4))==read_u32(bytes,bytes.size()-4),"checksum mismatch");
    Reader r{bytes.first(bytes.size()-4),8};
    PcActor actor;actor.kind=r.u32();
    const auto vertices=r.u32(),faces=r.u32(),clips=r.u32(),width=r.u32(),height=r.u32(),palette_size=r.u32(),attachments=r.u32(),spheres=r.u32(),reserved=r.u32();
    require(actor.kind>=1 && actor.kind<=255,"kind");
    require(vertices>=3 && vertices<=s2actor_max_vertices,"vertex count (8-bit animated indices)");
    require(faces>=1 && faces<=s2actor_max_faces && clips>=1 && clips<=s2actor_max_clips,"face/clip count");
    require(pow2(width) && pow2(height) && width>=16 && width<=128 && height>=16 && height<=128,"texture size");
    require(palette_size==16 && attachments<=8 && spheres<=8 && reserved==0,"palette/attachments");
    std::array<std::uint16_t,16> palette{};for(auto& p:palette)p=r.u16();
    actor.texture_width=width;actor.texture_height=height;actor.texture_rgba.resize(std::size_t(width)*height*4);
    for(unsigned i=0;i<width*height/2;++i) {
        const auto pair=r.u8();
        for(unsigned half=0;half<2;++half) {
            const auto color=palette[half?pair>>4:pair&15];auto* p=&actor.texture_rgba[(std::size_t(i)*2+half)*4];
            for(unsigned c=0;c<3;++c){const auto v=(color>>(5*c))&31;p[c]=static_cast<std::uint8_t>((v<<3)|(v>>2));}
            p[3]=color?((color&0x8000)?128:255):0;
        }
    }
    ObjectMesh mesh;
    for(unsigned f=0;f<faces;++f) {
        ObjectFace face;
        for(unsigned k=0;k<4;++k){face.vertices[k]=r.u8();require(face.vertices[k]<vertices,"face vertex");}
        const auto flags=r.u8();r.u8();r.u8();r.u8();
        face.quad=(flags&1)!=0;face.translucent=(flags&2)!=0;face.blend_mode=(flags>>2)&3;
        if(!face.quad)face.vertices[3]=face.vertices[2];
        for(unsigned k=0;k<4;++k){const float u=r.u8(),v=r.u8();require(u<width && v<height,"uv outside texture");face.uv[k]={u,v};}
        for(unsigned k=0;k<4;++k){const float cr=r.u8(),cg=r.u8(),cb=r.u8();face.colors[k]={cr/128,cg/128,cb/128};}
        face.textured=true;face.animated=true;face.flags=0x20;face.palette_valid=true;
        mesh.faces.push_back(face);
    }
    auto& model=actor.model;model.animated=true;model.animation_count=clips;model.attachment_vertices=attachments;
    for(unsigned c=0;c<clips;++c) {
        const auto frames=r.u32(),ticks=r.u32();
        require(frames>=1 && frames<=s2actor_max_frames && ticks>=1 && ticks<=15,"clip timing");
        ObjectAnimation clip;
        for(unsigned f=0;f<frames;++f) {
            ObjectFrame frame;frame.ticks=ticks;frame.metadata=(ticks<<9)|(0x80u<<16);frame.mesh=0;
            for(unsigned v=0;v<vertices;++v) {
                const float x=r.i16(),y=r.i16(),z=r.i16();
                const Vec3 p{x/1024.0f,y/1024.0f,z/1024.0f};require(finite(p) && length(p)<16,"vertex");
                frame.vertices.push_back(p);clip.radius=std::max(clip.radius,length(p));
            }
            clip.frames.push_back(std::move(frame));
        }
        for(unsigned f=0;f<frames;++f)for(unsigned a=0;a<attachments;++a) {
            const float x=r.i16(),y=r.i16(),z=r.i16();clip.frames[f].collision_vertices.push_back({x/1024.0f,y/1024.0f,z/1024.0f});
        }
        model.animations.push_back(std::move(clip));
    }
    for(unsigned s=0;s<spheres;++s){const auto v=r.u16(),rad=r.u16();require(v<vertices && rad>0,"damage sphere");actor.spheres.push_back({v,rad});}
    require(r.at==r.data.size(),"trailing bytes");
    mesh.vertices=model.animations.front().frames.front().vertices;
    for(const auto& clip:model.animations)mesh.radius=std::max(mesh.radius,clip.radius);
    // Reject degenerate faces in the rest pose.
    for(const auto& face:mesh.faces) {
        const auto& v=mesh.vertices;const auto n=cross(v[face.vertices[1]]-v[face.vertices[0]],v[face.vertices[2]]-v[face.vertices[0]]);
        require(length(n)>1e-9f,"degenerate face");
    }
    model.meshes.push_back(std::move(mesh));
    return actor;
}
unsigned embedded_pc_actor_count(){return pc_actor_count;}
std::span<const std::uint8_t> embedded_pc_actor(unsigned index) {
    if(index>=pc_actor_count)throw FormatError("Embedded PC actor index out of range");
    return {pc_actor_blob+pc_actor_offset[index],pc_actor_size[index]};
}
void install_pc_actors(ObjectModels& target,std::span<const PcActor> actors,std::uint16_t type_base) {
    if(actors.empty())return;
    // Up to four 128x128 actor textures share one appended 256x256 tile.
    unsigned used=0;std::vector<std::pair<unsigned,unsigned>> slots;
    for(const auto& a:actors) {
        require(a.texture_width<=128 && a.texture_height<=128,"texture exceeds its 128x128 tile quarter");
        slots.push_back({(used%2)*128,(used/2)*128});++used;
        require(used<=4,"more than four actors in one atlas tile");
    }
    if(target.texture_width && target.texture_width!=4096)throw FormatError("Unsupported object atlas width for PC actors");
    const unsigned old_height=target.texture_height,tile=target.texture_tiles,new_count=tile+1,new_height=((new_count+15)/16)*256;
    if(new_height>16384)throw FormatError("Object atlas cannot hold PC actors");
    target.texture_width=4096;target.texture_rgba.resize(std::size_t(4096)*new_height*4);
    if(new_height!=old_height && old_height)
        for(auto& [id,model]:target.models)for(auto& mesh:model.meshes)for(auto& face:mesh.faces)if(face.textured)
            for(auto& uv:face.uv)uv[1]*=float(old_height)/new_height;
    const unsigned tx=tile%16*256,ty=tile/16*256;
    for(std::size_t i=0;i<actors.size();++i) {
        const auto& a=actors[i];const auto [sx,sy]=slots[i];
        for(unsigned y=0;y<a.texture_height;++y)
            std::copy_n(a.texture_rgba.begin()+std::size_t(y)*a.texture_width*4,a.texture_width*4,
                target.texture_rgba.begin()+(std::size_t(ty+sy+y)*4096+tx+sx)*4);
        auto model=a.model;
        for(auto& mesh:model.meshes)for(auto& face:mesh.faces)for(auto& uv:face.uv) {
            uv[0]=(tx+sx+uv[0]+.5f)/4096.0f;uv[1]=(ty+sy+uv[1]+.5f)/new_height;
        }
        target.models.insert_or_assign(type_base+a.kind,std::move(model));
    }
    target.texture_height=new_height;target.texture_tiles=new_count;
}
void install_embedded_demons(ObjectModels& models) {
    std::vector<PcActor> actors;
    for(unsigned i=0;i<embedded_pc_actor_count();++i)actors.push_back(decode_s2actor(embedded_pc_actor(i)));
    install_pc_actors(models,actors,pc_actor_base);
}
} // namespace spyro

#include "spyro/spyro_dark.hpp"
#include <algorithm>
#include <cmath>

namespace spyro {
namespace {
// Ramp in 8-bit PS1 space. Every colour family becomes the same near-black
// with a faint cool-violet lift (sampled from the gpt-image concept,
// assets/actors/references/dark-spyro/dark-spyro-concept.png: body median
// ~29,28,30); luminance order is kept so the scale shading still reads.
// Horns, crest, wings and tail tip are not remapped: they keep the retail
// atlas under dark_spyro_keep_tint (see DarkSpyroKeep).
struct Ramp {Vec3 low,high;float gamma;};
constexpr Ramp black{{7,6,10},{70,64,84},1.1f};
Vec3 ramp(const Ramp& r,float luminance) {
    const float t=std::pow(std::clamp(luminance,0.0f,1.0f),r.gamma);
    return r.low+(r.high-r.low)*t;
}
Vec3 remap(Vec3 rgb) {
    // rgb in 0..255. Purple skin is darker than the belly gold in luminance,
    // so lift it slightly to keep the two shades distinct in black.
    const float lum=(rgb.x*.30f+rgb.y*.59f+rgb.z*.11f)/255;
    const bool purple=rgb.z>=rgb.y && rgb.z>=rgb.x*.82f;
    return ramp(black,std::min(1.0f,lum*(purple?1.45f:1.0f)));
}
unsigned to5(float v){return static_cast<unsigned>(std::clamp(std::lround(v/255*31),0l,31l));}
}
DarkSpyroKeep dark_spyro_keep(const SpyroModel& model) {
    // Landmarks on the verified 230-vertex main mesh (probe output
    // data/analysis/elements/spyro-faces-17.txt): wing frames/membranes 54-73,
    // the tail's arrow tip 101-107, horns 115-119/130/204-219 and the gold
    // crest 187/194-203/220-224. The atlas is not consulted, so banks whose
    // Spyro tiles are empty (WAD 95) classify identically.
    static constexpr unsigned kept[]{54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,101,102,103,104,105,106,107,
        115,116,117,118,119,130,187,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,
        217,218,219,220,221,222,223,224};
    DarkSpyroKeep keep;
    const auto found=model.animations.find(0);
    if(found==model.animations.end() || found->second.frames.empty() || found->second.frames.front().vertices.size()!=230)return keep;
    for(const auto v:kept)keep.vertices[v]=true;
    for(const auto& face:found->second.faces)if(dark_spyro_kept(keep,face,230))++keep.faces;
    return keep;
}
bool dark_spyro_kept(const DarkSpyroKeep& keep,const SpyroFace& face,std::size_t vertex_count) {
    if(vertex_count<230)return false;
    const unsigned n=face.quad?4:3;bool v201=false,v220=false,v223=false;
    for(unsigned k=0;k<n;++k) {
        const auto v=face.vertices[k];if(v>=230 || !keep.vertices[v])return false;
        v201|=v==201;v220|=v==220;v223|=v==223;
    }
    // The two purple scalp faces between the crest and the horn bases.
    return !(v201 && v220 && v223);
}
std::uint16_t dark_spyro_color(std::uint16_t word) {
    if(!word)return 0;   // index 0 transparency is preserved exactly
    const auto stp=word&0x8000u;
    const Vec3 rgb{float(((word&31)<<3)|((word&31)>>2)),float((((word>>5)&31)<<3)|(((word>>5)&31)>>2)),float((((word>>10)&31)<<3)|(((word>>10)&31)>>2))};
    const auto out=remap(rgb);
    std::uint16_t result=static_cast<std::uint16_t>(to5(out.x)|(to5(out.y)<<5)|(to5(out.z)<<10)|stp);
    // A non-zero source must never collapse into the transparent zero word.
    if(!(result&0x7fffu) && !stp)result=1u<<10;
    return result;
}
void dark_spyro_texel(std::uint8_t* p) {
    if(!p[3])return;
    // RGBA came from (c<<3)|(c>>2); c=p>>3 recovers the 5-bit CLUT value exactly.
    const auto word=static_cast<std::uint16_t>((p[0]>>3)|((p[1]>>3)<<5)|((p[2]>>3)<<10)|(p[3]==128?0x8000u:0u));
    const auto dark=dark_spyro_color(word ? word : static_cast<std::uint16_t>(0x8000u));
    for(unsigned c=0;c<3;++c){const unsigned v=(dark>>(5*c))&31;p[c]=static_cast<std::uint8_t>((v<<3)|(v>>2));}
}
Vec3 dark_spyro_vertex(Vec3 c) {
    // Vertex colours are Gouraud light (128 = 1.0). Neutral light is kept; any
    // tint follows the same family mapping with its brightness retained.
    const float hi=std::max({c.x,c.y,c.z}),lo=std::min({c.x,c.y,c.z});
    if(hi-lo<.06f)return c;
    const float lum=c.x*.30f+c.y*.59f+c.z*.11f;
    return Vec3{lum*.92f,lum*.88f,lum*1.08f};
}
EyeCell spyro_eye_cell(const SpyroModel& model) {
    EyeCell cell;const auto found=model.animations.find(0);if(found==model.animations.end())return cell;
    const auto in=[](unsigned v,const auto& set){return std::find(set.begin(),set.end(),v)!=set.end();};
    float u0=1,v0=1,u1=0,v1=0;
    for(const auto& face:found->second.faces) {
        const unsigned n=face.quad?4:3;bool eye=true;
        for(unsigned k=0;k<n;++k)eye&=in(face.vertices[k],spyro_left_eye) || in(face.vertices[k],spyro_right_eye);
        if(!eye)continue;
        if(cell.tile==UINT32_MAX)cell.tile=face.atlas_tile;else if(cell.tile!=face.atlas_tile)continue;
        for(unsigned k=0;k<n;++k){u0=std::min(u0,face.uv[k][0]);u1=std::max(u1,face.uv[k][0]);v0=std::min(v0,face.uv[k][1]);v1=std::max(v1,face.uv[k][1]);}
    }
    if(cell.tile!=UINT32_MAX){cell.u0=u0;cell.v0=v0;cell.u1=u1;cell.v1=v1;}
    return cell;
}
Bytes dark_spyro_atlas(const SpyroModel& model) {
    auto atlas=model.texture_rgba;
    for(std::size_t i=0;i+3<atlas.size();i+=4)dark_spyro_texel(&atlas[i]);
    const auto cell=spyro_eye_cell(model);
    if(cell.valid() && model.texture_width && model.texture_height) {
        const auto x0=static_cast<unsigned>(std::floor(cell.u0*model.texture_width)),x1=static_cast<unsigned>(std::ceil(cell.u1*model.texture_width));
        const auto y0=static_cast<unsigned>(std::floor(cell.v0*model.texture_height)),y1=static_cast<unsigned>(std::ceil(cell.v1*model.texture_height));
        for(unsigned y=y0;y<=y1 && y<model.texture_height;++y)for(unsigned x=x0;x<=x1 && x<model.texture_width;++x) {
            auto* p=&atlas[(std::size_t(y)*model.texture_width+x)*4];
            if(p[3]){p[0]=236;p[1]=236;p[2]=255;}   // flat near-white glowing eyes, no pupil
        }
    }
    return atlas;
}
std::array<std::vector<unsigned>,2> spyro_eye_vertices(const SpyroModel& model,const SpyroAnimation& clip,std::size_t count) {
    std::array<std::vector<unsigned>,2> result;
    if(count==230) {
        result[0].assign(spyro_left_eye.begin(),spyro_left_eye.end());
        result[1].assign(spyro_right_eye.begin(),spyro_right_eye.end());
        return result;
    }
    const auto cell=spyro_eye_cell(model);if(!cell.valid())return result;
    const float du=1.5f/std::max(1u,model.texture_width),dv=1.5f/std::max(1u,model.texture_height);
    std::vector<unsigned> eye;
    for(const auto& face:clip.faces) {
        if(face.atlas_tile!=cell.tile)continue;
        const unsigned n=face.quad?4:3;bool inside=true;
        for(unsigned k=0;k<n;++k)inside&=face.uv[k][0]>=cell.u0-du && face.uv[k][0]<=cell.u1+du && face.uv[k][1]>=cell.v0-dv && face.uv[k][1]<=cell.v1+dv;
        if(inside)for(unsigned k=0;k<n;++k)if(face.vertices[k]<count && std::find(eye.begin(),eye.end(),face.vertices[k])==eye.end())eye.push_back(face.vertices[k]);
    }
    if(clip.frames.empty())return result;
    const auto& rest=clip.frames.front().vertices;
    for(auto v:eye)if(v<rest.size())result[rest[v].x<0?0:1].push_back(v);
    return result;
}
std::optional<std::array<Vec3,2>> spyro_eye_centers(const SpyroModel& model,const SpyroPose& pose,const SpyroAnimation* clip) {
    std::array<std::vector<unsigned>,2> sets;
    if(pose.vertices.size()==230 || !clip)sets=pose.vertices.size()==230?spyro_eye_vertices(model,model.animations.begin()->second,230):sets;
    else sets=spyro_eye_vertices(model,*clip,pose.vertices.size());
    if((sets[0].empty() || sets[1].empty()) && pose.vertices.size()==255) {
        // The retail supercharge mesh has no eye texture: carry the main mesh's
        // eye centres (charge bind pose) through the retarget head transfer.
        const auto main=model.animations.find(12);
        if(main==model.animations.end() || main->second.loop_start>=main->second.frames.size())return {};
        const auto& bind=main->second.frames[main->second.loop_start].vertices;if(bind.size()!=230)return {};
        Vec3 head{};for(std::size_t i=115;i<bind.size();++i)head+=bind[i];head=head/float(bind.size()-115);
        std::array<Vec3,2> out{};
        for(unsigned e=0;e<2;++e) {
            Vec3 c{};for(auto v:e?spyro_right_eye:spyro_left_eye)c+=bind[v];c=c/7.0f;
            const auto dir=normalized(c-head);
            const auto mapped=retail_supercharge_head_point(model,pose.vertices,c+(length(dir)>.5f?dir*.012f:Vec3{}));
            if(!mapped)return {};
            out[e]=*mapped;
        }
        return out;
    }
    if(sets[0].empty() || sets[1].empty())return {};
    Vec3 head{};unsigned n=0;
    for(std::size_t i=std::min<std::size_t>(115,pose.vertices.size());i<pose.vertices.size();++i){head+=pose.vertices[i];++n;}
    if(n)head=head/float(n);
    std::array<Vec3,2> out{};
    for(unsigned e=0;e<2;++e) {
        Vec3 c{};for(auto v:sets[e])c+=pose.vertices.at(v);c=c/float(sets[e].size());
        const auto out_dir=normalized(c-head);
        out[e]=c+(length(out_dir)>.5f?out_dir*.012f:Vec3{});
    }
    return out;
}
unsigned spyro_tail_tip(const SpyroModel& model) {
    const auto found=model.animations.find(0);
    if(found==model.animations.end() || found->second.frames.empty() || found->second.frames.front().vertices.size()<112)return 107;
    const auto& v=found->second.frames.front().vertices;unsigned best=107;float far=-1;
    for(unsigned i=96;i<112;++i){const float d=length(v[i]-v[95]);if(d>far){far=d;best=i;}}
    return best;
}
std::vector<unsigned> spyro_snout_vertices(const SpyroModel& model) {
    std::vector<unsigned> out;
    const auto found=model.animations.find(0);
    if(found==model.animations.end() || found->second.frames.empty() || found->second.frames.front().vertices.size()!=230)return out;
    const auto& v=found->second.frames.front().vertices;
    float front=-1e9f;for(unsigned i=115;i<230;++i)front=std::max(front,v[i].z);
    for(unsigned i=115;i<230;++i)if(v[i].z>front-.035f)out.push_back(i);
    return out;
}
std::optional<Vec3> spyro_mouth(const SpyroModel& model,const SpyroPose& pose) {
    if(pose.vertices.size()!=230)return {};
    const auto snout=spyro_snout_vertices(model);if(snout.empty())return {};
    Vec3 tip{};for(auto i:snout)tip+=pose.vertices[i];tip=tip/float(snout.size());
    Vec3 head{};for(unsigned i=115;i<230;++i)head+=pose.vertices[i];head=head/115.0f;
    const auto forward=normalized(tip-head);
    return tip+forward*.03f-Vec3{0,.02f,0};
}
} // namespace spyro

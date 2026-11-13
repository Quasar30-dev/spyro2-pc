#include "spyro/spyro_model.hpp"
#include "spyro/coordinates.hpp"
#include "spyro/game_build.hpp"
#include "spyro/yotd_assets.hpp"
#include <set>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void extent(View bytes,std::size_t at,std::size_t size) {
    if(at>bytes.size() || size>bytes.size()-at)throw FormatError("Spyro model data outside its archive record");
}
int sign_extend(unsigned value,unsigned bits) {
    const unsigned sign=1u<<(bits-1),mask=(1u<<bits)-1;
    return static_cast<int>((value&mask)^sign)-static_cast<int>(sign);
}
std::array<int,3> unpack(std::uint32_t word) {
    return {sign_extend(word>>21,11),sign_extend(word>>10,11),sign_extend(word,10)*2};
}
Vec3 native_position(const std::array<int,3>& p) {
    return retail::model_position({static_cast<float>(p[0]),static_cast<float>(p[1]),static_cast<float>(p[2])});
}
unsigned yotd_animation_id(unsigned raw) {
    // YOTD moved Spyro 2's alternate walk (22) to slot 2, added/reordered
    // the roll/skate entries. Verified later PC motion families retain their
    // IDs; unused later entries preserve raw IDs without claiming semantics.
    if(raw==2)return 22;
    if(raw>=3 && raw<=9)return raw-1;
    if(raw==11)return 9;
    if(raw==12)return 11;
    if(raw>=13 && raw<=22)return raw-1;
    return raw;
}
SpyroAnimation decode_animation(unsigned id,View record,std::size_t info,View common,
                               View exe,const ExeTables& tables,bool extra,std::span<const std::uint32_t> lookup) {
    extent(record,info,20);
    const unsigned count=record[info],color_count=record[info+1],head=record[info+2],total=record[info+3];
    if(!count || !total || head>total || !color_count)throw FormatError("Invalid Spyro animation dimensions");
    extent(record,info+20,static_cast<std::size_t>(count)*20);
    const auto vertices_at=extra?info+20+count*20:read_u32(record,info+4);
    const auto stream=extra?vertices_at+read_u32(record,info+16):read_u32(record,info+16);
    const View geometry=extra?record:common;
    extent(geometry,vertices_at,static_cast<std::size_t>(total)*4);
    extent(geometry,stream,0);
    SpyroAnimation animation;animation.id=id;animation.source_id=id;animation.split_vertex=head;animation.level_specific=extra;
    // 80060334: loop start, loop end (exclusive), transition end, flags.
    const auto loop_table=tables(0x80060334);
    extent(exe,loop_table+id*4,4);
    animation.loop_start=exe[loop_table+id*4];animation.loop_end=exe[loop_table+id*4+1];
    animation.transition_end=exe[loop_table+id*4+2];animation.flags=exe[loop_table+id*4+3];
    // August retains stale terminal indices for the shortened death/whirlwind
    // banks (31:25 frames, 59:24). Preserve the last authored frame instead of
    // reading the following compressed record as a vertex pose.
    if(tables.profile.build==GameBuild::August1999 && (id==31 || id==59) &&
       animation.loop_start==count && animation.loop_end==count && animation.transition_end==count)
        animation.loop_start=animation.loop_end=count-1;
    if(animation.loop_start>=count || animation.loop_end>count || animation.loop_end<animation.loop_start)
        throw FormatError("Spyro animation "+std::to_string(id)+" loop ["+std::to_string(animation.loop_start)+","+std::to_string(animation.loop_end)+"] outside "+std::to_string(count)+" frames");
    for(unsigned f=0;f<count;++f) {
        const auto at=info+20+f*20;
        const auto control=read_u32(record,at),splits=read_u32(record,at+4);
        const auto root=unpack(read_u32(record,at+8));
        SpyroFrame frame;frame.ticks=((control>>18)&15)+1;frame.vertices.reserve(total);
        frame.attachment=native_position(root);
        const auto start=stream+(control&65535);
        for(unsigned part=0;part<2;++part) {
            bool wide=((control>>(16+part))&1)!=0;
            std::size_t words_at=start+(part?(splits&1023):0);
            std::size_t bytes_at=start+(part?(splits>>20):((splits>>10)&1023));
            std::array<int,3> position{};
            for(unsigned v=part?head:0;v<(part?total:head);++v) {
                const auto base=read_u32(geometry,vertices_at+v*4);
                if(!wide) {
                    extent(geometry,bytes_at,1);const auto code=geometry[bytes_at++];wide=(code&1)!=0;
                    if((code>>1)>=lookup.size())throw FormatError("Invalid Spyro vertex codebook index");
                    const auto delta=unpack(base+lookup[code>>1]);
                    for(unsigned k=0;k<3;++k)position[k]+=delta[k];
                } else {
                    const auto code=read_u16(geometry,words_at);words_at+=2;wide=(code&1)!=0;
                    if(code&2) {
                        const auto next=read_u16(geometry,words_at);words_at+=2;
                        position={sign_extend(next,16)>>5,
                            -((sign_extend(next,6)*32)|((code&0xf000)>>11)),
                            -sign_extend(code>>1,11)};
                    } else {
                        auto delta=unpack(base);
                        delta[0]+=sign_extend(code,16)>>9;delta[1]+=sign_extend(code>>4,7);
                        delta[2]+=sign_extend(code>>1,5)*4;
                        for(unsigned k=0;k<3;++k)position[k]+=delta[k];
                    }
                }
                auto p=position;
                if(part)for(unsigned k=0;k<3;++k)p[k]+=root[k];
                frame.vertices.push_back(native_position(p));
            }
        }
        animation.frames.push_back(std::move(frame));
    }
    const auto face_at=read_u32(record,info+8),colors_at=read_u32(record,info+12);
    const auto face_size=read_u32(common,face_at);
    if(face_size%20 || face_size>1024*1024)throw FormatError("Invalid Spyro polygon stream size");
    extent(common,static_cast<std::size_t>(face_at)+4,face_size);
    extent(common,colors_at,static_cast<std::size_t>(color_count)*4);
    for(std::size_t at=face_at+4;at<static_cast<std::size_t>(face_at)+4+face_size;at+=20) {
        SpyroFace face;
        const auto indices=read_u32(common,at),colors=read_u32(common,at+4);
        face.clut=read_u16(common,at+10);face.page=read_u16(common,at+14);
        // 80041dbc adds GPU command bit 1 when the packed TPAGE is negative.
        face.translucent=(face.page&0x8000)!=0;face.blend_mode=(face.page>>5)&3;
        for(unsigned i=0;i<4;++i) {
            face.vertices[i]=static_cast<std::uint8_t>(indices>>(24-i*8));
            const unsigned ci=(colors>>(24-i*7))&127;
            if(face.vertices[i]>=total || ci>=color_count)throw FormatError("Spyro face references an invalid vertex or color");
            const auto rgb=read_u32(common,colors_at+ci*4);
            face.colors[i]={static_cast<float>(rgb&255)/128.0f,static_cast<float>((rgb>>8)&255)/128.0f,static_cast<float>((rgb>>16)&255)/128.0f};
            const auto uv_at=at+(i<2?8+i*4:16+(i-2)*2);
            face.uv[i]={static_cast<float>(common[uv_at]),static_cast<float>(common[uv_at+1])};
        }
        face.quad=face.vertices[2]!=face.vertices[3];animation.faces.push_back(face);
    }
    return animation;
}
void make_textures(SpyroModel& model,View level,unsigned area) {
    // 80070498/80077374 upload child 0's first 0x80000 bytes to VRAM
    // (512,0,512,512). This includes Spyro's CLUTs and texture page.
    const auto texture_at=read_u32(level,0),texture_size=read_u32(level,4);
    if(texture_size<0x80000)throw FormatError("Level is missing the retail VRAM upload");
    extent(level,texture_at,0x80000);YotdVramImage mounted;
    const GameProfile profile{model.source_game,0,"Spyro material archive"};
    if(profile.yotd())mounted=decode_yotd_vram(level,profile,area);
    const View vram=profile.yotd()?View{mounted.pixels}:level.subspan(texture_at,0x80000);
    const auto word=[&](unsigned x,unsigned y) {
        if(x<512 || x>=1024 || y>=512)throw FormatError("Spyro texture references unloaded VRAM");
        return read_u16(vram,(static_cast<std::size_t>(y)*512+x-512)*2);
    };
    std::map<std::pair<unsigned,unsigned>,unsigned> tiles;
    for(auto& [id,animation]:model.animations)for(auto& face:animation.faces) {
        const auto key=std::make_pair(face.page,face.clut);
        const auto [it,inserted]=tiles.emplace(key,static_cast<unsigned>(tiles.size()));
        (void)inserted;face.atlas_tile=it->second;
    }
    if(tiles.empty() || tiles.size()>256)throw FormatError("Invalid Spyro texture tile count");
    model.texture_tiles=static_cast<unsigned>(tiles.size());
    constexpr unsigned columns=5;
    model.texture_width=columns*256;model.texture_height=((model.texture_tiles+columns-1)/columns)*256;
    model.texture_rgba.resize(static_cast<std::size_t>(model.texture_width)*model.texture_height*4);
    for(const auto& [key,tile]:tiles) {
        const auto [page,clut]=key;
        const unsigned page_x=(page&15)*64,page_y=((page>>4)&1)*256,depth=(page>>7)&3;
        const unsigned clut_x=(clut&63)*16,clut_y=(clut>>6)&511;
        if(depth!=0)throw FormatError("Unexpected Spyro texture depth; this profile uses 4bpp");
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
            const unsigned index=(word(page_x+x/4,page_y+y)>>((x%4)*4))&15;
            const auto color=word(clut_x+index,clut_y);
            const auto at=(static_cast<std::size_t>((tile/columns)*256+y)*model.texture_width+(tile%columns)*256+x)*4;
            for(unsigned c=0;c<3;++c) {
                const auto component=(color>>(c*5))&31;
                model.texture_rgba[at+c]=static_cast<std::uint8_t>((component<<3)|(component>>2));
            }
            model.texture_rgba[at+3]=color?((color&0x8000)?128:255):0;
        }
    }
    for(auto& [id,animation]:model.animations)for(auto& face:animation.faces)for(auto& uv:face.uv) {
        uv[0]=(uv[0]+0.5f+(face.atlas_tile%columns)*256)/model.texture_width;
        uv[1]=(uv[1]+0.5f+(face.atlas_tile/columns)*256)/model.texture_height;
    }
}
SpyroPose interpolate(const SpyroAnimation& clip,unsigned current,unsigned next,float t) {
    SpyroPose pose;pose.frame=current;pose.next_frame=next;pose.blend=t;
    pose.attachment=lerp(clip.frames.at(current).attachment,clip.frames.at(next).attachment,t);
    const auto& a=clip.frames.at(current).vertices;const auto& b=clip.frames.at(next).vertices;
    pose.vertices.reserve(a.size());
    for(std::size_t i=0;i<a.size();++i)pose.vertices.push_back(lerp(a[i],b[i],t));
    return pose;
}
void normalize_yotd_vertices(SpyroModel& target,const SpyroModel& donor) {
    const auto& native=target.animations.at(0);const auto& retail=donor.animations.at(0);
    constexpr unsigned count=230;
    if(native.frames.empty() || retail.frames.empty() || native.frames[0].vertices.size()!=count ||
       retail.frames[0].vertices.size()!=count || native.split_vertex!=115 || retail.split_vertex!=115 ||
       native.faces.size()!=retail.faces.size()+1)
        throw FormatError("YOTD PC donor has incompatible neutral topology");
    // One extra face and two changed edges prevent a strict face-by-face
    // bijection. Find the inserted face from the mounted topology, then solve
    // a one-to-one assignment maximizing agreement of the remaining corners.
    // The original idle motion breaks ties between adjacent mouth vertices.
    std::vector<unsigned> best_votes(count*count);unsigned best_score=0,best_quad=UINT32_MAX;
    for(unsigned skip=0;skip<native.faces.size();++skip) {
        std::vector<unsigned> votes(count*count);unsigned quad=0;
        for(unsigned face=0;face<retail.faces.size();++face) {
            const auto& a=native.faces[face+unsigned(face>=skip)];const auto& b=retail.faces[face];
            if(a.quad!=b.quad)++quad;
            for(unsigned corner=0;corner<4;++corner) {
                const auto v=a.vertices[corner],w=b.vertices[corner];
                if(v>=count || w>=count)throw FormatError("YOTD neutral face outside its vertex bank");
                ++votes[v*count+w];
            }
        }
        unsigned score=0;
        for(unsigned v=0;v<count;++v)score+=*std::max_element(votes.begin()+v*count,votes.begin()+(v+1)*count);
        if(score>best_score || (score==best_score && quad<best_quad)) {best_score=score;best_quad=quad;best_votes=std::move(votes);}
    }
    if(best_score+8<retail.faces.size()*4 || best_quad>2)throw FormatError("YOTD donor face correspondence is incomplete");
    const auto frames=std::min(native.frames.size(),retail.frames.size());
    std::vector<double> costs(count*count);
    for(unsigned v=0;v<count;++v)for(unsigned w=0;w<count;++w) {
        if((v<115)!=(w<115)) {costs[v*count+w]=1e12;continue;}
        double distance=0;
        for(unsigned frame=0;frame<frames;++frame) {
            const auto delta=native.frames[frame].vertices[v]-retail.frames[frame].vertices[w];
            distance+=dot(delta,delta);
        }
        costs[v*count+w]=distance-best_votes[v*count+w]*1000000.0;
    }
    // Hungarian assignment; potentials and dummy column are one-based.
    std::vector<double> row(count+1),column(count+1),minimum(count+1);
    std::vector<unsigned> matched(count+1),previous(count+1);
    for(unsigned next=1;next<=count;++next) {
        matched[0]=next;unsigned j0=0;std::fill(minimum.begin(),minimum.end(),1e30);
        std::vector<bool> used(count+1);
        do {
            used[j0]=true;const auto i0=matched[j0];double delta=1e30;unsigned j1=0;
            for(unsigned j=1;j<=count;++j)if(!used[j]) {
                const auto value=costs[(i0-1)*count+j-1]-row[i0]-column[j];
                if(value<minimum[j]) {minimum[j]=value;previous[j]=j0;}
                if(minimum[j]<delta) {delta=minimum[j];j1=j;}
            }
            for(unsigned j=0;j<=count;++j)if(used[j]) {row[matched[j]]+=delta;column[j]-=delta;}else minimum[j]-=delta;
            j0=j1;
        } while(matched[j0]);
        do {const auto j1=previous[j0];matched[j0]=matched[j1];j0=j1;}while(j0);
    }
    std::vector<unsigned> map(count);unsigned agreement=0;
    target.source_vertices.resize(count);
    for(unsigned canonical=0;canonical<count;++canonical) {
        const auto original=matched[canonical+1]-1;
        if((original<115)!=(canonical<115))throw FormatError("YOTD donor mapping crosses the head partition");
        map[original]=canonical;target.source_vertices[canonical]=original;
        agreement+=best_votes[original*count+canonical];
    }
    if(agreement+8<retail.faces.size()*4)throw FormatError("YOTD donor vertex assignment lost face correspondence");
    for(auto& [id,animation]:target.animations) {
        if(animation.frames.empty() || animation.frames[0].vertices.size()!=count)continue;
        if(animation.split_vertex!=115)throw FormatError("YOTD movement has a different head partition");
        for(auto& frame:animation.frames) {
            if(frame.vertices.size()!=count)throw FormatError("YOTD movement changes vertex dimensions");
            const auto original=frame.vertices;
            for(unsigned v=0;v<count;++v)frame.vertices[map[v]]=original[v];
        }
        for(auto& face:animation.faces)for(auto& v:face.vertices) {
            if(v>=count)throw FormatError("YOTD movement face outside its vertex bank");
            v=static_cast<std::uint16_t>(map[v]);
        }
    }
    target.wing_tips=donor.wing_tips;
}
}
SpyroModel decode_spyro(View global,View level,View executable,View powerup_archive,unsigned powerup_slot,
                        View swimming_archive,View sliding_archive,View underwater_hurt_archive,View skating_archive,unsigned area) {
    const ExeTables tables(executable);
    const auto model_at=read_u32(global,4);extent(global,model_at,60);
    const unsigned count=read_u32(global,model_at);
    if(count!=62)throw FormatError("Unexpected Spyro animation slot count");
    extent(global,model_at+60,count*4);
    const auto common_at=static_cast<std::size_t>(model_at)+read_u32(global,model_at+56);
    extent(global,common_at,0);const auto common=global.subspan(common_at);
    std::array<std::uint32_t,125> lookup{};
    for(unsigned i=0;i<lookup.size();++i)lookup[i]=read_u32(executable,tables(0x80062674)+i*4);
    SpyroModel model;model.source_game=tables.profile.build;
    const auto semantic_id=[&](unsigned raw){return tables.profile.yotd()?yotd_animation_id(raw):raw;};
    const auto decode=[&](unsigned raw,View record,std::size_t at,bool extra) {
        auto clip=decode_animation(raw,record,at,common,executable,tables,extra,lookup);
        clip.id=semantic_id(raw);return clip;
    };
    extent(executable,tables(0x8006042c),model.transitions.size());
    if(tables.profile.yotd()) {
        for(unsigned from=0;from<count;++from)for(unsigned to=0;to<count;++to)
            model.transitions[semantic_id(from)*62+semantic_id(to)]=executable[tables(0x8006042c)+from*62+to];
    } else std::copy_n(executable.begin()+tables(0x8006042c),model.transitions.size(),model.transitions.begin());
    std::copy_n(executable.begin()+tables(0x80062868),model.blends.size(),model.blends.begin());
    if(tables.profile.yotd()) {
        // The first eight actions keep their meaning; two extra YOTD flame
        // actions extend the original table to 10x10. Preserve its real stride.
        const auto matrix=tables.native(0x80068ab0),clips=tables.native(0x80067ba0);
        extent(executable,matrix,100);extent(executable,clips,10);
        for(unsigned from=0;from<8;++from)for(unsigned to=0;to<8;++to)
            model.secondary_transitions[from*8+to]=executable[matrix+from*10+to];
        for(unsigned i=0;i<8;++i)model.secondary_clips[i]=static_cast<std::uint8_t>(semantic_id(executable[clips+i]));
    } else {
        std::copy_n(executable.begin()+tables(0x80061330),model.secondary_transitions.size(),model.secondary_transitions.begin());
        std::copy_n(executable.begin()+tables(0x80066d54),model.secondary_clips.size(),model.secondary_clips.begin());
    }
    for(unsigned id=0;id<count;++id) {
        const auto pointer=read_u32(global,model_at+60+id*4);
        if(pointer==0xffffffff)continue;
        model.animations.emplace(semantic_id(id),decode(id,global,static_cast<std::size_t>(model_at)+pointer,false));
    }
    // 8006feb0: level header +0x40 points to variable-sized animation records.
    std::size_t extra_at=read_u32(level,level_model_directory(level,tables.profile));
    if(area) {
        if(!tables.profile.yotd() || area>4)throw FormatError("Invalid Spyro animation area");
        const auto header=0x20+(area-1)*16;extent(level,header,8);
        const auto blob=read_u32(level,header),size=read_u32(level,header+4);
        if(!blob || !size)throw FormatError("Missing Spyro animation area");
        extent(level,blob,size);const auto banks=read_u32(level,blob);
        if(banks<4 || banks>size)throw FormatError("Invalid Spyro animation area bank");
        extra_at=std::size_t(blob)+4;
    }
    if(extra_at) {
        for(unsigned guard=0;;++guard) {
            if(guard>62)throw FormatError("Unterminated level-specific Spyro animation bank");
            const auto id=read_u32(level,extra_at);
            if(id==0xffffffff)break;
            if(id>=count)throw FormatError("Invalid level-specific Spyro animation ID");
            const auto size=read_u32(level,extra_at+4);
            extent(level,extra_at+8,size);
            auto record=level.subspan(extra_at+8,size);
            model.animations.insert_or_assign(semantic_id(id),decode(id,record,0,true));
            extra_at+=8+size;
        }
    }
    if(!powerup_archive.empty()) {
        if(!powerup_slot)throw FormatError("Supplemental Spyro animations require a source scene slot");
        // Only the verified flight family and supercharge clip may cross scene
        // banks. Their faces/palettes still use global common data and the
        // selected scene's Spyro VRAM, not a foreign texture-atlas index.
        auto at=static_cast<std::size_t>(read_u32(powerup_archive,level_model_directory(powerup_archive,tables.profile)));
        if(!at)throw FormatError("Power-up source has no Spyro animation bank");
        for(unsigned guard=0;;++guard) {
            if(guard>62)throw FormatError("Unterminated supplemental Spyro bank");
            const auto raw=read_u32(powerup_archive,at);
            if(raw==0xffffffff)break;
            if(raw>=62)throw FormatError("Invalid supplemental Spyro animation ID");
            const auto id=semantic_id(raw);
            const auto size=read_u32(powerup_archive,at+4);extent(powerup_archive,at+8,size);
            if((id==25 || id==33 || id==34 || id==35 || id==61) && !model.animations.contains(id)) {
                auto clip=decode(raw,powerup_archive.subspan(at+8,size),0,true);
                clip.imported_from_slot=powerup_slot;model.animations.emplace(id,std::move(clip));
            }
            at+=8+size;
        }
        for(const auto id:{25u,33u,34u,35u,61u})if(!model.animations.contains(id))
            throw FormatError("Power-up source is missing a required Spyro animation");
    }
    // PC movement can reach water, sand and whirlwinds in authored/generated
    // worlds or with cheats even when the selected retail bank omits that clip.
    // Import only verified movement families; never substitute an idle pose.
    const auto supplement=[&](View archive,unsigned slot,std::initializer_list<unsigned> needed) {
        if(archive.empty())return;
        auto at=static_cast<std::size_t>(read_u32(archive,level_model_directory(archive,tables.profile)));
        if(!at)throw FormatError("Movement source has no Spyro animation bank");
        for(unsigned guard=0;;++guard) {
            if(guard>62)throw FormatError("Unterminated movement Spyro animation bank");
            const auto raw=read_u32(archive,at);if(raw==0xffffffff)break;
            if(raw>=62)throw FormatError("Invalid movement Spyro animation ID");
            const auto id=semantic_id(raw);
            const auto size=read_u32(archive,at+4);extent(archive,at+8,size);
            if(std::find(needed.begin(),needed.end(),id)!=needed.end() && !model.animations.contains(id)) {
                auto clip=decode(raw,archive.subspan(at+8,size),0,true);
                clip.imported_from_slot=slot;model.animations.emplace(id,std::move(clip));
            }
            at+=8+size;
        }
        for(auto id:needed)if(!model.animations.contains(id))
            throw FormatError("Movement source "+std::to_string(slot)+" lacks Spyro animation "+std::to_string(id));
    };
    supplement(swimming_archive,15,{18,40,41,42,43,44});
    supplement(sliding_archive,45,{49});
    // Mystic Marsh overlay 58: 8006f374 / 8006fdd4, state 0x39.
    supplement(underwater_hurt_archive,59,{57});
    // Winter Tundra overlay 56: 8006d950 / 8006eb54, state 9 skates on surface
    // type 4 with clip 9 (also in Colossus' and Metropolis' banks).
    supplement(skating_archive,57,{9});
    make_textures(model,level,area);
    // The same authored face corners survive the early vertex reordering:
    // retail/August 63,62; July/Jampack 58,57. Do not use retail vertex IDs
    // on an early mesh (they select the same wing/root instead of both tips).
    const auto& body=model.animations.at(0);
    model.wing_tips={body.faces.at(197).vertices[2],body.faces.at(253).vertices[1]};
    if(model.animations.contains(61) && !tables.profile.prerelease() && !tables.profile.yotd())retarget_supercharge(model);
    if(!tables.profile.yotd()) {
        model.charge_smoke=decode_charge_smoke(level);
        if(model.charge_smoke.texture_rgba.empty() && !powerup_archive.empty())model.charge_smoke=decode_charge_smoke(powerup_archive);
    }
    return model;
}
void import_retail_power_animations(SpyroModel& target,const SpyroModel& donor) {
    const bool yotd=target.source_game==GameBuild::YearOfTheDragonUS;
    if(yotd && target.pc_motion_imported)return;
    if(yotd && target.source_vertices.empty())normalize_yotd_vertices(target,donor);
    const auto& native=target.animations.at(0);const auto& retail=donor.animations.at(0);
    const auto count=native.frames.at(0).vertices.size();
    if(count!=230 || retail.frames.at(0).vertices.size()!=count || (!yotd && native.faces.size()!=retail.faces.size()))
        throw FormatError("Retail power donor has incompatible Spyro topology");
    std::vector<unsigned> map(count,UINT32_MAX),reverse(count,UINT32_MAX);
    if(yotd)for(unsigned v=0;v<count;++v)map[v]=reverse[v]=v;
    else for(unsigned f=0;f<native.faces.size();++f)for(unsigned k=0;k<(native.faces[f].quad?4u:3u);++k) {
        const auto a=native.faces[f].vertices[k],b=retail.faces[f].vertices[k];
        if(native.faces[f].quad!=retail.faces[f].quad || a>=count || b>=count ||
           (map[a]!=UINT32_MAX && map[a]!=b) || (reverse[b]!=UINT32_MAX && reverse[b]!=a))
            throw FormatError("Retail power vertex correspondence is not bijective");
        map[a]=b;reverse[b]=a;
    }
    for(unsigned v=0;v<count;++v)if(map[v]==UINT32_MAX || ((v<native.split_vertex)!=(map[v]<retail.split_vertex)))
        throw FormatError("Retail power vertex partitions differ");
    std::vector<unsigned> imported;
    const std::vector<unsigned> needed=yotd?std::vector<unsigned>{9,18,25,33,34,35,40,41,42,43,44,49,57}:std::vector<unsigned>{25,33,34,35};
    for(unsigned id:needed) {
        if(id!=25 && target.animations.contains(id))continue;
        auto clip=donor.animations.at(id);
        for(auto& frame:clip.frames) {
            if(frame.vertices.size()!=count)throw FormatError("Retail power frame has incompatible vertices");
            const auto vertices=frame.vertices;for(unsigned v=0;v<count;++v)frame.vertices[v]=vertices[map[v]];
        }
        clip.faces=native.faces;clip.split_vertex=native.split_vertex;
        target.animations.insert_or_assign(id,std::move(clip));imported.push_back(id);
    }
    // Clip 61 is a separate 255-vertex mesh; carry its own atlas with it.
    // Normal Spyro, flight and jump keep the mounted build's original skin.
    auto charge=donor.animations.at(61);const auto old_height=target.texture_height;
    if(target.texture_width!=donor.texture_width || !old_height || !donor.texture_height)
        throw FormatError("Retail supercharge atlas dimensions differ");
    const auto height=old_height+donor.texture_height;
    for(auto& [id,clip]:target.animations)for(auto& face:clip.faces)for(auto& uv:face.uv)uv[1]*=float(old_height)/height;
    for(auto& face:charge.faces) {
        face.atlas_tile+=old_height/256*5;
        for(auto& uv:face.uv)uv[1]=(uv[1]*donor.texture_height+old_height)/height;
    }
    target.texture_rgba.insert(target.texture_rgba.end(),donor.texture_rgba.begin(),donor.texture_rgba.end());
    target.texture_tiles=old_height/256*5+donor.texture_tiles;target.texture_height=height;
    target.animations.insert_or_assign(61,std::move(charge));imported.push_back(61);
    for(unsigned id:imported)for(unsigned i=0;i<62;++i) {
        target.transitions[id*62+i]=donor.transitions[id*62+i];
        target.transitions[i*62+id]=donor.transitions[i*62+id];
    }
    // July's powered-head action still names clip 54. Use retail's firing
    // action/transition row with the mounted clip 7, not an unreferenced head.
    target.secondary_clips[6]=donor.secondary_clips[6];
    for(unsigned i=0;i<8;++i) {
        target.secondary_transitions[6*8+i]=donor.secondary_transitions[6*8+i];
        target.secondary_transitions[i*8+6]=donor.secondary_transitions[i*8+6];
    }
    target.charge_smoke=donor.charge_smoke;
    if(yotd) {retarget_supercharge(target);target.pc_motion_imported=true;}
}
SpyroRollBank decode_july_rolls(View global,View executable) {
    const ExeTables tables(executable);
    if(tables.profile.build!=GameBuild::July1999)throw FormatError("The cut rolls require the full July 13, 1999 build");
    const auto base=read_u32(global,4);extent(global,base,60+62*4);
    if(read_u32(global,base)!=62)throw FormatError("Unexpected July Spyro animation directory");
    const auto common_at=std::size_t(base)+read_u32(global,base+56);extent(global,common_at,0);
    const auto common=global.subspan(common_at);std::array<unsigned,125> lookup;
    for(unsigned i=0;i<lookup.size();++i)lookup[i]=read_u32(executable,tables(0x80062674)+i*4);
    const auto clip=[&](unsigned id){const auto p=read_u32(global,base+60+id*4);if(p==UINT32_MAX)throw FormatError("July roll clip is absent");return decode_animation(id,global,std::size_t(base)+p,common,executable,tables,false,lookup);};
    SpyroRollBank result;result.neutral=clip(0);result.rolls={clip(10),clip(11)};
    std::copy_n(executable.begin()+tables(0x8006042c),result.transitions.size(),result.transitions.begin());return result;
}
void import_july_rolls(SpyroModel& model,const SpyroRollBank& source) {
    const auto& target=model.animations.at(0);const auto& donor=source.neutral;
    const bool yotd=model.source_game==GameBuild::YearOfTheDragonUS;
    if((!yotd && target.faces.size()!=donor.faces.size()) || target.frames.empty() || donor.frames.empty() ||
       target.frames.front().vertices.size()!=donor.frames.front().vertices.size())throw FormatError("July roll donor mesh has incompatible topology");
    const auto count=target.frames.front().vertices.size();std::vector<unsigned> map(count,UINT32_MAX),reverse(count,UINT32_MAX);
    if(yotd) {
        // Derive current mounted-order -> July-order correspondence without
        // altering the mounted model. This also accepts already canonical PC
        // ordering and preserves YOTD's extra face and all native materials.
        SpyroModel correspondence,july;
        correspondence.animations.emplace(0,target);july.animations.emplace(0,donor);
        normalize_yotd_vertices(correspondence,july);
        for(unsigned original=0;original<correspondence.source_vertices.size();++original) {
            const auto mounted=correspondence.source_vertices[original];
            if(mounted>=count)throw FormatError("YOTD July roll correspondence is incomplete");
            map[mounted]=original;reverse[original]=mounted;
        }
    } else for(unsigned f=0;f<target.faces.size();++f) {
        const auto& a=target.faces[f];const auto& b=donor.faces[f];
        if(a.quad!=b.quad)throw FormatError("July roll face layout differs");
        for(unsigned c=0;c<4;++c) {
            const auto v=a.vertices[c],w=b.vertices[c];
            if(v>=count || w>=count || (map[v]!=UINT32_MAX && map[v]!=w) || (reverse[w]!=UINT32_MAX && reverse[w]!=v))throw FormatError("July roll vertex correspondence is not bijective");
            map[v]=w;reverse[w]=v;
        }
    }
    for(unsigned v=0;v<count;++v)if(map[v]==UINT32_MAX || ((v<target.split_vertex)!=(map[v]<donor.split_vertex)))throw FormatError("July roll partition correspondence is incomplete");
    for(const auto& original:source.rolls) {
        auto clip=original;clip.split_vertex=target.split_vertex;
        if(clip.faces.size()!=donor.faces.size())throw FormatError("Cut roll has a different face bank");
        for(unsigned f=0;f<clip.faces.size();++f)if(clip.faces[f].vertices!=donor.faces[f].vertices)throw FormatError("Cut roll vertex order differs from its neutral mesh");
        for(auto& frame:clip.frames) {
            if(frame.vertices.size()!=count)throw FormatError("Truncated cut roll vertex frame");
            const auto vertices=frame.vertices;
            for(unsigned v=0;v<count;++v)frame.vertices[v]=vertices[map[v]];
        }
        // Reuse the mounted model's faces, vertex colors, UVs and atlas tiles.
        // Only the recovered vertex motion and original VBlank timing transfer.
        clip.faces=target.faces;model.animations.insert_or_assign(clip.id,std::move(clip));
    }
    for(unsigned i=0;i<62;++i)for(unsigned roll:{10u,11u}) {
        model.transitions[i*62+roll]=source.transitions[i*62+roll];
        model.transitions[roll*62+i]=source.transitions[roll*62+i];
    }
    model.cut_rolls_imported=true;
}
const SpyroAnimation& SpyroModel::clip(unsigned id,SuperchargeAppearance appearance) const {
    if(id==61 && appearance==SuperchargeAppearance::MainModelChargeWings) {
        if(main_model_supercharge_charge_wings.frames.empty())throw FormatError("Charge-wing supercharge has not been retargeted");
        return main_model_supercharge_charge_wings;
    }
    if(id==61 && appearance==SuperchargeAppearance::MainModel) {
        if(main_model_supercharge.frames.empty())throw FormatError("Main-model supercharge has not been retargeted");
        return main_model_supercharge;
    }
    return animations.at(id);
}
SpyroPose SpyroModel::frame(unsigned animation,float frame_index,SuperchargeAppearance appearance) const {
    const auto& clip=this->clip(animation,appearance);
    if(!std::isfinite(frame_index))throw FormatError("Non-finite animation frame");
    if(animation==61 && appearance==SuperchargeAppearance::MainModelChargeWings && frame_index>=0 && frame_index<clip.loop_end) {
        const auto current=static_cast<unsigned>(frame_index);
        const auto next=current+1<clip.loop_end?current+1:clip.loop_start;
        auto pose=interpolate(clip,current,next,frame_index-current);
        charge_wings(pose,frame_tick(animation,current,frame_index-current));
        return pose;
    }
    frame_index=std::clamp(frame_index,0.0f,static_cast<float>(clip.frames.size()-1));
    const auto current=static_cast<unsigned>(frame_index),next=std::min(current+1,static_cast<unsigned>(clip.frames.size()-1));
    auto pose=interpolate(clip,current,next,frame_index-current);
    if(animation==61 && appearance==SuperchargeAppearance::MainModelChargeWings)
        charge_wings(pose,frame_tick(animation,current,frame_index-current));
    return pose;
}
SpyroPose SpyroModel::sample(unsigned animation,double seconds,bool retail_loop,SuperchargeAppearance appearance) const {
    if(!std::isfinite(seconds*60.0) || seconds<0)throw FormatError("Invalid animation time");
    const auto& clip=this->clip(animation,appearance);
    const auto start=retail_loop?clip.loop_start:0u;
    const auto end=retail_loop?clip.loop_end:static_cast<unsigned>(clip.frames.size());
    double ticks=seconds*60.0;
    double intro=0,cycle=0;
    for(unsigned i=0;i<start;++i)intro+=clip.frames[i].ticks;
    for(unsigned i=start;i<end;++i)cycle+=clip.frames[i].ticks;
    unsigned current=0;
    if(ticks>=intro) {
        if(start==end)return interpolate(clip,start,start,0);
        ticks=std::fmod(ticks-intro,cycle);current=start;
    }
    while(current+1<end && ticks>=clip.frames[current].ticks)ticks-=clip.frames[current++].ticks;
    const unsigned next=current+1<end?current+1:start;
    auto pose=interpolate(clip,current,next,static_cast<float>(ticks/clip.frames[current].ticks));
    if(animation==61 && appearance==SuperchargeAppearance::MainModelChargeWings)
        charge_wings(pose,frame_tick(animation,current,pose.blend));
    return pose;
}
SpyroPose SpyroModel::secondary_track(unsigned animation,SpyroPose primary,const SpyroPose& secondary,SuperchargeAppearance appearance) const {
    if(primary.vertices.size()!=secondary.vertices.size())throw FormatError("Incompatible Spyro animation tracks");
    const auto split=clip(animation,appearance).split_vertex;
    if(split>primary.vertices.size())throw FormatError("Invalid Spyro vertex partition");
    // 80041dbc uses the primary track's attachment translation for the
    // secondary track, allowing the head to flame while the body runs.
    for(std::size_t i=split;i<primary.vertices.size();++i)
        primary.vertices[i]=secondary.vertices[i]-secondary.attachment+primary.attachment;
    return primary;
}
void SpyroModel::turn_head(SpyroPose& pose,unsigned animation,float yaw,float pitch,SuperchargeAppearance appearance) const {
    const auto split=clip(animation,appearance).split_vertex;
    if(split>=pose.vertices.size() || (yaw==0 && pitch==0))return;
    // Model space: +y up, +z forward, +x Spyro's right; a left turn swings
    // forward toward -x.
    const float sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(-yaw),cy=std::cos(-yaw);
    for(std::size_t i=split;i<pose.vertices.size();++i) {
        const auto d=pose.vertices[i]-pose.attachment;
        const Vec3 raised{d.x,d.y*cp+d.z*sp,-d.y*sp+d.z*cp};
        pose.vertices[i]=pose.attachment+Vec3{raised.x*cy+raised.z*sy,raised.y,-raised.x*sy+raised.z*cy};
    }
}
const char* animation_name(unsigned id) {
    // Main state selection: 80035d38 / 800351d8. Flame track: 8002fc20,
    // 80066d54[1] == 7. Other IDs stay unnamed until verified.
    switch(id) {
        case 0:return "Idle";case 1:return "Walk";case 2:return "Run";
        case 5:return "Jump";case 6:return "Fall";case 7:return "Flame";
        case 10:return "July roll left";case 11:return "July roll right";
        case 12:return "Charge";case 13:return "Charge wall recoil";case 16:return "Glide";case 24:return "Glide brake";
        case 17:return "Hover";case 46:return "Headbash";case 49:return "Sand slide";
        case 25:return "Supercharge jump";
        case 33:return "Superfly";case 34:return "Superfly climb / flap";case 35:return "Superfly dive";
        case 40:return "Underwater swim";case 41:return "Underwater idle";case 42:return "Underwater charge";
        case 43:return "Surface swim";case 44:return "Surface idle";case 21:return "Charge jump";case 57:return "Underwater hurt";
        case 61:return "Supercharge";default:return "Animation";
    }
}
} // namespace spyro

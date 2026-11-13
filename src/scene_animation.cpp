#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void check(bool ok,const char* why){if(!ok)throw FormatError(why);}
void bounds(View b,std::size_t at,std::size_t size){check(at<=b.size() && size<=b.size()-at,"Terrain animation outside its section");}
int signed9(unsigned value){return static_cast<int>((value&511)^256)-256;}
std::array<Vec3,3> morph_triangle(View b,unsigned at) {
    const auto x=read_u32(b,at),y=read_u32(b,at+4),z=read_u32(b,at+8);
    const int bx=x&0x3fff,by=y&0x3fff,bz=z&0x1fff;
    const auto point=[](int a,int b,int c){return retail::world_position({a*16.0f,b*16.0f,c*16.0f});};
    // 800231f4/80022b7c: animated source triangles use signed 9-bit Z
    // deltas, unlike the already-repacked static collision triangle format.
    return {point(bx,by,bz),point(bx+signed9(x>>14),by+signed9(y>>14),bz+signed9(z>>14)),
            point(bx+signed9(x>>23),by+signed9(y>>23),bz+signed9(z>>23))};
}
Vec3 pose(const TerrainMorph& morph,const TerrainStep& step,unsigned vertex) {
    return lerp(morph.frames.at(step.first).at(vertex),morph.frames.at(step.second).at(vertex),step.weight/256.0f);
}
void apply(Level& level,const TerrainMorph& morph,bool smooth) {
    const auto& step=morph.steps.at(morph.node);
    // Retain the original keyframe sequence, with intermediate presentation
    // and collision at 60 Hz between its two-VBlank samples.
    const auto& next=morph.steps.at((morph.flags&1)?step.reverse:step.forward);
    const float phase=smooth && !(morph.flags&2) && step.duration?
        std::clamp(1.0f-float(morph.remaining)/step.duration,0.0f,1.0f):0;
    const auto sample=[&](unsigned vertex){return lerp(pose(morph,step,vertex),pose(morph,next,vertex),phase);};
    // A hidden step (byte 7) sets the region's 0x4000 flag: it is not drawn
    // (the morph spans the whole region) and its moving collision is gone.
    if(morph.collision) {
        for(unsigned i=0;i<morph.frames.front().size()/3;++i) {
            auto& t=level.triangles.at(morph.first_triangle+i);
            t.a=sample(i*3);t.b=sample(i*3+1);t.c=sample(i*3+2);t.disabled=step.hidden;
        }
        level.touch_triangles(morph.first_triangle,static_cast<std::uint32_t>(morph.frames.front().size()/3));
    }else for(const auto& [output,vertex]:morph.targets) {
        if(morph.color)level.visual_vertices.at(output).color=sample(vertex);
        else level.visual_vertices.at(output).position=step.hidden?sample(0):sample(vertex);
        level.touch_vertices(output,1);
    }
}
}
static void decode_native_morphs(View archive,Level& level);
void decode_scene_morphs(View archive,Level& level) {
    // Glimmer's bridge (Moneybags, overlay 16 at 8007ded0) and Breeze
    // Harbor's machinery are driven natively. Every other scene's groups
    // (doors, gates, bridges, lifts, lights) are started by its original
    // scripts (8002365c flags, 80023710 node) and follow the RAM timelines.
    if(level.universe)return;
    const bool native=level.wad_slot==17 || level.wad_slot==39;
    if(native)decode_native_morphs(archive,level);
    else try {decode_native_morphs(archive,level);} catch(const FormatError&){level.terrain_morphs.clear();++level.skipped_morphs;}
}
static void decode_native_morphs(View archive,Level& level) {
    const bool native=level.wad_slot==17 || level.wad_slot==39;
    const auto children=parse_level_archive(archive);
    const auto data=archive.subspan(children.at(3).offset,children.at(3).size);
    const auto scene=archive.subspan(children.at(1).offset,children.at(1).size);
    const auto region_at=read_u32(scene,0);bounds(scene,region_at,4);
    const auto regions=scene.subspan(region_at,read_u32(scene,region_at));
    const auto directory=4+read_u32(regions,4);
    // Loader 8006fa98 and translator 8003c310: 28-byte records precede
    // the visible-region directory. Their second short is the moving group.
    if(level.wad_slot==39) {
        for(unsigned p=8;p<directory;p+=28) {
            bounds(regions,p,28);const unsigned region=read_u16(regions,p),group=read_u16(regions,p+2);
            auto t=std::find_if(level.terrain_translations.begin(),level.terrain_translations.end(),[&](const auto& x){return x.group==group;});
            if(t==level.terrain_translations.end()){level.terrain_translations.push_back({group});t=std::prev(level.terrain_translations.end());}
            t->regions.push_back(region);
        }
        for(auto& t:level.terrain_translations)if(t.group==1) {
            // The ship uses a rigid region translation; its walkable hull
            // collision is selected once in the original region bounds.
            std::vector<Bounds> boxes;
            for(auto r:t.regions){Bounds box;for(const auto& v:level.visual_vertices)if(v.source_region==r)box.add(v.position);boxes.push_back(box);}
            for(unsigned i=0;i<level.triangles.size();++i) {
                const auto& tri=level.triangles[i];if(tri.water)continue;
                const auto within=[](Vec3 p,const Bounds& b){return p.x>=b.min.x-0.04f && p.x<=b.max.x+0.04f && p.y>=b.min.y-0.04f && p.y<=b.max.y+0.04f && p.z>=b.min.z-0.04f && p.z<=b.max.z+0.04f;};
                if(std::any_of(boxes.begin(),boxes.end(),[&](const auto& b){return within(tri.a,b)&&within(tri.b,b)&&within(tri.c,b);}))t.triangles.push_back(i);
            }
        }
    }
    std::size_t section=0x2c;
    for(unsigned kind=0;kind<7;++kind) {
        const auto size=read_u32(data,section),count=read_u32(data,section+4);bounds(data,section,size);
        check(size>=8 && count<=256 && 8+count*4<=size,"Invalid terrain animation directory");
        if(kind==2 || kind==4 || (kind==5 && level.wad_slot!=17))for(unsigned i=0;i<count;++i) try {
            const auto at=4+read_u32(data,section+8+i*4);
            const auto end=i+1<count?4+read_u32(data,section+12+i*4):size;
            check(at>=8+count*4 && at<end && end<=size,"Invalid terrain animation record");
            const auto record=data.subspan(section+at,end-at);bounds(record,0,12);
            if(level.wad_slot==17 && record[0]!=1)continue;
            TerrainMorph morph;morph.collision=kind==4;morph.color=kind==5;morph.group=record[0];
            morph.kind=kind;morph.record=i;morph.scripted=!native;
            morph.flags=record[1];morph.node=record[2];morph.remaining=record[3];
            const auto frame_at=read_u32(record,8);
            check(frame_at>=20 && frame_at<=record.size() && (frame_at-12)%8==0,"Invalid terrain animation steps");
            for(unsigned p=12;p<frame_at;p+=8) {
                morph.steps.push_back({record[p],record[p+1],record[p+2],record[p+3],record[p+4],record[p+5],record[p+6],record[p+7]!=0});
            }
            const unsigned frame_bytes=morph.collision?read_u16(record,4)*12:read_u16(record,6);
            check(frame_bytes>0 && frame_bytes%4==0 && (record.size()-frame_at)%frame_bytes==0,"Invalid terrain keyframe length");
            const unsigned frames=static_cast<unsigned>((record.size()-frame_at)/frame_bytes);
            check(frames>0 && frames<=256 && morph.node<morph.steps.size(),"Invalid terrain keyframe count");
            for(const auto& step:morph.steps) {
                check(step.forward<morph.steps.size() && step.reverse<morph.steps.size(),"Terrain animation step link is invalid");
                check(step.first<frames && step.second<frames,"Terrain animation step keyframe is invalid");
                // Scripted timelines run in 800225f8, which passes through
                // zero-length steps; byte 7 sets the region's 0x4000 (not
                // drawn) flag at that node (80024168), e.g. an opened door.
                check(morph.scripted || step.duration>0,"Terrain animation step has no duration");
                check(morph.scripted || !step.hidden,"Unmapped terrain animation step");
            }
            Vec3 origin;float z_scale=0;
            if(morph.collision) {
                morph.first_triangle=read_u16(record,6);
                check(morph.first_triangle+frame_bytes/12<=level.triangles.size(),"Terrain animation collision range is invalid");
            }else {
                morph.region=record[4];morph.first_vertex=record[5];
                check(morph.region<read_u32(regions,directory),"Terrain animation region is invalid");
                const auto region=4+read_u32(regions,directory+4+morph.region*4);bounds(regions,region,28);
                check(morph.first_vertex+frame_bytes/4<=unsigned(regions[region+(morph.color?21:20)])*(morph.color?2:1),"Terrain animation vertex range is invalid");
                origin={read_u16(regions,region+10)*16.0f,read_u16(regions,region+8)*16.0f,read_u16(regions,region+14)*16.0f};
                z_scale=(read_u16(regions,region+4)&0x1000)?4.0f:32.0f;
                for(unsigned v=0;v<level.visual_vertices.size();++v) {
                    const auto& vertex=level.visual_vertices[v];
                    const auto index=morph.color?vertex.source_color:vertex.source_vertex;
                    if(vertex.source_region==morph.region && index>=morph.first_vertex && index<morph.first_vertex+frame_bytes/4)
                        morph.targets.emplace_back(v,index-morph.first_vertex);
                }
            }
            for(unsigned f=0;f<frames;++f) {
                std::vector<Vec3> frame;
                for(unsigned p=0;p<frame_bytes;p+=morph.collision?12:4) {
                    if(morph.collision){const auto t=morph_triangle(record,frame_at+f*frame_bytes+p);frame.insert(frame.end(),t.begin(),t.end());}
                    else {const auto packed=read_u32(record,frame_at+f*frame_bytes+p);
                        if(morph.color)frame.push_back({float(packed&255)/128,float(packed>>8&255)/128,float(packed>>16&255)/128});
                        else frame.push_back(retail::world_position(origin+Vec3{(packed>>21)*16.0f,((packed>>10)&2047)*16.0f,(packed&1023)*z_scale}));}
                }
                morph.frames.push_back(std::move(frame));
            }
            level.terrain_morphs.push_back(std::move(morph));
        } catch(const FormatError& e) {if(native)throw;++level.skipped_morphs;if(std::getenv("SPYRO_MORPH_DEBUG"))std::fprintf(stderr,"slot %u kind %u record %u: %s\n",level.wad_slot,kind,i,e.what());}
        section+=size;
    }
    // Speech section, then Moby records. Class 220's +2C pointer supplies
    // the camera eye and look-at consumed by resident 8001f24c.
    if(!compact_demo_layout(archive))section+=read_u32(data,section);
    const auto count=read_u32(data,section+4);bounds(data,section+8,count*88);
    for(unsigned i=0;i<count;++i)if(read_u16(data,section+8+i*88+54)==0x220) {
        const auto parameter=read_u32(data,section+8+i*88);bounds(data,parameter,48);
        const auto camera=read_u32(data,parameter+44);bounds(data,camera,40);
        const auto position=[&](unsigned at){return retail::world_position({float(std::bit_cast<int>(read_u32(data,at))),
            float(std::bit_cast<int>(read_u32(data,at+4))),float(std::bit_cast<int>(read_u32(data,at+8)))});};
        level.bridge_camera=std::array{position(camera+12),position(camera+28)};break;
    }
    if(!std::getenv("SPYRO_STATIC_MORPHS"))for(const auto& morph:level.terrain_morphs)apply(level,morph,false);   // debug: keep the static table
    // Morphing and translated collision moves every tick: never gridded.
    for(const auto& morph:level.terrain_morphs)if(morph.collision)
        for(unsigned i=0;i<morph.frames.front().size()/3;++i)level.dynamic_triangles.push_back(morph.first_triangle+i);
    for(const auto& t:level.terrain_translations)level.dynamic_triangles.insert(level.dynamic_triangles.end(),t.triangles.begin(),t.triangles.end());
}
bool pose_scene_morph(Level& level,TerrainMorph& morph,unsigned flags,unsigned node,unsigned remaining) {
    if(node>=morph.steps.size())return false;
    const bool playing=!(flags&2);
    if(flags==morph.flags && node==morph.node && remaining==morph.remaining && !playing)return false;
    morph.flags=flags;morph.node=node;morph.remaining=remaining;
    apply(level,morph,true);
    return true;
}
void advance_scene_morphs(Level& level) {
    bool changed=false;
    for(auto& morph:level.terrain_morphs)if(!(morph.flags&2) && !morph.scripted) {
        if(morph.remaining)--morph.remaining;
        if(!morph.remaining) {
            const auto& step=morph.steps.at(morph.node);
            morph.node=(morph.flags&1)?step.reverse:step.forward;
            morph.flags=morph.steps.at(morph.node).flags;morph.remaining=morph.steps.at(morph.node).duration;
        }
        apply(level,morph,true);changed=true;
    }
    if(changed)++level.terrain_revision;
}
void translate_scene_group(Level& level,unsigned group,Vec3 delta) {
    for(auto& t:level.terrain_translations)if(t.group==group) {
        // The group's vertices are cached, not scanned per tick.
        for(const auto i:level.translation_vertices(t)){level.visual_vertices[i].position+=delta;level.touch_vertices(i,1);}
        for(auto i:t.triangles){auto& tri=level.triangles.at(i);tri.a+=delta;tri.b+=delta;tri.c+=delta;level.touch_triangles(i,1);}
        t.offset+=delta;++level.terrain_revision;
    }
}
void set_scene_morphs(Level& level,unsigned group,bool finished) {
    for(auto& morph:level.terrain_morphs)if(morph.group==group) {
        morph.node=0;morph.flags=2;morph.remaining=1;
        if(finished)for(unsigned guard=0;guard<morph.steps.size();++guard) {
            morph.node=morph.steps.at(morph.node).forward;
            const auto& step=morph.steps.at(morph.node);
            if(step.flags&2){morph.flags=step.flags;morph.remaining=step.duration;break;}
        }
        apply(level,morph,false);
    }
    ++level.terrain_revision;
}
void Game::restore_scene_progress() {
    scene_locked=false;camera.scripted_view.reset();bridge_camera_ticks_=bridge_end_ticks_=0;
    bridge_started_=bridge_complete_=(purchases&(1u<<3))!=0;
    if(level.wad_slot==17 && !level.universe)set_scene_morphs(level,1,bridge_complete_);
}
void Game::update_scene_progress() {
    if(level.wad_slot!=17 || level.universe || level.terrain_morphs.empty() || !(purchases&(1u<<3)) || bridge_complete_)return;
    if(!bridge_started_) {
        if(dialogue.active())return;
        bridge_started_=scene_locked=true;bridge_camera_ticks_=bridge_end_ticks_=0;
        if(level.bridge_camera)camera.scripted_view=CameraView{(*level.bridge_camera)[0],normalized((*level.bridge_camera)[1]-(*level.bridge_camera)[0])};
    }
    // Wait for the authored camera view before starting group 1, matching the
    // overlay's camera-ready gate. The native camera easing is continuous.
    if(++bridge_camera_ticks_<=40)return;
    if(bridge_camera_ticks_==41) {
        for(auto& morph:level.terrain_morphs)if(morph.group==1)morph.flags&=~3u;
        const auto npc=std::find_if(retail_objects.begin(),retail_objects.end(),[](const auto& o){return o.type==0x220;});
        if(npc!=retail_objects.end())sound_events.push_back({SoundScope::Object,2,0x220,npc->position});
    }
    advance_scene_morphs(level);
    const bool stopped=std::all_of(level.terrain_morphs.begin(),level.terrain_morphs.end(),[](const auto& morph){return morph.group!=1 || (morph.flags&2);});
    if(stopped && ++bridge_end_ticks_>=30){bridge_complete_=true;scene_locked=false;camera.scripted_view.reset();}
}
} // namespace spyro

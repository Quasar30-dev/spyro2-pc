#include "spyro/game_build.hpp"
#include "spyro/objects.hpp"
#include "spyro/level.hpp"
#include "spyro/enemy.hpp"
#include "spyro/yotd_assets.hpp"
#include <bit>
#include <set>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View b,std::size_t at,std::size_t size) {
    if(at>b.size() || size>b.size()-at)throw FormatError("Object data extends outside its archive section");
}
int sx(unsigned v,unsigned bits) { const auto sign=1u<<(bits-1);return static_cast<int>((v&((1u<<bits)-1))^sign)-static_cast<int>(sign); }
Vec3 rgb(unsigned word,float divisor=255) {return {static_cast<float>(word&255)/divisor,static_cast<float>((word>>8)&255)/divisor,static_cast<float>((word>>16)&255)/divisor};}
void decode_damage_banks(ObjectModel& model,View bank) {
    for(unsigned index=0;index<8;++index) {
        const auto offset=read_u32(bank,20+index*4);if(!offset)continue;
        require(bank,offset,4);const auto header=read_u32(bank,offset);
        if(header>>16)throw FormatError("Unmapped enemy collision mesh header");
        ObjectDamageBank damage;damage.broad_radius=(header&255)*32;damage.vertex_count=(header>>8)&127;
        std::size_t at=offset+4;
        for(unsigned count=0;;++count) {
            if(count>=128)throw FormatError("Unterminated enemy collision primitives");
            const auto shape=read_u32(bank,at);const unsigned kind=(shape>>8)&255;
            if(kind==0 || kind==1) {
                // 800409c8: fixed/rotated offset sphere, four words. The
                // Marsh banks use these as passive body colliders; native
                // player contact currently resolves their visible hull.
                require(bank,at,16);
                if((shape&4) && read_u32(bank,at+12))throw FormatError("Unmapped damaging offset sphere");
                at+=16;
            } else if(kind==2) {
                require(bank,at,12);const int a=read_u16(bank,at+4),b=std::bit_cast<std::int16_t>(read_u16(bank,at+6));
                if(a>=static_cast<int>(damage.vertex_count) || b>=static_cast<int>(damage.vertex_count))throw FormatError("Enemy collision vertex outside prefix");
                if((shape&4) && read_u32(bank,at+8))damage.spheres.push_back({shape>>16,a,b});
                at+=12;
            } else if(kind==3) {
                require(bank,at,20);
                if((shape&4) && read_u32(bank,at+16))damage.cylinder=ObjectCylinder{damage.broad_radius,shape>>16,
                    read_u32(bank,at+12),shape&255,std::bit_cast<std::int16_t>(read_u16(bank,at+4)),std::bit_cast<std::int16_t>(read_u16(bank,at+6))};
                at+=20;
            } else throw FormatError("Unmapped enemy damage primitive");
            if(shape&1)break;
        }
        model.damage_banks[index]=std::move(damage);
    }
}
ObjectMesh static_mesh(View bank,std::size_t at,std::size_t common) {
    require(bank,at,16);const unsigned colors=bank[at],vertices=bank[at+1];
    ObjectMesh mesh;
    if(!vertices)return mesh;
    require(bank,at+16,vertices*4);
    for(unsigned i=0;i<vertices;++i) {
        mesh.vertices.push_back(decode_static_vertex(read_u32(bank,at+16+i*4)));
        mesh.radius=std::max(mesh.radius,length(mesh.vertices.back()));
    }
    const auto faces_at=common+read_u16(bank,at+8),colors_at=common+read_u16(bank,at+10);
    require(bank,colors_at,colors*4);
    const auto bytes=read_u32(bank,faces_at);require(bank,faces_at+4,bytes);
    for(std::size_t p=faces_at+4,end=p+bytes;p<end;) {
        require(bank,p,8);ObjectFace face;
        const auto indices=read_u32(bank,p);face.flags=read_u32(bank,p+4);
        face.textured=(face.flags&0x80000000)!=0;face.shiny=(face.flags&2)!=0;
        const std::size_t packet=face.textured?20:8;if(packet>end-p)throw FormatError("Truncated object face packet");
        const unsigned shifts[]={21,14,7,0};
        for(unsigned k=0;k<4;++k) {
            face.vertices[k]=(indices>>shifts[k])&127;
            if(face.vertices[k]>=vertices)throw FormatError("Object polygon references an invalid vertex");
        }
        face.quad=face.vertices[2]!=face.vertices[3];
        if(face.shiny) {
            // 80047cac: the color word instead contains an 8-bit face normal.
            face.shiny_normal={static_cast<float>(sx(face.flags>>15,8)),
                static_cast<float>(-sx(face.flags>>7,8)),static_cast<float>(sx(face.flags>>23,8))};
        } else {
            const unsigned shifts_c[]={24,17,10,3};
            for(unsigned k=0;k<(face.quad?4u:3u);++k) {
                const auto color=(face.flags>>shifts_c[k])&127;
                if(color>=colors)throw FormatError("Object polygon references an invalid palette color");
                face.colors[k]=rgb(read_u32(bank,colors_at+color*4),face.textured?128.0f:255.0f);
            }
        }
        if(face.textured) {
            face.clut=read_u16(bank,p+10);face.page=read_u16(bank,p+14);
            face.translucent=(face.page&0x8000)!=0;face.blend_mode=(face.page>>5)&3;
            face.uv={{{static_cast<float>(bank[p+8]),static_cast<float>(bank[p+9])},
                {static_cast<float>(bank[p+12]),static_cast<float>(bank[p+13])},
                {static_cast<float>(bank[p+16]),static_cast<float>(bank[p+17])},
                {static_cast<float>(bank[p+18]),static_cast<float>(bank[p+19])}}};
        }
        mesh.faces.push_back(face);p+=packet;
    }
    return mesh;
}
void textures(ObjectModels& models,View archive,unsigned area=0) {
    using Key=std::pair<unsigned,unsigned>;std::map<Key,unsigned> tiles;
    for(const auto& [id,model]:models.models)for(const auto& mesh:model.meshes)for(const auto& f:mesh.faces)
        if(f.textured)tiles.try_emplace({f.page&0x1ff,f.clut},static_cast<unsigned>(tiles.size()));
    const GameProfile profile{models.source_game,0,"object model archive"};
    const auto entries=parse_level_archive(archive,profile);
    if(entries.empty() || entries[0].size<0x80000)throw FormatError("Missing level VRAM for object materials");
    // 8001c734 copies gameplay header +0x10 to 80066ed8. Mode 1
    // supplies this TPAGE/CLUT itself; its face packets can be untextured.
    const unsigned environment_offset=models.source_game==GameBuild::YearOfTheDragonUS?20:16;
    if(entries.size()>3 && entries[3].size>=environment_offset+4) {
        const auto environment=read_u32(archive,entries[3].offset+environment_offset);
        if(environment) {
            const auto entry=tiles.try_emplace(Key{environment&0x1ff,environment>>16},static_cast<unsigned>(tiles.size()));
            models.environment_tile=entry.first->second;
        }
    }
    YotdVramImage mounted;
    if(profile.yotd())mounted=decode_yotd_vram(archive,profile,area);
    const View vram=profile.yotd()?View{mounted.pixels}:archive.subspan(entries[0].offset,0x80000);
    const auto word=[&](unsigned x,unsigned y) {
        if(x<512 || x>=1024 || y>=512)throw FormatError("Object material references an unmapped VRAM region");
        return read_u16(vram,(static_cast<std::size_t>(y)*512+x-512)*2);
    };
    models.texture_tiles=static_cast<unsigned>(tiles.size());
    if(tiles.empty())return;
    if(tiles.size()>512)throw FormatError("Too many object texture palettes");
    constexpr unsigned columns=16;models.texture_width=columns*256;
    models.texture_height=static_cast<unsigned>((tiles.size()+columns-1)/columns)*256;
    models.texture_rgba.resize(static_cast<std::size_t>(models.texture_width)*models.texture_height*4);
    for(const auto& [key,tile]:tiles) {
        const auto [page,clut]=key;const auto depth=(page>>7)&3;
        if(depth>1)throw FormatError("Unsupported object texture pixel depth");
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
            const unsigned packed=word((page&15)*64+x/(depth?2:4),((page>>4)&1)*256+y);
            const unsigned index=depth?(packed>>((x&1)*8))&255:(packed>>((x&3)*4))&15;
            const auto color=word((clut&63)*16+index,(clut>>6)&511);
            const auto p=(static_cast<std::size_t>(tile/columns*256+y)*models.texture_width+tile%columns*256+x)*4;
            for(unsigned c=0;c<3;++c) {const auto v=(color>>(c*5))&31;models.texture_rgba[p+c]=static_cast<std::uint8_t>((v<<3)|(v>>2));}
            models.texture_rgba[p+3]=color?((color&0x8000)?128:255):0;
        }
    }
    for(auto& [id,model]:models.models)for(auto& mesh:model.meshes)for(auto& f:mesh.faces)if(f.textured) {
        const auto tile=tiles.at({f.page&0x1ff,f.clut});
        for(auto& uv:f.uv) {uv[0]=(uv[0]+0.5f+(tile%columns)*256)/models.texture_width;uv[1]=(uv[1]+0.5f+(tile/columns)*256)/models.texture_height;}
    }
}
}
std::vector<RetailObject> decode_object_instances(View archive,unsigned slot) {
    return decode_object_instances(archive,slot,game_profile(0xcdc0917c));
}
std::vector<RetailObject> decode_object_instances(View archive,unsigned slot,const GameProfile& profile,unsigned area) {
    // Slot 95 is a special scene with a shorter gameplay header, not the
    // standard 8001c734 object layout. Preserve it for separate reconstruction.
    if(!profile.yotd() && slot==95)return {};
    const auto entries=parse_level_archive(archive,profile);
    if(entries.size()!=4)throw FormatError("Missing object gameplay child");
    auto section=entries[3];
    if(area) {
        if(!profile.yotd() || area>4)throw FormatError("Invalid object gameplay area");
        const auto header=0x20+(area-1)*16;require(archive,header,16);
        section.offset=read_u32(archive,header+8);section.size=read_u32(archive,header+12);
        if(!section.offset || !section.size)throw FormatError("Missing object gameplay area");
        require(archive,section.offset,section.size);
    }
    const auto data=archive.subspan(section.offset,section.size);
    // YOTD extends the fixed gameplay header by one word and inserts four
    // additional sections before the unchanged 88-byte Moby records.
    std::size_t at=profile.yotd()?0x30:0x2c;
    const auto sections=profile.yotd()?12u:(compact_demo_layout(archive)?7u:8u);
    for(unsigned i=0;i<sections;++i) {const auto n=read_u32(data,at);if(n<4 || n%4)throw FormatError("Invalid gameplay section chain");require(data,at,n);at+=n;}
    const auto size=read_u32(data,at),count=read_u32(data,at+4);require(data,at,size);
    if(size<8 || count>10000 || static_cast<std::uint64_t>(count)*88>size-8)throw FormatError("Invalid retail object table");
    std::vector<RetailObject> objects;objects.reserve(count);
    for(unsigned i=0;i<count;++i) {
        const auto p=at+8+i*88;RetailObject object;object.source_game=profile.build;object.source_area=area;
        object.id=i;object.record_offset=static_cast<unsigned>(section.offset+p);object.data_offset=read_u32(data,p);
        if(object.data_offset>=data.size())throw FormatError("Object parameter pointer outside gameplay data");
        object.type=read_u16(data,p+54);object.source_type=object.type;
        object.position=retail::world_position({static_cast<float>(std::bit_cast<std::int32_t>(read_u32(data,p+12))),
            static_cast<float>(std::bit_cast<std::int32_t>(read_u32(data,p+16))),static_cast<float>(std::bit_cast<std::int32_t>(read_u32(data,p+20)))});
        object.previous_position=object.position;
        object.model=data[p+60];object.variant=data[p+73];object.held_gem=data[p+80];object.size=data[p+79];
        object.rotation={static_cast<float>(data[p+68]),static_cast<float>(data[p+69]),static_cast<float>(data[p+70])};
        object.previous_rotation=object.rotation;object.draw_flags=data[p+76];object.persistence=data[p+59];
        object.state=std::bit_cast<std::int8_t>(data[p+72]);object.material_color=read_u32(data,p+84);
        if(object.type==1){require(data,object.data_offset,24);std::copy_n(data.begin()+object.data_offset,24,object.gem_parameters.begin());}
        if(profile.yotd()) {
            // Preserve original YOTD classes and parameters. Class IDs can
            // overlap unrelated Spyro 2 scripts, so those mappings stay below.
            // C8/C9 retain the same bounded parameter blocks, wobble table,
            // held-gem field and type-3 collision as the earlier common props.
            if(object.type==0xc8 || object.type==0xc9) {
                auto& container=object.container.emplace();const unsigned bytes=object.type==0xc9?28:4;
                require(data,object.data_offset,bytes);
                std::copy_n(data.begin()+object.data_offset,bytes,container.parameters.begin());
                if(object.type==0xc9) {
                    const auto wave=read_u32(container.parameters,24);require(data,wave,64);
                    for(unsigned j=0;j<64;++j)container.wobble[j]=std::bit_cast<std::int8_t>(data[wave+j]);
                }
                container.rest_position=object.position;container.rest_rotation=object.rotation;
            }
            std::copy_n(data.begin()+p,88,object.record.begin());objects.push_back(object);continue;
        }
        if(object.type>=0x1cc && object.type<=0x1d3) {
            const auto q=object.data_offset;require(data,q,0x68);
            auto& gate=object.powerup_gate.emplace();gate.kind=read_u32(data,q+0x60);
            gate.duration=read_u32(data,q+0x58);gate.requirement=read_u32(data,q+0x64);
            gate.origin_offset=std::bit_cast<std::int16_t>(read_u16(data,p+56))/retail::units_per_world;
            if(gate.kind>10 || gate.duration>36000 || gate.requirement>1000)throw FormatError("Unmapped power-up gate parameters");
            // Route header: count (s16), current, ...; 12 bytes, then 16-byte
            // waypoints {x,y,z,link}. Only the last waypoint's link is read.
            const auto routes=std::bit_cast<std::int32_t>(read_u32(data,q));
            if(routes<0 || routes>20)throw FormatError("Unmapped power-up gate spirit routes");
            require(data,q,4+routes*4);
            for(int k=0;k<routes;++k) {
                const auto r=read_u32(data,q+4+k*4);require(data,r,12);
                const auto waypoints=std::bit_cast<std::int16_t>(read_u16(data,r));
                if(waypoints<1 || waypoints>256)throw FormatError("Invalid power-up gate spirit route");
                require(data,r,12+waypoints*16);auto& route=gate.spirit_routes.emplace_back();
                for(int n=0;n<waypoints;++n) {
                    const auto w=r+12+n*16;const auto s=[&](unsigned at){return static_cast<float>(std::bit_cast<std::int32_t>(read_u32(data,w+at)));};
                    route.points.push_back(retail::world_position({s(0),s(4),s(8)}));
                }
                route.link=std::bit_cast<std::int32_t>(read_u32(data,r+12+(waypoints-1)*16+12));
                if(route.link==-1)route.points.back()=object.position+Vec3{0,0x520/retail::units_per_world,0};
            }
            for(const auto& route:gate.spirit_routes)if(route.link!=-1 &&
                (static_cast<unsigned>(route.link&255)>=gate.spirit_routes.size() || static_cast<std::size_t>(route.link>>8)>=gate.spirit_routes[route.link&255].points.size()))
                throw FormatError("Power-up gate spirit route link outside its routes");
        }
        if(data[p+83]>0 && data[p+83]<128)object.power_target.emplace();
        // Cloud Temples' troll: overlay 60 at 800751d0 consumes ice mask
        // 01000000, enters pose 4 and creates the 271 ice platform.
        if(slot==61 && object.type==0x1ee)object.scripted_contact=true;
        // Overlay 64, 8007d814 / 8007d92c: pumpkins use bit 0x40000;
        // heavy doors require 0x400000, absent from retail superflame.
        if(slot==65 && (object.type==0x281 || object.type==0x282))
            object.power_target=PowerTarget{object.type==0x282?3u:2u,true};
        if(is_breakable(object.type) || is_heavy_container(object.type)) {
            auto& container=object.container.emplace();
            const bool metal=object.type==0xc9 || is_heavy_container(object.type);
            const unsigned bytes=metal?28:4;require(data,object.data_offset,bytes);
            std::copy_n(data.begin()+object.data_offset,bytes,container.parameters.begin());
            if(metal) {
                const auto wave=read_u32(container.parameters,24);require(data,wave,64);
                for(unsigned j=0;j<64;++j)container.wobble[j]=std::bit_cast<std::int8_t>(data[wave+j]);
            }
            container.rest_position=object.position;container.rest_rotation=object.rotation;
        }
        // Glimmer overlay 16: 8007c300 (small lizard), 8007d008 (club lizard).
        // Keep this mapping scene-specific: a class number alone is not a script.
        if(slot==17 && (object.type==0x1f0 || object.type==0x22b)) {
            auto& enemy=object.enemy.emplace();const auto q=object.data_offset;
            require(data,q,object.type==0x1f0?28:12);
            enemy.kind=object.type==0x1f0?EnemyKind::SmallLizard:EnemyKind::ClubLizard;
            enemy.notice=read_u16(data,q+(object.type==0x1f0?4:0))/retail::units_per_world;
            enemy.reach=read_u16(data,q+(object.type==0x1f0?6:2))/retail::units_per_world;
            if(object.type==0x1f0)if(const auto route=read_u32(data,q)) {
                require(data,route,12);const auto points=read_u16(data,route);
                if(!points || points>512)throw FormatError("Invalid enemy route length");
                require(data,route+12,points*16);
                enemy.target=read_u16(data,route+2);enemy.direction=std::bit_cast<std::int16_t>(read_u16(data,route+10));
                if(enemy.target>=points || std::abs(enemy.direction)!=1)throw FormatError("Invalid enemy route cursor");
                for(unsigned j=0;j<points;++j) {
                    const auto point=route+12+j*16;
                    enemy.route.push_back({retail::to_world({std::bit_cast<std::int32_t>(read_u32(data,point)),
                        std::bit_cast<std::int32_t>(read_u32(data,point+4)),std::bit_cast<std::int32_t>(read_u32(data,point+8))}),
                        std::bit_cast<std::int32_t>(read_u32(data,point+12))});
                }
                enemy.loop=enemy.route.front().action==99;
            }
        }
        // Hurricos 800765fc / 80078ef8 and Idol Springs 80079bcc.
        if((slot==23 && (object.type==0x2c || object.type==0x30)) || (slot==19 && object.type==0x116)) {
            auto& e=object.enemy.emplace();const auto q=object.data_offset;
            e.kind=object.type==0x2c?EnemyKind::GearGrinder:object.type==0x30?EnemyKind::Robot:EnemyKind::MetalIdol;
            e.home=object.position;e.home_heading=object.rotation[2];
            e.notice=(object.type==0x2c?9500:object.type==0x30?3500:4608)/retail::units_per_world;
            e.reach=(object.type==0x2c?3200:object.type==0x30?1100:1024)/retail::units_per_world;
            require(data,q,object.type==0x2c?0x30:object.type==0x30?0x58:0x14);
            if(object.type!=0x116) {
                e.linked=read_u32(data,q+(object.type==0x2c?0xc:0x2c));
                if(e.linked!=UINT32_MAX && e.linked>=count)throw FormatError("Enemy link outside object table");
                if(object.type==0x2c) {
                    e.activation=read_u32(data,q+0x14)/retail::units_per_world;
                    e.command_delay=read_u32(data,q+0x18);e.patrol_initial=data[q+0x27]!=0;
                    e.command_available=e.linked!=UINT32_MAX;
                } else {
                    e.assigned_waypoint=std::bit_cast<std::int32_t>(read_u32(data,q+0x14));
                    e.patrol_duration=read_u32(data,q+0x38);e.maximum_speed=static_cast<float>(read_u32(data,q+0x3c));
                    if(e.maximum_speed>1024 || e.patrol_duration>36000)throw FormatError("Invalid robot movement parameters");
                }
                if(const auto route=read_u32(data,q+(object.type==0x2c?0x2c:0x10))) {
                    require(data,route,12);const auto points=read_u16(data,route);
                    if(!points || points>512)throw FormatError("Invalid Hurricos route length");
                    require(data,route+12,points*16);e.target=read_u16(data,route+2);
                    if(e.target>=points || e.assigned_waypoint>=static_cast<int>(points))throw FormatError("Invalid Hurricos route cursor");
                    for(unsigned j=0;j<points;++j) {
                        const auto a=route+12+j*16;
                        e.route.push_back({retail::to_world({std::bit_cast<std::int32_t>(read_u32(data,a)),
                            std::bit_cast<std::int32_t>(read_u32(data,a+4)),std::bit_cast<std::int32_t>(read_u32(data,a+8))}),
                            std::bit_cast<std::int32_t>(read_u32(data,a+12))});
                    }
                }
            }
        }
        if((slot==39 && (object.type==0x169 || object.type==0x16a)) ||
           (slot==59 && (object.type==0x1f2 || object.type==0x1f3 || object.type==0x1fc || object.type==0x1fd))) {
            auto& e=object.enemy.emplace();const auto q=object.data_offset;e.home=object.position;e.home_heading=object.rotation[2];
            e.kind=object.type==0x169?EnemyKind::BucketBlubber:object.type==0x16a?EnemyKind::FireBlubber:
                object.type==0x1f2?EnemyKind::ElephantSnail:object.type==0x1f3?EnemyKind::RhinoSnail:
                object.type==0x1fc?EnemyKind::SpittingMonkey:EnemyKind::Platypus;
            e.notice=(slot==39?5120:object.type==0x1f3?4096:12245)/retail::units_per_world;
            e.reach=(slot==39?3072:object.type==0x1f3?2048:1024)/retail::units_per_world;
            if(object.type==0x1f2){require(data,q,0x30);e.notice=(read_u32(data,q+0x1c)+0xc00)/retail::units_per_world;e.reach=read_u32(data,q+0x1c)/retail::units_per_world;e.linked=read_u32(data,q+0x2c);}
            // Verified route pointers: elephant snail +24 (8007d71c),
            // platypus +10 (8007f748); the same 12+16*n shared path format.
            const auto route_at=object.type==0x1f2?0x24:object.type==0x1fd?0x10:UINT32_MAX;
            if(route_at!=UINT32_MAX)if(const auto route=read_u32(data,q+route_at)) {
                require(data,route,12);const auto points=read_u16(data,route);require(data,route+12,std::size_t(points)*16);
                if(!points || points>512)throw FormatError("Invalid Marsh enemy route");
                e.target=read_u16(data,route+2);e.direction=std::bit_cast<std::int16_t>(read_u16(data,route+10));
                if(e.target>=points || std::abs(e.direction)!=1)throw FormatError("Invalid Marsh path cursor");
                for(unsigned j=0;j<points;++j) {const auto a=route+12+j*16;
                    e.route.push_back({retail::to_world({std::bit_cast<std::int32_t>(read_u32(data,a)),std::bit_cast<std::int32_t>(read_u32(data,a+4)),std::bit_cast<std::int32_t>(read_u32(data,a+8))}),std::bit_cast<std::int32_t>(read_u32(data,a+12))});}
            }
        }
        std::copy_n(data.begin()+p,88,object.record.begin());objects.push_back(object);
    }
    if(profile.yotd())for(auto& object:objects)if(object.type==0x3f3) {
        // Deferred gem records identify their original container by stable
        // table index. They have no display bank and stay hidden until its
        // break handler releases them; the raw class and record are retained.
        object.hidden=true;require(data,object.data_offset,4);
        const auto parent=read_u32(data,object.data_offset);
        if(parent>=objects.size() || parent==object.id)continue;
        require(data,object.data_offset,24);object.source_id=parent;
        std::copy_n(data.begin()+object.data_offset,24,object.gem_parameters.begin());
    }
    return objects;
}
ObjectModels decode_object_models(View archive,View exe,unsigned area) {
    const ExeTables tables(exe);
    auto directory=std::size_t(level_model_directory(archive,tables.profile));std::size_t model_base=0;
    if(area) {
        if(!tables.profile.yotd() || area>4)throw FormatError("Invalid object model area");
        const auto header=0x20+(area-1)*16;require(archive,header,8);
        const auto blob=read_u32(archive,header),size=read_u32(archive,header+4);
        if(!blob || !size)throw FormatError("Missing object model area");
        require(archive,blob,size);const auto bytes=read_u32(archive,blob);
        if(bytes<4 || bytes>size || size-bytes<0x180)throw FormatError("Invalid object model area directory");
        model_base=std::size_t(blob)+4;directory=std::size_t(blob)+bytes;
    }
    ObjectModels result;result.source_game=tables.profile.build;
    if(!tables.profile.yotd())for(unsigned i=0;i<result.purchase_prices.size();++i)result.purchase_prices[i]=read_u16(exe,tables(0x80064670)+i*4);
    for(unsigned i=0;i<28;++i)result.gem_variants[i]=exe[tables(0x8006440c)+i];
    for(unsigned i=0;i<5;++i) {
        result.gem_values[i]=exe[tables(0x80064428)+i];result.gem_meshes[i]=exe[tables(0x80064430)+i];
        result.gem_colors[i]=exe[tables(0x80064438)+i];
    }
    for(unsigned i=0;i<256;++i) {
        result.sine[i]=std::bit_cast<std::int16_t>(read_u16(exe,tables(0x80061bd8)+i*2));
        result.cosine[i]=std::bit_cast<std::int16_t>(read_u16(exe,tables(0x80061c58)+i*2));
    }
    for(unsigned i=0;i<10;++i)for(unsigned c=0;c<4;++c)result.shiny_colors[i][c]=rgb(read_u32(exe,tables(0x80064440)+i*16+c*4));
    std::array<std::uint32_t,125> codebook;
    for(unsigned i=0;i<codebook.size();++i)codebook[i]=read_u32(exe,tables(0x80062674)+i*4);
    std::copy_n(exe.begin()+tables(0x80062868),256,result.animation_blends.begin());
    std::copy_n(exe.begin()+tables(0x80062630),65,result.environment_angles.begin());
    std::copy_n(exe.begin()+tables(0x800625ac),129,result.environment_radii.begin());
    for(unsigned i=0;i<result.math.atan.size();++i)result.math.atan[i]=read_u16(exe,tables(0x80061e5c)+i*2);
    for(unsigned i=0;i<result.math.square_root.size();++i)result.math.square_root[i]=read_u16(exe,tables(0x80066478)+i*2);
    for(unsigned i=0;i<result.sparx_colors.size();++i)result.sparx_colors[i]=read_u32(exe,tables(0x80061adc)+i*4);
    if(!tables.profile.yotd()) {
    result.sparx_max_health=read_u32(exe,tables(0x80061a8c));
    result.sparx_pickup_radius=read_u32(exe,tables(0x80061a90));result.sparx_pickup_height=read_u32(exe,tables(0x80061a98));
    }
    // 8004a8d4: resident outline/triangle tables for Sparx's untextured glow.
    for(unsigned shape=0;shape<result.sparx_glows.size();++shape)for(unsigned i=0;i<16;++i) {
        const auto at=tables(0x8006430c)+shape*64+i*2;
        result.sparx_glows[shape].outline[i]={float(sx(exe[at],8)),float(sx(exe[at+1],8)),0};
        const auto face=read_u16(exe,at+32);
        result.sparx_glows[shape].faces[i]={(face>>8)&15u,face&15u};
    }
    if(!tables.profile.yotd()) {
    result.flame=decode_flame_assets(archive,exe);
    for(unsigned i=0;i<62;++i)result.powered_fire_allowed[i]=exe[tables(0x80061373)+i*4];
    }
    // 80070398 pairs header offsets at +0x40 with uint16 class IDs at +0x140.
    // Index zero is Spyro's extra animation bank. A final nonzero offset marks
    // the end of the last model; the next offset is zero.
    for(unsigned i=1;i<63;++i) {
        const auto relative_begin=read_u32(archive,directory+i*4),relative_end=read_u32(archive,directory+(i+1)*4);
        if(!relative_end)break;
        if(!relative_begin || relative_end<relative_begin)throw FormatError("Invalid object model bank directory");
        const auto begin=model_base+relative_begin,end=model_base+relative_end;
        require(archive,begin,end-begin);const auto bank=archive.subspan(begin,end-begin);
        const unsigned type=read_u16(archive,directory+0x100+i*2);
        if(type>=(tables.profile.yotd()?0x1000u:0x300u) || result.models.contains(type))throw FormatError("Invalid / duplicate object model class");
        const int count=std::bit_cast<std::int32_t>(read_u32(bank,0));
        if(!count || count< -255 || count>255)throw FormatError("Invalid object mesh / animation count");
        ObjectModel model;model.source_offset=static_cast<unsigned>(begin);model.source_bytes=static_cast<unsigned>(end-begin);model.animated=count>0;
        if(type==0x78)model.attachment_vertices=1;
        // Overlay 70's rider attachment calls (80041730): Mecha-Gulp
        // averages collision-prefix vertices 14/15; the bird uses vertex 0.
        if(!tables.profile.yotd() && type==0x2b0)model.attachment_vertices=24;
        if(!tables.profile.yotd() && type==0x2ad)model.attachment_vertices=1;
        if(!tables.profile.yotd() && type==0x2af)model.attachment_vertices=1;
        if((tables.profile.yotd() && (type==0xc8 || type==0xc9 || type==0xfd || type==0xee)) ||
           (!tables.profile.yotd() && (is_breakable(type) || type==0x1f0 || type==0x22b || type==0x2c || type==0x30)))
            model.cylinder=decode_container_collision(bank,model.animated);
        if(!tables.profile.yotd() && (type==0x2c || type==0x30 || type==0x169 || type==0x16a || type==0x1f2 || type==0x1f3 || type==0x1fc || type==0x1fd)) {
            decode_damage_banks(model,bank);
            if(!model.cylinder && model.damage_banks[0] && model.damage_banks[0]->cylinder)model.cylinder=model.damage_banks[0]->cylinder;
        }
        if(model.animated) {
            model.animation_count=static_cast<unsigned>(count);
            try {decode_object_animations(model,bank,codebook);}
            catch(const FormatError& error){throw FormatError("Object class "+std::to_string(type)+": "+error.what());}
        }
        else {
            const auto common=read_u32(bank,16);require(bank,common,0);require(bank,20,static_cast<std::size_t>(-count)*4);
            for(int j=0;j< -count;++j)model.meshes.push_back(static_mesh(bank,read_u32(bank,20+j*4),common));
        }
        result.models.emplace(type,std::move(model));
    }
    textures(result,archive,area);return result;
}
void decode_object_textures(ObjectModels& models,View archive,unsigned area){textures(models,archive,area);}
ObjectModel decode_static_object_model(View bank) {
    const int count=std::bit_cast<std::int32_t>(read_u32(bank,0));
    if(count>=0 || count< -255)throw FormatError("Invalid static cinematic model");
    ObjectModel model;const auto common=read_u32(bank,16);
    require(bank,20,static_cast<std::size_t>(-count)*4);
    for(int i=0;i< -count;++i)model.meshes.push_back(static_mesh(bank,read_u32(bank,20+i*4),common));
    return model;
}
void import_object_models(ObjectModels& target,const ObjectModels& donor,std::span<const unsigned> types) {
    std::map<unsigned,unsigned> tiles;std::map<unsigned,ObjectModel> added;
    // Mounted YOTD atlas rows include unused cells. Append after the full
    // allocation so donor effects preserve every native pixel, including
    // that padding. Earlier profiles keep their existing compact placement.
    const auto first_tile=target.source_game==GameBuild::YearOfTheDragonUS?
        ((target.texture_tiles+15)/16)*16:target.texture_tiles;
    for(const auto type:types)if(!target.models.contains(type)) {
        const auto found=donor.models.find(type);if(found==donor.models.end())throw FormatError("Missing retail power-up model");
        added.emplace(type,found->second);
        for(const auto& mesh:found->second.meshes)for(const auto& face:mesh.faces)if(face.textured) {
            const auto x=static_cast<unsigned>(face.uv[0][0]*donor.texture_width)/256;
            const auto y=static_cast<unsigned>(face.uv[0][1]*donor.texture_height)/256;
            tiles.try_emplace(y*16+x,first_tile+static_cast<unsigned>(tiles.size()));
        }
    }
    const auto old_height=target.texture_height;
    const auto new_count=tiles.empty()?target.texture_tiles:first_tile+static_cast<unsigned>(tiles.size());
    const auto new_height=((new_count+15)/16)*256;
    if(!tiles.empty()) {
        if(new_height>16384 || (target.texture_width && target.texture_width!=4096) || donor.texture_width!=4096)
            throw FormatError("Unsupported power-up atlas extent");
        target.texture_width=4096;target.texture_rgba.resize(std::size_t(4096)*new_height*4);
        for(const auto [from,to]:tiles)for(unsigned y=0;y<256;++y) {
            const auto src=(std::size_t(from/16*256+y)*4096+from%16*256)*4;
            const auto dst=(std::size_t(to/16*256+y)*4096+to%16*256)*4;
            std::copy_n(donor.texture_rgba.begin()+src,256*4,target.texture_rgba.begin()+dst);
        }
        for(auto& [id,model]:target.models)for(auto& mesh:model.meshes)for(auto& face:mesh.faces)if(face.textured)
            for(auto& uv:face.uv)uv[1]*=float(old_height)/new_height;
        for(auto& [id,model]:added)for(auto& mesh:model.meshes)for(auto& face:mesh.faces)if(face.textured) {
            const auto x=static_cast<unsigned>(face.uv[0][0]*donor.texture_width)/256;
            const auto y=static_cast<unsigned>(face.uv[0][1]*donor.texture_height)/256;
            const auto tile=tiles.at(y*16+x);
            for(auto& uv:face.uv) {
                uv[0]=(uv[0]*donor.texture_width-x*256+tile%16*256)/4096;
                uv[1]=(uv[1]*donor.texture_height-y*256+tile/16*256)/new_height;
            }
        }
        target.texture_height=new_height;target.texture_tiles=new_count;
    }
    target.models.merge(added);
}
ObjectCylinder decode_container_collision(View bank,bool animated) {
    const auto at=read_u32(bank,animated?20:8);require(bank,at,24);
    const auto header=read_u32(bank,at),shape=read_u32(bank,at+4);
    // All 43 mapped container banks use one unrotated type-3 primitive.
    if(!at || (header>>8)!=0 || (shape&0xff00)!=0x300 || !(shape&1))
        throw FormatError("Unmapped container collision primitive");
    ObjectCylinder result;result.broad_radius=(header&255)*32;result.flags=shape&255;result.radius=shape>>16;
    result.bottom=std::bit_cast<std::int16_t>(read_u16(bank,at+8));result.top=std::bit_cast<std::int16_t>(read_u16(bank,at+10));
    result.cap_radius=read_u32(bank,at+16);
    if(!result.radius || result.bottom>result.top || result.cap_radius>result.broad_radius || result.radius>result.broad_radius)
        throw FormatError("Invalid container collision dimensions");
    return result;
}
Vec3 decode_static_vertex(std::uint32_t packed) {
    // 8004753c..8004755c: overlapping signed fields, followed by ADD (not OR)
    // to pack VX and VY. Negative VX borrows one from the high halfword.
    // Static rendering uses half-world-unit translation, so vertices are *2.
    const int vx=sx(packed>>11,11),vy=sx(packed,12)-(vx<0?1:0),vz=sx(packed>>21,11);
    return {vx*2/retail::units_per_world,-vy*2/retail::units_per_world,vz*2/retail::units_per_world};
}
void initialize_retail_objects(Level& level,const ObjectModels& models) {
    for(auto& object:level.objects) {
        if(models.source_game!=GameBuild::YearOfTheDragonUS && (object.type==0x104 || object.type==0x229) && !object.material_color)object.material_color=2;
        const auto found=models.models.find(object.type);
        if(found!=models.models.end() && found->second.animated) {
            object.animation=start_object_animation(found->second,object.model);
            // StartMoby resets interpolation, not the authored clip/frame
            // fields. Several scenes deliberately offset repeated animations.
            object.animation.next_clip=object.record[61];object.animation.frame=object.record[62];
            object.animation.next_frame=object.record[63];
            const auto& clips=found->second.animations;
            if(object.animation.frame>=clips[object.animation.clip].frames.size() ||
               object.animation.next_clip>=clips.size() || object.animation.next_frame>=clips[object.animation.next_clip].frames.size())
                throw FormatError("Retail object initial animation state outside its model bank");
            object.previous_animation=object.animation;
        }
    }
    for(auto& object:level.objects)if(object.type==1 && object.state==0) {
        // Summer Forest 80070ac0..80070b60: state zero projects the gem onto
        // the ground and selects variant, mesh, and material using EXE tables.
        if(object.held_gem>=models.gem_variants.size())throw FormatError("Invalid loose gem denomination");
        object.variant=static_cast<std::uint8_t>(models.gem_variants[object.held_gem]);
        if(object.variant>=5 || models.gem_values[object.variant]!=object.held_gem)throw FormatError("Unmapped loose gem denomination");
        object.gem_value=models.gem_values[object.variant];
        object.model=static_cast<std::uint8_t>(models.gem_meshes[object.variant]);
        object.material_color=models.gem_colors[object.variant];
        const auto bank=models.models.find(1);
        if(bank==models.models.end() || bank->second.animated || object.model>=bank->second.meshes.size())
            throw FormatError("Loose gem refers to a missing retail mesh");
        if(const auto floor=level.raycast(object.position+Vec3{0,1,0},{0,-1,0},64)) {
            object.position.y=floor->point.y;
            const auto n=floor->normal.y<0?floor->normal*-1:floor->normal;
            constexpr float byte_angles=128/std::numbers::pi_v<float>;
            // 80038604 derives roll/pitch from the surface normal.
            object.ground_angles={-std::atan2(n.z,std::sqrt(n.x*n.x+n.y*n.y))*byte_angles,
                                  -std::atan2(n.x,n.y)*byte_angles};
            if(std::abs(object.ground_angles[0])>=1)object.rotation[2]=0;
        }
        object.gem_phase=object.gem_parameters[18];
        // The original uses 256 + a random byte for the sparkle timer.
        // Fixed 256 preserves the direct-pickup gate; sparkle scheduling is pending.
        object.gem_age=256;object.state=1;
        object.rotation[0]=object.ground_angles[0]+(models.cosine[object.gem_phase]>>7);
        object.rotation[1]=object.ground_angles[1]+(models.sine[object.gem_phase]>>7);
        object.previous_rotation=object.rotation;
        object.previous_position=object.position;
    }
    if(models.source_game!=GameBuild::YearOfTheDragonUS)initialize_enemy_objects(level.objects);
    else initialize_yotd_enemy_objects(level,models);
}
bool spirit_released(const RetailObject& object) {
    return !object.runtime_spawned && (object.record[83]&127) && (object.spirit_awarded ||
        ((object.enemy || (object.power_target && !object.power_target->scenery)) && (object.collected || object.state<0)));
}
bool gem_touching_spyro(Vec3 gem,Vec3 origin) {
    // 8001bf6c returns max(|dx|,|dy|) + floor(3*min/8). The direct pickup
    // path at 8007120c uses <512 horizontally and -383 <= dz <512 vertically.
    const auto x=static_cast<int>(std::abs(origin.x-gem.x)*retail::units_per_world);
    const auto z=static_cast<int>(std::abs(origin.z-gem.z)*retail::units_per_world);
    const auto height=static_cast<int>(std::floor((origin.y-gem.y)*retail::units_per_world));
    return std::max(x,z)+(std::min(x,z)*3>>3)<512 && height>=-383 && height<512;
}
void tick_retail_objects(std::span<RetailObject> objects,const ObjectModels* models,Vec3 origin,unsigned& total) {
    if(models)tick_object_animations(objects,*models);
    for(auto& object:objects) {
        // Sparx and flying gems preserve two-VBlank transform snapshots in
        // the companion dispatcher; their animations still advance above.
        if(object.enemy || object.retail_script || object.type==0x78 || ((object.type==0xaa || object.type==0x104) && object.runtime_spawned) || object.butterfly || is_breakable(object.type) || is_heavy_container(object.type) || is_container_fragment(object.type) ||
           (object.type==1 && (object.state==2 || object.state==3)))continue;
        if(object.type!=0x1bc)object.previous_rotation=object.rotation;
        if(object.collected || object.state<0 || object.type!=1 || !object.gem_value)continue;
        if(object.interpolate_position)begin_object_transform(object,1);
        if(models) {
            // 80071008..80071078: cosine/sine >>7 rocking, two phase bytes
            // per VBlank. Only resting loose gems are implemented here.
            object.rotation[0]=object.ground_angles[0]+(models->cosine[object.gem_phase]>>7);
            object.rotation[1]=object.ground_angles[1]+(models->sine[object.gem_phase]>>7);
            object.gem_phase=(object.gem_phase+2)&255;
        }
        ++object.gem_age;
        if(object.gem_age>=30 && gem_touching_spyro(object.position,origin)) {
            total+=object.gem_value;object.collected=true;
        }
    }
}
void begin_object_transform(RetailObject& object,unsigned span) {
    object.previous_position=object.position;object.previous_rotation=object.rotation;
    object.transform_span=std::max(1u,span);object.transform_phase=0;object.interpolate_position=true;
}
float object_transform_alpha(const RetailObject& object,float alpha) {
    return std::clamp((object.transform_phase+alpha)/std::max(1u,object.transform_span),0.0f,1.0f);
}
Vec3 object_position(const RetailObject& object,float alpha) {
    return object.interpolate_position?lerp(object.previous_position,object.position,object_transform_alpha(object,alpha)):object.position;
}
Vec3 object_rotation(Vec3 p,const RetailObject& object,float alpha) {
    return object_basis(object,alpha)*p;
}
ObjectBasis object_basis(const RetailObject& object,float alpha) {
    // Local +Z forward uses the same orientation basis as Spyro. Original
    // packed Euler order composes yaw, pitch, then roll (8001b6c8).
    alpha=object_transform_alpha(object,alpha);
    const auto angle=[&](unsigned axis){return object.previous_rotation[axis]+
        std::remainder(object.rotation[axis]-object.previous_rotation[axis],256.0f)*alpha;};
    const auto roll=angle(0)*16*retail::angle_to_radians;
    const auto pitch=angle(1)*16*retail::angle_to_radians;
    const auto yaw=retail::native_heading(angle(2)*16.0f);
    const float cr=std::cos(roll),sr=std::sin(roll),cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    const auto heading=[&](Vec3 p){return Vec3{p.x*cy+p.z*sy,p.y,-p.x*sy+p.z*cy};};
    return {heading({cr,-sr*cp,sr*sp}),heading({sr,cr*cp,-cr*sp}),heading({0,sp,cp})};
}
Vec3 shiny_object_color(const ObjectModels& models,const RetailObject& object,const ObjectFace& face,
                       const ObjectBasis& basis,Vec3 right,Vec3 up,Vec3 forward,bool backface) {
    // 80047cac: camera-space normals pass through the repeated light row
    // [-1916,-3065,-3072], then the selected base/specular pair from 80064440.
    // Preserve its 320/512 camera Y correction. This float implementation
    // retains the recovered lighting equation, without emulating GTE rounding.
    const auto n=basis*face.shiny_normal;
    // IR0 is signed when written directly; clamp the final RGB, not this gain.
    const auto lighting=(-1916*dot(n,right)+3065*dot(n,up)*0.625f-3072*dot(n,forward))/256;
    const auto intensity=lighting*(backface?0.25f:1.0f);
    const auto& colors=models.shiny_colors.at(object.material_color%10);
    const auto pair=(face.flags&1)?0u:2u;
    auto color=colors[pair]*(backface?0.5f:1.0f)+colors[pair+1]*(intensity/4096);
    const float highlight=std::max(0.0f,intensity-1472)/510;
    color+=Vec3{highlight,highlight,highlight};
    return {std::clamp(color.x,0.0f,1.0f),std::clamp(color.y,0.0f,1.0f),std::clamp(color.z,0.0f,1.0f)};
}
bool object_backface(Vec3 a,Vec3 b,Vec3 c,Vec3 eye) {
    // Positive screen-space NCLIP area is the front. Native camera Y-up
    // becomes screen Y-down, so a normal facing the eye is also positive.
    return dot(cross(b-a,c-a),eye-a)<0;
}
ObjectFaceView object_face_view(const ObjectFace& face,const std::array<Vec3,4>& v,Vec3 eye) {
    ObjectFaceView result;result.count=face.quad?4:3;
    const float first=dot(cross(v[1]-v[0],v[2]-v[0]),eye-v[0]);
    result.backface=first<0;
    if(!face.quad)return result;
    // 80047c68..80047cac (shiny) AND 800479c0..80047ad0 (plain):
    // every quad uses the opposite winding from triangle packets. The two
    // NCLIP signs also select the visible half of a folded quad.
    const float second=dot(cross(v[1]-v[3],v[2]-v[3]),eye-v[3]);
    if(first<0) {result.backface=false;if(second<=0)result.count=3;}
    else if(second<=0) {result.backface=true;if(second==0 || (!face.shiny && !face.animated))result.count=0;}
    else {result.backface=false;result.count=3;result.corners[0]=3;}
    return result;
}
ObjectPass object_face_pass(const ObjectFace& face,bool backface,float depth) {
    if(face.animated)return backface && (face.quad || !(face.flags&8))?ObjectPass::Hidden:ObjectPass::Opaque;
    if(!face.shiny)return backface && (face.flags&3)==0?ObjectPass::Hidden:ObjectPass::Opaque;
    // 80047cac uses TRZ (half original units) and subtracts 0x800 for bit 0.
    // Near shiny fronts retain polygon bit 25 (semi-transparency); backs are
    // opaque. 80047fe0 emits E1000620: additive B+F blending for this pass.
    const bool near=depth*retail::units_per_world*0.5f<((face.flags&1)?2048:0);
    if(backface)return near?ObjectPass::Opaque:ObjectPass::Hidden;
    return near?ObjectPass::Additive:ObjectPass::Opaque;
}
} // namespace spyro
